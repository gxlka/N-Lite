#ifndef MyAppVersion
  #define MyAppVersion "0.2.17"
#endif

#define MyAppName "N-Lite"
#define MyAppExeName "N-Lite.exe"

[Setup]
AppId={{8C9241D7-3A13-4A5E-95F3-BD254A065318}}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher=N-Lite
AppPublisherURL=https://github.com/gxlka/N-Lite
AppSupportURL=https://github.com/gxlka/N-Lite/issues
AppUpdatesURL=https://github.com/gxlka/N-Lite/releases
SetupIconFile=..\src\app.ico
DefaultDirName={localappdata}\Programs\N-Lite
DefaultGroupName=N-Lite
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\N-Lite.exe
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
CloseApplications=yes
RestartApplications=no
WizardStyle=modern
Compression=lzma2
SolidCompression=yes
OutputDir=..\dist
OutputBaseFilename=N-Lite-Setup-x64
Uninstallable=yes

[Tasks]
Name: "autostart"; Description: "Start N-Lite when I sign in to Windows"; GroupDescription: "Startup option:"; Flags: checkedonce
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"

[Files]
Source: "..\dist\N-Lite.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\N-Lite-Cleaner.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\N-Lite"; Filename: "{app}\N-Lite.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\N-Lite"; Filename: "{app}\N-Lite.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "N-Lite"; ValueData: """{app}\N-Lite.exe"" --startup"; Tasks: autostart; Flags: uninsdeletevalue

[Run]
Filename: "{app}\N-Lite.exe"; Description: "Launch N-Lite"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent
Filename: "{app}\N-Lite.exe"; Parameters: "--updated"; WorkingDir: "{app}"; Flags: nowait skipifnotsilent

[Code]
procedure CurStepChanged(CurStep: TSetupStep);
var
  Sid: string;
  ExitCode: Integer;
begin
  if (CurStep = ssPostInstall) and
     RegQueryStringValue(HKCU, 'Software\N-Lite', 'CleanerSid', Sid) and
     FileExists(ExpandConstant('{commonappdata}\N-Lite\N-Lite-Cleaner.exe')) then begin
    { Upgrade the protected helper once during installation, before normal app use. }
    if ShellExec('runas', ExpandConstant('{app}\N-Lite-Cleaner.exe'),
       '--install ' + Sid, ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, ExitCode) and
       (ExitCode = 0) then
      RegWriteDWordValue(HKCU, 'Software\N-Lite', 'CleanerSetupBlockedVersion', 0)
    else
      RegWriteDWordValue(HKCU, 'Software\N-Lite', 'CleanerSetupBlockedVersion', 6);
  end;
end;

function InitializeUninstall(): Boolean;
var
  Sid: string;
  ExitCode: Integer;
begin
  Result := True;
  if RegQueryStringValue(HKCU, 'Software\N-Lite', 'CleanerSid', Sid) and
     FileExists(ExpandConstant('{app}\N-Lite-Cleaner.exe')) then begin
    if not ShellExec('runas', ExpandConstant('{app}\N-Lite-Cleaner.exe'),
       '--uninstall ' + Sid, ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, ExitCode) or
       (ExitCode <> 0) then begin
      MsgBox('Windows did not remove the protected standby cleaner task. N-Lite setup will stay installed.',
        mbError, MB_OK);
      Result := False;
    end;
  end;
end;
