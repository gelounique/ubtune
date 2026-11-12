// UBTune — Realtime Pitch Correction Engine
// by unique & Mbezza (Gelo Santino Mares · Meron Negash) — UNIQUEBABY
//
// Pure C++17, no framework dependency (used by the JUCE plugin and the offline tests).
//  - Pitch detection: YIN on a decimated (~11-12 kHz) mono signal + full-rate refinement
//  - Pitch shifting:  pitch-synchronous overlap-add (TD-PSOLA) with formant control
//  - Note logic:      scale masks (incl. Pentatonic Major/Minor), hysteresis, retune speed,
//                     humanize (keeps vibrato), three characters: SMOOTH / PRO / T-PAIN
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ubt
{

static constexpr double kPi = 3.14159265358979323846;

enum class Mode : int { Smooth = 0, Pro = 1, TPain = 2 };

enum Scale : int
{
    Chromatic = 0, Major, Minor, PentMajor, PentMinor, Blues, HarmonicMinor, Dorian, NumScales
};

inline const char* scaleName (int s)
{
    static const char* n[] = { "CHROMATIC", "MAJOR", "MINOR", "PENTATONIC MAJOR", "PENTATONIC MINOR",
                               "BLUES", "HARMONIC MINOR", "DORIAN" };
    return (s >= 0 && s < NumScales) ? n[s] : "CUSTOM";
}

inline const char* noteName (int pc)
{
    static const char* n[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return n[((pc % 12) + 12) % 12];
}

/** Interval mask (bit i = semitone i above root). */
inline uint16_t scaleIntervals (int s)
{
    auto m = [] (std::initializer_list<int> iv) { uint16_t r = 0; for (int i : iv) r |= (uint16_t) (1u << i); return r; };
    switch (s)
    {
        case Major:         return m ({ 0, 2, 4, 5, 7, 9, 11 });
        case Minor:         return m ({ 0, 2, 3, 5, 7, 8, 10 });
        case PentMajor:     return m ({ 0, 2, 4, 7, 9 });
        case PentMinor:     return m ({ 0, 3, 5, 7, 10 });
        case Blues:         return m ({ 0, 3, 5, 6, 7, 10 });
        case HarmonicMinor: return m ({ 0, 2, 3, 5, 7, 8, 11 });
        case Dorian:        return m ({ 0, 2, 3, 5, 7, 9, 10 });
        default:            return 0x0FFF;
    }
}

/** Absolute pitch-class mask (bit pc) for key root + scale. */
inline uint16_t scaleMask (int root, int s)
{
    const uint16_t iv = scaleIntervals (s);
    uint16_t r = 0;
    for (int i = 0; i < 12; ++i)
        if (iv & (1u << i)) r |= (uint16_t) (1u << ((root + i) % 12));
    return r;
}

struct Params
{
    Mode  mode        = Mode::Pro;
    uint16_t mask     = 0x0FFF;   // allowed pitch classes
    float retuneMs    = 20.0f;    // 0 = hard snap
    float humanize    = 0.25f;    // 0..1
    float formant     = 0.0f;     // semitones, -6..+6
    float transpose   = 0.0f;     // semitones, -12..+12
    float mix         = 1.0f;     // 0..1
    float outGain     = 1.0f;     // linear
};

class Engine
{
public:
    static constexpr double kMinF = 75.0, kMaxF = 1050.0;
    static constexpr float  kFormantRange = 6.0f;

    // ---------- telemetry for the UI (written on audio thread) ----------
    std::atomic<float> tInputMidi  { -1.0f };   // detected sung pitch (fractional MIDI), -1 = none
    std::atomic<int>   tTargetNote { -1 };      // note we tune to
    std::atomic<float> tCorrection { 0.0f };    // applied shift in semitones
    std::atomic<float> tLevel      { 0.0f };    // input RMS
    std::atomic<float> tDetectProgress { -1.0f }; // -1 idle, 0..1 listening
    std::atomic<int>   tDetectedKey { -1 };     // root pc (0..11) after detection
    std::atomic<int>   tDetectedMinor { 0 };
    std::atomic<int>   tDetectSerial { 0 };     // increments on every finished detection

    void prepare (double sampleRate, int numChannels)
    {
        fs = sampleRate;
        nch = std::max (1, std::min (numChannels, 8));

        D = std::max (1, (int) std::floor (fs / 11000.0));
        fsd = fs / D;
        tauMaxD = (int) std::ceil (fsd / kMinF) + 2;
        tauMinD = std::max (2, (int) std::floor (fsd / kMaxF));
        winD = 2 * tauMaxD;
        hopD = std::max (8, (int) std::round (0.0053 * fsd)); // ~5.3 ms
        hopFull = hopD * D;

        Pmax = fs / kMinF;
        Punv = std::round (0.0045 * fs);
        refineWin = std::min ((int) (0.022 * fs), 4096);

        // analysis window centre lies (winD+tauMaxD)/2 decimated samples behind "now"
        centerDelay = (int) std::ceil ((winD + tauMaxD) * 0.5 * D) + D * 4;
        const double fFmin = std::pow (2.0, -kFormantRange / 12.0);
        latency = (int) std::ceil (Pmax * (1.0 + 1.0 / fFmin)) + 64;
        latency = std::max (latency, centerDelay + 2 * hopFull + 64);

        int need = 4 * latency + (int) (8 * Pmax) + refineWin + 8192;
        ringSize = 1; while (ringSize < need) ringSize <<= 1;
        ringMask = ringSize - 1;

        in.assign ((size_t) nch, std::vector<float> ((size_t) ringSize, 0.0f));
        out.assign ((size_t) nch, std::vector<float> ((size_t) ringSize, 0.0f));
        mono.assign ((size_t) ringSize, 0.0f);

        decSize = 1; while (decSize < 4 * (winD + tauMaxD) + 64) decSize <<= 1;
        dec.assign ((size_t) decSize, 0.0f);

        // anti-alias low-pass for decimation (windowed sinc)
        const int taps = 8 * D + 1;
        fir.assign ((size_t) taps, 0.0f);
        const double fc = 0.5 / D * 0.8;
        double sum = 0;
        for (int i = 0; i < taps; ++i)
        {
            const double x = i - (taps - 1) * 0.5;
            const double s = (x == 0.0) ? 2.0 * fc : std::sin (2.0 * kPi * fc * x) / (kPi * x);
            const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * i / (taps - 1));
            fir[(size_t) i] = (float) (s * w); sum += s * w;
        }
        for (auto& c : fir) c = (float) (c / sum);

        diff.assign ((size_t) tauMaxD + 2, 0.0f);
        cmnd.assign ((size_t) tauMaxD + 2, 0.0f);
        reset();
    }

    int getLatency() const { return latency; }

    void reset()
    {
        for (auto& c : in)  std::fill (c.begin(), c.end(), 0.0f);
        for (auto& c : out) std::fill (c.begin(), c.end(), 0.0f);
        std::fill (mono.begin(), mono.end(), 0.0f);
        std::fill (dec.begin(), dec.end(), 0.0f);
        n = 0; decN = 0;
        for (auto& f : raw)  f = Frame{};
        for (auto& f : filt) f = Frame{};
        latestRaw = -1; latestFilt = -1;
        nextMark = 0.0; inMark = 0.0; wasVoiced = false; polarity = 1.0f;
        curNote = -1; shift = 0.0; center = 0.0; voicedRun = 0; unvoicedRun = 0;
        tInputMidi = -1.0f; tTargetNote = -1; tCorrection = 0.0f; tLevel = 0.0f;
    }

    void setParams (const Params& p) { prm = p; if (prm.mask == 0) prm.mask = 0x0FFF; }

    void startKeyDetect()
    {
        std::fill (std::begin (pcHist), std::end (pcHist), 0.0);
        detectVoicedSec = 0.0;
        detecting = true;
        tDetectProgress = 0.0f;
    }
    void cancelKeyDetect() { detecting = false; tDetectProgress = -1.0f; }

    /** In-place processing of numChannels x numSamples. */
    void process (float* const* ch, int numChannels, int numSamples)
    {
        const int C = std::min (numChannels, nch);
        const double fF = std::pow (2.0, std::clamp ((double) prm.formant, -(double) kFormantRange, (double) kFormantRange) / 12.0);
        const float mix = std::clamp (prm.mix, 0.0f, 1.0f);
        double lvl = 0.0;

        for (int i = 0; i < numSamples; ++i)
        {
            const int w = (int) (n & ringMask);
            float m = 0.0f;
            for (int c = 0; c < C; ++c) { in[(size_t) c][(size_t) w] = ch[c][i]; m += ch[c][i]; }
            m /= (float) C;
            mono[(size_t) w] = m;
            lvl += (double) m * m;

            // ---- decimate + analysis ----
            if ((n % D) == 0)
            {
                const int taps = (int) fir.size();
                float acc = 0.0f;
                for (int k = 0; k < taps; ++k) acc += fir[(size_t) k] * mono[(size_t) ((n - k) & ringMask)];
                dec[(size_t) (decN & (decSize - 1))] = acc;
                ++decN;
                if ((decN % hopD) == 0 && decN >= winD + tauMaxD) analyse();
            }

            // ---- synthesis: add every grain that starts at or before this sample ----
            synthesise (C, fF);

            const int r = (int) (n & ringMask);
            const int d = (int) ((n - latency) & ringMask);
            for (int c = 0; c < C; ++c)
            {
                const float wet = out[(size_t) c][(size_t) r];
                out[(size_t) c][(size_t) r] = 0.0f;
                const float dry = in[(size_t) c][(size_t) d];
                ch[c][i] = (dry + (wet - dry) * mix) * prm.outGain;
            }
            ++n;
        }
        if (numSamples > 0) tLevel = (float) std::sqrt (lvl / numSamples);
    }

private:
    struct Frame { double period = 0.0; bool voiced = false; float rms = 0.0f; int64_t center = 0; };

    // ---------------- analysis ----------------
    void analyse()
    {
        const int len = winD + tauMaxD;
        const int64_t start = decN - len;
        auto X = [&] (int j) { return dec[(size_t) ((start + j) & (decSize - 1))]; };

        double e = 0.0;
        for (int j = 0; j < len; ++j) e += (double) X (j) * X (j);
        const float rms = (float) std::sqrt (e / len);

        Frame f; f.rms = rms; f.center = n - centerDelay;
        if (rms > 0.0025f)
        {
            // YIN difference function
            for (int tau = 1; tau <= tauMaxD; ++tau)
            {
                float s = 0.0f;
                for (int j = 0; j < winD; ++j) { const float dd = X (j) - X (j + tau); s += dd * dd; }
                diff[(size_t) tau] = s;
            }
            cmnd[0] = 1.0f;
            double run = 0.0;
            for (int tau = 1; tau <= tauMaxD; ++tau)
            {
                run += diff[(size_t) tau];
                cmnd[(size_t) tau] = run > 0 ? (float) (diff[(size_t) tau] * tau / run) : 1.0f;
            }
            const float thr = 0.18f;
            int best = -1;
            for (int tau = tauMinD; tau < tauMaxD; ++tau)
            {
                if (cmnd[(size_t) tau] < thr)
                {
                    while (tau + 1 < tauMaxD && cmnd[(size_t) tau + 1] < cmnd[(size_t) tau]) ++tau;
                    best = tau; break;
                }
            }
            bool voiced = best > 0;
            if (! voiced)
            {
                float mn = 1e9f;
                for (int tau = tauMinD; tau < tauMaxD; ++tau)
                    if (cmnd[(size_t) tau] < mn) { mn = cmnd[(size_t) tau]; best = tau; }
                voiced = mn < 0.3f;
            }
            if (voiced && best > 1 && best < tauMaxD)
            {
                const double a = cmnd[(size_t) best - 1], b = cmnd[(size_t) best], c = cmnd[(size_t) best + 1];
                const double den = a - 2 * b + c;
                double tau = best + (std::abs (den) > 1e-12 ? 0.5 * (a - c) / den : 0.0);
                f.period = refine (tau * D);
                f.voiced = f.period > fs / kMaxF * 0.9 && f.period < fs / kMinF * 1.1;
            }
        }

        // store raw, then 3-point median-filter period (kills octave glitches)
        const int64_t k = ++latestRaw;
        raw[(size_t) (k & (kFrames - 1))] = f;
        if (k >= 2)
        {
            Frame a = raw[(size_t) ((k - 2) & (kFrames - 1))], b = raw[(size_t) ((k - 1) & (kFrames - 1))], c = f;
            Frame o = b;
            const int vc = (int) a.voiced + (int) b.voiced + (int) c.voiced;
            o.voiced = vc >= 2;
            if (o.voiced)
            {
                double p[3]; int np = 0;
                if (a.voiced) p[np++] = a.period;
                if (b.voiced) p[np++] = b.period;
                if (c.voiced) p[np++] = c.period;
                std::sort (p, p + np);
                o.period = (np == 3) ? p[1] : (b.voiced ? b.period : p[0]);
            }
            filt[(size_t) ((k - 1) & (kFrames - 1))] = o;
            latestFilt = k - 1;

            if (o.voiced)
            {
                const double midi = 69.0 + 12.0 * std::log2 (fs / o.period / 440.0);
                tInputMidi = (float) midi;
                if (detecting)
                {
                    const int pc = (((int) std::lround (midi)) % 12 + 12) % 12;
                    pcHist[pc] += std::min (1.0, (double) o.rms * 20.0);
                    detectVoicedSec += hopFull / fs;
                    tDetectProgress = (float) std::min (1.0, detectVoicedSec / kDetectSeconds);
                    if (detectVoicedSec >= kDetectSeconds) finishDetect();
                }
            }
            else tInputMidi = -1.0f;
        }
    }

    /** Full-rate refinement of the period around an estimate (raw difference minimum). */
    double refine (double est)
    {
        const int lo = std::max (2, (int) std::floor (est) - D - 1);
        const int hi = std::min ((int) std::ceil (fs / kMinF) + 4, (int) std::ceil (est) + D + 1);
        if (hi - lo < 2) return est;
        const int W = std::min (refineWin, (int) (3 * est));
        const int64_t s0 = n - W - hi;
        float best = 1e30f; int bt = lo;
        float vals[64]; int nv = 0;
        for (int t = lo; t <= hi && nv < 64; ++t, ++nv)
        {
            float s = 0.0f;
            for (int j = 0; j < W; ++j)
            {
                const float dd = mono[(size_t) ((s0 + j) & ringMask)] - mono[(size_t) ((s0 + j + t) & ringMask)];
                s += dd * dd;
            }
            vals[nv] = s;
            if (s < best) { best = s; bt = t; }
        }
        const int idx = bt - lo;
        if (idx > 0 && idx < nv - 1)
        {
            const double a = vals[idx - 1], b = vals[idx], c = vals[idx + 1];
            const double den = a - 2 * b + c;
            if (den > 1e-12) return bt + 0.5 * (a - c) / den;
        }
        return (double) bt;
    }

    Frame lookup (double t) const
    {
        if (latestFilt < 0) return {};
        const Frame& last = filt[(size_t) (latestFilt & (kFrames - 1))];
        int64_t back = (int64_t) std::floor (((double) last.center - t) / hopFull + 0.5);
        back = std::clamp<int64_t> (back, 0, std::min<int64_t> (latestFilt, kFrames - 4));
        return filt[(size_t) ((latestFilt - back) & (kFrames - 1))];
    }

    void finishDetect()
    {
        static const double maj[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
        static const double mnr[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
        auto corr = [&] (const double* prof, int root)
        {
            double mx = 0, my = 0;
            for (int i = 0; i < 12; ++i) { mx += pcHist[(i + root) % 12]; my += prof[i]; }
            mx /= 12; my /= 12;
            double sxy = 0, sxx = 0, syy = 0;
            for (int i = 0; i < 12; ++i)
            {
                const double x = pcHist[(i + root) % 12] - mx, y = prof[i] - my;
                sxy += x * y; sxx += x * x; syy += y * y;
            }
            return sxy / std::sqrt (sxx * syy + 1e-12);
        };
        double best = -2; int bk = 0, bm = 0;
        for (int r = 0; r < 12; ++r)
        {
            const double a = corr (maj, r), b = corr (mnr, r);
            if (a > best) { best = a; bk = r; bm = 0; }
            if (b > best) { best = b; bk = r; bm = 1; }
        }
        tDetectedKey = bk; tDetectedMinor = bm;
        tDetectSerial.fetch_add (1);
        detecting = false;
        tDetectProgress = -1.0f;
    }

    // ---------------- note logic ----------------
    int nearestAllowed (double midi) const
    {
        const int base = (int) std::lround (midi);
        int best = base; double bd = 1e9;
        for (int o = -7; o <= 7; ++o)
        {
            const int cand = base + o;
            if (prm.mask & (1u << (((cand % 12) + 12) % 12)))
            {
                const double d = std::abs (midi - cand);
                if (d < bd) { bd = d; best = cand; }
            }
        }
        return best;
    }

    double hysteresis() const
    {
        switch (prm.mode)
        {
            case Mode::Smooth: return 0.22;
            case Mode::Pro:    return 0.12;
            default:           return 0.03;
        }
    }

    /** Returns the shift (semitones) to apply for a voiced grain at input pitch midi. */
    double updateShift (double midi, double dt, bool onset)
    {
        const int cand = nearestAllowed (midi);
        const int prevNote = curNote;
        if (onset || curNote < 0 || ! (prm.mask & (1u << (((curNote % 12) + 12) % 12))))
            curNote = cand;
        else if (cand != curNote && std::abs (midi - curNote) - std::abs (midi - cand) > hysteresis())
            curNote = cand;

        // humanize: slow-moving centre keeps natural vibrato (re-centred on every new note)
        const double tauC = 0.12;
        if (onset || curNote != prevNote || std::abs (center - midi) > 1.0) center = midi;
        else center += (midi - center) * (1.0 - std::exp (-dt / tauC));

        const double h = std::clamp ((double) prm.humanize, 0.0, 1.0);
        const double eff = midi + (center - midi) * h;
        const double desired = curNote - eff;

        double tauR = std::max (0.0, (double) prm.retuneMs) / 1000.0;
        if (prm.mode == Mode::TPain) tauR = std::min (tauR, 0.004); // robot = instant snap
        if (onset || tauR <= 0.0005) shift = desired;
        else shift += (desired - shift) * (1.0 - std::exp (-dt / tauR));
        return shift;
    }

    // ---------------- synthesis (TD-PSOLA) ----------------
    void synthesise (int C, double fF)
    {
        if (nextMark < (double) n - 4 * Pmax) nextMark = (double) n;

        for (int guard = 0; guard < 64; ++guard)
        {
            const double target = nextMark - latency;     // input time this grain represents
            const Frame f = lookup (target);
            const bool voiced = f.voiced && f.period > 0;
            const double P = voiced ? f.period : Punv;
            const double H = P / fF;
            if (nextMark - H > (double) n) break;

            double hop = P, src;
            if (voiced)
            {
                const bool onset = ! wasVoiced;
                if (onset || inMark > target || inMark < target - 3 * P)
                {
                    inMark = peakIn (target - P, target, true);
                }
                while (inMark + P <= target) inMark += P;
                inMark = peakIn (inMark - P * 0.12, inMark + P * 0.12, false);
                if (inMark > target) inMark -= P;

                const double midi = 69.0 + 12.0 * std::log2 (fs / P / 440.0);
                const double s = updateShift (midi, lastHop / fs, onset);
                const double ratio = std::pow (2.0, (s + prm.transpose) / 12.0);
                hop = P / std::clamp (ratio, 0.25, 4.0);
                lastHop = hop;
                src = inMark;
                wasVoiced = true;
                tTargetNote = curNote;
                tCorrection = (float) s;
            }
            else
            {
                src = target;
                if (wasVoiced) { tTargetNote = -1; tCorrection = 0.0f; }
                wasVoiced = false;
            }
            addGrain (C, src, nextMark, H, fF, std::min (hop / H, 2.0));
            nextMark += hop;
        }
    }

    /** Find a pitch mark (signal peak) in [a,b] of the mono input. */
    double peakIn (double a, double b, bool choosePolarity)
    {
        const int64_t ia = (int64_t) std::ceil (a), ib = (int64_t) std::floor (b);
        if (ib <= ia) return a;
        if (choosePolarity)
        {
            float mx = -1e9f, mn = 1e9f;
            for (int64_t t = ia; t <= ib; ++t) { const float v = mono[(size_t) (t & ringMask)]; mx = std::max (mx, v); mn = std::min (mn, v); }
            polarity = (mx >= -mn) ? 1.0f : -1.0f;
        }
        float best = -1e9f; int64_t bt = ia;
        for (int64_t t = ia; t <= ib; ++t)
        {
            const float v = polarity * mono[(size_t) (t & ringMask)];
            if (v > best) { best = v; bt = t; }
        }
        return (double) bt;
    }

    void addGrain (int C, double srcCenter, double dstCenter, double H, double fF, double gain)
    {
        const int64_t k0 = std::max<int64_t> (n, (int64_t) std::ceil (dstCenter - H));
        const int64_t k1 = (int64_t) std::floor (dstCenter + H);
        const double invH = 1.0 / H;
        for (int64_t k = k0; k <= k1; ++k)
        {
            const double u = (k - dstCenter) * invH;
            const float w = (float) ((0.5 + 0.5 * std::cos (kPi * u)) * gain);
            const double sp = srcCenter + (k - dstCenter) * fF;
            const double fl = std::floor (sp);
            const float fr = (float) (sp - fl);
            const int64_t i0 = (int64_t) fl;
            const size_t a = (size_t) (i0 & ringMask), b = (size_t) ((i0 + 1) & ringMask), o = (size_t) (k & ringMask);
            for (int c = 0; c < C; ++c)
            {
                const auto& x = in[(size_t) c];
                out[(size_t) c][o] += (x[a] + (x[b] - x[a]) * fr) * w;
            }
        }
    }

    // ---------------- state ----------------
    static constexpr int kFrames = 256;
    static constexpr double kDetectSeconds = 8.0;

    double fs = 48000.0, fsd = 12000.0;
    int nch = 2, D = 4, tauMaxD = 160, tauMinD = 11, winD = 320, hopD = 64, hopFull = 256;
    double Pmax = 640, Punv = 216;
    int refineWin = 1024, centerDelay = 1000, latency = 1600;
    int ringSize = 8192, ringMask = 8191, decSize = 2048;

    std::vector<std::vector<float>> in, out;
    std::vector<float> mono, dec, fir, diff, cmnd;
    int64_t n = 0, decN = 0;

    std::array<Frame, kFrames> raw {}, filt {};
    int64_t latestRaw = -1, latestFilt = -1;

    double nextMark = 0, inMark = 0, lastHop = 200;
    bool wasVoiced = false;
    float polarity = 1.0f;
    int curNote = -1;
    double shift = 0, center = 0;
    int voicedRun = 0, unvoicedRun = 0;

    bool detecting = false;
    double pcHist[12] {};
    double detectVoicedSec = 0;

    Params prm;
};

} // namespace ubt
