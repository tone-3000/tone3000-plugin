; Inno Setup script for the TONE3000 Windows installer.
; Compiled in CI with ISCC (preinstalled on GitHub windows-latest runners):
;
;   iscc /DVersion=0.0.1 /DArtefactsDir=..\..\..\build\plugin\TONE3000_artefacts\Release script\installer\windows\tone3000.iss
;
; Defines (override with /D on the command line):
;   Version       plugin version string (default 0.0.1)
;   ArtefactsDir  path to the JUCE Release artefacts dir, absolute or relative
;                 to this script (default ..\..\..\build\plugin\TONE3000_artefacts\Release)
;   OutputDir     where the setup exe is written (default ..\..\..\build)

#ifndef Version
  #define Version "0.0.1"
#endif
#ifndef ArtefactsDir
  #define ArtefactsDir "..\..\..\build\plugin\TONE3000_artefacts\Release"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\..\build"
#endif

[Setup]
AppId={{7B5C3F1E-9D24-4A8B-B1E6-3FD82A6C41B7}
AppName=TONE3000
AppVersion={#Version}
AppPublisher=TONE3000
DefaultDirName={autopf64}\TONE3000
DefaultGroupName=TONE3000
OutputDir={#OutputDir}
OutputBaseFilename=TONE3000-v{#Version}-windows-x64-setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
DisableDirPage=no
DisableProgramGroupPage=yes
PrivilegesRequired=admin
UninstallDisplayName=TONE3000

[Types]
Name: "full";   Description: "Full installation"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "standalone"; Description: "Standalone application"; Types: full custom
Name: "vst3";       Description: "VST3 plug-in";           Types: full custom

[Files]
; Standalone app → Program Files\TONE3000
Source: "{#ArtefactsDir}\Standalone\TONE3000.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
; VST3 bundle → Common Files\VST3 (standard VST3 location)
Source: "{#ArtefactsDir}\VST3\TONE3000.vst3\*"; DestDir: "{commoncf64}\VST3\TONE3000.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\TONE3000"; Filename: "{app}\TONE3000.exe"; Components: standalone

[Run]
Filename: "{app}\TONE3000.exe"; Description: "Launch TONE3000"; Components: standalone; Flags: nowait postinstall skipifsilent
