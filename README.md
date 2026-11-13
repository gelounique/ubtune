# UBTune — by unique & Mbezza

Realtime Pitch Correction · ~3 ms Latenz · Schwarz/Weiß-UI mit lila-blauem Shockwave-Orb · Oktav-Keyboard mit Input/Output-Heatmaps.
Entwickelt von **unique & Mbezza** — Gelo Santino Mares · Meron Negash · [uniquebaby.de](https://uniquebaby.de)

**Webseite & Download:** https://ubtune.uniquebaby.de · [Releases](https://github.com/gelounique/ubtune/releases/latest)

| Modus | Charakter | Retune | Humanize |
|---|---|---|---|
| **SMOOTH** | natürlich, Real-Time-Style, Vibrato bleibt | 70 ms | 60 % |
| **PRO** | klassisch tight, Pro-Style | 20 ms | 25 % |
| **ROBOTIC** | Robot / Hard-Snap (T-Pain-/MetaTune-Vibe), stellt auf **Pentatonik Dur/Moll** | 0 ms | 0 % |

Skalen: Chromatic, Major, Minor, **Pentatonic Major**, **Pentatonic Minor**, Blues, Harmonic Minor, Dorian, Custom (Tasten am Piano anklicken).
Regler: **Retune Speed** (groß), **Humanize** (mittel), **Formant**, **Mix** (klein), Transpose (±12 st), Output. **Detect Key** (großer Button) hört 8 s zu und setzt Key + Scale.

## Formate
- **Windows 10/11 x64:** VST3 + Standalone — `UBTune-Windows-Setup.exe` (Installer + Deinstaller)
- **macOS 10.13+ Universal:** VST3 + AU + Standalone — `UBTune-macOS.dmg` (.pkg + Deinstaller)
- Läuft in FL Studio, Ableton Live, Logic Pro, GarageBand, Cubase, Studio One, Reaper, Bitwig … (Pro Tools bräuchte AAX — nicht enthalten)

## Technik
- `plugin/source/dsp/UBTuneEngine.h` — reine C++17-Engine: YIN-Pitch-Detection (dezimiert + Full-Rate-Refinement) → Notenlogik mit Hysterese/Retune/Humanize → Low-Latency-TD-PSOLA: jedes Grain wird erst im Moment der Ausgabe aus der zuletzt gesungenen Periode gelesen → echte Verzögerung ≈ 1–5 ms (typisch ~3 ms), der DAW werden 2 ms gemeldet. Formant ±6 st, automatische Pegel-Angleichung.
- `plugin/source/ui/` — Shockwave-Orb (1500 Staub-Partikel mit Erosion + Trails, Nebel-Kern, Shockwaves bei Notenwechseln), Oktav-Keyboard mit Heatmaps, segmentierte Regler, Theme (Instrument Sans + JetBrains Mono).
- `tests/` — `engine_test.cpp` (synthetische Stimme → Engine → WAV), `analyze.py` (misst Cent-Abweichung), `snapshot.cpp` (rendert die GUI als PNG), `stress.cpp` (Automation-Stress mit offenem Editor).
- `web/` — Webseite (GitHub Pages, Branch `gh-pages`) inkl. Live-Browser-Demo.

## Build
```
cmake -S . -B build            # holt JUCE 8 automatisch (oder -DJUCE_PATH=...)
cmake --build build --config Release
```
CI: `.github/workflows/build.yml` baut Windows + macOS, prüft mit pluginval (Level 7) + auval, packt die Installer und veröffentlicht automatisch das Release `v<Version aus CMakeLists.txt>`. Version hochzählen → neues Release.

© 2026 unique & Mbezza · UNIQUEBABY — alle Rechte vorbehalten.
