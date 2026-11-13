// Measures "note chatter": how often the target note flips while a note is HELD
// (vibrato + drift sitting close to the boundary between two allowed notes).
#include "../plugin/source/dsp/UBTuneEngine.h"
#include <cstdio>
#include <random>
#include <string>

struct Case { const char* name; double midi; double vibDepth; double drift; int scale; int root; };

int main (int argc, char** argv)
{
    const int fs = 48000;
    Case cases[] = {
        { "chromatic, sung 60.45 (+45c), vib 30c", 60.45, 0.30, 0.10, ubt::Chromatic, 0 },
        { "chromatic, sung 60.52 (on edge), vib 25c", 60.52, 0.25, 0.08, ubt::Chromatic, 0 },
        { "A pent-minor, sung 61.0 (C|D mid), vib 35c", 61.00, 0.35, 0.15, ubt::PentMinor, 9 },
        { "A pent-minor, sung 65.6 (E|G edge), vib 30c", 65.60, 0.30, 0.10, ubt::PentMinor, 9 },
        { "C major, sung 64.48 (E|F edge), vib 20c", 64.48, 0.20, 0.06, ubt::Major, 0 },
        { "chromatic, low voice 45.5, vib 30c", 45.50, 0.30, 0.10, ubt::Chromatic, 0 },
    };
    int totalFlips[3] = { 0, 0, 0 };
    for (int mode = 0; mode < 3; ++mode)
    {
        for (auto& c : cases)
        {
            ubt::Engine e; e.prepare (fs, 1);
            ubt::Params p; p.mode = (ubt::Mode) mode; p.mask = ubt::scaleMask (c.root, c.scale);
            p.retuneMs = mode == 2 ? 0.0f : (mode == 1 ? 20.0f : 70.0f); p.humanize = mode == 2 ? 0.0f : (mode == 1 ? 0.25f : 0.6f);
            e.setParams (p);
            std::mt19937 rng (5); std::normal_distribution<double> nd (0, 1);
            double ph = 0, wander = 0; const int N = fs * 3;
            std::vector<float> x (N);
            for (int i = 0; i < N; ++i)
            {
                const double t = (double) i / fs;
                wander += (nd (rng) * 0.002 - wander * 0.0005);
                const double m = c.midi + c.vibDepth * std::sin (2 * ubt::kPi * 5.6 * t) + c.drift * std::sin (2 * ubt::kPi * 0.7 * t) + wander;
                ph += 440 * std::pow (2, (m - 69) / 12) / fs; if (ph >= 1) ph -= 1;
                x[i] = (float) (0.25 * ((ph < 0.6 ? std::pow (std::sin (ubt::kPi * ph / 0.6), 2) : 0) - 0.3) + 0.003 * nd (rng));
            }
            int last = -1, flips = 0;
            for (int i = 0; i < N; i += 32)
            {
                float* ch[1] = { x.data() + i }; e.process (ch, 1, 32);
                const int tn = e.tTargetNote.load();
                if (i > fs / 5 && tn >= 0) { if (last >= 0 && tn != last) ++flips; last = tn; }
            }
            totalFlips[mode] += flips;
            std::printf ("%-8s %-46s flips: %d\n", mode == 0 ? "SMOOTH" : mode == 1 ? "PRO" : "ROBOTIC", c.name, flips);
        }
    }
    std::printf ("TOTAL flips  smooth %d  pro %d  robotic %d\n", totalFlips[0], totalFlips[1], totalFlips[2]);
}
