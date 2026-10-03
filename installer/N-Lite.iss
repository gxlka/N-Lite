#ifndef MyAppVersion
  #define MyAppVersion "0.2.2"
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

[Icons]
Name: "{group}\N-Lite"; Filename: "{app}\N-Lite.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\N-Lite"; Filename: "{app}\N-Lite.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "N-Lite"; ValueData: """{app}\N-Lite.exe"" --startup"; Tasks: autostart; Flags: uninsdeletevalue

[Run]
Filename: "{app}\N-Lite.exe"; Description: "Launch N-Lite"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent
Filename: "{app}\N-Lite.exe"; Parameters: "--updated"; WorkingDir: "{app}"; Flags: nowait skipifnotsilent

[UninstallRun]
Filename: "{sys}\schtasks.exe"; Parameters: "/Delete /F /TN ""N-Lite Auto Clean"""; Flags: runhidden; RunOnceId: "DeleteN-LiteAutoCleanTask"
