#!/bin/bash
# UBTune — macOS installer: .pkg (VST3 + AU + App, wählbar) in einer .dmg mit Deinstaller
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="$HERE/../.."
ART="${1:-$ROOT/build/UBTune_artefacts/Release}"
VER="${2:-1.0.0}"
OUT="$ROOT/dist"; WORK="$ROOT/build/pkg"
rm -rf "$WORK"; mkdir -p "$WORK"/{vst3,au,app,pkgs,dmg} "$OUT"

cp -R "$ART/VST3/UBTune.vst3" "$WORK/vst3/"
cp -R "$ART/AU/UBTune.component" "$WORK/au/"
cp -R "$ART/Standalone/UBTune.app" "$WORK/app/"

# ad-hoc signature (keine Developer-ID vorhanden) – nötig für Apple Silicon
for b in "$WORK/vst3/UBTune.vst3" "$WORK/au/UBTune.component" "$WORK/app/UBTune.app"; do
  codesign --force --deep --sign - --timestamp=none "$b"
done

chmod +x "$HERE/scripts/postinstall"

# never let Installer "relocate" a bundle to some other copy it finds on disk -> always install to the fixed paths
noreloc () {
  pkgbuild --analyze --root "$1" "$2"
  i=0
  while /usr/libexec/PlistBuddy -c "Print :$i" "$2" >/dev/null 2>&1; do
    /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$2"
    i=$((i+1))
  done
  echo "  $2: $i bundle(s) non-relocatable"
}
noreloc "$WORK/vst3" "$WORK/vst3.plist"
noreloc "$WORK/au"   "$WORK/au.plist"
noreloc "$WORK/app"  "$WORK/app.plist"
pkgbuild --root "$WORK/vst3" --component-plist "$WORK/vst3.plist" --identifier de.uniquebaby.ubtune.vst3 --version "$VER" \
  --scripts "$HERE/scripts" --install-location "/Library/Audio/Plug-Ins/VST3" "$WORK/pkgs/UBTune-VST3.pkg"
pkgbuild --root "$WORK/au" --component-plist "$WORK/au.plist" --identifier de.uniquebaby.ubtune.au --version "$VER" \
  --scripts "$HERE/scripts" --install-location "/Library/Audio/Plug-Ins/Components" "$WORK/pkgs/UBTune-AU.pkg"
pkgbuild --root "$WORK/app" --component-plist "$WORK/app.plist" --identifier de.uniquebaby.ubtune.app --version "$VER" \
  --scripts "$HERE/scripts" --install-location "/Applications" "$WORK/pkgs/UBTune-App.pkg"

sed "s/__VERSION__/$VER/g" "$HERE/distribution.xml" > "$WORK/distribution.xml"
productbuild --distribution "$WORK/distribution.xml" --resources "$HERE/resources" \
  --package-path "$WORK/pkgs" "$WORK/dmg/UBTune Installer.pkg"

cp "$HERE/UBTune deinstallieren.command" "$WORK/dmg/"
chmod +x "$WORK/dmg/UBTune deinstallieren.command"
cp "$HERE/LIES MICH.txt" "$WORK/dmg/"

rm -f "$OUT/UBTune-macOS.dmg"
hdiutil create -volname "UBTune $VER" -srcfolder "$WORK/dmg" -ov -format UDZO "$OUT/UBTune-macOS.dmg"
shasum -a 256 "$OUT/UBTune-macOS.dmg" | tee "$OUT/UBTune-macOS.dmg.sha256"
