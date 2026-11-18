#define AppName "MacroFlow"
#define AppVersion "1.0.0"
#define AppPublisher "MacroFlow"
#define AppExeName "MacroFlow.exe"
#ifndef SourceDir
  #define SourceDir "..\out\portable\MacroFlow"
#endif
#ifndef OutputDir
  #define OutputDir "..\out\setup"
#endif


[Setup]
AppId={{E8D03A53-8D6C-4A41-9CB4-6B98DCE77C6A}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={localappdata}\Programs\MacroFlow
DefaultGroupName=MacroFlow
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename=MacroFlow-Setup
SetupIconFile=..\resources\icons\app.ico
UninstallDisplayIcon={app}\{#AppExeName}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
VersionInfoVersion=1.0.0.0
VersionInfoProductName=MacroFlow
VersionInfoDescription=Windows mouse and keyboard macro recorder

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional icons:"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs


[Icons]
Name: "{autoprograms}\MacroFlow"; Filename: "{app}\{#AppExeName}"
Name: "{autodesktop}\MacroFlow"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon


[Run]
Filename: "{app}\{#AppExeName}"; Description: "Launch MacroFlow"; Flags: postinstall nowait skipifsilent
