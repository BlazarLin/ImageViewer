; 2026-09-12
; 功能：生成 ImageViewer Windows x64 安装版（Inno Setup 6）。
; 目的：为 GitHub Release 提供带版本号的 EXE 安装包，与便携 ZIP 内容一致。
; 用法：ISCC /DAppVersion=x.y.z /DSourceDir=<便携包目录> /DOutputDir=<输出目录> /DRoot=<仓库根> scripts/installer.iss

#ifndef AppVersion
#error 缺少 /DAppVersion 版本号
#endif
#ifndef SourceDir
#error 缺少 /DSourceDir 便携包目录
#endif
#ifndef OutputDir
#error 缺少 /DOutputDir 输出目录
#endif
#ifndef Root
#error 缺少 /DRoot 仓库根目录
#endif

#define AppName "ImageViewer"

[Setup]
AppId={{7A3E1C92-4B8D-4F6E-9A05-3C7D21B6E4F8}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} v{#AppVersion}
VersionInfoVersion={#AppVersion}
VersionInfoProductVersion={#AppVersion}
AppPublisher=Blazar
AppPublisherURL=https://github.com/BlazarLin/ImageViewer
AppSupportURL=https://github.com/BlazarLin/ImageViewer/issues
DefaultDirName={autopf}\{#AppName}
; 默认按当前用户安装（无需管理员），允许在向导中改为所有用户。
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename=ImageViewer-{#AppVersion}-windows-x64-setup
SetupIconFile={#Root}\resources\ImageViewer.ico
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\ImageViewer.exe
LicenseFile={#Root}\LICENSE

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; \
    GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; \
    Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\ImageViewer.exe"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\ImageViewer.exe"; \
    Tasks: desktopicon

[Run]
Filename: "{app}\ImageViewer.exe"; Description: "{cm:LaunchProgram,{#AppName}}"; \
    Flags: nowait postinstall skipifsilent
