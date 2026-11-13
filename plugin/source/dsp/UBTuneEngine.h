// UBTune — Realtime Pitch Correction Engine
// by unique & Mbezza (Gelo Santino Mares · Meron Negash) — UNIQUEBABY
//
// Pure C++17, no framework dependency (used by the JUCE plugin and the offline tests).
//  - Pitch detection: YIN on the most recent ~26 ms (decimated to ~11-12 kHz) + full-rate refinement
//  - Pitch shifting:  low-latency TD-PSOLA. Every grain is evaluated lazily at output time and is
//                     cut from the most recent *already sung* pitch period, so the audio delay is only
//                     a fraction of one period (~1-6 ms) instead of a fixed worst-case buffer.
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

enum class Mode : int { Smooth = 0, Pro = 1, Robotic = 2 };

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
    static constexpr double kMinF = 70.0, kMaxF = 1100.0;
    static constexpr float  kFormantRange = 6.0f;
    static constexpr double kReportedLatencyMs = 2.0;   // what we tell the host (typical real delay ~1-5 ms)

    // ---------- telemetry for the UI (written on audio thread) ----------
    std::atomic<float> tInputMidi  { -1.0f };   // detected sung pitch (fractional MIDI), -1 = none
    std::atomic<int>   tTargetNote { -1 };      // note we tune to
    std::atomic<float> tCorrection { 0.0f };    // applied shift in semitones
    std::atomic<float> tLevel      { 0.0f };    // input RMS
    std::atomic<float> tDelayMs    { 0.0f };    // current real audio delay of the tuned signal
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
        winD = tauMaxD + 8;
        hopD = std::max (8, (int) std::round (0.0027 * fsd)); // ~2.7 ms analysis hop
        Pmax = fs / kMinF;
        Punv = std::round (0.004 * fs);
        refineWin = std::min ((int) (0.016 * fs), 4096);
        latency = std::max (1, (int) std::lround (kReportedLatencyMs * 0.001 * fs));

        int need = (int) (16 * Pmax) + refineWin + 8192;
        ringSize = 1; while (ringSize < need) ringSize <<= 1;
        ringMask = ringSize - 1;
        in.assign ((size_t) nch, std::vector<float> ((size_t) ringSize, 0.0f));
        mono.assign ((size_t) ringSize, 0.0f);

        decSize = 1; while (decSize < 4 * (winD + tauMaxD) + 64) decSize <<= 1;
        dec.assign ((size_t) decSize, 0.0f);

        // anti-alias low-pass for decimation (windowed sinc)
        const int taps = 6 * D + 1;
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
        aLvl = 1.0 - std::exp (-1.0 / (0.08 * fs));
        aComp = 1.0 - std::exp (-1.0 / (0.05 * fs));
        reset();
    }

    int getLatency() const { return latency; }

    void reset()
    {
        for (auto& c : in) std::fill (c.begin(), c.end(), 0.0f);
        std::fill (mono.begin(), mono.end(), 0.0f);
        std::fill (dec.begin(), dec.end(), 0.0f);
        n = 0; decN = 0;
        raw[0] = raw[1] = raw[2] = Frame{}; nRaw = 0;
        cur = Frame{};
        numGrains = 0;
        nextMark = 0.0; lastMark = 0.0; markValid = false; wasVoiced = false; polarity = 1.0f; lastHop = 200.0;
        curNote = -1; shift = 0.0; center = 0.0; decPitch = 0.0; pendSlow = pendFast = 0.0; pendSlowNote = -1; delaySm = 0.0; eIn = eOut = 0.0; comp = 1.0;
        tInputMidi = -1.0f; tTargetNote = -1; tCorrection = 0.0f; tLevel = 0.0f; tDelayMs = 0.0f;
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
            const size_t w = (size_t) (n & ringMask);
            float m = 0.0f;
            for (int c = 0; c < C; ++c) { in[(size_t) c][w] = ch[c][i]; m += ch[c][i]; }
            m /= (float) C;
            mono[w] = m;
            lvl += (double) m * m;

            if ((n % D) == 0)
            {
                const int taps = (int) fir.size();
                float acc = 0.0f;
                for (int k = 0; k < taps; ++k) acc += fir[(size_t) k] * mono[(size_t) ((n - k) & ringMask)];
                dec[(size_t) (decN & (decSize - 1))] = acc;
                ++decN;
                if ((decN % hopD) == 0 && decN >= winD + tauMaxD) analyse();
            }

            trackMarks();
            spawnGrains (fF);

            // ---- render all active grains for this output sample ----
            float y[8] = {};
            const double t = (double) n;
            for (int g = 0; g < numGrains;)
            {
                Grain& gr = grains[g];
                const double u = (t - gr.dst) * gr.invH;
                if (u >= 1.0) { grains[g] = grains[--numGrains]; continue; }
                if (u > -1.0)
                {
                    const float wv = (float) ((0.5 + 0.5 * std::cos (kPi * u)) * gr.gain);
                    const double sp = gr.src + (t - gr.dst) * gr.fF;
                    const double fl = std::floor (sp);
                    const float fr = (float) (sp - fl);
                    const int64_t i0 = (int64_t) fl;
                    const size_t a = (size_t) (i0 & ringMask), b = (size_t) ((i0 + 1) & ringMask);
                    for (int c = 0; c < C; ++c)
                    {
                        const auto& x = in[(size_t) c];
                        y[c] += (x[a] + (x[b] - x[a]) * fr) * wv;
                    }
                }
                ++g;
            }

            // ---- level match: overlapping grains at big shifts partly cancel -> keep loudness = input ----
            {
                double yo = 0.0; for (int c = 0; c < C; ++c) yo += y[c];
                yo /= C;
                eIn += ((double) m * m - eIn) * aLvl;
                eOut += (yo * yo - eOut) * aLvl;
                const double target = (eIn > 1e-7 && eOut > 1e-9) ? std::clamp (std::sqrt (eIn / eOut), 0.5, 8.0) : comp;
                comp += (target - comp) * aComp;
            }
            const size_t d = (size_t) ((n - latency) & ringMask);
            for (int c = 0; c < C; ++c)
            {
                const float dry = in[(size_t) c][d];
                const float wet = y[c] * (float) comp;
                ch[c][i] = (dry + (wet - dry) * mix) * prm.outGain;
            }
            ++n;
        }
        if (numSamples > 0)
        {
            tLevel = (float) std::sqrt (lvl / numSamples);
            tDelayMs = (float) (delaySm * 1000.0 / fs);
        }
    }

