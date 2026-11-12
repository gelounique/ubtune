#!/bin/bash
# UBTune — Deinstaller (entfernt VST3, AU und App)
echo "UBTune wird entfernt … (Admin-Passwort nötig)"
sudo rm -rf "/Library/Audio/Plug-Ins/VST3/UBTune.vst3" \
            "/Library/Audio/Plug-Ins/Components/UBTune.component" \
            "/Applications/UBTune.app" \
            "$HOME/Library/Audio/Plug-Ins/VST3/UBTune.vst3" \
            "$HOME/Library/Audio/Plug-Ins/Components/UBTune.component"
for id in de.uniquebaby.ubtune.vst3 de.uniquebaby.ubtune.au de.uniquebaby.ubtune.app; do sudo pkgutil --forget "$id" >/dev/null 2>&1; done
killall -9 AudioComponentRegistrar 2>/dev/null
echo "Fertig – UBTune ist deinstalliert. Fenster kann geschlossen werden."
