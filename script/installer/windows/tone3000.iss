; Inno Setup script for the TONE3000 Windows installer.
; Compiled in CI with ISCC (preinstalled on GitHub windows-latest runners):
;
;   iscc /DVersion=x.y.z /DArtefactsDir=..\..\..\build\plugin\TONE3000_artefacts\Release script\installer\windows\tone3000.iss
;
; Defines (override with /D on the command line):
;   Version       plugin version string, REQUIRED. Pass the contents of the
;                 repo-root VERSION file (single source of truth, no default
;                 here so the installer can never ship a stale version)
;   ArtefactsDir  path to the JUCE Release artefacts dir, absolute or relative
;                 to this script (default ..\..\..\build\plugin\TONE3000_artefacts\Release)
;   OutputDir     where the setup exe is written (default ..\..\..\build)

#ifndef Version
  #pragma error "Version not set - pass /DVersion=x.y.z (from the repo-root VERSION file)"
#endif
#ifndef ArtefactsDir
  #define ArtefactsDir "..\..\..\build\plugin\TONE3000_artefacts\Release"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\..\build"
#endif

; AAX is only in the installer when the artefact exists (it is built with
; BUILD_AAX=ON and, in CI, PACE-signed beforehand; see sign-aax-windows.ps1).
; Compile-time check so local builds without AAX still compile this script.
#define AaxBundle ArtefactsDir + "\AAX\TONE3000.aaxplugin"
#if DirExists(AaxBundle)
  #define HaveAax
#endif

[Setup]
AppId={{7B5C3F1E-9D24-4A8B-B1E6-3FD82A6C41B7}
AppName=TONE3000
AppVersion={#Version}
AppPublisher=TONE3000
AppPublisherURL=https://www.tone3000.com
AppSupportURL=https://github.com/tone-3000/plugin/issues
AppUpdatesURL=https://github.com/tone-3000/plugin/releases
VersionInfoVersion={#Version}
DefaultDirName={autopf64}\TONE3000
DefaultGroupName=TONE3000
OutputDir={#OutputDir}
OutputBaseFilename=TONE3000-v{#Version}-windows-x64
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
Compression=lzma2
SolidCompression=yes
; InfoBeforeFile (not LicenseFile): shows the MIT text on an Information
; page without forcing an "I accept" step.
InfoBeforeFile=..\..\..\LICENSE
; Branding: dark T3K banner on the welcome/finish pages, mark chip on the
; inner-page header, and the T3K icon on the setup exe itself. The BMP pairs
; are 100% / 200% DPI variants rendered from design/tone3000-wordmark.svg and
; plugin/ui/assets/t3k-mark.svg (BMP, not PNG: PNG needs Inno 6.5.2+).
WizardStyle=modern
DisableWelcomePage=no
WizardImageFile=wizard-image-100.bmp,wizard-image-200.bmp
WizardSmallImageFile=wizard-small-100.bmp,wizard-small-200.bmp
SetupIconFile=tone3000.ico
UninstallDisplayIcon={app}\TONE3000.exe
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
#ifdef HaveAax
Name: "aax";        Description: "AAX plug-in (Pro Tools)"; Types: full custom
#endif
Name: "clap";       Description: "CLAP plug-in";           Types: full custom
Name: "presets";    Description: "Factory presets";        Types: full custom

[Files]
; Standalone app → Program Files\TONE3000
Source: "{#ArtefactsDir}\Standalone\TONE3000.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
; VST3 bundle → Common Files\VST3 (standard VST3 location)
Source: "{#ArtefactsDir}\VST3\TONE3000.vst3\*"; DestDir: "{commoncf64}\VST3\TONE3000.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
#ifdef HaveAax
; AAX bundle → Common Files\Avid\Audio\Plug-Ins (standard AAX location)
Source: "{#AaxBundle}\*"; DestDir: "{commoncf64}\Avid\Audio\Plug-Ins\TONE3000.aaxplugin"; Components: aax; Flags: ignoreversion recursesubdirs createallsubdirs
#endif
; CLAP (single file on Windows) → Common Files\CLAP (standard CLAP location)
Source: "{#ArtefactsDir}\CLAP\TONE3000.clap"; DestDir: "{commoncf64}\CLAP"; Components: clap; Flags: ignoreversion
; Factory presets → ProgramData (all-users; PresetManager scans this as system Factory)
Source: "..\..\..\resources\factory-presets\*.t3kpreset"; DestDir: "{commonappdata}\TONE3000\Presets\Factory"; Components: presets; Flags: ignoreversion

[InstallDelete]
; Installs overlay files and never remove presets dropped from (or renamed
; in) the shipped set, so clear the factory folder before copying the new
; one. User presets live elsewhere (%APPDATA%) and are untouched.
Type: files; Name: "{commonappdata}\TONE3000\Presets\Factory\*.t3kpreset"; Components: presets

[Icons]
Name: "{autoprograms}\TONE3000"; Filename: "{app}\TONE3000.exe"; Components: standalone

[Run]
Filename: "{app}\TONE3000.exe"; Description: "Launch TONE3000"; Components: standalone; Flags: nowait postinstall skipifsilent
