// UBTune — Realtime Pitch Correction Engine
// by unique & Mbezza (Gelo Santino Mares · Meron Negash) — UNIQUEBABY
//
// Pure C++17, no framework dependency (used by the JUCE plugin and the offline tests).
//  - Pitch detection: YIN on the most recent ~26 ms (decimated to ~11-12 kHz) + full-rate refinement
//  - Pitch shifting:  low-latency TD-PSOLA. Every grain is evaluated lazily at output time and is
//                     cut from the most recent *already sung* pitch period, so the audio delay is only
//                     a fraction of one period instead of a fixed buffer; on a steady voice the next periods
//                     are predicted (LOW LATENCY: ~2 ms in normal singing range).
//  - Note logic:      scale masks (incl. Pentatonic Major/Minor), hysteresis, retune speed,
//                     humanize (keeps vibrato), three characters: SMOOTH / PRO / ROBOTIC.
//                     Notes are chosen like a singer hears them: capture zones with a dead band,
//                     a minimum note length (no flutter), glides pass through, and a freshly
//                     chosen note may settle once onto the note you actually land on.
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
    float amount      = 1.0f;     // correction strength 0..1
    bool  lowLatency  = true;     // tight delay window (about half the delay) vs CLEAN
    float outGain     = 1.0f;     // linear
};

