; MeowAi Windows 安装包脚本（Inno Setup 6）
; 编译：ISCC.exe scripts\MeowAi.iss
; 产物：dist\MeowAi-Setup-0.2.7.1.exe
; 说明：按用户安装（无需管理员权限），默认装到 %LOCALAPPDATA%\Programs\MeowAi

#define AppName "MeowAi"
#define AppVersion "0.2.7.1"
#define AppPublisher "Longling"
#define AppExe "MeowAi.exe"

[Setup]
AppId={{8F3E9C2A-5B7D-4E1F-9C3A-2D6B8A4F1E77}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppContact=3086865618@qq.com
DefaultDirName={localappdata}\Programs\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir=..\dist
OutputBaseFilename=MeowAi-Setup-{#AppVersion}
SetupIconFile=..\resources\branding\app.ico
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\{#AppExe}
VersionInfoVersion={#AppVersion}
VersionInfoDescription={#AppName} 安装程序

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加任务:"; Flags: checkedonce

[Files]
Source: "..\dist\MeowAi\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "立即启动 {#AppName}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{app}"
