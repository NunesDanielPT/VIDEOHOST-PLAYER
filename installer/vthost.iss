; Instalador do VIDEOHOST PLAYER (antes VTHOST) (Inno Setup 6).
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
AppName=VIDEOHOST PLAYER
AppVersion={#AppVersion}
AppVerName=VIDEOHOST PLAYER {#AppVersion}
AppPublisher=Daniel Nunes
AppPublisherURL=https://github.com/NunesDanielPT
AppSupportURL=https://github.com/NunesDanielPT/VIDEOHOST-PLAYER
AppUpdatesURL=https://github.com/NunesDanielPT/VIDEOHOST-PLAYER/releases
AppCopyright=© 2026 Daniel Nunes
VersionInfoVersion={#AppVersion}
VersionInfoCompany=Daniel Nunes
VersionInfoDescription=VIDEOHOST PLAYER Setup
; Atualizações instalam na pasta onde já está (o AppId não muda).
DefaultDirName={localappdata}\Programs\VIDEOHOST PLAYER
DefaultGroupName=VIDEOHOST PLAYER
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename=VIDEOHOST-PLAYER-Setup-{#AppVersion}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
SetupIconFile=..\assets\logo\vthost.ico
UninstallDisplayName=VIDEOHOST PLAYER
UninstallDisplayIcon={app}\vthost-studio.exe
CloseApplications=yes
RestartApplications=no
; Primeiro passo: escolher Português ou English (também a língua do programa).
ShowLanguageDialog=yes
LanguageDetectionMethod=uilanguage

[Languages]
Name: "pt"; MessagesFile: "compiler:Languages\Portuguese.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
pt.DesktopIcon=Criar atalho no ambiente de trabalho
en.DesktopIcon=Create a desktop shortcut
pt.LaunchStudio=Abrir o VIDEOHOST PLAYER
en.LaunchStudio=Launch VIDEOHOST PLAYER
pt.StoppingEngine=A parar o motor do VIDEOHOST PLAYER...
en.StoppingEngine=Stopping the VIDEOHOST PLAYER engine...

[Tasks]
Name: "desktopicon"; Description: "{cm:DesktopIcon}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[InstallDelete]
; Atalhos com o nome antigo (VTHOST).
Type: filesandordirs; Name: "{userprograms}\VTHOST"
Type: files; Name: "{userdesktop}\VTHOST Studio.lnk"

[Registry]
; Língua do programa = a escolhida no instalador (só na instalação normal: as
; atualizações silenciosas não mexem na escolha feita nas Definições).
Root: HKCU; Subkey: "Software\Daniel Nunes\VTHOST Studio"; ValueType: string; ValueName: "language"; ValueData: "pt_PT"; Languages: pt; Check: not WizardSilent
Root: HKCU; Subkey: "Software\Daniel Nunes\VTHOST Studio"; ValueType: string; ValueName: "language"; ValueData: "en"; Languages: en; Check: not WizardSilent

[Icons]
Name: "{group}\VIDEOHOST PLAYER"; Filename: "{app}\vthost-studio.exe"
Name: "{group}\{cm:UninstallProgram,VIDEOHOST PLAYER}"; Filename: "{uninstallexe}"
Name: "{userdesktop}\VIDEOHOST PLAYER"; Filename: "{app}\vthost-studio.exe"; Tasks: desktopicon

[Run]
; Instalação normal: caixa "Abrir o VIDEOHOST PLAYER" no fim.
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
