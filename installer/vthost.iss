; Instalador do VTHOST (Inno Setup 6).
;   iscc /DAppVersion=0.1.0 /DSourceDir=..\dist\VTHOST installer\vthost.iss
;
; Instala por utilizador (%LOCALAPPDATA%\Programs\VTHOST): as atualizações
; automáticas correm sem pedir permissões de administrador.
; Os projetos e o estado do show (%LOCALAPPDATA%\VTHOST) nunca são apagados.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\dist\VTHOST"
#endif
#ifndef OutputDir
  #define OutputDir "..\dist"
#endif

[Setup]
AppId={{7B3C9E21-4A6D-4F0B-9C55-2E1D8A6F4B10}
AppName=VTHOST
AppVersion={#AppVersion}
AppVerName=VTHOST {#AppVersion}
AppPublisher=Daniel Nunes
AppPublisherURL=https://github.com/NunesDanielPT
AppSupportURL=https://github.com/NunesDanielPT/VTHOST
AppUpdatesURL=https://github.com/NunesDanielPT/VTHOST/releases
AppCopyright=© 2026 Daniel Nunes
VersionInfoVersion={#AppVersion}
VersionInfoCompany=Daniel Nunes
VersionInfoDescription=VTHOST Setup
DefaultDirName={localappdata}\Programs\VTHOST
DefaultGroupName=VTHOST
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename=VTHOST-Setup-{#AppVersion}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
SetupIconFile=..\assets\logo\vthost.ico
UninstallDisplayName=VTHOST
UninstallDisplayIcon={app}\vthost-studio.exe
CloseApplications=yes
RestartApplications=no
ShowLanguageDialog=auto

[Languages]
Name: "pt"; MessagesFile: "compiler:Languages\Portuguese.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
pt.DesktopIcon=Criar atalho no ambiente de trabalho
en.DesktopIcon=Create a desktop shortcut
pt.LaunchStudio=Abrir o VTHOST Studio
en.LaunchStudio=Launch VTHOST Studio
pt.StoppingEngine=A parar o motor do VTHOST...
en.StoppingEngine=Stopping the VTHOST engine...

[Tasks]
Name: "desktopicon"; Description: "{cm:DesktopIcon}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\VTHOST Studio"; Filename: "{app}\vthost-studio.exe"
Name: "{group}\{cm:UninstallProgram,VTHOST}"; Filename: "{uninstallexe}"
Name: "{userdesktop}\VTHOST Studio"; Filename: "{app}\vthost-studio.exe"; Tasks: desktopicon

[Run]
; Instalação normal: caixa "Abrir o VTHOST Studio" no fim.
Filename: "{app}\vthost-studio.exe"; Description: "{cm:LaunchStudio}"; Flags: nowait postinstall skipifsilent
; Atualização automática (silenciosa, /RELAUNCH=1): reabre o Studio sozinho.
Filename: "{app}\vthost-studio.exe"; Flags: nowait; Check: ShouldRelaunch

[Code]
function ShouldRelaunch: Boolean;
begin
  Result := ExpandConstant('{param:RELAUNCH|0}') = '1';
end;

// O watchdog primeiro (senão relançava o motor), depois o motor e o Studio.
procedure StopVthostProcesses;
var
  Code: Integer;
begin
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM vthost-watchdog.exe', '', SW_HIDE, ewWaitUntilTerminated, Code);
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM vthost-engine.exe', '', SW_HIDE, ewWaitUntilTerminated, Code);
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM vthost-studio.exe', '', SW_HIDE, ewWaitUntilTerminated, Code);
  Sleep(500);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  WizardForm.StatusLabel.Caption := CustomMessage('StoppingEngine');
  StopVthostProcesses;
  Result := '';
end;

function InitializeUninstall: Boolean;
begin
  StopVthostProcesses;
  Result := True;
end;
