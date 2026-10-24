; Inno Setup script of the Windows installer. Built by .github/workflows/release.yml (and by installer/build-windows.ps1) with
;   ISCC /DAppVersion=0.0.3 /DSourceDir=<folder with jad-daw.exe and the Qt files> /DOutputDir=<where the setup goes> jad-daw.iss
; It installs for the current user (no administrator rights), so the updater of the program can run it silently over the installed copy:
;   jad-daw-<version>-windows-x64-setup.exe /SILENT /SUPPRESSMSGBOXES /NOCANCEL /CLOSEAPPLICATIONS /RELAUNCH=1

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #error SourceDir is not defined
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif
#define RepoRoot "..\.."

[Setup]
; never change AppId: it is how a new setup recognises the copy it has to replace
AppId={{B3A1C6E2-7D41-4F0B-9C55-2E8A1D6F4A37}
AppName=JAD Daw
AppVersion={#AppVersion}
AppPublisher=JAD
AppPublisherURL=https://github.com/LuigiElleBalotta/jad-daw
AppSupportURL=https://github.com/LuigiElleBalotta/jad-daw/issues
AppUpdatesURL=https://github.com/LuigiElleBalotta/jad-daw/releases
DefaultDirName={autopf}\JAD Daw
DefaultGroupName=JAD Daw
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile={#RepoRoot}\LICENSE
SetupIconFile={#RepoRoot}\assets\branding\app-icon.ico
UninstallDisplayIcon={app}\jad-daw.exe
OutputDir={#OutputDir}
OutputBaseFilename=jad-daw-{#AppVersion}-windows-x64-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
VersionInfoVersion={#AppVersion}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{autoprograms}\JAD Daw"; Filename: "{app}\jad-daw.exe"
Name: "{autodesktop}\JAD Daw"; Filename: "{app}\jad-daw.exe"; Tasks: desktopicon

[Run]
; started again after an update that the program started itself (it passes /RELAUNCH=1)
Filename: "{app}\jad-daw.exe"; Flags: nowait runasoriginaluser; Check: Relaunch
; the last page of the wizard
Filename: "{app}\jad-daw.exe"; Description: "{cm:LaunchProgram,JAD Daw}"; Flags: nowait postinstall skipifsilent runasoriginaluser

[Code]
function Relaunch: Boolean;
begin
  Result := ExpandConstant('{param:RELAUNCH|0}') = '1';
end;
