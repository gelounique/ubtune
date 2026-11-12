// Offline test: synthetic voice -> UBTune engine -> wav (+ csv of telemetry)
#include "../plugin/source/dsp/UBTuneEngine.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <random>
#include <chrono>

static void writeWav (const char* path, const std::vector<float>& x, int fs)
{
    FILE* f = fopen (path, "wb");
    int n = (int) x.size(); int bytes = n * 2;
    auto w32 = [&] (uint32_t v) { fwrite (&v, 4, 1, f); }; auto w16 = [&] (uint16_t v) { fwrite (&v, 2, 1, f); };
    fwrite ("RIFF", 1, 4, f); w32 (36 + bytes); fwrite ("WAVEfmt ", 1, 8, f); w32 (16); w16 (1); w16 (1); w32 (fs); w32 (fs * 2); w16 (2); w16 (16);
    fwrite ("data", 1, 4, f); w32 (bytes);
    for (float v : x) { int s = (int) std::lround (std::clamp (v, -1.0f, 1.0f) * 32767); w16 ((uint16_t) (int16_t) s); }
    fclose (f);
}

// formant resonator
struct Reso { double a1, a2, b0, y1 = 0, y2 = 0;
  Reso (double f, double bw, double fs) { double r = std::exp (-ubt::kPi * bw / fs); a1 = 2 * r * std::cos (2 * ubt::kPi * f / fs); a2 = -r * r; b0 = 1 - r; }
  double tick (double x) { double y = b0 * x + a1 * y1 + a2 * y2; y2 = y1; y1 = y; return y; } };

int main (int argc, char** argv)
{
    const int fs = argc > 1 ? atoi (argv[1]) : 48000;
    const int mode = argc > 2 ? atoi (argv[2]) : 2;
    const std::string outp = argc > 3 ? argv[3] : "/tmp/ubt_out.wav";
    // pitch contour (midi): A minor-ish melody sung slightly off, with vibrato & glides
    struct Seg { double midi, dur; };
    std::vector<Seg> segs = { {57.3, .6}, {60.4, .5}, {62.2, .5}, {63.6, .6}, {64.2, .7}, {66.8, .5}, {67.3, .6}, {69.0, .8}, {45.4,.6}, {72.6,.6} };
    std::vector<float> x; std::vector<double> truth;
    double ph = 0; Reso f1 (700, 90, fs), f2 (1200, 110, fs), f3 (2600, 160, fs);
    std::mt19937 rng (1); std::normal_distribution<double> nd (0, 1);
    double cur = segs[0].midi, t = 0;
    for (auto& s : segs)
    {
        int N = (int) (s.dur * fs);
        for (int i = 0; i < N; ++i, t += 1.0 / fs)
        {
            cur += (s.midi - cur) * (1 - std::exp (-1.0 / (0.03 * fs)));
            double m = cur + 0.25 * std::sin (2 * ubt::kPi * 5.5 * t);
            double f = 440 * std::pow (2, (m - 69) / 12);
            ph += f / fs; if (ph >= 1) ph -= 1;
            double src = (ph < 0.6 ? std::sin (ubt::kPi * ph / 0.6) * std::sin (ubt::kPi * ph / 0.6) : 0) - 0.3 + 0.01 * nd (rng);
            double v = f1.tick (src) + 0.6 * f2.tick (src) + 0.3 * f3.tick (src);
            x.push_back ((float) (v * 0.25)); truth.push_back (m);
        }
        // short gap (unvoiced breath)
        for (int i = 0; i < fs / 20; ++i, t += 1.0 / fs) { x.push_back ((float) (0.004 * nd (rng))); truth.push_back (-1); }
    }
    ubt::Engine e; e.prepare (fs, 1);
    ubt::Params p; p.mode = (ubt::Mode) mode; p.mask = ubt::scaleMask (9, ubt::PentMinor);
    p.retuneMs = mode == 2 ? 0 : (mode == 1 ? 20 : 70); p.humanize = mode == 2 ? 0 : (mode == 1 ? .25f : .6f);
    e.setParams (p);
    std::vector<float> y = x; const int B = 256;
    FILE* csv = fopen ("/tmp/ubt_tel.csv", "w");
    for (size_t i = 0; i < y.size(); i += B)
    {
        int nn = (int) std::min<size_t> (B, y.size() - i); float* ch[1] = { y.data() + i };
        e.process (ch, 1, nn);
        fprintf (csv, "%zu,%.3f,%d,%.3f\n", i, e.tInputMidi.load(), e.tTargetNote.load(), e.tCorrection.load());
    }
    fclose (csv);
    printf ("latency %d samples (%.1f ms)\n", e.getLatency(), 1000.0 * e.getLatency() / fs);
    writeWav ("/tmp/ubt_in.wav", x, fs); writeWav (outp.c_str(), y, fs);
    FILE* tf = fopen ("/tmp/ubt_truth.txt", "w"); for (size_t i = 0; i < truth.size(); i += 480) fprintf (tf, "%zu %.3f\n", i, truth[i]); fclose (tf);
    // perf
    std::vector<float> big (fs * 20); for (auto& v : big) v = (float) (0.1 * nd (rng)); 
    for (size_t i = 0; i < big.size(); ++i) big[i] += x[i % x.size()];
    float* chb[1] = { big.data() };
    auto t0 = std::chrono::steady_clock::now(); e.process (chb, 1, (int) big.size());
    double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    printf ("perf: 20 s audio in %.3f s (%.1f%% of realtime, mono)\n", sec, sec / 20 * 100);
}
