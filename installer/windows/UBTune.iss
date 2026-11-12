; UBTune — Windows installer (Inno Setup 6)
; by unique & Mbezza (Gelo Santino Mares · Meron Negash) — UNIQUEBABY
#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\..\build\UBTune_artefacts\Release"
#endif

[Setup]
AppId={{6B1E2C0A-5D7E-4C3B-9A61-7B1E0A2C3D4F}
AppName=UBTune
AppVersion={#AppVersion}
AppVerName=UBTune {#AppVersion}
AppPublisher=UNIQUEBABY · unique & Mbezza
AppPublisherURL=https://ubtune.uniquebaby.de
AppSupportURL=https://ubtune.uniquebaby.de
AppCopyright=© 2026 unique & Mbezza – Gelo Santino Mares, Meron Negash
DefaultDirName={autopf}\UBTune
DefaultGroupName=UBTune
DisableProgramGroupPage=yes
OutputBaseFilename=UBTune-Windows-Setup
OutputDir=..\..\dist
SetupIconFile=..\..\branding\UBTune.ico
UninstallDisplayIcon={app}\UBTune.exe
UninstallDisplayName=UBTune {#AppVersion}
WizardStyle=modern
WizardImageFile=wizard-large-100.bmp,wizard-large.bmp
WizardSmallImageFile=wizard-small-100.bmp,wizard-small.bmp
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
CloseApplications=yes
VersionInfoVersion={#AppVersion}
VersionInfoCompany=UNIQUEBABY
VersionInfoDescription=UBTune Setup

[Languages]
Name: "de"; MessagesFile: "compiler:Languages\German.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
de.CompVst3=VST3-Plugin (FL Studio, Ableton Live, Cubase, Studio One, Reaper, Bitwig ...)
en.CompVst3=VST3 plugin (FL Studio, Ableton Live, Cubase, Studio One, Reaper, Bitwig ...)
de.CompApp=UBTune Standalone-App (live mit Mikrofon)
en.CompApp=UBTune standalone app (live with microphone)
de.Desktop=Desktop-Verknüpfung für die Standalone-App
en.Desktop=Desktop shortcut for the standalone app
de.UninstallEntry=UBTune deinstallieren
en.UninstallEntry=Uninstall UBTune
de.Rescan=Fertig! Starte deine DAW neu bzw. lass sie nach neuen Plugins suchen – UBTune findest du unter UNIQUEBABY.
en.Rescan=Done! Restart your DAW or rescan plugins – UBTune is listed under UNIQUEBABY.

[Types]
Name: "full"; Description: "Komplett / Full"
Name: "custom"; Description: "Anpassen / Custom"; Flags: iscustom

[Components]
Name: "vst3"; Description: "{cm:CompVst3}"; Types: full custom; Flags: fixed
Name: "app"; Description: "{cm:CompApp}"; Types: full

[Tasks]
Name: "desktopicon"; Description: "{cm:Desktop}"; Components: app; Flags: unchecked

[Files]
Source: "{#BuildDir}\VST3\UBTune.vst3\*"; DestDir: "{commoncf64}\VST3\UBTune.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\Standalone\UBTune.exe"; DestDir: "{app}"; Components: app; Flags: ignoreversion
Source: "..\..\branding\UBTune.ico"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\UBTune"; Filename: "{app}\UBTune.exe"; Components: app
Name: "{group}\{cm:UninstallEntry}"; Filename: "{uninstallexe}"; IconFilename: "{app}\UBTune.ico"
Name: "{autodesktop}\UBTune"; Filename: "{app}\UBTune.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\UBTune.exe"; Description: "UBTune starten"; Components: app; Flags: postinstall nowait skipifsilent unchecked

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\UBTune.vst3"
Type: filesandordirs; Name: "{app}"

[Code]
procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssDone) and (not WizardSilent) then
    MsgBox(CustomMessage('Rescan'), mbInformation, MB_OK);
end;
