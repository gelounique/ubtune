# UBTune — by unique & Mbezza

Realtime Pitch Correction im UNIQUEBABY-Look · lila Partikel-Ring · animiertes Piano.
Entwickelt von **unique & Mbezza** — Gelo Santino Mares · Meron Negash · [uniquebaby.de](https://uniquebaby.de)

**Webseite & Download:** https://ubtune.uniquebaby.de · [Releases](https://github.com/gelounique/ubtune/releases/latest)

| Modus | Charakter | Retune | Humanize |
|---|---|---|---|
| **SMOOTH** | natürlich, Real-Time-Style, Vibrato bleibt | 70 ms | 60 % |
| **PRO** | klassisch tight, Pro-Style | 20 ms | 25 % |
| **T-PAIN** | Robot / Hard-Snap, MetaTune-Vibe, stellt auf **Pentatonik Dur/Moll** | 0 ms | 0 % |

Skalen: Chromatic, Major, Minor, **Pentatonic Major**, **Pentatonic Minor**, Blues, Harmonic Minor, Dorian, Custom (Tasten am Piano anklicken).
Regler: Retune Speed, Humanize, Formant (±6 st), Mix, Transpose (±12 st), Output. **Detect Key** hört 8 s zu und setzt die Tonart.

## Formate
- **Windows 10/11 x64:** VST3 + Standalone — `UBTune-Windows-Setup.exe` (Installer + Deinstaller)
- **macOS 10.13+ Universal:** VST3 + AU + Standalone — `UBTune-macOS.dmg` (.pkg + Deinstaller)
- Läuft in FL Studio, Ableton Live, Logic Pro, GarageBand, Cubase, Studio One, Reaper, Bitwig … (Pro Tools bräuchte AAX — nicht enthalten)

## Technik
- `plugin/source/dsp/UBTuneEngine.h` — reine C++17-Engine: YIN-Pitch-Detection (dezimiert + Full-Rate-Refinement) → Notenlogik mit Hysterese/Retune/Humanize → TD-PSOLA-Pitch-Shift mit Formant-Kontrolle. Latenz ≈ 34 ms (wird der DAW gemeldet → automatisch kompensiert).
- `plugin/source/ui/` — Partikel-Ring (620 Partikel, 12-Ton-Rad, Kometen-Pitch), Piano C2–C6, Theme (Instrument Sans + JetBrains Mono).
- `tests/` — `engine_test.cpp` (synthetische Stimme → Engine → WAV), `analyze.py` (misst Cent-Abweichung), `snapshot.cpp` (rendert die GUI als PNG), `stress.cpp` (Automation-Stress mit offenem Editor).
- `web/` — Webseite (GitHub Pages, Branch `gh-pages`) inkl. Live-Browser-Demo.

## Build
```
cmake -S . -B build            # holt JUCE 8 automatisch (oder -DJUCE_PATH=...)
cmake --build build --config Release
```
CI: `.github/workflows/build.yml` baut Windows + macOS, prüft mit pluginval (Level 7) + auval, packt die Installer und veröffentlicht automatisch das Release `v<Version aus CMakeLists.txt>`. Version hochzählen → neues Release.

© 2026 unique & Mbezza · UNIQUEBABY — alle Rechte vorbehalten.
