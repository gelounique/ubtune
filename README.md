# UBTune — by unique & Mbezza

Realtime Pitch Correction · ~2 ms Latenz (tiefe Stimmen ca. 4–9 ms) · Schwarz/Weiß-UI mit lila-blauem Shockwave-Orb · Oktav-Keyboard mit Input/Output-Heatmaps.
Entwickelt von **unique & Mbezza** — Gelo Santino Mares · Meron Negash · [uniquebaby.de](https://uniquebaby.de)

**Webseite & Download:** https://ubtune.uniquebaby.de · [Releases](https://github.com/gelounique/ubtune/releases/latest)

| Modus | Charakter | Retune | Humanize |
|---|---|---|---|
| **SMOOTH** | natürlich, Real-Time-Style, Vibrato bleibt | 70 ms | 60 % |
| **PRO** | klassisch tight, Pro-Style | 20 ms | 25 % |
| **ROBOTIC** | Robot / Hard-Snap (T-Pain-/MetaTune-Vibe), stellt auf **Pentatonik Dur/Moll** | 0 ms | 0 % |

Skalen: Chromatic, Major, Minor, **Pentatonic Major**, **Pentatonic Minor**, Blues, Harmonic Minor, Dorian, Custom (Tasten am Piano anklicken).
Regler: **Retune Speed** (groß), **Humanize** (mittel), **Formant**, **Mix** (klein), Transpose (±12 st), Output. **LOW LATENCY / CLEAN** (Klick auf die Latenzanzeige unten rechts, automatisierbar). **Detect Key** (großer Button) hört 8 s zu und setzt Key + Scale.

## Notenwahl (1.1.2)
- **Fangzonen ohne Überlappung:** jede erlaubte Note gehört bis zur Mitte zum nächsten Skalenton, um jede Grenze liegt eine **Totzone** (Vibrato wird rausgemittelt).
- **Range-Lock in der Erkennung:** die Tonhöhenerkennung bleibt in der Oktave, in der du gerade singst (Oktavfehler auf echten Stimmen 5,3 % → 1,4 %), Kandidaten-Auswahl statt erstem Treffer, Stimmhaft-Hysterese, Median über 5 Frames.
- **Mindest-Notenlänge** (SMOOTH 120 ms · PRO 90 ms · ROBOTIC 60 ms): keine 20-ms-Flatternoten mehr – Noten < 60 ms auf echten Stimmen 62 % → 18 %, Notenwechsel/s halbiert.
- **Glides gehen durch:** während du slidest, zieht UBTune nicht zur alten Note zurück; beim Landen wird **einmal** die Note gewählt, auf der du wirklich ankommst (keine Treppen bei Scoops).
- **Weniger kratzig:** Pitch-Marks per Kreuzkorrelation statt Spitzenwert, Grains im festen Ein-Perioden-Fenster → 1:1-Wiedergabe der Stimme ohne doppelte/verlorene Perioden (Klangtreue bei 0 % Korrektur 5,9 → 10,6 dB SNR auf echten Aufnahmen).

Tests: `tests/chatter_test.cpp` (gehaltene Töne auf der Grenze), `tests/voice_run.cpp` + `tests/voice_eval.py` (echte Aufnahmen gegen librosa-pYIN: Oktavfehler, Notenwechsel, Artefakte, Quantisierung).

## Latenz (1.1.3)
- **Vorhersage:** bei stabilem Ton (letzte 4 Perioden weichen < 8 % ab) werden die nächsten 2 Schwingungen aus dem Rhythmus vorhergesagt, statt auf ihre Bestätigung zu warten.
- **Enges Verzögerungsfenster:** die Verzögerung darf nicht mehr auf über 2 Perioden auflaufen, bevor eine Periode übersprungen wird.
- **LOW LATENCY** (Standard): Fenster 0,35 Perioden. **CLEAN**: 1 Periode – etwas mehr Latenz, weniger Perioden-Sprünge bei wackligen Stimmen.

| echte Verzögerung (Median) | 1.1.2 | 1.1.3 CLEAN | 1.1.3 LOW LATENCY |
|---|---|---|---|
| Gesang ~220 Hz | 5,8 ms | 3,9 ms | 1,8 ms |
| Männerstimme ~110–140 Hz | 8–11 ms | 6–7 ms | 4 ms |
| sehr tiefe Stimme ~76 Hz | 16 ms | 13 ms | 9 ms |

## Formate
- **Windows 10/11 x64:** VST3 + Standalone — `UBTune-Windows-Setup.exe` (Installer + Deinstaller)
- **macOS 10.13+ Universal:** VST3 + AU + Standalone — `UBTune-macOS.dmg` (.pkg + Deinstaller)
- Läuft in FL Studio, Ableton Live, Logic Pro, GarageBand, Cubase, Studio One, Reaper, Bitwig … (Pro Tools bräuchte AAX — nicht enthalten)

## Technik
- `plugin/source/dsp/UBTuneEngine.h` — reine C++17-Engine: YIN-Pitch-Detection (dezimiert + Full-Rate-Refinement) → Notenlogik mit Hysterese/Retune/Humanize → Low-Latency-TD-PSOLA: jedes Grain wird erst im Moment der Ausgabe aus der zuletzt gesungenen Periode gelesen + Perioden-Vorhersage → echte Verzögerung ≈ 2 ms in normaler Gesangslage, bei sehr tiefen Stimmen ca. 4–9 ms (LOW LATENCY); der DAW werden 2 ms gemeldet. Formant ±6 st, automatische Pegel-Angleichung.
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
