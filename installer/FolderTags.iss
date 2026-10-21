; FolderTags setup (Inno Setup 6.7+). Build with scripts\build-installer.ps1.
;
; Per-machine install: the DLL goes to Program Files and registers itself under HKLM, so
; Explorer loads it whether it runs unelevated (UAC on) or elevated (UAC off).
; Silent install (Microsoft Store, winget): /VERYSILENT /SUPPRESSMSGBOXES /NORESTART

#ifndef AppVersion
  #define AppVersion "1.2.0"
#endif

[Setup]
; Never change AppId: upgrades and uninstall are matched on it.
AppId={{4B4894D9-5081-48F2-8ACD-57FC463A565C}
AppName=FolderTags
AppVersion={#AppVersion}
AppVerName=FolderTags {#AppVersion}
AppPublisher=bhpdev1
AppPublisherURL=https://github.com/bhpdev1/FolderTags
AppSupportURL=https://github.com/bhpdev1/FolderTags/issues
AppUpdatesURL=https://github.com/bhpdev1/FolderTags/releases
AppCopyright=Copyright (C) bhpdev1. Licensed under the GNU GPLv3.
VersionInfoVersion={#AppVersion}
VersionInfoProductName=FolderTags
VersionInfoProductVersion={#AppVersion}
VersionInfoCompany=bhpdev1
VersionInfoDescription=FolderTags Setup
DefaultDirName={autopf}\FolderTags
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputDir=..\dist
OutputBaseFilename=FolderTags-Setup-{#AppVersion}
SetupIconFile=..\assets\brand\logo.ico
UninstallDisplayIcon={app}\FolderTags.ico
UninstallDisplayName=FolderTags
WizardStyle=modern dynamic windows11
WizardImageFile=..\assets\brand\installer\wizard-262.png,..\assets\brand\installer\wizard-541.png
WizardSmallImageFile=..\assets\brand\logo-256.png
ShowLanguageDialog=no
Compression=lzma2/max
SolidCompression=yes
; Explorer and every app showing file icons keep the DLL loaded: the [Code] section renames it
; instead of asking to close them.
CloseApplications=no
RestartApplications=no
; {localappdata} is the installing user's: per-user tag data, created again for other users.
UsedUserAreasWarning=no

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"

[Messages]
en.FinishedLabelNoIcons=FolderTags is installed. Right-click any folder to give it a color tag.
fr.FinishedLabelNoIcons=FolderTags est installé. Faites un clic droit sur un dossier pour lui attribuer une étiquette de couleur.

[CustomMessages]
en.RestartingExplorer=Restarting File Explorer...
fr.RestartingExplorer=Redémarrage de l'Explorateur de fichiers...

[Files]
Source: "..\build\FolderTags.dll"; DestDir: "{app}"; Flags: ignoreversion regserver uninsrestartdelete
Source: "..\assets\brand\logo.ico"; DestDir: "{app}"; DestName: "FolderTags.ico"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion

[InstallDelete]
; Copies set aside by previous updates, and the DLL of the v1.x per-user install.
Type: files; Name: "{app}\FolderTags.*.old"
Type: files; Name: "{localappdata}\FolderTags\FolderTags.dll"
Type: files; Name: "{localappdata}\FolderTags\FolderTags.*.old"

[UninstallDelete]
; Icons and the Tags sidebar index of the uninstalling user. Tags stored on folders are kept.
Type: filesandordirs; Name: "{localappdata}\FolderTags"
Type: files; Name: "{app}\FolderTags.*.old"

[Code]
function ExplorerRunning: Boolean;
var
  Code: Integer;
begin
  Result := Exec(ExpandConstant('{cmd}'),
    '/C tasklist /FI "IMAGENAME eq explorer.exe" /NH | find /I "explorer.exe"',
    '', SW_HIDE, ewWaitUntilTerminated, Code) and (Code = 0);
end;

{ Explorer only reads the overlay handlers at startup. Windows restarts the shell by itself,
  unelevated (AutoRestartShell); start it ourselves only if it did not come back. }
procedure RestartExplorer(AsOriginalUser: Boolean);
var
  Code, I: Integer;
begin
  WizardForm.StatusLabel.Caption := CustomMessage('RestartingExplorer');
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM explorer.exe', '', SW_HIDE,
    ewWaitUntilTerminated, Code);
  for I := 1 to 20 do
  begin
    Sleep(250);
    if ExplorerRunning then
      Exit;
  end;
  if AsOriginalUser then
    ExecAsOriginalUser(ExpandConstant('{win}\explorer.exe'), '', '', SW_SHOWNORMAL, ewNoWait, Code)
  else
    Exec(ExpandConstant('{win}\explorer.exe'), '', '', SW_SHOWNORMAL, ewNoWait, Code);
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Dll: String;
begin
  if CurStep = ssInstall then
  begin
    { A loaded DLL cannot be overwritten but can be renamed. }
    Dll := ExpandConstant('{app}\FolderTags.dll');
    if FileExists(Dll) then
      RenameFile(Dll, ExpandConstant('{app}\FolderTags.') +
        GetDateTimeString('yyyymmddhhnnss', #0, #0) + '.old');
  end
  else if CurStep = ssPostInstall then
    RestartExplorer(True);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Code: Integer;
begin
  if CurUninstallStep = usUninstall then
  begin
    { Unregister first so the restarted Explorer no longer loads the DLL, which can then be
      deleted right away. The regserver flag unregisters it again, harmlessly. }
    Exec(ExpandConstant('{sysnative}\regsvr32.exe'), '/s /u "' + ExpandConstant('{app}\FolderTags.dll') + '"',
      '', SW_HIDE, ewWaitUntilTerminated, Code);
    Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM explorer.exe', '', SW_HIDE,
      ewWaitUntilTerminated, Code);
    Sleep(1500);
  end
  else if CurUninstallStep = usPostUninstall then
  begin
    if not ExplorerRunning then
      Exec(ExpandConstant('{win}\explorer.exe'), '', '', SW_SHOWNORMAL, ewNoWait, Code);
  end;
end;
