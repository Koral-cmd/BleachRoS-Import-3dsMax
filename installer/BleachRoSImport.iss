; ================================================================
;  Inno Setup 6: Setup.exe для BleachRoSImport.
;  AppId = UpgradeCode пакета: новая версия ставится поверх старой,
;  папка Contents перед установкой очищается (без остатков старых .dlu).
;  Собирается из installer\BUILD_RELEASE.bat.
; ================================================================
#ifndef AppVer
  #define AppVer "1.4.1"
#endif
#define Stage "..\dist\stage"

[Setup]
AppId={{F7C19872-21CE-4544-9B77-7D12F1F4B7B9}
AppName=BLEACH RoS Import for 3ds Max
AppVersion={#AppVer}
AppPublisher=Koral-cmd
DefaultDirName={commonappdata}\Autodesk\ApplicationPlugins\BleachRoSImport
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
OutputDir=..\dist
OutputBaseFilename=BleachRoSImport-{#AppVer}-Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
CloseApplications=yes
CloseApplicationsFilter=3dsmax.exe
UninstallDisplayName=BLEACH RoS Import for 3ds Max

[Languages]
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[InstallDelete]
Type: filesandordirs; Name: "{app}\Contents"
Type: filesandordirs; Name: "{userappdata}\Autodesk\ApplicationPlugins\BleachRoSImport"

[Files]
Source: "{#Stage}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion
#ifexist "vc_redist.x64.exe"
Source: "vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall

[Run]
; Visual C++ Runtime 14.x (плагину нужна свежая версия; если уже стоит - ничего не меняет)
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Visual C++ Runtime..."; Flags: waituntilterminated
#endif

[UninstallDelete]
Type: filesandordirs; Name: "{app}"
