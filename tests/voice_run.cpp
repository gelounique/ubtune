// Runs the UBTune engine over a real recording (float wav, mono) and writes output + telemetry.
// usage: voice_run in.wav out.wav tel.csv mode key scale
#include "../plugin/source/dsp/UBTuneEngine.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
static std::vector<float> readWav (const char* p, int& fs)
{
    FILE* f = fopen (p, "rb"); std::vector<float> x; if (! f) return x;
    char id[4]; uint32_t sz; fread (id, 1, 4, f); fread (&sz, 4, 1, f); fread (id, 1, 4, f);
    uint16_t fmt = 1, ch = 1, bits = 16; uint32_t rate = 48000;
    while (fread (id, 1, 4, f) == 4 && fread (&sz, 4, 1, f) == 1)
    {
        if (! memcmp (id, "fmt ", 4)) { std::vector<char> b (sz); fread (b.data(), 1, sz, f); memcpy (&fmt, &b[0], 2); memcpy (&ch, &b[2], 2); memcpy (&rate, &b[4], 4); memcpy (&bits, &b[14], 2); }
        else if (! memcmp (id, "data", 4))
        {
            const int n = (int) (sz / (bits / 8) / ch); x.resize ((size_t) n);
            for (int i = 0; i < n; ++i) { float v = 0; for (int c = 0; c < ch; ++c) { float s; if (bits == 32) fread (&s, 4, 1, f); else { int16_t q; fread (&q, 2, 1, f); s = q / 32768.0f; } if (c == 0) v = s; } x[(size_t) i] = v; }
            break;
        }
        else fseek (f, sz, SEEK_CUR);
    }
    fs = (int) rate; fclose (f); return x;
}
static void writeWav (const char* p, const std::vector<float>& x, int fs)
{
    FILE* f = fopen (p, "wb"); uint32_t n = (uint32_t) x.size(), b = n * 4;
    auto w32 = [&] (uint32_t v) { fwrite (&v, 4, 1, f); }; auto w16 = [&] (uint16_t v) { fwrite (&v, 2, 1, f); };
    fwrite ("RIFF", 1, 4, f); w32 (36 + b); fwrite ("WAVEfmt ", 1, 8, f); w32 (16); w16 (3); w16 (1); w32 (fs); w32 (fs * 4); w16 (4); w16 (32);
    fwrite ("data", 1, 4, f); w32 (b); fwrite (x.data(), 4, n, f); fclose (f);
}
int main (int argc, char** argv)
{
    if (argc < 7) { std::printf ("usage\n"); return 1; }
    int fs; auto x = readWav (argv[1], fs);
    ubt::Engine e; e.prepare (fs, 1);
    ubt::Params p; const int mode = atoi (argv[4]);
    p.mode = (ubt::Mode) mode; p.mask = ubt::scaleMask (atoi (argv[5]), atoi (argv[6]));
    p.retuneMs = mode == 2 ? 0.0f : (mode == 1 ? 20.0f : 70.0f); p.humanize = mode == 2 ? 0.0f : (mode == 1 ? 0.25f : 0.6f);
    if (argc > 7) p.retuneMs = (float) atof (argv[7]);
    if (argc > 8) p.humanize = (float) atof (argv[8]);
    if (argc > 9) p.transpose = (float) atof (argv[9]);
    if (argc > 10) p.amount = (float) atof (argv[10]);
    if (getenv ("UBT_CLEAN")) p.lowLatency = false;
    e.setParams (p);
    FILE* t = fopen (argv[3], "w");
    for (size_t i = 0; i < x.size(); i += 64)
    {
        const int n = (int) std::min<size_t> (64, x.size() - i); float* ch[1] = { x.data() + i };
        e.process (ch, 1, n);
        fprintf (t, "%zu,%.4f,%d,%.4f,%.3f\n", i + n, e.tInputMidi.load(), e.tTargetNote.load(), e.tCorrection.load(), e.tDelayMs.load());
    }
    fclose (t); writeWav (argv[2], x, fs);
   #ifdef UBT_DEBUG
    std::fprintf (stderr, "grains %ld  discontinuities %ld (%.1f%%)\n", e.dbgGrains, e.dbgEvents, 100.0 * e.dbgEvents / std::max (1L, e.dbgGrains));
   #endif
}