class Engine
{
public:
    static constexpr double kMinF = 55.0, kMaxF = 1100.0;
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
        rmsDecay = std::exp (-(hopD * D / fs) / 1.5);         // level reference forgets over ~1.5 s
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
        for (auto& r : raw) r = Frame{};
        nRaw = 0; stablePeriod = 0.0; rmsPeak = 0.0; unvoicedFrames = 0;
        cur = Frame{};
        numGrains = 0;
        nextMark = 0.0; lastMark = 0.0; delayCur = 0.0; markCount = 0; markValid = false; wasVoiced = false; polarity = 1.0f; lastHop = 200.0;
        curNote = -1; shift = 0.0; center = 0.0; decPitch = 0.0; pendSlow = pendFast = 0.0; pendSlowNote = -1; slideVel = slideAmt = 0.0; sinceSwitch = 1.0; sinceOnset = 1.0; pendRefine = 0.0; provisional = false; lastNote = -1; sliding = false; delaySm = 0.0; eIn = eOut = 0.0; comp = 1.0;
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
            // ---- candidate selection (robust against octave errors) ----
            // local minima of the normalised difference; global minimum gives the periodicity
            int nCand = 0; int candT[32]; float candV[32];
            float gmin = 1e9f; int gtau = -1;
            for (int tau = tauMinD; tau < tauMaxD && nCand < 32; ++tau)
            {
                const float v = cmnd[(size_t) tau];
                if (v < gmin) { gmin = v; gtau = tau; }
                if (v < 0.6f && v <= cmnd[(size_t) tau - 1] && v <= cmnd[(size_t) tau + 1]) { candT[nCand] = tau; candV[nCand] = v; ++nCand; }
            }
            // voicing with hysteresis: harder to become voiced than to stay voiced
            const bool wasVoiced = cur.voiced;
            bool voiced = gtau > 0 && gmin < (wasVoiced ? 0.33f : 0.22f) && rms > 0.03f * rmsPeak;
            int best = -1;
            if (voiced)
            {
                // the SHORTEST lag that is nearly as periodic as the best one -> no octave-down errors
                const float thr = std::max (0.12f, gmin * 1.6f + 0.05f);
                for (int c = 0; c < nCand; ++c) if (candV[c] < thr) { best = candT[c]; break; }
                if (best < 0) best = gtau;
                // continuity: if this jumps an octave against the stable pitch, keep the stable octave
                // when that lag is (almost) as good -> the detector stays in the range you're singing in
                if (wasVoiced && stablePeriod > 0)
                {
                    const double prevT = stablePeriod / D;
                    const double r = best / prevT;
                    if (std::abs (r - 2.0) < 0.15 || std::abs (r - 0.5) < 0.04 || std::abs (r - 3.0) < 0.2 || std::abs (r - 1.0 / 3.0) < 0.03)
                    {
                        int alt = -1; float altV = 1e9f;
                        for (int c = 0; c < nCand; ++c)
                            if (std::abs (std::log2 (candT[c] / prevT)) < 0.12 && candV[c] < altV) { alt = candT[c]; altV = candV[c]; }
                        if (alt > 0 && altV < cmnd[(size_t) best] + 0.10f) best = alt;
                    }
                }
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
        rmsPeak = std::max ((double) rms, rmsPeak * rmsDecay);

        // smoothing: 2-of-3 voicing, period = median of the voiced frames among the last 5 (log domain)
        for (int k = 0; k < 4; ++k) raw[k] = raw[k + 1];
        raw[4] = f; ++nRaw;
        Frame o = f;
        const int vc3 = (int) raw[2].voiced + (int) raw[3].voiced + (int) raw[4].voiced;
        o.voiced = (nRaw >= 3) && vc3 >= 2;
        if (o.voiced)
        {
            double pv[5]; int np = 0;
            for (int k = 0; k < 5; ++k) if (raw[k].voiced) pv[np++] = raw[k].period;
            for (int i = 1; i < np; ++i) for (int j = i; j > 0 && pv[j - 1] > pv[j]; --j) std::swap (pv[j - 1], pv[j]);
            o.period = np > 0 ? pv[np / 2] : f.period;
            if (np == 0) o.voiced = false;
        }
        if (o.voiced) { stablePeriod = o.period; unvoicedFrames = 0; }
        else if (++unvoicedFrames > 15) stablePeriod = 0.0;   // ~40 ms of silence: forget the range
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

    // ---------------- pitch marks (one per period) ----------------
    // First mark at onset = waveform peak. Every following mark is found by cross-correlating the
    // last full period with the candidate period (robust on rough / breathy voices), so the marks
    // follow the real period-to-period spacing of the voice.
    void pushMark (double m)
    {
        marks[(size_t) (markCount % kMarks)] = m;
        ++markCount;
        lastMark = m;
    }
    double markAt (int64_t idx) const { return marks[(size_t) (idx % kMarks)]; }

    void trackMarks()
    {
        if (! cur.voiced) { markValid = false; return; }
        const double P = cur.period;
        const double guard = std::ceil (0.16 * P) + 2.0;
        if (! markValid || lastMark < (double) n - 3.0 * P)
        {
            if ((double) n < 2.0 * P + guard + 2) return;
            markCount = 0;
            pushMark (peakIn ((double) n - P - guard, (double) n - guard, true));
            markValid = true;
            return;
        }
        while (lastMark + P + guard <= (double) n)
        {
            const double cand = lastMark + P;
            double m = corrMark (lastMark, cand, P);
            if (m <= lastMark + 0.6 * P || m >= lastMark + 1.5 * P) m = cand;
            pushMark (m);
        }
    }

    /** Best position near cand whose preceding period looks most like the period before prev. */
    double corrMark (double prev, double cand, double P)
    {
        const int L = std::max (16, std::min ((int) std::lround (P), 768));
        const int R = std::max (2, (int) std::lround (0.14 * P));
        const int64_t ip = (int64_t) std::lround (prev), ic = (int64_t) std::lround (cand);
        double best = -1e30; int bk = 0; double prevV = -1e30;
        double bestL = 0, bestR = 0;
        const int step = L > 256 ? 2 : 1;
        for (int k = -R; k <= R; ++k)
        {
            double xy = 0, yy = 0;
            for (int j = 0; j < L; j += step)
            {
                const double a = mono[(size_t) ((ip - L + j) & ringMask)];
                const double b = mono[(size_t) ((ic + k - L + j) & ringMask)];
                xy += a * b; yy += b * b;
            }
            const double v = xy / std::sqrt (yy + 1e-12);
            if (v > best) { best = v; bk = k; bestL = prevV; bestR = -1e30; }
            else if (k == bk + 1) bestR = v;
            prevV = v;
        }
        double frac = 0.0;
        if (bestL > -1e29 && bestR > -1e29)
        {
            const double den = bestL - 2 * best + bestR;
            if (std::abs (den) > 1e-12) frac = std::clamp (0.5 * (bestL - bestR) / den, -0.5, 0.5);
        }
        return (double) ic + bk + frac;
    }

    /** Period of a steady voice (last 4 mark spacings agree), else 0 -> no prediction. */
    double predictedPeriod() const
    {
        if (kPredict <= 0 || markCount < 5 || ! cur.voiced) return 0.0;
        const double pAvg4 = (markAt (markCount - 1) - markAt (markCount - 5)) / 4.0;
        double maxDev = 0.0;
        for (int64_t k = markCount - 4; k < markCount; ++k)
            maxDev = std::max (maxDev, std::abs ((markAt (k) - markAt (k - 1)) - pAvg4));
        return (maxDev < kPredictJitter * pAvg4 && std::abs (pAvg4 - cur.period) < 0.06 * cur.period) ? pAvg4 : 0.0;
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
    struct ZoneCfg { double tauDec, margin, hold, fastMargin, fastHold, minDur, slideThr; };
    ZoneCfg zoneCfg() const
    {
        switch (prm.mode)
        {
            case Mode::Smooth: return { 0.090, 0.25, 0.060, 0.40, 0.035, 0.120, 12.0 };
            case Mode::Pro:    return { 0.075, 0.22, 0.045, 0.40, 0.018, 0.090, 20.0 };
            default:           return { 0.065, 0.20, 0.030, 0.40, 0.012, 0.060, 1e9 };
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
        const double prevDec = decPitch;
        if (onset) { decPitch = midi; slideVel = 0.0; }
        else decPitch += (midi - decPitch) * (1.0 - std::exp (-dt / z.tauDec));
        if (! onset && dt > 0) slideVel += ((decPitch - prevDec) / dt - slideVel) * (1.0 - std::exp (-dt / 0.03));
        sinceSwitch += dt;
        const bool wasSliding = sliding;
        sliding = std::abs (slideVel) > z.slideThr;

        if (onset) sinceOnset = 0.0; else sinceOnset += dt;
        if (onset || curNote < 0 || ! allowedIn (prm.mask, curNote))
        {
            curNote = nearestAllowed (decPitch); lastNote = -1;
            pendSlow = pendFast = 0.0; pendSlowNote = -1; sinceSwitch = 0.0; provisional = true; pendRefine = 0.0;
            return;
        }
        if (provisional && sinceSwitch < 0.25 && sinceSwitch > 0.03 && ! sliding)
        {
            // a quickly chosen note is only provisional: once the voice has settled, move ONCE to the
            // note it actually holds (averaged pitch, no hysteresis) - kills scoop "staircases"
            const int c = nearestAllowed (decPitch);
            if (c != curNote && c != lastNote && pastBoundary (decPitch, curNote, c) > 0.15) pendRefine += dt; else pendRefine = 0.0;
            if (pendRefine >= 0.06)
            {
                curNote = c; decPitch = midi; provisional = false; pendRefine = 0.0;
                pendSlow = pendFast = 0.0; pendSlowNote = -1;
                return;
            }
        }
        else if (sinceSwitch >= 0.25) provisional = false;
        if (wasSliding && ! sliding)
        {
            // landing after a scoop / glide: take the note you actually landed on (no hysteresis)
            const int c = nearestAllowed (midi);
            if (c != curNote && pastBoundary (midi, curNote, c) > 0.08)
            {
                lastNote = curNote; curNote = c; decPitch = midi; provisional = true; pendRefine = 0.0;
                pendSlow = pendFast = 0.0; pendSlowNote = -1; sinceSwitch = 0.0;
                return;
            }
        }
        if (sinceOnset < 0.08 && ! sliding)
        {
            // the first frames after an onset are still settling: if the voice lands clearly
            // elsewhere (> 1 semitone past the boundary), re-pick at once instead of waiting out minDur
            const int c = nearestAllowed (midi);
            if (c != curNote && pastBoundary (midi, curNote, c) > 1.0)
            {
                lastNote = curNote; curNote = c; decPitch = midi; provisional = true; pendRefine = 0.0;
                pendSlow = pendFast = 0.0; pendSlowNote = -1; sinceSwitch = 0.0;
                return;
            }
        }
        const int candSlow = nearestAllowed (decPitch);
        const int candFast = nearestAllowed (midi);
        int target = -1;

        // fast path: raw pitch sits deep inside another note's zone (a real jump / slide).
        // Way past the boundary (> 1 semitone, wider than any vibrato) -> switch at once;
        // otherwise it has to stay away from the current note for fastHold.
        const double pastFast = candFast != curNote ? pastBoundary (midi, curNote, candFast) : -1.0;
        // timers keep running while a note is young / a glide is in flight; only the switch itself waits
        const bool canSwitch = sinceSwitch >= z.minDur && ! sliding;
        if (pastFast > 2.0 && ! sliding) target = candFast;
        else if (pastFast > 1.0) target = candFast;
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
            // real leaps (> 2 st past the boundary) go as soon as the glide has settled;
            // everything closer has to respect minDur (no flutter between neighbours)
            const bool leap = pastFast > 2.0 && pastBoundary (decPitch, curNote, target) > 1.0;
            if (leap ? (sliding && sinceSwitch < 0.15) : ! canSwitch) target = -1;
        }

        if (target >= 0)
        {
            provisional = true; pendRefine = 0.0;
            lastNote = curNote; curNote = target;
            sinceSwitch = 0.0;
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
        // while you slide (SMOOTH / PRO) the correction steps back, so glides stay natural
        slideAmt += ((sliding ? 1.0 : 0.0) - slideAmt) * (1.0 - std::exp (-dt / (sliding ? 0.02 : 0.06)));
        // voice already far inside another note's zone (glide not yet decided): don't drag it back
        double away = 0.0;
        if (prm.mode != Mode::Robotic)
        {
            const int nn = nearestAllowed (midi);
            if (nn != curNote) away = std::clamp (pastBoundary (midi, curNote, nn) - 1.0, 0.0, 1.0);
        }
        const double desired = (curNote - eff) * std::clamp ((double) prm.amount, 0.0, 1.0) * (1.0 - 0.85 * slideAmt) * (1.0 - away);

        double tauR = std::max (0.0, (double) prm.retuneMs) / 1000.0;
        if (prm.mode == Mode::Robotic) tauR = std::min (tauR, 0.004); // robotic = instant snap
        if (onset || tauR <= 0.0005) shift = desired;
        else shift += (desired - shift) * (1.0 - std::exp (-dt / tauR));
        return shift;
    }

    // ---------------- low-latency PSOLA: grains are created just in time ----------------
    // Each output grain is centred on an output mark t; its source is the confirmed input mark closest
    // to t - delay. The delay is held inside a fixed one-period window [dLo, dLo + P]: at 0 % correction
    // output marks advance by exactly the real mark spacing, so the voice is reproduced 1:1 (no repeated
    // or dropped periods). Periods are only repeated / skipped where the pitch really gets shifted.
    void spawnGrains (double fF)
    {
        const double t = (double) n;
        if (nextMark < t - 2.0 * Pmax) nextMark = t;

        for (int guard = 0; guard < 16 && numGrains < kMaxGrains; ++guard)
        {
            const bool voiced = cur.voiced && markValid && markCount > 0;
            const double Pdet = voiced ? cur.period : Punv;
            const double H = Pdet / fF;
            if (nextMark - H > t) break;

            const double limit = nextMark - H * std::abs (fF - 1.0) - 3.0;   // causality inside the grain
            double src, hop, Hs = H;
            if (voiced)
            {
                const bool onset = ! wasVoiced;
                const double dLo = H * std::abs (fF - 1.0) + 3.0;
                // newest usable source position (confirmed mark, or a predicted one on a steady voice)
                double newest = -1e30;
                for (int64_t k = markCount - 1; k >= std::max<int64_t> (0, markCount - kMarks + 1); --k)
                    if (markAt (k) <= limit) { newest = markAt (k); break; }
                const double pred = predictedPeriod();
                if (pred > 0) for (int j = 1; j <= kPredict; ++j) { const double m = lastMark + j * pred; if (m <= limit) newest = std::max (newest, m); }
                // delay is kept inside ONE period above the minimum: a period is skipped / repeated as soon
                // as the shift needs it, so the delay never piles up
                // LOW LATENCY: tight window, follows the newest periods (skips/repeats a period more often)
                // CLEAN: wider window, fewer period skips on wobbly voices
                const double win = prm.lowLatency ? kWindowLow : kWindowClean;
                const double dHi = std::max (dLo, nextMark - newest) + win * Pdet;
                const double d = std::clamp (delayCur, dLo, dHi);
                const double a = nextMark - d;
                // nearest confirmed mark to a, that is still causal
                int64_t bi = -1; double bd = 1e30;
                const int64_t first = std::max<int64_t> (0, markCount - kMarks + 1);
                for (int64_t k = markCount - 1; k >= first; --k)
                {
                    const double m = markAt (k);
                    if (m > limit) continue;
                    const double dist = std::abs (m - a);
                    if (dist < bd) { bd = dist; bi = k; }
                    if (m < a - 2.0 * Pdet) break;
                }
                if (bi < 0) { bi = markCount - 1; }
                src = markAt (bi);
                double Ploc = Pdet, Pavg = Pdet;
                if (bi > 0 && bi - 1 >= first)
                {
                    Ploc = std::clamp (src - markAt (bi - 1), 0.75 * Pdet, 1.33 * Pdet);
                    const int64_t back = std::min<int64_t> (prm.mode == Mode::Robotic ? 8 : 4, bi - first);
                    const double avg = (src - markAt (bi - back)) / (double) back;
                    if (avg > 0.8 * Pdet && avg < 1.25 * Pdet) Pavg = avg;
                }
                // low-latency: on a steady voice the next periods are predicted from the rhythm of the
                // last ones instead of waiting for their confirmation (saves ~1 period of delay)
                if (pred > 0 && bi == markCount - 1)
                {
                    for (int j = 1; j <= kPredict; ++j)
                    {
                        const double m = lastMark + j * pred;
                        if (m > limit) break;
                        const double dist = std::abs (m - a);
                        if (dist < bd) { bd = dist; src = m; Ploc = Pavg = pred; }
                    }
                }
                while (src > limit) src -= Pdet;
                Hs = Ploc / fF;

                // pitch + timing from the real mark spacing: exact (no drift) and smooth (no jitter);
                // humanize decides how much of the voice's own period-to-period jitter survives
                const double midi = 69.0 + 12.0 * std::log2 (fs / Pavg / 440.0);
                const double sh = updateShift (midi, lastHop / fs, onset);
                const double ratio = std::pow (2.0, (sh + prm.transpose) / 12.0);
                // ROBOTIC: averaged period -> dead-steady pitch; otherwise the real local period -> 1:1 voice texture
                const double Pbase = prm.mode == Mode::Robotic ? Pavg : Ploc;
                hop = Pbase / std::clamp (ratio, 0.25, 4.0);
                lastHop = hop;
                wasVoiced = true;
                tTargetNote = curNote;
                tCorrection = (float) sh;
            }
            else
            {
                // keep the delay continuous through consonants / breaths, drift slowly to the minimum
                const double dTarget = H * std::abs (fF - 1.0) + 3.0 + 0.5 * Punv;
                delayCur += (dTarget - delayCur) * 0.05;
                src = std::min (limit, nextMark - delayCur);
                hop = Punv;
                if (wasVoiced) { tTargetNote = -1; tCorrection = 0.0f; }
                wasVoiced = false;
            }

           #ifdef UBT_DEBUG
            if (voiced && dbgPrevVoiced) { const double jump = (src - dbgPrevSrc) - dbgPrevHop; dbgGrains++; if (std::abs (jump) > 0.25 * Pdet) dbgEvents++; }
            dbgPrevSrc = src; dbgPrevHop = hop; dbgPrevVoiced = voiced;
           #endif
            delayCur = nextMark - src;
            Grain& g = grains[numGrains++];
            g.src = src; g.dst = nextMark; g.invH = 1.0 / Hs; g.fF = fF; g.gain = std::min (hop / Hs, 2.0);
            delaySm += ((nextMark - src) - delaySm) * 0.05;
            nextMark += hop;
        }
    }

    // ---------------- state ----------------
    static constexpr double kDetectSeconds = 8.0;
    static constexpr int kMaxGrains = 48;
    static constexpr int kPredict = 2;                 // predicted periods on a steady voice
    static constexpr double kPredictJitter = 0.08;     // max period-to-period deviation for prediction
    static constexpr double kWindowClean = 1.0;        // delay window (periods above the minimum): CLEAN
    static constexpr double kWindowLow = 0.35;         //                                          LOW LATENCY

    double fs = 48000.0, fsd = 12000.0;
    int nch = 2, D = 4, tauMaxD = 174, tauMinD = 10, winD = 182, hopD = 32;
    double Pmax = 686, Punv = 192;
    int refineWin = 768, latency = 96;
    int ringSize = 16384, ringMask = 16383, decSize = 2048;

    std::vector<std::vector<float>> in;
    std::vector<float> mono, dec, fir, diff, cmnd;
    int64_t n = 0, decN = 0;

    Frame raw[5] {}, cur {};
    double stablePeriod = 0.0, rmsPeak = 0.0, rmsDecay = 0.999;
    int unvoicedFrames = 0;
    int64_t nRaw = 0;

    Grain grains[kMaxGrains] {};
    int numGrains = 0;
    double nextMark = 0, lastMark = 0, lastHop = 200, delaySm = 0, delayCur = 0;
    static constexpr int kMarks = 64;
    std::array<double, kMarks> marks {};
    int64_t markCount = 0;
   #ifdef UBT_DEBUG
public:
    long dbgGrains = 0, dbgEvents = 0; double dbgPrevSrc = 0, dbgPrevHop = 0; bool dbgPrevVoiced = false;
private:
   #endif
    bool markValid = false, wasVoiced = false;
    float polarity = 1.0f;
    int curNote = -1;
    double shift = 0, center = 0, decPitch = 0, pendSlow = 0, pendFast = 0;
    int pendSlowNote = -1;
    double slideVel = 0, slideAmt = 0, sinceSwitch = 1.0, sinceOnset = 1.0, pendRefine = 0.0; bool provisional = false; int lastNote = -1;
    bool sliding = false;
    double eIn = 0, eOut = 0, comp = 1.0, aLvl = 0.0003, aComp = 0.0004;

    bool detecting = false;
    double pcHist[12] {};
    double detectVoicedSec = 0;

    Params prm;
};

} // namespace ubt