private:
    struct Frame { double period = 0.0; bool voiced = false; float rms = 0.0f; };
    struct Grain { double src, dst, invH, fF, gain; };

    // ---------------- analysis (always on the most recent window) ----------------
    void analyse()
    {
        const int len = winD + tauMaxD;
        const int64_t start = decN - len;
        auto X = [&] (int j) { return dec[(size_t) ((start + j) & (decSize - 1))]; };

        double e = 0.0;
        for (int j = 0; j < len; ++j) e += (double) X (j) * X (j);
        const float rms = (float) std::sqrt (e / len);

        Frame f; f.rms = rms;
        if (rms > 0.0025f)
        {
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
            int best = -1;
            for (int tau = tauMinD; tau < tauMaxD; ++tau)
                if (cmnd[(size_t) tau] < 0.17f)
                {
                    while (tau + 1 < tauMaxD && cmnd[(size_t) tau + 1] < cmnd[(size_t) tau]) ++tau;
                    best = tau; break;
                }
            bool voiced = best > 0;
            if (! voiced)
            {
                float mn = 1e9f;
                for (int tau = tauMinD; tau < tauMaxD; ++tau)
                    if (cmnd[(size_t) tau] < mn) { mn = cmnd[(size_t) tau]; best = tau; }
                voiced = mn < 0.28f;
            }
            if (voiced && best > 1 && best < tauMaxD)
            {
                const double a = cmnd[(size_t) best - 1], b = cmnd[(size_t) best], c = cmnd[(size_t) best + 1];
                const double den = a - 2 * b + c;
                const double tau = best + (std::abs (den) > 1e-12 ? 0.5 * (a - c) / den : 0.0);
                f.period = refine (tau * D);
                f.voiced = f.period > fs / kMaxF * 0.9 && f.period < fs / kMinF * 1.1;
            }
        }

        // octave guard: a sudden x2 / x0.5 jump against the last stable pitch is almost always a
        // detector error -> keep the previous octave if that lag is also a good YIN candidate
        if (f.voiced && cur.voiced && cur.period > 0)
        {
            const double r = f.period / cur.period;
            for (double k : { 2.0, 0.5 })
                if (std::abs (r / k - 1.0) < 0.06)
                {
                    const double alt = f.period / k;               // period in the old octave
                    const int altD = (int) std::lround (alt / D);
                    if (altD > tauMinD && altD < tauMaxD && cmnd[(size_t) altD] < 0.30f)
                        f.period = refine (alt);
                }
        }

        // 3-frame median (octave-glitch killer), 2-of-3 voicing
        raw[0] = raw[1]; raw[1] = raw[2]; raw[2] = f; ++nRaw;
        Frame o = f;
        const int vc = (int) raw[0].voiced + (int) raw[1].voiced + (int) raw[2].voiced;
        o.voiced = (nRaw >= 3) && vc >= 2;
        if (o.voiced)
        {
            if (vc == 3)
            {
                const double a = raw[0].period, b = raw[1].period, c = raw[2].period;
                o.period = std::max (std::min (a, b), std::min (std::max (a, b), c)); // median of 3
            }
            else o.period = f.voiced ? f.period : raw[1].period;
        }
        cur = o;

        if (o.voiced)
        {
            const double midi = 69.0 + 12.0 * std::log2 (fs / o.period / 440.0);
            tInputMidi = (float) midi;
            if (detecting)
            {
                const int pc = (((int) std::lround (midi)) % 12 + 12) % 12;
                pcHist[pc] += std::min (1.0, (double) o.rms * 20.0);
                detectVoicedSec += hopD * D / fs;
                tDetectProgress = (float) std::min (1.0, detectVoicedSec / kDetectSeconds);
                if (detectVoicedSec >= kDetectSeconds) finishDetect();
            }
        }
        else tInputMidi = -1.0f;
    }

    double refine (double est)
    {
        const int lo = std::max (2, (int) std::floor (est) - D - 1);
        const int hi = std::min ((int) std::ceil (fs / kMinF) + 4, (int) std::ceil (est) + D + 1);
        if (hi - lo < 2) return est;
        const int W = std::min (refineWin, (int) (2 * est));
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

    // ---------------- pitch marks (one per period, on the waveform peak) ----------------
    void trackMarks()
    {
        if (! cur.voiced) { markValid = false; return; }
        const double P = cur.period;
        const double guard = std::ceil (0.13 * P) + 1.0;
        if (! markValid || lastMark < (double) n - 3.0 * P)
        {
            if ((double) n < P + guard + 2) return;
            lastMark = peakIn ((double) n - P - guard, (double) n - guard, true);
            markValid = true;
            return;
        }
        while (lastMark + P + guard <= (double) n)
        {
            const double cand = lastMark + P;
            double m = peakIn (cand - 0.12 * P, cand + 0.12 * P, false);
            if (m <= lastMark + 0.5 * P) m = cand;
            lastMark = m;
        }
    }

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

    // Note decision ("capture zones"): every allowed note owns the range up to the midpoint to its
    // neighbouring allowed notes, so zones never overlap. Around every boundary sits a dead band:
    // we only leave the current note when the *smoothed* pitch (vibrato averaged out) is clearly past
    // the boundary for a minimum time, or when the raw pitch is deep inside the new note (real jump).
    struct ZoneCfg { double tauDec, margin, hold, fastMargin, fastHold; };
    ZoneCfg zoneCfg() const
    {
        switch (prm.mode)
        {
            case Mode::Smooth: return { 0.090, 0.25, 0.060, 0.40, 0.035 };
            case Mode::Pro:    return { 0.075, 0.22, 0.045, 0.40, 0.018 };
            default:           return { 0.065, 0.20, 0.030, 0.40, 0.012 };
        }
    }

    static bool allowedIn (uint16_t mask, int note) { return (mask >> (((note % 12) + 12) % 12)) & 1; }

    /** How far (semitones) pitch m is past the boundary between curNote and cand, towards cand. */
    static double pastBoundary (double m, int from, int to)
    {
        const double boundary = 0.5 * (from + to);
        return to > from ? m - boundary : boundary - m;
    }

    void decideNote (double midi, double dt, bool onset)
    {
        const ZoneCfg z = zoneCfg();
        if (onset) decPitch = midi;
        else decPitch += (midi - decPitch) * (1.0 - std::exp (-dt / z.tauDec));

        if (onset || curNote < 0 || ! allowedIn (prm.mask, curNote))
        {
            curNote = nearestAllowed (decPitch);
            pendSlow = pendFast = 0.0; pendSlowNote = -1;
            return;
        }
        const int candSlow = nearestAllowed (decPitch);
        const int candFast = nearestAllowed (midi);
        int target = -1;

        // fast path: raw pitch sits deep inside another note's zone (a real jump / slide).
        // Way past the boundary (> 1 semitone, wider than any vibrato) -> switch at once;
        // otherwise it has to stay away from the current note for fastHold.
        const double pastFast = candFast != curNote ? pastBoundary (midi, curNote, candFast) : -1.0;
        if (pastFast > 1.0) target = candFast;
        else if (pastFast > z.fastMargin)
        {
            pendFast += dt;
            if (pendFast >= z.fastHold) target = candFast;
        }
        else pendFast = 0.0;

        // slow path: averaged pitch (vibrato removed) clearly over the boundary for a while
        if (target < 0 && candSlow != curNote && pastBoundary (decPitch, curNote, candSlow) > z.margin)
        {
            if (pendSlowNote != candSlow) { pendSlowNote = candSlow; pendSlow = 0.0; }
            pendSlow += dt;
            if (pendSlow >= z.hold) target = candSlow;
        }
        else if (target < 0) { pendSlowNote = -1; pendSlow = 0.0; }

        if (target >= 0)
        {
            curNote = target;
            pendSlowNote = -1; pendSlow = pendFast = 0.0;
            decPitch = midi;
        }
    }

    double updateShift (double midi, double dt, bool onset)
    {
        const int prevNote = curNote;
        decideNote (midi, dt, onset);

        const double tauC = 0.12;
        if (onset || curNote != prevNote || std::abs (center - midi) > 1.0) center = midi;
        else center += (midi - center) * (1.0 - std::exp (-dt / tauC));

        const double h = std::clamp ((double) prm.humanize, 0.0, 1.0);
        const double eff = midi + (center - midi) * h;
        const double desired = curNote - eff;

        double tauR = std::max (0.0, (double) prm.retuneMs) / 1000.0;
        if (prm.mode == Mode::Robotic) tauR = std::min (tauR, 0.004); // robotic = instant snap
        if (onset || tauR <= 0.0005) shift = desired;
        else shift += (desired - shift) * (1.0 - std::exp (-dt / tauR));
        return shift;
    }

    // ---------------- low-latency PSOLA: grains are created just in time ----------------
    void spawnGrains (double fF)
    {
        const double t = (double) n;
        if (nextMark < t - 2.0 * Pmax) nextMark = t;

        for (int guard = 0; guard < 16 && numGrains < kMaxGrains; ++guard)
        {
            const bool voiced = cur.voiced && markValid;
            const double P = voiced ? cur.period : Punv;
            const double H = P / fF;
            if (nextMark - H > t) break;

            // causality: reading src + (tau-dst)*fF must stay <= tau-2 for the whole grain
            const double limit = nextMark - H * std::abs (fF - 1.0) - 3.0;
            double src, hop = P;
            if (voiced)
            {
                const bool onset = ! wasVoiced;
                src = lastMark;
                while (src > limit) src -= P;

                const double midi = 69.0 + 12.0 * std::log2 (fs / P / 440.0);
                const double s = updateShift (midi, lastHop / fs, onset);
                const double ratio = std::pow (2.0, (s + prm.transpose) / 12.0);
                hop = P / std::clamp (ratio, 0.25, 4.0);
                lastHop = hop;
                wasVoiced = true;
                tTargetNote = curNote;
                tCorrection = (float) s;
            }
            else
            {
                src = std::min (limit, nextMark - 0.5 * Punv);
                if (wasVoiced) { tTargetNote = -1; tCorrection = 0.0f; }
                wasVoiced = false;
            }

            Grain& g = grains[numGrains++];
            g.src = src; g.dst = nextMark; g.invH = 1.0 / H; g.fF = fF; g.gain = std::min (hop / H, 2.0);
            delaySm += ((nextMark - src) - delaySm) * 0.05;
            nextMark += hop;
        }
    }

    // ---------------- state ----------------
    static constexpr double kDetectSeconds = 8.0;
    static constexpr int kMaxGrains = 48;

    double fs = 48000.0, fsd = 12000.0;
    int nch = 2, D = 4, tauMaxD = 174, tauMinD = 10, winD = 182, hopD = 32;
    double Pmax = 686, Punv = 192;
    int refineWin = 768, latency = 96;
    int ringSize = 16384, ringMask = 16383, decSize = 2048;

    std::vector<std::vector<float>> in;
    std::vector<float> mono, dec, fir, diff, cmnd;
    int64_t n = 0, decN = 0;

    Frame raw[3] {}, cur {};
    int64_t nRaw = 0;

    Grain grains[kMaxGrains] {};
    int numGrains = 0;
    double nextMark = 0, lastMark = 0, lastHop = 200, delaySm = 0;
    bool markValid = false, wasVoiced = false;
    float polarity = 1.0f;
    int curNote = -1;
    double shift = 0, center = 0, decPitch = 0, pendSlow = 0, pendFast = 0;
    int pendSlowNote = -1;
    double eIn = 0, eOut = 0, comp = 1.0, aLvl = 0.0003, aComp = 0.0004;

    bool detecting = false;
    double pcHist[12] {};
    double detectVoicedSec = 0;

    Params prm;
};

} // namespace ubt
