; ============================================================================
; VCMI Installer – Adding a New Translation
; ============================================================================
;
; 1. Download the base ISL file for your language:
;    - Get the appropriate .isl file from the official Inno Setup repository:
;      https://github.com/jrsoftware/issrc/tree/main/Files/Languages
;
; 2. Add VCMI custom messages:
;    - Open the downloaded .isl file and insert all VCMI-specific messages.
;    - Use English.isl (VCMI version) as a reference.
;    - Ensure translations keep placeholders (%1, %2, etc.) intact and match
;      the format of the English version exactly.
;
; 3. Update/translate the following modified messages:
;    These differ from the default Inno Setup language files and are required
;    for VCMI's custom installer functionality.
;
;    ------------------------------------------------------------------------
;    WindowsVersionNotSupported
;    ------------------------------------------------------------------------
;    Why: VCMI adds a more descriptive message for unsupported Windows versions.
;    Original:
;      WindowsVersionNotSupported=This program does not support the version of Windows your computer is running.
;    VCMI version:
;      WindowsVersionNotSupported=This program cannot run on your version of Windows. Please ensure you are using the correct Windows version.
;
;    ------------------------------------------------------------------------
;    PrivilegesRequiredOverride* messages
;    ------------------------------------------------------------------------
;    Why: VCMI customizes privilege escalation messages to clarify installation
;         for all users vs. current user and highlight administrative rights.
;    Messages to add/update:
;      PrivilegesRequiredOverrideTitle
;      PrivilegesRequiredOverrideInstruction
;      PrivilegesRequiredOverrideText1
;      PrivilegesRequiredOverrideText2
;      PrivilegesRequiredOverrideAllUsers
;      PrivilegesRequiredOverrideAllUsersRecommended
;      PrivilegesRequiredOverrideCurrentUser
;      PrivilegesRequiredOverrideCurrentUserRecommended
;
;    Example (VCMI English):
;      PrivilegesRequiredOverrideTitle=Administrator Privileges Required
;      PrivilegesRequiredOverrideInstruction=Choose how to run the installer
;      PrivilegesRequiredOverrideText1=%1 requires administrative rights to install for all users. You can also install just for your account (no administrative rights required) or for all users (administrator rights required).
;      PrivilegesRequiredOverrideText2=%1 can be installed only for your account (no administrative rights required) or for all users (administrator rights required).
;      PrivilegesRequiredOverrideAllUsers=Run as &Administrator (install for all users)
;      PrivilegesRequiredOverrideAllUsersRecommended=Run as &Administrator (recommended)
;      PrivilegesRequiredOverrideCurrentUser=Run as &Standard User (install for me only)
;      PrivilegesRequiredOverrideCurrentUserRecommended=Run as &Standard User (recommended)
;
;    ------------------------------------------------------------------------
;    ConfirmUninstall
;    ------------------------------------------------------------------------
;    Why: VCMI uses a custom uninstall wizard. The message must reflect this.
;    Original:
;      ConfirmUninstall=Are you sure you want to completely remove %1 and all of its components?
;    VCMI version:
;      ConfirmUninstall=Are you sure you want to run the %1 uninstall wizard?
;
; 4. Add the new language to the installer:
;    - In the [Languages] section of the script, register your translation:
;      Name: "YourLanguage"; MessagesFile: "{#LangPath}\YourLanguage.isl"
;
; 5. Verify consistency:
;    - Check all custom messages against the English VCMI ISL file.
;    - Test the installer to confirm all messages appear correctly.


; Manual preprocessor definitions are provided using ISCC.exe parameters.

; #define AppVersion "1.7.0"
; #define AppBuild "1122334455A"
; #define InstallerArch "x64"
; #define AllowedArch "x64compatible"
; #define VCMIFolder "VCMI"
; #define InstallerName "VCMI-Windows"
; #define SourceFilesPath "C:\_VCMI_source\bin\Release"
; #define UCRTFilesPath "C:\Program Files (x86)\Windows Kits\10\Redist\10.0.22621.0\ucrt\DLLs"
; #define LangPath "C:\_VCMI_Source\CI\wininstaller\lang"
; #define LicenseFile "C:\_VCMI_Source\license.txt"
; #define IconFile "C:\_VCMI_Source\clientapp\icons\vcmi.ico"
; #define SmallLogo "C:\_VCMI_Source\CI\wininstaller\vcmismalllogo.bmp"
; #define WizardLogo "C:\_VCMI_Source\CI\wininstaller\vcmilogo.bmp"

; DMB: the name the wizard, the Start menu, Windows' list of installed apps, the install folder, the
; registry key and the firewall rules show. The installer id stays the short VCMIFolder ("DMB"), so an
; install over an earlier one still replaces it.
#define DMBName "Dead Man's Boots"

; DMB: its own user folder, so an install never touches stock VCMI's (the uninstaller's "delete user
; data" option removes this folder)
#define VCMIFilesFolder "My Games\" + DMBName

; DMB: the name DMB used on disk until September 27th, 2026, when K ruled "DMB" too likely to be some
; other program's. An install over an earlier DMB renames its user folder and moves an install folder
; of that name, once it has checked they are DMB's (UserFolderMadeByDmb, DmbProgramsIn); the game
; renames the user folder the same way on its first start (lib/VCMIDirs.cpp).
#define EarlierName "DMB"
#define EarlierFilesFolder "My Games\" + EarlierName

; the names inside Pascal strings, where an apostrophe is written twice
#define DMBNamePas StringChange(DMBName, "'", "''")
#define FilesFolderPas StringChange(VCMIFilesFolder, "'", "''")

#define AppComment "Dead Man's Boots, built on VCMI, the open-source engine for Heroes III."
#define VCMITeam "Dead Man's Boots"
#define VCMICopyright "Copyright © VCMI Team and the Dead Man's Boots contributors."

; DMB's repository page; the workflow passes /DDMBHome with the real address
#ifndef DMBHome
  #define DMBHome "https://github.com/"
#endif
#define VCMIHome DMBHome
#define VCMIContact DMBHome


[Setup]
AppId={#VCMIFolder}.{#InstallerArch}
AppName={#DMBName}
AppVersion={#AppVersion}.{#AppBuild}
AppVerName={#DMBName} Straker Beta
AppPublisher={#VCMITeam}
AppPublisherURL={#VCMIHome}
AppSupportURL={#VCMIContact}
AppComments={#AppComment}
DefaultDirName={code:GetDefaultDir}
DefaultGroupName={#DMBName}
UninstallDisplayIcon={app}\VCMI_launcher.exe
OutputBaseFilename={#InstallerName}
PrivilegesRequiredOverridesAllowed=commandline dialog
ShowLanguageDialog=yes
DisableWelcomePage=no
DisableProgramGroupPage=yes
ChangesAssociations=no
UsePreviousLanguage=yes
DirExistsWarning=no
UsePreviousAppDir=yes
UsePreviousTasks=yes
UsePreviousGroup=yes
DisableStartupPrompt=yes
UsedUserAreasWarning=no
WindowResizable=no
CloseApplicationsFilter=*.exe
CloseApplications=force
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesAllowed={#AllowedArch}
LicenseFile={#LicenseFile}
SetupIconFile={#IconFile}
WizardSmallImageFile={#SmallLogo}
WizardImageFile={#WizardLogo}

; Version informations
MinVersion=6.1sp1
VersionInfoCompany={#VCMITeam}
VersionInfoDescription={#DMBName} {#AppVersion} Setup (Build {#AppBuild})
VersionInfoProductName={#DMBName}
VersionInfoCopyright={#VCMICopyright}
VersionInfoVersion={#AppVersion}
VersionInfoOriginalFileName={#InstallerName}.exe


[Messages]
; DMB (K, October 9th): this release goes by the name "Straker Beta"; the wizard's title says so
SetupWindowTitle=Setup - {#DMBName} Straker Beta


[Languages]
Name: "english"; MessagesFile: "{#LangPath}\English.isl"
Name: "czech"; MessagesFile: "{#LangPath}\Czech.isl"
Name: "chinese"; MessagesFile: "{#LangPath}\ChineseSimplified.isl"
Name: "finnish"; MessagesFile: "{#LangPath}\Finnish.isl"
Name: "french"; MessagesFile: "{#LangPath}\French.isl"
Name: "german"; MessagesFile: "{#LangPath}\German.isl"
Name: "hungarian"; MessagesFile: "{#LangPath}\Hungarian.isl"
Name: "italian"; MessagesFile: "{#LangPath}\Italian.isl"
Name: "korean"; MessagesFile: "{#LangPath}\Korean.isl"
Name: "polish"; MessagesFile: "{#LangPath}\Polish.isl"
Name: "portuguese"; MessagesFile: "{#LangPath}\BrazilianPortuguese.isl"
Name: "russian"; MessagesFile: "{#LangPath}\Russian.isl"
Name: "spanish"; MessagesFile: "{#LangPath}\Spanish.isl"
Name: "swedish"; MessagesFile: "{#LangPath}\Swedish.isl"
Name: "turkish"; MessagesFile: "{#LangPath}\Turkish.isl"
Name: "ukrainian"; MessagesFile: "{#LangPath}\Ukrainian.isl"
Name: "vietnamese"; MessagesFile: "{#LangPath}\Vietnamese.isl"


[Files]
Source: "{#SourceFilesPath}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "*.pdb,*.lib,*.exp,*.ilk,*.obj,*.tlog,*.log,*.pch,*.idb,*.res,*.tmp,*.bak,*.sdf,*.ipch,*.vc.db,*.iobj,*.ipdb"; BeforeInstall: RunPreInstallTasks
Source: "{#UCRTFilesPath}\{#InstallerArch}\*"; DestDir: "{app}"; Flags: ignoreversion; Check: IsUCRTNeeded


[Icons]
; DMB: named for the product (the translated names say VCMI); the map editor only when a build ships it
Name: "{group}\Dead Man's Boots{code:GetBranchSuffix}"; Filename: "{app}\VCMI_launcher.exe"; Comment: "Dead Man's Boots{code:GetBranchSuffix}";  Tasks: startmenu
Name: "{group}\Dead Man's Boots Map Editor{code:GetBranchSuffix}"; Filename: "{app}\VCMI_mapeditor.exe"; Comment: "Dead Man's Boots Map Editor{code:GetBranchSuffix}";  Tasks: startmenu; Check: MapEditorInstalled

Name: "{code:GetUserDesktopFolder}\Dead Man's Boots{code:GetBranchSuffix}"; Filename: "{app}\VCMI_launcher.exe"; Comment: "Dead Man's Boots{code:GetBranchSuffix}"; Tasks: desktop


[Tasks]
Name: "desktop"; Description: "{cm:CreateDesktopShortcuts}"; GroupDescription: "{cm:SystemIntegration}"; Check: not IsPRInstaller
Name: "startmenu"; Description: "{cm:CreateStartMenuShortcuts}"; GroupDescription: "{cm:SystemIntegration}"; Check: not IsPRInstaller
; DMB: no file associations; .h3m, .vmap and .vcmp stay with whatever the player already uses

Name: "firewallrules"; Description: "{cm:AddFirewallRules}"; GroupDescription: "{cm:VCMISettings}"; Check: not IsPRInstaller and IsAdminInstallMode
Name: "h3copyfiles"; Description: "{cm:CopyH3Files}"; GroupDescription: "{cm:VCMISettings}"; Check: not IsPRInstaller and IsHeroes3Installed and IsCopyFilesNeeded

[Registry]
Root: HKCU; Subkey: "Software\{#DMBName}"; ValueType: string; ValueName: "InstallPath"; ValueData: "{app}"; Flags: uninsdeletekey



[Run]
; DMB: the earlier rules (dmb_server, dmb_client) go when they are for this install's programs, and a
; rule of the current name is replaced, so an upgrade never leaves two
Filename: "netsh.exe"; Parameters: "advfirewall firewall delete rule name=dmb_server program=""{code:EarlierAppDir}\vcmi_server.exe"""; Flags: runhidden; Check: IsAdmin
Filename: "netsh.exe"; Parameters: "advfirewall firewall delete rule name=dmb_client program=""{code:EarlierAppDir}\vcmi_client.exe"""; Flags: runhidden; Check: IsAdmin
Filename: "netsh.exe"; Parameters: "advfirewall firewall delete rule name=""{#DMBName} server"""; Flags: runhidden; Tasks: firewallrules; Check: IsAdmin
Filename: "netsh.exe"; Parameters: "advfirewall firewall delete rule name=""{#DMBName} client"""; Flags: runhidden; Tasks: firewallrules; Check: IsAdmin
Filename: "netsh.exe"; Parameters: "advfirewall firewall add rule name=""{#DMBName} server"" dir=in action=allow program=""{app}\vcmi_server.exe"" enable=yes profile=public,private"; Flags: runhidden; Tasks: firewallrules; Check: IsAdmin
Filename: "netsh.exe"; Parameters: "advfirewall firewall add rule name=""{#DMBName} client"" dir=in action=allow program=""{app}\vcmi_client.exe"" enable=yes profile=public,private"; Flags: runhidden; Tasks: firewallrules; Check: IsAdmin

Filename: "{app}\VCMI_launcher.exe"; Description: "{cm:RunVCMILauncherAfterInstall}"; Flags: nowait postinstall; Check: ShouldRunLauncher


[UninstallRun]
; DMB: no taskkill by program name here. DMB's programs share stock VCMI's file names, so that
; would end a running stock VCMI game too; files still in use make the uninstaller ask instead.

; Remove firewall rules, the earlier names' too
Filename: "netsh.exe"; Parameters: "advfirewall firewall delete rule name=""{#DMBName} server"""; Flags: runhidden; Check: IsAdmin; RunOnceId: "RemoveFirewallDMBServer"
Filename: "netsh.exe"; Parameters: "advfirewall firewall delete rule name=""{#DMBName} client"""; Flags: runhidden; Check: IsAdmin; RunOnceId: "RemoveFirewallDMBClient"
Filename: "netsh.exe"; Parameters: "advfirewall firewall delete rule name=dmb_server program=""{app}\vcmi_server.exe"""; Flags: runhidden; Check: IsAdmin; RunOnceId: "RemoveFirewallEarlierDMBServer"
Filename: "netsh.exe"; Parameters: "advfirewall firewall delete rule name=dmb_client program=""{app}\vcmi_client.exe"""; Flags: runhidden; Check: IsAdmin; RunOnceId: "RemoveFirewallEarlierDMBClient"


[CustomMessages]
; DMB: what an install over an earlier DMB moves, shown only when it moves something (K, September 27th)
DMBMovingCaption=New folder names
DMBMovingDescription=Earlier versions used the short name "DMB" on disk. This install moves them to "Dead Man's Boots".
DMBMovingText=This install moves:%n%n%1%nYour saves, settings and mods come along unchanged.
DMBMovingUserFolder=your saves, settings and mods%n    from %1%n    to %2%n%n
DMBMovingProgramFolder=the program%n    from %1%n    to %2%n    (the old program folder is deleted once the new one is installed)%n%n
DMBMovedUserFolder=Your saves, settings and mods are now in %1.
; DMB (K, October 8th): the same version is already installed
DMBRepairQuestion=Dead Man's Boots %1 is already installed.%n%nContinue with the installation (repair)?
DMBNotMovedUserFolder=Your saves, settings and mods are still in %1: a file in it was in use. Dead Man's Boots moves them to %2 the next time it starts.


[InstallDelete]
; DMB: test builds carried the map generator in {app}\mapgen; it is a mod now, so an install over
; one of them removes that folder
Type: filesandordirs; Name: "{app}\mapgen"


[Code]
var
  InstallModePage: TInputOptionWizardPage;
  FooterLabel: TLabel;
  IsUpgrade: Boolean;
  Heroes3Path: String;
  GlobalUserName: String;
  GlobalUserDocsFolder: String;
  GlobalUserAppdataFolder: String;

  VCMIMapsFolder, VCMIDataFolder, VCMIMp3Folder: String;
  Heroes3MapsFolder, Heroes3DataFolder, Heroes3Mp3Folder: String;

  // DMB: an earlier DMB's folders this install moves, found before anything is installed, and what
  // became of the user folder
  EarlierUserFolder, EarlierProgramFolder: String;
  EarlierKeyIsDmb, UserFolderMoved, UserFolderNotMoved: Boolean;

  // DMB (K, October 9th): the first-time setup. A VCMI install found on this PC, what the player chose
  // (0 copy, 1 copy and then remove VCMI, 2 neither: the usual install), and what became of it
  VcmiFound, VcmiDataFound, VcmiUsesDirsJson, RemoveConfirmed: Boolean;
  VcmiName, VcmiVersion, VcmiInstallDir, VcmiUninstallExe, VcmiUninstallParams, VcmiUserDir, VcmiConfigDir, VcmiSavesDir, VcmiKeyPath: String;
  VcmiKeyRoot, SetupChoice, VcmiUninstallWaitSeconds: Integer;
  SetupChoicePage, RemoveConfirmPage: TInputOptionWizardPage;
  SetupSummary: String;
  MigLog: TStringList;
  MigCopied, MigExisting, MigFailed: Integer;
  MigBytes: Int64;

function RegistryQueryPath(Key, ValueName: String): String;
begin
  if RegQueryStringValue(HKLM, Key, ValueName, Result) then
    Exit
  else
    Result := '';
end;


function ShouldRunLauncher(): Boolean;
begin
  Result := True;

  if Pos('SILENT', UpperCase(GetCmdTail())) > 0 then
    Result := False;

  if Pos('LAUNCH', UpperCase(GetCmdTail())) > 0 then
    Result := True;
end;


function FolderSize(FolderPath: String): Int64;
var
  FindRec: TFindRec;
begin
  Result := 0;
  if FindFirst(FolderPath + '\*', FindRec) then
  begin
    try
      repeat
        if (FindRec.Attributes and FILE_ATTRIBUTE_DIRECTORY) = 0 then
          Result := Result + FindRec.SizeLow
        else if (FindRec.Name <> '.') and (FindRec.Name <> '..') then
          Result := Result + FolderSize(FolderPath + '\' + FindRec.Name);
      until not FindNext(FindRec);
    finally
      FindClose(FindRec);
    end;
  end;
end;


function IsFolderValid(FolderPath: String): Boolean;
begin
  Result := DirExists(FolderPath) and (FolderSize(FolderPath) > 1024 * 1024);
end;


/// DMB: whether a file holds a piece of text
function FileHolds(const FileName, Text: String): Boolean;
var
  Content: AnsiString;
begin
  Result := False;
  if FileExists(FileName) and LoadStringFromFile(FileName, Content) then
    Result := Pos(Text, String(Content)) > 0;
end;


/// DMB: whether a user folder was made by DMB. It holds what no other program leaves there, the traces
/// the game looks for too (lib/VCMIDirs.cpp): the catalog's pins, a map generator's log, a build
/// approved for testing, or a line only DMB logs. K: an upgrade checks a folder's contents, not its name.
function UserFolderMadeByDmb(const Dir: String): Boolean;
begin
  Result := DirExists(Dir) and (FileExists(Dir + '\cache\downloads\dmbCodePins.json') or
    FileExists(Dir + '\logs\extmapgen_log.txt') or FileExists(Dir + '\config\dmbTestedCode.json') or
    FileHolds(Dir + '\logs\VCMI_Launcher_log.txt', 'DMB''s mod catalog') or
    FileHolds(Dir + '\logs\VCMI_Launcher_log.txt', 'Launcher look:') or
    FileHolds(Dir + '\logs\VCMI_Client_log.txt', 'OmniAI''s folder:'));
end;


/// DMB: whether a folder holds DMB's programs: the launcher, the game, and the settings schema naming
/// DMB's catalog, which every DMB since 0.1.0-rc.1 has
function DmbProgramsIn(const Dir: String): Boolean;
begin
  Result := FileExists(Dir + '\VCMI_launcher.exe') and FileExists(Dir + '\VCMI_client.exe') and
    FileHolds(Dir + '\config\schemas\settings.json', 'DeadMansBoots');
end;


/// DMB: the earlier user folder this install renames, or '': one DMB made, while the current one does
/// not exist yet. With both, the game decides on its first start.
function EarlierUserFolderIn(const Docs: String): String;
begin
  Result := '';
  if UserFolderMadeByDmb(Docs + '\{#EarlierFilesFolder}') and not DirExists(Docs + '\{#FilesFolderPas}') then
    Result := Docs + '\{#EarlierFilesFolder}';
end;


/// DMB: renames the earlier user folder, in one step on the same drive. Nothing moves when it fails (a
/// file in it still open), and the game tries again on its first start.
function MoveEarlierUserFolder(const Docs: String): Boolean;
begin
  Result := RenameFile(Docs + '\{#EarlierFilesFolder}', Docs + '\{#FilesFolderPas}');
end;


/// DMB: an earlier install folder that moves, or '': one named DMB that holds DMB's programs and no
/// config\dirs.json (which keeps the player's own files in the install folder)
function EarlierProgramFolderOf(const Dir: String): String;
var
  Folder: String;
begin
  Result := '';
  Folder := RemoveBackslashUnlessRoot(Dir);
  if (CompareText(ExtractFileName(Folder), '{#EarlierName}') = 0) and DmbProgramsIn(Folder) and
     not FileExists(Folder + '\config\dirs.json') then
    Result := Folder;
end;


/// DMB: where an earlier install folder moves: the new name, beside it
function RelocatedProgramFolder(const Earlier: String): String;
begin
  Result := AddBackslash(ExtractFileDir(RemoveBackslashUnlessRoot(Earlier))) + '{#DMBNamePas}';
end;


/// DMB: deletes the earlier install folder once the new one is installed, checking again first that it
/// is DMB's and is not where this install went
function RemoveEarlierProgramFolder(const Earlier, App: String): Boolean;
begin
  Result := False;
  if (Earlier = '') or (EarlierProgramFolderOf(Earlier) = '') or
     (CompareText(RemoveBackslashUnlessRoot(Earlier), RemoveBackslashUnlessRoot(App)) = 0) then
    Exit;
  Result := DelTree(Earlier, True, True, True);
end;


/// DMB: the install folder the earlier firewall rules name: the earlier one, when it moved
function EarlierAppDir(Param: String): String;
begin
  if EarlierProgramFolder <> '' then
    Result := EarlierProgramFolder
  else
    Result := ExpandConstant('{app}');
end;


/// DMB: where Heroes III's files are looked for and copied: the earlier user folder until it is renamed
procedure SetUserFilesFolder(const Root: String);
begin
  VCMIMapsFolder := Root + '\Maps';
  VCMIDataFolder := Root + '\Data';
  VCMIMp3Folder := Root + '\Mp3';
end;


/// DMB (K, October 8th: "I notice DMB launcher just blindly reinstalls over existing installs"): the version of the install on this PC,
/// read from the key every Inno Setup install writes (an install for all users or for this user only), or '' when there is none
function InstalledDisplayVersion(): String;
var
  Key, Value: String;
begin
  Result := '';
  Key := 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{#VCMIFolder}.{#InstallerArch}_is1';
  if RegQueryStringValue(HKLM, Key, 'DisplayVersion', Value) then
    Result := Value
  else if RegQueryStringValue(HKCU, Key, 'DisplayVersion', Value) then
    Result := Value;
end;


/// The question is asked only when the version installed is exactly the one this setup carries (a repair); an older, a different or no
/// install goes on with no question, as before
function RepairQuestionNeeded(const Installed, Carried: String): Boolean;
begin
  Result := (Installed <> '') and (Installed = Carried);
end;


/// DMB (K, October 9th: "NEED TO OFFER THE USER A FIRST-TIME SETUP SPECIFIC OPTIONS"): a first install on a PC that has VCMI offers to
/// copy VCMI's mods and settings, or to copy them and remove VCMI, or to install with neither. Everything below runs only on that
/// choice; the third choice, a silent install and an upgrade never reach it. Nothing here deletes a folder: VCMI is removed by its
/// own uninstaller and only after every copied file has been checked. Heroes III's files, Steam and GOG folders are never touched.
///
/// Where VCMI's files are (K, October 9th: "intelligently determines where all the local files are"): the install folder comes from VCMI's
/// own entry in Windows' list of installed programs, checked against the files in it. The user folders come from the rules VCMI itself
/// follows (lib/VCMIDirs.cpp): the keys of <install folder>\config\dirs.json when that file names them, otherwise the Documents folder
/// Windows reports for the user (a moved or OneDrive Documents folder included), \My Games\vcmi, with config, Saves, Mods and Maps in it.
const
  ReparsePointAttribute = $400;
  DmbCatalogUrl = 'https://raw.githubusercontent.com/DeadMansBoots/DMB-Mods-Repository/main/dmb-1.7.json';
  DmbUpdateUrl = 'https://raw.githubusercontent.com/DeadMansBoots/DMB-Mods-Repository/main/dmb-updates.json';

function GetTickCount: Cardinal; external 'GetTickCount@kernel32.dll stdcall';


function YesNo(Value: Boolean): String;
begin
  if Value then
    Result := 'yes'
  else
    Result := 'no';
end;


function MegabytesText(Bytes: Int64): String;
begin
  Result := IntToStr(Bytes div 1048576) + ' MB';
end;


function FilesText(Count: Integer): String;
begin
  if Count = 1 then
    Result := '1 file'
  else
    Result := IntToStr(Count) + ' files';
end;


/// where Needle (ASCII) first appears in Content at or after From, or 0. Byte by byte: the settings are UTF-8, and a character set
/// conversion of them could shift or spoil what is not ASCII
function BytePos(const Needle: String; const Content: AnsiString; From: Integer): Integer;
var
  I, J, L: Integer;
  Match: Boolean;
begin
  Result := 0;
  L := Length(Needle);
  I := From;
  while (Result = 0) and (I + L - 1 <= Length(Content)) do
  begin
    Match := True;
    J := 1;
    while Match and (J <= L) do
    begin
      if Ord(Content[I + J - 1]) <> Ord(Needle[J]) then
        Match := False;
      J := J + 1;
    end;
    if Match then
      Result := I;
    I := I + 1;
  end;
end;


/// the string a JSON key holds, when it is plain ASCII (a path with other characters is not trusted: '' then), with JSON's doubled
/// backslashes undone and slashes turned into backslashes
function JsonStringValue(const Content: AnsiString; const Key: String): String;
var
  P, Colon, OpenQuote, CloseQuote, I: Integer;
  OnlySpace: Boolean;
begin
  Result := '';
  P := BytePos('"' + Key + '"', Content, 1);
  if P = 0 then
    Exit;
  Colon := BytePos(':', Content, P + Length(Key) + 2);
  if Colon = 0 then
    Exit;
  OpenQuote := BytePos('"', Content, Colon + 1);
  if OpenQuote = 0 then
    Exit;
  OnlySpace := True;
  for I := Colon + 1 to OpenQuote - 1 do
    if Ord(Content[I]) > 32 then
      OnlySpace := False;
  if not OnlySpace then
    Exit;
  CloseQuote := BytePos('"', Content, OpenQuote + 1);
  if CloseQuote = 0 then
    Exit;
  for I := OpenQuote + 1 to CloseQuote - 1 do
  begin
    if Ord(Content[I]) > 127 then
    begin
      Result := '';
      Exit;
    end;
    Result := Result + Chr(Ord(Content[I]));
  end;
  StringChangeEx(Result, '\\', '\', True);
  StringChangeEx(Result, '/', '\', True);
end;


/// %NAME% in a path replaced by the environment variable, as Windows' ExpandEnvironmentStrings does for VCMI
function ExpandPercent(const Text: String): String;
var
  Rest, Name: String;
  P, Q: Integer;
begin
  Result := '';
  Rest := Text;
  while Rest <> '' do
  begin
    P := Pos('%', Rest);
    if P = 0 then
    begin
      Result := Result + Rest;
      Rest := '';
    end
    else
    begin
      Result := Result + Copy(Rest, 1, P - 1);
      Delete(Rest, 1, P);
      Q := Pos('%', Rest);
      if Q = 0 then
      begin
        Result := Result + '%' + Rest;
        Rest := '';
      end
      else
      begin
        Name := Copy(Rest, 1, Q - 1);
        Delete(Rest, 1, Q);
        if GetEnv(Name) <> '' then
          Result := Result + GetEnv(Name)
        else
          Result := Result + '%' + Name + '%';
      end;
    end;
  end;
end;


/// the program a Windows uninstall string runs: "C:\path\unins000.exe" /something, or the path alone
function UninstallExeOf(const UninstallString: String): String;
var
  S: String;
  P: Integer;
begin
  S := Trim(UninstallString);
  Result := S;
  if S = '' then
    Exit;
  if S[1] = '"' then
  begin
    Delete(S, 1, 1);
    P := Pos('"', S);
    if P > 0 then
      Result := Copy(S, 1, P - 1)
    else
      Result := S;
  end
  else
  begin
    P := Pos(' /', S);
    if P > 0 then
      Result := Copy(S, 1, P - 1);
  end;
end;


function PathInside(const Child, Parent: String): Boolean;
begin
  Result := (Parent <> '') and (Pos(Lowercase(AddBackslash(RemoveBackslashUnlessRoot(Parent))), Lowercase(Child)) = 1);
end;


/// whether a folder holds VCMI's programs and is not DMB's (DMB's share VCMI's file names)
function VcmiProgramsIn(const Dir: String): Boolean;
begin
  Result := DirExists(Dir) and (FileExists(Dir + '\VCMI_client.exe') or FileExists(Dir + '\VCMI_launcher.exe')) and not DmbProgramsIn(Dir);
end;


/// whether one entry of Windows' list of installed programs is a VCMI install this setup may offer to remove: named VCMI, installed in a
/// folder that holds VCMI's programs, with an Inno uninstaller inside that folder. On a yes the VcmiXxx variables describe it.
function ReadVcmiCandidate(Root: Integer; const Base, Sub: String): Boolean;
var
  Key, DisplayName, Version, Location, Unins, Exe: String;
begin
  Result := False;
  Key := Base + '\' + Sub;
  if Pos('VCMI', UpperCase(Sub)) <> 1 then
    Exit;
  if not RegQueryStringValue(Root, Key, 'DisplayName', DisplayName) then
    Exit;
  if Pos('VCMI', UpperCase(DisplayName)) <> 1 then
    Exit;
  if not RegQueryStringValue(Root, Key, 'InstallLocation', Location) then
    Exit;
  if not RegQueryStringValue(Root, Key, 'UninstallString', Unins) then
    Exit;
  if not RegQueryStringValue(Root, Key, 'DisplayVersion', Version) then
    Version := '';
  Location := RemoveBackslashUnlessRoot(Trim(Location));
  Exe := UninstallExeOf(Unins);
  if not VcmiProgramsIn(Location) then
    Exit;
  if not FileExists(Exe) then
    Exit;
  if not PathInside(Exe, Location) then
    Exit;
  if Pos('UNINS', UpperCase(ExtractFileName(Exe))) <> 1 then
    Exit;
  VcmiName := DisplayName;
  VcmiVersion := Version;
  VcmiInstallDir := Location;
  VcmiUninstallExe := Exe;
  VcmiKeyRoot := Root;
  VcmiKeyPath := Key;
  Result := True;
end;


function FindVcmiIn(Root: Integer; const Base: String): Boolean;
var
  Names: TArrayOfString;
  I: Integer;
begin
  Result := False;
  if RegGetSubkeyNames(Root, Base, Names) then
    for I := 0 to GetArrayLength(Names) - 1 do
      if not Result then
        Result := ReadVcmiCandidate(Root, Base, Names[I]);
end;


function CountFilesIn(const Dir: String; var Bytes: Int64): Integer;
var
  FindRec: TFindRec;
begin
  Result := 0;
  if FindFirst(Dir + '\*', FindRec) then
  begin
    try
      repeat
        if (FindRec.Name <> '.') and (FindRec.Name <> '..') then
        begin
          if (FindRec.Attributes and FILE_ATTRIBUTE_DIRECTORY) <> 0 then
          begin
            if (FindRec.Attributes and ReparsePointAttribute) = 0 then
              Result := Result + CountFilesIn(Dir + '\' + FindRec.Name, Bytes);
          end
          else
          begin
            Result := Result + 1;
            Bytes := Bytes + Int64(FindRec.SizeHigh) * 4294967296 + Int64(FindRec.SizeLow);
          end;
        end;
      until not FindNext(FindRec);
    finally
      FindClose(FindRec);
    end;
  end;
end;


/// the four things that move over, by the name they have in both user folders (config holds the settings and the presets) ...
function MigratedFolderName(Index: Integer): String;
begin
  case Index of
    0: Result := 'config';
    1: Result := 'Mods';
    2: Result := 'Saves';
  else
    Result := 'Maps';
  end;
end;


function MigratedFolderWords(Index: Integer): String;
begin
  case Index of
    0: Result := 'settings and presets';
    1: Result := 'mods';
    2: Result := 'saves';
  else
    Result := 'maps';
  end;
end;


/// ... and where each one is in VCMI's: the folders VCMI itself would use
function VcmiSourceFolder(Index: Integer): String;
begin
  case Index of
    0: Result := VcmiConfigDir;
    1: Result := VcmiUserDir + '\Mods';
    2: Result := VcmiSavesDir;
  else
    Result := VcmiUserDir + '\Maps';
  end;
end;


/// VCMI's user folders by VCMI's own rules (lib/VCMIDirs.cpp): the keys config\dirs.json in its install folder names, and for the rest
/// Documents (as Windows reports it for this user) + \My Games\vcmi, with config and Saves inside
procedure ResolveVcmiFolders(const Docs: String);
var
  Content: AnsiString;
  Value: String;
begin
  VcmiUserDir := Docs + '\My Games\vcmi';
  VcmiConfigDir := '';
  VcmiSavesDir := '';
  VcmiUsesDirsJson := False;
  if (VcmiInstallDir <> '') and FileExists(VcmiInstallDir + '\config\dirs.json') and LoadStringFromFile(VcmiInstallDir + '\config\dirs.json', Content) then
  begin
    Value := ExpandPercent(JsonStringValue(Content, 'userDataPath'));
    if Value <> '' then
    begin
      VcmiUserDir := RemoveBackslashUnlessRoot(Value);
      VcmiUsesDirsJson := True;
    end;
    Value := ExpandPercent(JsonStringValue(Content, 'userConfigPath'));
    if Value <> '' then
    begin
      VcmiConfigDir := RemoveBackslashUnlessRoot(Value);
      VcmiUsesDirsJson := True;
    end;
    Value := ExpandPercent(JsonStringValue(Content, 'userSavePath'));
    if Value <> '' then
    begin
      VcmiSavesDir := RemoveBackslashUnlessRoot(Value);
      VcmiUsesDirsJson := True;
    end;
  end;
  if VcmiConfigDir = '' then
    VcmiConfigDir := VcmiUserDir + '\config';
  if VcmiSavesDir = '' then
    VcmiSavesDir := VcmiUserDir + '\Saves';
end;


/// whether any of VCMI's four folders holds a file
function VcmiHasUserData(): Boolean;
var
  I: Integer;
  Bytes: Int64;
begin
  Result := False;
  for I := 0 to 3 do
  begin
    Bytes := 0;
    if not Result then
      Result := CountFilesIn(VcmiSourceFolder(I), Bytes) > 0;
  end;
end;


/// looks for VCMI: its entry in Windows' list of installed programs (all users, 32-bit view, this user), then its user folders by VCMI's
/// own rules. VcmiFound: an install that can be removed; VcmiDataFound: VCMI files that can be copied (a portable VCMI or one that was
/// already uninstalled leaves only these)
procedure FindVcmiInstall(const Docs: String);
var
  Base: String;
begin
  Base := 'Software\Microsoft\Windows\CurrentVersion\Uninstall';
  VcmiFound := False;
  VcmiInstallDir := '';
#ifdef DMBFakeVcmi
  VcmiFound := FindVcmiIn(HKCU, ExpandConstant('{%DMB_FAKE_VCMI_KEY|}'));
#else
  VcmiFound := FindVcmiIn(HKLM64, Base);
  if not VcmiFound then
    VcmiFound := FindVcmiIn(HKLM32, Base);
  if not VcmiFound then
    VcmiFound := FindVcmiIn(HKCU, Base);
#endif
  ResolveVcmiFolders(Docs);
  VcmiDataFound := VcmiHasUserData();
end;


function IsHashedFile(const Name: String): Boolean;
begin
  Result := (CompareText(Name, 'settings.json') = 0) or (CompareText(Name, 'modSettings.json') = 0) or (CompareText(Name, 'mod.json') = 0);
end;


/// copies a folder tree file by file and checks each file: its size, and for the settings, the presets and every mod.json its SHA-256.
/// A file already in the destination is kept as it is, never overwritten. A link is not followed.
procedure CopyTreeVerified(const Src, Dst: String);
var
  FindRec: TFindRec;
  S, D: String;
  SSize, DSize: Int64;
  Ok: Boolean;
begin
  if not DirExists(Dst) then
    if not ForceDirectories(Dst) then
    begin
      MigFailed := MigFailed + 1;
      MigLog.Add('FAILED: the folder ' + Dst + ' could not be made');
      Exit;
    end;
  if FindFirst(Src + '\*', FindRec) then
  begin
    try
      repeat
        if (FindRec.Name <> '.') and (FindRec.Name <> '..') then
        begin
          S := Src + '\' + FindRec.Name;
          D := Dst + '\' + FindRec.Name;
          if (FindRec.Attributes and FILE_ATTRIBUTE_DIRECTORY) <> 0 then
          begin
            if (FindRec.Attributes and ReparsePointAttribute) = 0 then
              CopyTreeVerified(S, D)
            else
              MigLog.Add('skipped, a link: ' + S);
          end
          else if FileExists(D) then
          begin
            MigExisting := MigExisting + 1;
            MigLog.Add('kept, already in Dead Man''s Boots: ' + D);
          end
          else
          begin
            Ok := False;
            SSize := 0;
            if FileCopy(S, D, True) then
              if FileSize64(S, SSize) and FileSize64(D, DSize) and (SSize = DSize) then
              begin
                Ok := True;
                if IsHashedFile(FindRec.Name) then
                  Ok := CompareText(GetSHA256OfFile(S), GetSHA256OfFile(D)) = 0;
              end;
            if Ok then
            begin
              MigCopied := MigCopied + 1;
              MigBytes := MigBytes + SSize;
            end
            else
            begin
              MigFailed := MigFailed + 1;
              MigLog.Add('FAILED: ' + S);
            end;
          end;
        end;
      until not FindNext(FindRec);
    finally
      FindClose(FindRec);
    end;
  end;
end;


/// copies VCMI's user data into Dead Man's Boots' folder and checks it: every file's size, a hash for settings, presets and mod.json,
/// and that the number of files copied and kept equals the number of files in VCMI's folders. True only when all of it holds.
function MigrateUserData(const DstUser: String): Boolean;
var
  I, SrcCount: Integer;
  Dummy: Int64;
begin
  MigCopied := 0;
  MigExisting := 0;
  MigFailed := 0;
  MigBytes := 0;
  SrcCount := 0;
  Dummy := 0;
  Result := False;
  for I := 0 to 3 do
    if DirExists(VcmiSourceFolder(I)) then
    begin
      MigLog.Add(MigratedFolderWords(I) + ': ' + VcmiSourceFolder(I));
      SrcCount := SrcCount + CountFilesIn(VcmiSourceFolder(I), Dummy);
      CopyTreeVerified(VcmiSourceFolder(I), DstUser + '\' + MigratedFolderName(I));
    end;
  if SrcCount = 0 then
  begin
    MigLog.Add('no VCMI files found to copy');
    Exit;
  end;
  if MigCopied + MigExisting + MigFailed <> SrcCount then
  begin
    MigFailed := MigFailed + 1;
    MigLog.Add('FAILED: ' + IntToStr(SrcCount) + ' files in VCMI''s folders, ' + IntToStr(MigCopied + MigExisting + MigFailed) + ' accounted for');
  end;
  MigLog.Add('files in VCMI''s folders: ' + IntToStr(SrcCount) + '; copied and checked: ' + IntToStr(MigCopied) + ' (' + MegabytesText(MigBytes) +
    '); kept as they were: ' + IntToStr(MigExisting) + '; failed: ' + IntToStr(MigFailed));
  Result := MigFailed = 0;
end;


/// in the copied settings, the addresses of the mod list and of the update notice name DMB's own again (VCMI's settings can hold VCMI's
/// list, which would leave Omni Map Gen and the other DMB mods out of the launcher)
function SetJsonString(var Content: AnsiString; const Key: String; const Value: AnsiString): Boolean;
var
  P, Colon, OpenQuote, CloseQuote, I: Integer;
  OnlySpace: Boolean;
begin
  Result := False;
  P := BytePos('"' + Key + '"', Content, 1);
  if P = 0 then
    Exit;
  Colon := BytePos(':', Content, P + Length(Key) + 2);
  if Colon = 0 then
    Exit;
  OpenQuote := BytePos('"', Content, Colon + 1);
  if OpenQuote = 0 then
    Exit;
  OnlySpace := True;
  for I := Colon + 1 to OpenQuote - 1 do
    if Ord(Content[I]) > 32 then
      OnlySpace := False;
  if not OnlySpace then
    Exit;
  CloseQuote := BytePos('"', Content, OpenQuote + 1);
  if CloseQuote = 0 then
    Exit;
  Content := Copy(Content, 1, OpenQuote) + Value + Copy(Content, CloseQuote, Length(Content));
  Result := True;
end;


function FixCopiedSettings(const DstUser: String): Boolean;
var
  Path: String;
  Content: AnsiString;
begin
  Result := False;
  Path := DstUser + '\config\settings.json';
  if not (FileExists(Path) and LoadStringFromFile(Path, Content)) then
    Exit;
  if SetJsonString(Content, 'defaultRepositoryURL', DmbCatalogUrl) then
    Result := True;
  if SetJsonString(Content, 'updateConfigUrl', DmbUpdateUrl) then
    Result := True;
  if Result then
    Result := SaveStringToFile(Path, Content, False);
end;


/// runs VCMI's own uninstaller and waits for its entry in the list of installed programs to go. VCMI's uninstaller (like DMB's, from the
/// same script) is a window with an Uninstall button and a "Delete user data" box that the person presses: this setup starts it and waits.
function RunVcmiUninstall(): Boolean;
var
  Code, I: Integer;
begin
  Result := False;
  // not waited for by its process (a window of its own may stay up): what counts is its entry in the list of programs going away
  if not ShellExec('', VcmiUninstallExe, VcmiUninstallParams, ExtractFileDir(VcmiUninstallExe), SW_SHOWNORMAL, ewNoWait, Code) then
  begin
    MigLog.Add('VCMI''s uninstaller could not be started: ' + VcmiUninstallExe);
    Exit;
  end;
  for I := 1 to VcmiUninstallWaitSeconds * 2 do
  begin
    if not RegKeyExists(VcmiKeyRoot, VcmiKeyPath) then
    begin
      Result := True;
      Break;
    end;
    Sleep(500);
  end;
  MigLog.Add('VCMI''s uninstaller was started; VCMI is gone: ' + YesNo(Result));
end;


/// the whole first-time setup for the choice made (SetupChoice, RemoveConfirmed) on the VCMI found: copy and check, and only after a clean
/// check, on the player's second confirmation, remove VCMI with its own uninstaller. Returns the plain summary the last page shows.
function FirstTimeSetup(const DstUser: String): String;
var
  Started: Cardinal;
  CopyOk, FixedSettings, Removed, WantsRemoval: Boolean;
  Nl: String;
begin
  Nl := #13#10;
  Result := '';
  if (SetupChoice > 1) or not (VcmiFound or VcmiDataFound) then
    Exit;
  MigLog := TStringList.Create;
  try
    if VcmiFound then
      MigLog.Add(VcmiName + ' ' + VcmiVersion + ' in ' + VcmiInstallDir + '; its user folder ' + VcmiUserDir + '; dirs.json used: ' + YesNo(VcmiUsesDirsJson))
    else
      MigLog.Add('no VCMI install in the list of programs; VCMI files in ' + VcmiUserDir);
    Started := GetTickCount;
    CopyOk := MigrateUserData(DstUser);
    MigLog.Add('copy and check took ' + IntToStr(GetTickCount - Started) + ' ms');
    FixedSettings := CopyOk and FixCopiedSettings(DstUser);
    WantsRemoval := VcmiFound and (SetupChoice = 1) and RemoveConfirmed;
    Removed := False;
    if CopyOk and WantsRemoval then
    begin
      Started := GetTickCount;
      Removed := RunVcmiUninstall();
      MigLog.Add('removing VCMI took ' + IntToStr(GetTickCount - Started) + ' ms');
    end;
    if CopyOk then
    begin
      Result := 'From VCMI to Dead Man''s Boots: ' + FilesText(MigCopied) + ' (' + MegabytesText(MigBytes) + ') copied and checked: mods, saves, maps, settings and presets.';
      if MigExisting > 0 then
        Result := Result + Nl + FilesText(MigExisting) + ' already in Dead Man''s Boots were kept as they were.';
      if FixedSettings then
        Result := Result + Nl + 'The copied settings now use Dead Man''s Boots'' own mod list.';
      if Removed then
        Result := Result + Nl + 'VCMI was removed with its own uninstaller.'
      else if WantsRemoval then
        Result := Result + Nl + 'VCMI''s uninstaller did not finish, so VCMI stays installed.'
      else if VcmiFound then
        Result := Result + Nl + 'VCMI stays installed as it was.';
    end
    else if WantsRemoval then
      Result := 'Nothing was removed. The copy from VCMI could not be completed and checked, so VCMI is left in place.'
    else
      Result := 'The copy from VCMI could not be completed and checked. VCMI is left as it was.';
    Result := Result + Nl + 'Left alone: Heroes III''s files, Steam and GOG folders, and VCMI''s own user folder.';
    ForceDirectories(DstUser + '\logs');
    MigLog.Add(Result);
    MigLog.SaveToFile(DstUser + '\logs\first-time-setup.txt');
  finally
    MigLog.Free;
  end;
end;


/// what the first page says about the VCMI found
function VcmiDetailsText(): String;
var
  Nl: String;
  Bytes: Int64;
  Count, I: Integer;
begin
  Nl := #13#10;
  if VcmiFound then
  begin
    Result := VcmiName;
    if VcmiVersion <> '' then
      Result := Result + ' ' + VcmiVersion;
    Result := Result + ' is installed in ' + VcmiInstallDir + '.';
  end
  else
    Result := 'No VCMI install is in the list of installed programs, but VCMI''s files were found, and can be copied.';
  Result := Result + Nl + 'Its files:';
  for I := 0 to 3 do
    if DirExists(VcmiSourceFolder(I)) then
    begin
      Bytes := 0;
      Count := CountFilesIn(VcmiSourceFolder(I), Bytes);
      Result := Result + Nl + '    ' + MigratedFolderWords(I) + ': ' + FilesText(Count) + ', ' + MegabytesText(Bytes) + ' (' + VcmiSourceFolder(I) + ')';
    end;
end;


#ifdef DMBSelfTest
function BoolText(Value: Boolean): String;
begin
  if Value then
    Result := 'true'
  else
    Result := 'false';
end;


/// puts the VCMI user folders of a scenario where the test made them (Root\sN\vcmi-user), as ResolveVcmiFolders would for a default VCMI
procedure SelfTestUserFolders(const Dir: String);
begin
  VcmiUserDir := Dir;
  VcmiConfigDir := Dir + '\config';
  VcmiSavesDir := Dir + '\Saves';
  VcmiDataFound := True;
end;


/// the first-time setup on pretend VCMI installs that test_installer_moves.py made: scenario N has its pretend install's entry under
/// HKCU\Software\DMBSelfTest\sN, a pretend VCMI user folder Root\sN\vcmi-user and a Dead Man's Boots folder Root\sN\dmb-user
procedure SelfTestFirstTime(const Root: String; Results: TStringList);
var
  N: Integer;
  Scenario, Prefix, Summary: String;
begin
  Results.Add('first.exe.quoted=' + UninstallExeOf('"C:\Program Files\VCMI\unins000.exe" /SILENT'));
  Results.Add('first.exe.plain=' + UninstallExeOf('C:\Apps\VCMI\unins000.exe /X'));
  Results.Add('first.exe.alone=' + UninstallExeOf('C:\Apps\VCMI\unins000.exe'));
  Results.Add('first.neg.noprograms=' + BoolText(ReadVcmiCandidate(HKCU, 'Software\DMBSelfTest\neg', 'VCMI.noprograms_is1')));
  Results.Add('first.neg.outside=' + BoolText(ReadVcmiCandidate(HKCU, 'Software\DMBSelfTest\neg', 'VCMI.outside_is1')));
  Results.Add('first.neg.dmb=' + BoolText(ReadVcmiCandidate(HKCU, 'Software\DMBSelfTest\neg', 'VCMI.dmb_is1')));
  Results.Add('first.neg.othername=' + BoolText(ReadVcmiCandidate(HKCU, 'Software\DMBSelfTest\neg', 'Other.app_is1')));
  Results.Add('first.neg.nouninstaller=' + BoolText(ReadVcmiCandidate(HKCU, 'Software\DMBSelfTest\neg', 'VCMI.nouninstaller_is1')));
  Results.Add('first.neg.findany=' + BoolText(FindVcmiIn(HKCU, 'Software\DMBSelfTest\neg')));
  Results.Add('first.words.files1=' + FilesText(1));
  Results.Add('first.words.files12=' + FilesText(12));
  VcmiUninstallWaitSeconds := 4;
  for N := 0 to 4 do
  begin
    Scenario := 's' + IntToStr(N);
    Prefix := 'first.' + Scenario + '.';
    VcmiFound := FindVcmiIn(HKCU, 'Software\DMBSelfTest\' + Scenario);
    Results.Add(Prefix + 'found=' + BoolText(VcmiFound));
    if VcmiFound then
    begin
      Results.Add(Prefix + 'name=' + VcmiName);
      Results.Add(Prefix + 'version=' + VcmiVersion);
      Results.Add(Prefix + 'dir=' + VcmiInstallDir);
      Results.Add(Prefix + 'exe=' + VcmiUninstallExe);
      SetupChoice := 1;
      RemoveConfirmed := True;
      if N = 0 then
      begin
        SetupChoice := 0;
        RemoveConfirmed := False;
      end;
      if N = 2 then
        RemoveConfirmed := False;
      if N = 4 then
        SetupChoice := 2;
      VcmiUninstallParams := '/c "' + Root + '\' + Scenario + '\fake-install\uninstall.cmd"';
      SelfTestUserFolders(Root + '\' + Scenario + '\vcmi-user');
      MigCopied := 0;
      MigExisting := 0;
      MigFailed := 0;
      Summary := FirstTimeSetup(Root + '\' + Scenario + '\dmb-user');
      StringChangeEx(Summary, #13#10, ' | ', True);
      Results.Add(Prefix + 'summary=' + Summary);
      Results.Add(Prefix + 'copied=' + IntToStr(MigCopied));
      Results.Add(Prefix + 'kept=' + IntToStr(MigExisting));
      Results.Add(Prefix + 'failed=' + IntToStr(MigFailed));
      Results.Add(Prefix + 'registered-after=' + BoolText(RegKeyExists(VcmiKeyRoot, VcmiKeyPath)));
    end;
  end;
  // s5: a real Inno Setup uninstaller with the custom uninstall form VCMI's own installer script has. The test made a real (pretend) VCMI
  // install with its own Inno setup, under the test's own key name VCMItest, and says so with a marker file. Only that one key is read here:
  // never the PC's own list of installed programs as a whole.
  if FileExists(Root + '\s5\real-installed.txt') then
  begin
    Prefix := 'first.s5.';
    VcmiFound := ReadVcmiCandidate(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Uninstall', 'VCMItest_is1');
    Results.Add(Prefix + 'found=' + BoolText(VcmiFound));
    if VcmiFound then
    begin
      Results.Add(Prefix + 'exe=' + VcmiUninstallExe);
      VcmiUninstallParams := '/SILENT /NORESTART /SUPPRESSMSGBOXES';
      VcmiUninstallWaitSeconds := 60;
      SetupChoice := 1;
      RemoveConfirmed := True;
      SelfTestUserFolders(Root + '\s5\vcmi-user');
      Summary := FirstTimeSetup(Root + '\s5\dmb-user');
      StringChangeEx(Summary, #13#10, ' | ', True);
      Results.Add(Prefix + 'summary=' + Summary);
      Results.Add(Prefix + 'copied=' + IntToStr(MigCopied));
      Results.Add(Prefix + 'failed=' + IntToStr(MigFailed));
      Results.Add(Prefix + 'registered-after=' + BoolText(RegKeyExists(VcmiKeyRoot, VcmiKeyPath)));
    end;
  end;
  // s6: VCMI told where its folders are by config\dirs.json (data and saves by path, config by an environment variable)
  Prefix := 'first.s6.';
  VcmiFound := FindVcmiIn(HKCU, 'Software\DMBSelfTest\s6');
  ResolveVcmiFolders(Root + '\s6\docs-never-used');
  Results.Add(Prefix + 'found=' + BoolText(VcmiFound));
  Results.Add(Prefix + 'dirsjson=' + BoolText(VcmiUsesDirsJson));
  Results.Add(Prefix + 'userdir=' + VcmiUserDir);
  Results.Add(Prefix + 'configdir=' + VcmiConfigDir);
  Results.Add(Prefix + 'savesdir=' + VcmiSavesDir);
  VcmiDataFound := VcmiHasUserData();
  Results.Add(Prefix + 'datafound=' + BoolText(VcmiDataFound));
  SetupChoice := 0;
  RemoveConfirmed := False;
  Summary := FirstTimeSetup(Root + '\s6\dmb-user');
  Results.Add(Prefix + 'copied=' + IntToStr(MigCopied));
  Results.Add(Prefix + 'failed=' + IntToStr(MigFailed));
  // s7: no entry in the list of installed programs, but VCMI's files are where VCMI keeps them by default (Documents\My Games\vcmi): the
  // copy is offered and a removal never happens
  Prefix := 'first.s7.';
  VcmiFound := FindVcmiIn(HKCU, 'Software\DMBSelfTest\s7-nothing-here');
  VcmiInstallDir := '';
  ResolveVcmiFolders(Root + '\s7\docs');
  VcmiDataFound := VcmiHasUserData();
  Results.Add(Prefix + 'found=' + BoolText(VcmiFound));
  Results.Add(Prefix + 'datafound=' + BoolText(VcmiDataFound));
  Results.Add(Prefix + 'userdir=' + VcmiUserDir);
  SetupChoice := 1;
  RemoveConfirmed := True;
  Summary := FirstTimeSetup(Root + '\s7\dmb-user');
  StringChangeEx(Summary, #13#10, ' | ', True);
  Results.Add(Prefix + 'summary=' + Summary);
  Results.Add(Prefix + 'copied=' + IntToStr(MigCopied));
end;


/// DMB: a build for tests only (make_installer.py --self-test): runs the functions above on the scratch
/// folders test_installer_moves.py made, writes what they returned into results.txt there, and ends
/// before the wizard opens, so nothing is installed
procedure SelfTest(const Root: String);
var
  Results: TStringList;
begin
  Results := TStringList.Create;
  try
    Results.Add('user-genuine.earlier=' + EarlierUserFolderIn(Root + '\docs-genuine'));
    Results.Add('user-genuine.moved=' + BoolText(MoveEarlierUserFolder(Root + '\docs-genuine')));
    Results.Add('user-logs.earlier=' + EarlierUserFolderIn(Root + '\docs-logs'));
    Results.Add('user-logs.moved=' + BoolText(MoveEarlierUserFolder(Root + '\docs-logs')));
    Results.Add('user-other.earlier=' + EarlierUserFolderIn(Root + '\docs-other'));
    Results.Add('user-both.earlier=' + EarlierUserFolderIn(Root + '\docs-both'));
    Results.Add('user-none.earlier=' + EarlierUserFolderIn(Root + '\docs-none'));
    Results.Add('user-held.earlier=' + EarlierUserFolderIn(Root + '\docs-held'));
    Results.Add('user-held.moved=' + BoolText(MoveEarlierUserFolder(Root + '\docs-held')));
    Results.Add('programs.earlier=' + EarlierProgramFolderOf(Root + '\progs\DMB\'));
    Results.Add('programs.relocated=' + RelocatedProgramFolder(Root + '\progs\DMB'));
    Results.Add('programs.removed-when-same=' + BoolText(RemoveEarlierProgramFolder(Root + '\progs\DMB', Root + '\progs\DMB\')));
    Results.Add('programs.removed=' + BoolText(RemoveEarlierProgramFolder(Root + '\progs\DMB', RelocatedProgramFolder(Root + '\progs\DMB'))));
    Results.Add('programs-portable.earlier=' + EarlierProgramFolderOf(Root + '\progs-portable\DMB'));
    Results.Add('programs-other.earlier=' + EarlierProgramFolderOf(Root + '\progs-other\DMB'));
    Results.Add('programs-other.removed=' + BoolText(RemoveEarlierProgramFolder(Root + '\progs-other\DMB', Root + '\elsewhere')));
    Results.Add('programs-custom.earlier=' + EarlierProgramFolderOf(Root + '\progs-custom\Heroes'));
    Results.Add('programs-none.removed=' + BoolText(RemoveEarlierProgramFolder('', Root + '\elsewhere')));
    Results.Add('message.user=' + FmtMessage(CustomMessage('DMBMovingUserFolder'), ['A', 'B']));
    Results.Add('message.moved=' + FmtMessage(CustomMessage('DMBMovedUserFolder'), [Root + '\{#FilesFolderPas}']));
    Results.Add('repair.same=' + BoolText(RepairQuestionNeeded('0.1.0.ed1132427', '0.1.0.ed1132427')));
    Results.Add('repair.older=' + BoolText(RepairQuestionNeeded('0.1.0.2e03bb511', '0.1.0.ed1132427')));
    Results.Add('repair.newer=' + BoolText(RepairQuestionNeeded('0.2.0.aaaaaaaaa', '0.1.0.ed1132427')));
    Results.Add('repair.none=' + BoolText(RepairQuestionNeeded('', '0.1.0.ed1132427')));
    Results.Add('repair.message=' + FmtMessage(CustomMessage('DMBRepairQuestion'), ['0.1.0.ed1132427']));
    SelfTestFirstTime(Root, Results);
    Results.Add('name={#DMBNamePas}');
    Results.SaveToFile(Root + '\results.txt');
  finally
    Results.Free;
  end;
end;
#endif


procedure CopyFolderContents(SourceDir, DestDir: String; Overwrite: Boolean);
var
  FindRec: TFindRec;
  SourceFile, DestFile: String;
begin
  // Ensure the destination directory exists
  if not DirExists(DestDir) then
    if not ForceDirectories(DestDir) then
    begin
      //MsgBox('Failed to create destination directory: ' + DestDir, mbError, MB_OK);
      Exit;
    end;

  // Start file copying
  if FindFirst(SourceDir + '\*.*', FindRec) then
  begin
    try
      repeat
        SourceFile := SourceDir + '\' + FindRec.Name;
        DestFile := DestDir + '\' + FindRec.Name;

        if (FindRec.Attributes and FILE_ATTRIBUTE_DIRECTORY) = 0 then
        begin
          if Overwrite or not FileExists(DestFile) then
          begin
            if not FileCopy(SourceFile, DestFile, False) then
              //MsgBox('Failed to copy file: ' + SourceFile + ' to ' + DestFile, mbError, MB_OK);
          end;
        end
        else if (FindRec.Name <> '.') and (FindRec.Name <> '..') then
        begin
          // Copy subdirectories recursively
          CopyFolderContents(SourceFile, DestFile, Overwrite);
        end;
      until not FindNext(FindRec);
    finally
      FindClose(FindRec);
    end;
  end
  //else
  //  MsgBox('No files found in directory: ' + SourceDir, mbError, MB_OK);
end;


// A huge workaround to get non-admin profile name on elevated installer as admin
function WTSQuerySessionInformation(hServer: THandle; SessionId: Cardinal; WTSInfoClass: Integer; var pBuffer: DWord; var BytesReturned: DWord): Boolean;
  external 'WTSQuerySessionInformationW@wtsapi32.dll stdcall';

procedure WTSFreeMemory(pMemory: DWord);
  external 'WTSFreeMemory@wtsapi32.dll stdcall';

procedure RtlMoveMemoryAsString(Dest: string; Source: DWord; Len: Integer);
  external 'RtlMoveMemory@kernel32.dll stdcall';

const
  WTS_CURRENT_SERVER_HANDLE = 0;
  WTS_CURRENT_SESSION = -1;
  WTSUserName = 5;

function GetCurrentSessionUserName: string;
var
  Buffer: DWord;
  BytesReturned: DWord;
  QueryResult: Boolean;
begin
  // Initialize Result to an empty string
  Result := '';

  // Query the username for the current session
  QueryResult := WTSQuerySessionInformation(
    WTS_CURRENT_SERVER_HANDLE, WTS_CURRENT_SESSION, WTSUserName, Buffer, BytesReturned);

  if not QueryResult then
  begin
    // Error if the query fails
    Exit;
  end;

  try
    // Set the length of the result string (BytesReturned includes null terminator)
    SetLength(Result, (BytesReturned div 2) - 1); // Divide by 2 for Unicode and exclude null terminator

    // Copy the buffer contents into the result string
    RtlMoveMemoryAsString(Result, Buffer, BytesReturned - 2); // Exclude null terminator
  finally
    // Free the allocated memory
    WTSFreeMemory(Buffer);
  end;

end;


function GetBranchSuffix(Param: string): string;
var
  Branch: string;
begin
  Branch := UpperCase(ExpandConstant('{#VCMIFolder}'));

  if Pos('(BRANCH BETA)', Branch) > 0 then
    Result := ' (Beta)'
  else
  if Pos('(BRANCH DEVELOP)', Branch) > 0 then
    Result := ' (Develop)'
  else
    Result := '';
end;


function GetCommonProgramFilesDir: String;
begin
  if IsARM64 then
  begin
    if ExpandConstant('{#InstallerArch}') = 'x86' then
      // For 32-bit installer on ARM64, return the 32-bit Program Files directory
      Result := ExpandConstant('{commonpf32}')
    else
      // For AMR64 installer, return the Program Files directory
      Result := ExpandConstant('{commonpf}')
  end
  else if IsWin64 then
  begin
    if ExpandConstant('{#InstallerArch}') = 'x64' then
      // For 64-bit installer, return the 64-bit Program Files directory
      Result := ExpandConstant('{commonpf64}')
    else
      // For 32-bit installer on 64-bit system, return the 32-bit Program Files directory
      Result := ExpandConstant('{commonpf32}');
  end
  else
    // On 32-bit systems, always return the 32-bit Program Files directory
    Result := ExpandConstant('{commonpf32}');
end;


function GetUserProgramsFolder: String; forward;

function GetDefaultDir(Default: String): String;
begin
  if IsAdmin then
    // Default to Program Files for admins
    Result := GetCommonProgramFilesDir + '\{#DMBNamePas}'
  else
    // DMB: the user's own programs folder for non-admin users
    Result := GetUserProgramsFolder + '\{#DMBNamePas}';
end;


function GetUserFolderPath(Constant: String): String;
var
  FolderPath: String;
  OriginalUserName: String;
  CurrentSessionUserName: String;
begin
  // Retrieve the current username from the session
  CurrentSessionUserName := '\' + GlobalUserName + '\';

  // Retrieve the original username
  OriginalUserName := '\' + GetUserNameString + '\';

  // Expand the specified constant
  FolderPath := ExpandConstant(Constant);

  // Replace the original username with the current session username in the path
  StringChangeEx(FolderPath, OriginalUserName, CurrentSessionUserName, True);

  // Return the modified folder path
  Result := FolderPath;
end;


procedure OnTaskCheck(Sender: TObject);
var
  idx: Integer;
begin
  // Get the index of the currently clicked task
  idx := WizardForm.TasksList.ItemIndex;

  // Check if the clicked task is the "AddFirewallRules" one
  if WizardForm.TasksList.Items[idx] = ExpandConstant('{cm:AddFirewallRules}') then
  begin
    // If it was just unchecked, show the warning
    if not WizardForm.TasksList.Checked[idx] then
    begin
      MsgBox(ExpandConstant('{cm:Warning}') + '!' + #13#10 + #13#10 + ExpandConstant('{cm:InstallForMeOnly1}') + #13#10 + ExpandConstant('{cm:InstallForMeOnly2}'), mbError, MB_OK);
    end;
  end;
end;


// Specific functions for user folders
function GetUserDocsFolder: String;
begin
  Result := GetUserFolderPath('{userdocs}');
end;


function GetUserAppdataFolder: String;
begin
  Result := GetUserFolderPath('{userappdata}');
end;


// DMB: a per-user install goes where Windows keeps per-user programs (AppData\Local\Programs),
// not into the roaming profile (AppData\Roaming), which can be copied between computers
function GetUserProgramsFolder: String;
begin
  Result := GetUserFolderPath('{userpf}');
end;


function GetUserDesktopFolder(Default: String): String;
begin
  Result := GetUserFolderPath('{userdesktop}');
end;


function IsUCRTNeeded: Boolean;
var
  FileName: String;
begin
  Result := True; // Default to copy the file

  FileName := ExtractFileName(ExpandConstant(CurrentFileName));

  // Only check system if the file name contains "api"
  if Pos('API', UpperCase(FileName)) = 1 then
  begin
    // Check existence based on architecture
    if IsWin64 then
    begin
      if ExpandConstant('{#InstallerArch}') = 'x64' then
        // For 64-bit installer on 64-bit OS, check System32
        Result := not FileExists(ExpandConstant('{win}\System32\' + FileName))
      else
        // For 32-bit installer on 64-bit OS, check SysWOW64
        Result := not FileExists(ExpandConstant('{win}\SysWOW64\' + FileName));
    end
    else
      // For 32-bit OS, always check System32
      Result := not FileExists(ExpandConstant('{win}\System32\' + FileName));
  end;
end;


function IsHeroes3Installed(): Boolean;
begin
  Result := False;

  if (Heroes3Path <> '') then
    Result := True;

end;


function IsCopyFilesNeeded(): Boolean;
begin
  // Check if any of the required folders are not valid
  Result := not (IsFolderValid(VCMIDataFolder) and IsFolderValid(VCMIMapsFolder) and IsFolderValid(VCMIMp3Folder));
  
end;


function IsPRInstaller(): Boolean;
begin
  // Skip Tasks page if this is a PR build
  Result := Pos('-PR-', ExpandConstant('{#InstallerName}')) > 0;

end;


function InitializeSetup(): Boolean;
var
  InstallPath: String;
begin
#ifdef DMBSelfTest
  SelfTest(ExpandConstant('{%DMB_SELFTEST|}'));
  Result := False;
  Exit;
#endif
  // DMB: the earlier registry key counts only when it names an install folder of DMB's, since a key of
  // that name could be another program's
  EarlierKeyIsDmb := RegQueryStringValue(HKCU, 'Software\{#EarlierName}', 'InstallPath', InstallPath) and DmbProgramsIn(InstallPath);

  // Check if the application is already installed
  IsUpgrade := RegQueryStringValue(HKCU, 'Software\{#DMBNamePas}', 'InstallPath', InstallPath) or EarlierKeyIsDmb;
#ifdef DMBFakeVcmi
  IsUpgrade := False; // the pages of a first install are what the view build shows
#endif

  // DMB: the same version is already installed: ask before reinstalling it (a silent install, as CI makes, is not asked)
  if IsUpgrade and not WizardSilent and RepairQuestionNeeded(InstalledDisplayVersion(), '{#AppVersion}.{#AppBuild}') then
    if SuppressibleMsgBox(FmtMessage(CustomMessage('DMBRepairQuestion'), ['{#AppVersion}.{#AppBuild}']), mbConfirmation, MB_YESNO, IDYES) = IDNO then
    begin
      Result := False;
      Exit;
    end;

  // Initialize the global variable during setup
  GlobalUserName := GetCurrentSessionUserName();
  GlobalUserDocsFolder := GetUserDocsFolder();
  GlobalUserAppdataFolder := GetUserAppdataFolder();
#ifdef DMBFakeVcmi
  // a build to look at the first-time setup's pages with a pretend VCMI (view_first_time_setup.py), never shipped
  if ExpandConstant('{%DMB_TEST_DOCS|}') <> '' then
    GlobalUserDocsFolder := ExpandConstant('{%DMB_TEST_DOCS|}');
#endif

  // DMB's first-time setup: a VCMI install on this PC is offered for copying or moving; an upgrade and a silent install never are
  SetupChoice := 2;
  RemoveConfirmed := False;
  VcmiName := 'VCMI';
  VcmiUninstallParams := '/SILENT /NORESTART /SUPPRESSMSGBOXES';
  VcmiUninstallWaitSeconds := 300;
  if not IsUpgrade then
    FindVcmiInstall(GlobalUserDocsFolder);

  // Define paths for VCMI: DMB, in the earlier user folder while it waits to be renamed
  EarlierUserFolder := EarlierUserFolderIn(GlobalUserDocsFolder);
  if EarlierUserFolder <> '' then
    SetUserFilesFolder(EarlierUserFolder)
  else
    SetUserFilesFolder(GlobalUserDocsFolder + '\{#FilesFolderPas}');

  // Check for Heroes 3 installation paths
  Heroes3Path := RegistryQueryPath('SOFTWARE\GOG.com\Games\1207658787', 'path');
  if Heroes3Path = '' then
    Heroes3Path := RegistryQueryPath('SOFTWARE\WOW6432Node\GOG.com\Games\1207658787', 'path');
  if Heroes3Path = '' then
    Heroes3Path := RegistryQueryPath('SOFTWARE\New World Computing\Heroes of Might and Magic® III\1.0', 'AppPath');
  if Heroes3Path = '' then
    Heroes3Path := RegistryQueryPath('SOFTWARE\WOW6432Node\New World Computing\Heroes of Might and Magic® III\1.0', 'AppPath');
  if Heroes3Path = '' then
    Heroes3Path := RegistryQueryPath('SOFTWARE\New World Computing\Heroes of Might and Magic III\1.0', 'AppPath');
  if Heroes3Path = '' then
    Heroes3Path := RegistryQueryPath('SOFTWARE\WOW6432Node\New World Computing\Heroes of Might and Magic III\1.0', 'AppPath'); 
  
  if (Heroes3Path <> '') then
  begin
    Heroes3MapsFolder := Heroes3Path + '\Maps';
    Heroes3DataFolder := Heroes3Path + '\Data';
    Heroes3Mp3Folder := Heroes3Path + '\Mp3';
  end;

  Result := True;
end;


function InitializeUninstall(): Boolean;
begin
  // Initialize the global variable during uninstall
  GlobalUserName := GetCurrentSessionUserName();
  GlobalUserDocsFolder := GetUserDocsFolder();
  GlobalUserAppdataFolder := GetUserAppdataFolder();
  
  Result := True;
end;


procedure InitializeWizard();
var
  Moves: String;
begin
  // DMB: an earlier install folder named DMB moves to the new name beside it. The player is told what
  // moves before anything does, and told nothing when nothing moves.
  EarlierProgramFolder := EarlierProgramFolderOf(WizardForm.DirEdit.Text);
  if EarlierProgramFolder <> '' then
    WizardForm.DirEdit.Text := RelocatedProgramFolder(EarlierProgramFolder);
  Moves := '';
  if EarlierUserFolder <> '' then
    Moves := Moves + FmtMessage(CustomMessage('DMBMovingUserFolder'), [EarlierUserFolder, GlobalUserDocsFolder + '\{#FilesFolderPas}']);
  if EarlierProgramFolder <> '' then
    Moves := Moves + FmtMessage(CustomMessage('DMBMovingProgramFolder'), [EarlierProgramFolder, WizardForm.DirEdit.Text]);
  if Moves <> '' then
    CreateOutputMsgPage(wpWelcome, CustomMessage('DMBMovingCaption'), CustomMessage('DMBMovingDescription'),
      FmtMessage(CustomMessage('DMBMovingText'), [Moves]));

  // Check if the application is already installed
  if not IsUpgrade then
  begin
    // Create the install mode selection page only if it's not an upgrade
    InstallModePage := CreateInputOptionPage(
      wpWelcome,
      ExpandConstant('{cm:SelectSetupInstallModeTitle}'),
      ExpandConstant('{cm:SelectSetupInstallModeDesc}'),
      ExpandConstant('{cm:SelectSetupInstallModeSubTitle}'),
      True, False
    );
    
    // Option 0
    InstallModePage.Add(ExpandConstant(#13#10 + '  {cm:InstallForAllUsers}' + #13#10 + '   • {cm:InstallForAllUsers1}' + #13#10 + #13#10));
    // Option 1
    InstallModePage.Add(ExpandConstant(#13#10 + '  {cm:InstallForMeOnly}' + #13#10  +  '   • {cm:InstallForMeOnly1}' + #13#10 + '   • {cm:InstallForMeOnly2}' + #13#10));

    if IsAdmin then
    begin
      // Default to "All Users"
      InstallModePage.SelectedValueIndex := 0;
    end
    else
    begin
      // Default to "Me Only"
      InstallModePage.SelectedValueIndex := 1;

      // Disable the first option ("Install for All Users") for non-admins
      InstallModePage.CheckListBox.ItemEnabled[0] := False;

      // Force a redraw of the CheckListBox to fix appearance
      InstallModePage.CheckListBox.Invalidate();
    end;

    // DMB (K, October 9th): the first-time setup, offered when VCMI or its files were found. The first choice is the harmless one; the
    // removal needs the second page's own answer, and exists only when VCMI is installed (an uninstaller to run).
    if VcmiFound or VcmiDataFound then
    begin
      SetupChoicePage := CreateInputOptionPage(
        InstallModePage.ID,
        'First-time setup',
        'VCMI was found on this PC.',
        VcmiDetailsText(),
        True, False);
      SetupChoicePage.Add('Copy mods and settings from my VCMI install' + #13#10 + '   VCMI stays installed and is not changed. Recommended.');
      if VcmiFound then
        SetupChoicePage.Add('Migrate: copy them, then remove VCMI' + #13#10 + '   Everything is copied and checked first; VCMI is removed with its own uninstaller after one more question.')
      else
        SetupChoicePage.Add('Migrate: copy them, then remove VCMI' + #13#10 + '   Not available: no VCMI install was found to remove.');
      SetupChoicePage.Add('Install without either' + #13#10 + '   The normal installation. Nothing is copied and VCMI is not touched.');
      SetupChoicePage.SelectedValueIndex := 0;
      if not VcmiFound then
        SetupChoicePage.CheckListBox.ItemEnabled[1] := False;

      if VcmiFound then
      begin
        RemoveConfirmPage := CreateInputOptionPage(
          SetupChoicePage.ID,
          'Remove VCMI?',
          'Please confirm what happens to ' + VcmiName + '.',
          'Before anything is removed, Dead Man''s Boots copies the mods, saves, maps, settings and presets from VCMI''s folders and checks every file: ' +
          'its size, and a hash for the settings, the presets and each mod.json. Only when all of them match, it runs the uninstaller of ' + VcmiName +
          ' (' + VcmiUninstallExe + '), and nothing else is deleted by this setup. If anything fails, VCMI stays installed.' + #13#10 + #13#10 +
          'VCMI''s uninstaller opens its own window with an Uninstall button and a "Delete user data" box. Leave that box unticked; ' +
          'your files are already copied, and the box would delete VCMI''s own copies.' + #13#10 + #13#10 +
          'Never touched by this setup: your Heroes III files, Steam and GOG folders, and VCMI''s own user folder.',
          True, False);
        RemoveConfirmPage.Add('No: copy only, and keep VCMI installed' + #13#10 + '   Recommended.');
        RemoveConfirmPage.Add('Yes: after the copy is checked, remove ' + VcmiName + ' with its own uninstaller');
        RemoveConfirmPage.SelectedValueIndex := 0;
      end;
    end;
  end;

    // Attach an OnClick event handler to the tasks list
  WizardForm.TasksList.OnClickCheck := @OnTaskCheck;
  
    // Enable word wrap for the ReadyMemo
  WizardForm.ReadyMemo.ScrollBars := ssNone; // No scrollbars
  WizardForm.ReadyMemo.WordWrap := True;

  // Create a custom label for the footer message
  FooterLabel := TLabel.Create(WizardForm);
  FooterLabel.Parent := WizardForm;
  FooterLabel.Caption := 'Dead Man''s Boots Straker Beta v' + '{#AppVersion}' + '.' + '{#AppBuild}';
  // Padding from the left edge
  FooterLabel.Left := 10;
  // Adjust to leave space for multiple lines
  FooterLabel.Top := WizardForm.ClientHeight - 30;
  // Adjust for padding
  FooterLabel.Width := WizardForm.ClientWidth - 20;
  // Adjust height to accommodate multiple lines
  FooterLabel.Height := 40; 
end;


function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := False; // Default is not to skip the page

  // DMB: the question about removing VCMI is asked only after the second choice
  if Assigned(RemoveConfirmPage) and (PageID = RemoveConfirmPage.ID) then
  begin
    Result := SetupChoice <> 1;
    Exit;
  end;

  // Don't skip Target page if this is a PR build and upgrade
  if IsPRInstaller and IsUpgrade and (PageID = wpSelectDir) then
  begin
    Result := False;
    Exit;
  end;

  // Skip Tasks page if this is a PR build
  if IsPRInstaller and (PageID = wpSelectTasks) then
  begin
    Result := True;
    Exit;
  end;

  if IsUpgrade then
  begin
    if (PageID = wpLicense) or (PageID = wpSelectTasks) or (PageID = wpReady) then
    begin
      Result := True; // Skip these pages during upgrade
      Exit;
    end;
  end;
end;


procedure CurPageChanged(CurPageID: Integer);
begin
  // Ensure the footer message is visible on every page
  FooterLabel.Visible := True;

  // DMB's first-time setup: what was copied and what was left alone
  if (CurPageID = wpFinished) and (SetupSummary <> '') then
  begin
    WizardForm.FinishedLabel.Caption := WizardForm.FinishedLabel.Caption + #13#10#13#10 + SetupSummary;
    WizardForm.FinishedLabel.AdjustHeight;
  end;

  // DMB: what became of the earlier user folder, when there was one to move
  if (CurPageID = wpFinished) and (UserFolderMoved or UserFolderNotMoved) then
  begin
    if UserFolderMoved then
      WizardForm.FinishedLabel.Caption := WizardForm.FinishedLabel.Caption + #13#10#13#10 +
        FmtMessage(CustomMessage('DMBMovedUserFolder'), [GlobalUserDocsFolder + '\{#FilesFolderPas}'])
    else
      WizardForm.FinishedLabel.Caption := WizardForm.FinishedLabel.Caption + #13#10#13#10 +
        FmtMessage(CustomMessage('DMBNotMovedUserFolder'), [EarlierUserFolder, GlobalUserDocsFolder + '\{#FilesFolderPas}']);
    WizardForm.FinishedLabel.AdjustHeight;
  end;
end;


procedure CurStepChanged(CurStep: TSetupStep);
begin
#ifdef DMBFakeVcmi
  if CurStep = ssInstall then
    Abort; // the view build never installs, whatever page was reached
#endif
  // DMB: the earlier user folder takes the new name before any file lands in it (RunPreInstallTasks
  // copies Heroes III's files after this step)
  if (CurStep = ssInstall) and (EarlierUserFolder <> '') then
  begin
    UserFolderMoved := MoveEarlierUserFolder(GlobalUserDocsFolder);
    UserFolderNotMoved := not UserFolderMoved;
    if UserFolderMoved then
      SetUserFilesFolder(GlobalUserDocsFolder + '\{#FilesFolderPas}');
    Log('DMB: renaming ' + EarlierUserFolder + ': ' + IntToStr(Ord(UserFolderMoved)));
  end;

  // DMB: once the new install is in place, the earlier install folder and registry key go
  if CurStep = ssPostInstall then
  begin
    // the first-time setup (K, October 9th): copy from VCMI, and remove it when that was asked for and the copy checked out
    if (SetupChoice < 2) and (VcmiFound or VcmiDataFound) then
    begin
      WizardForm.StatusLabel.Caption := 'Copying from VCMI and checking every file...';
      SetupSummary := FirstTimeSetup(GlobalUserDocsFolder + '\{#FilesFolderPas}');
    end;
    if EarlierProgramFolder <> '' then
      Log('DMB: deleting ' + EarlierProgramFolder + ': ' + IntToStr(Ord(RemoveEarlierProgramFolder(EarlierProgramFolder, ExpandConstant('{app}')))));
    if EarlierKeyIsDmb then
      RegDeleteKeyIncludingSubkeys(HKCU, 'Software\{#EarlierName}');
  end;
end;


function NextButtonClick(CurPageID: Integer): Boolean;
begin
#ifdef DMBFakeVcmi
  // the build for looking at the pages (view_first_time_setup.py) can never install: it stops at the destination page, which every
  // path to Ready and Install passes
  if (CurPageID = wpSelectDir) or (CurPageID = wpSelectTasks) or (CurPageID = wpReady) then
  begin
    Result := False;
    Exit;
  end;
#endif
  // DMB's first-time setup: what the player chose
  if Assigned(SetupChoicePage) and (CurPageID = SetupChoicePage.ID) then
    SetupChoice := SetupChoicePage.SelectedValueIndex;
  if Assigned(RemoveConfirmPage) and (CurPageID = RemoveConfirmPage.ID) then
    RemoveConfirmed := RemoveConfirmPage.SelectedValueIndex = 1;

  // Skip the custom page on upgrade
  if IsUpgrade and Assigned(InstallModePage) and (CurPageID = InstallModePage.ID) then
  begin
    Result := True;
    Exit;
  end;

  // Handle logic for the custom page if it exists
  if Assigned(InstallModePage) and (CurPageID = InstallModePage.ID) then
  begin
    if (InstallModePage.SelectedValueIndex = 0) and not IsAdmin then
    begin
      Result := False;
      Exit;
    end;

    if InstallModePage.SelectedValueIndex = 0 then
      WizardForm.DirEdit.Text := GetCommonProgramFilesDir + '\{#DMBNamePas}'
    else
      WizardForm.DirEdit.Text := GetUserProgramsFolder + '\{#DMBNamePas}';
  end;

  Result := True;
end;

/// DMB: the Ready page names the first-time setup's choice when there is one
function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo, MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
begin
  Result := '';
  if MemoUserInfoInfo <> '' then
    Result := Result + MemoUserInfoInfo + NewLine + NewLine;
  if MemoDirInfo <> '' then
    Result := Result + MemoDirInfo + NewLine + NewLine;
  if MemoTypeInfo <> '' then
    Result := Result + MemoTypeInfo + NewLine + NewLine;
  if MemoComponentsInfo <> '' then
    Result := Result + MemoComponentsInfo + NewLine + NewLine;
  if MemoGroupInfo <> '' then
    Result := Result + MemoGroupInfo + NewLine + NewLine;
  if MemoTasksInfo <> '' then
    Result := Result + MemoTasksInfo + NewLine + NewLine;
  if (VcmiFound or VcmiDataFound) and (SetupChoice = 0) then
    Result := Result + 'First-time setup:' + NewLine + Space + 'Copy mods and settings from ' + VcmiName + '; VCMI stays installed.'
  else if VcmiFound and (SetupChoice = 1) and RemoveConfirmed then
    Result := Result + 'First-time setup:' + NewLine + Space + 'Copy mods and settings from ' + VcmiName + ', check them, then remove ' + VcmiName + ' with its own uninstaller.'
  else if (VcmiFound or VcmiDataFound) and (SetupChoice = 1) then
    Result := Result + 'First-time setup:' + NewLine + Space + 'Copy mods and settings from ' + VcmiName + '; VCMI stays installed.';
  Result := Trim(Result);
end;


function MapEditorInstalled(): Boolean;
begin
  // DMB's builds may leave the map editor out; its shortcut appears only when it was installed
  Result := FileExists(ExpandConstant('{app}\VCMI_mapeditor.exe'));
end;


procedure PerformHeroes3FileCopy();
var
  i: Integer;
begin
  // Loop through all tasks to find the "h3copyfiles" task
  for i := 0 to WizardForm.TasksList.Items.Count - 1 do
  begin
    // Check if the current task is "h3copyfiles"
    if WizardForm.TasksList.Items[i] = ExpandConstant('{cm:CopyH3Files}') then
    begin
      // Check if the "h3copyfiles" task is checked
      if WizardForm.TasksList.Checked[i] then
      begin
        
        if IsCopyFilesNeeded then
        begin
          // Copy folders if conditions are met
          if (IsFolderValid(Heroes3MapsFolder) and not IsFolderValid(VCMIMapsFolder)) then
            CopyFolderContents(Heroes3MapsFolder, VCMIMapsFolder, True);

          if (IsFolderValid(Heroes3DataFolder) and not IsFolderValid(VCMIDataFolder)) then
            CopyFolderContents(Heroes3DataFolder, VCMIDataFolder, True);

          if (IsFolderValid(Heroes3Mp3Folder) and not IsFolderValid(VCMIMp3Folder)) then
            CopyFolderContents(Heroes3Mp3Folder, VCMIMp3Folder, True);
        end;
      end;
      Exit; // Task found, exit the loop
    end;
  end;
end;


procedure CreateDefaultSettingsFile();
var
  ConfigDir, SettingsFile, Language, JSONContent: String;
begin
  ConfigDir := GlobalUserDocsFolder + '\' + '{#FilesFolderPas}' + '\config';
  SettingsFile := ConfigDir + '\settings.json';

  if not FileExists(SettingsFile) then
  begin
    Language := ActiveLanguage;
    if Language = '' then
      Language := 'english';

      JSONContent :=
        '{' + #13#10 +
        Chr(9) + '"general" : {' + #13#10 +
        Chr(9) + Chr(9) + '"language" : "' + Language + '"' + #13#10 +
        Chr(9) + '}' + #13#10 +
        '}';

    if not DirExists(ConfigDir) then
      ForceDirectories(ConfigDir);

    SaveStringToFile(SettingsFile, JSONContent, False);
  end;
end;


procedure RunPreInstallTasks();
begin
  // DMB: never runs another installer's uninstaller. Stock VCMI's installer removes a legacy VCMI
  // install silently here; DMB installs beside whatever VCMI the player has.
  // Copy H3 files when needed
  PerformHeroes3FileCopy();
  // Create default language JSON - for future use
  // CreateDefaultSettingsFile();
end;


/// Uninstall ///////////////////////////////////////////////////////////////////////////////////////////////////////////////


var
  DeleteUserDataCheckbox: TNewCheckBox;
  DeleteUserDataLabel: TLabel;


function DeleteFolderContents(const FolderPath: String): Boolean;
var
  FindResult: TFindRec;
  SubPath: String;
begin
  Result := True;

  if FindFirst(FolderPath + '\*', FindResult) then
  begin
    try
      repeat
        if (FindResult.Name <> '.') and (FindResult.Name <> '..') then
        begin
          SubPath := FolderPath + '\' + FindResult.Name;

          if (FindResult.Attributes and FILE_ATTRIBUTE_DIRECTORY) <> 0 then
          begin
            if not DeleteFolderContents(SubPath) then
            begin
              Result := False;
              Exit;
            end;
            if not RemoveDir(SubPath) then
            begin
              Result := False;
              Exit;
            end;
          end
          else
          begin
            if not DeleteFile(SubPath) then
            begin
              Result := False;
              Exit;
            end;
          end;
        end;
      until not FindNext(FindResult);
    finally
      FindClose(FindResult);
    end;
  end;
end;


procedure DeleteUserFolder(const UserDataFolder: String);
begin
  if DirExists(UserDataFolder) then
  begin
    if DeleteFolderContents(UserDataFolder) then
    begin
      if not RemoveDir(UserDataFolder) then
      begin
        // Log or handle failed root directory removal if necessary
      end;
    end;
  end;
end;


procedure PerformFileDeletion;
begin
  if (DeleteUserDataCheckbox <> nil) and DeleteUserDataCheckbox.Checked then
  begin
    DeleteUserFolder(GlobalUserDocsFolder + '\' + '{#FilesFolderPas}');
    // DMB: the earlier user folder too, when it is still there (its rename failed) and DMB made it
    if UserFolderMadeByDmb(GlobalUserDocsFolder + '\' + '{#EarlierFilesFolder}') then
      DeleteUserFolder(GlobalUserDocsFolder + '\' + '{#EarlierFilesFolder}');
  end;
end;


procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
    PerformFileDeletion;
  // Repeat delete process after uninstall due logs from killed processes during uninstall
  if CurUninstallStep = usPostUninstall then
    PerformFileDeletion;
end;


procedure UninsNextButtonOnClick(Sender: TObject);
begin
  with UninstallProgressForm.InnerNotebook do
  begin
    ActivePage := Pages[ActivePage.PageIndex + 1];
    if ActivePage.PageIndex = PageCount - 1 then
    begin
      TButton(Sender).Hide;
      UninstallProgressForm.Close;
    end;
  end;
end;


procedure UninsCancelButtonOnClick(Sender: TObject);
begin
  // Optionally handle user cancellation
end;


procedure InitializeUninstallProgressForm();
var
  Page: TNewNotebookPage;
  UninsNextButton: TButton;
begin
  with UninstallProgressForm do
  begin
    // -- Create the "Uninstall" button
    UninsNextButton := TButton.Create(UninstallProgressForm);
    with UninsNextButton do
    begin
      Parent := UninstallProgressForm;
      Top := CancelButton.Top;
      Width := CancelButton.Width;
      Height := CancelButton.Height;
      Left := CancelButton.Left - Width - ScaleX(10);
      Caption := ExpandConstant('{cm:Uninstall}');
      OnClick := @UninsNextButtonOnClick;
      TabOrder := 1; // Ensure this button is first in the tab order
      Default := True; // Make it the default button (triggered by Enter key)
    end;

    // -- Configure the Cancel button so it aborts the form
    CancelButton.Enabled := True;
    CancelButton.ModalResult := mrAbort; 
    CancelButton.OnClick := @UninsCancelButtonOnClick;

    // -- Create a custom page (as the first page in the notebook)
    Page := TNewNotebookPage.Create(InnerNotebook);
    with Page do
    begin
      Parent := InnerNotebook;
      Notebook := InnerNotebook;
      PageIndex := 0; // first page
    end;

    // -- Create our "Delete user data" checkbox on that custom page
    DeleteUserDataCheckbox := TNewCheckBox.Create(UninstallProgressForm);
    with DeleteUserDataCheckbox do
    begin
      Parent := Page;
      Top := ScaleX(20);
      Left := ScaleX(20);
      Width := ScaleX(400);
      Checked := False;
      Caption := ExpandConstant('{cm:DeleteUserData}');
      TabOrder := 0; // Tab focus goes to this control after the Uninstall button
    end;

    // -- Add a label for the additional text
    DeleteUserDataLabel := TLabel.Create(UninstallProgressForm);
    with DeleteUserDataLabel do
    begin
      Parent := Page;
      Top := DeleteUserDataCheckbox.Top + ScaleY(20); // Position below the checkbox
      Left := DeleteUserDataCheckbox.Left + ScaleX(20); // Indent slightly to align with the text
      Width := ScaleX(400);
      Caption := GlobalUserDocsFolder + '\' + '{#FilesFolderPas}';
      if UserFolderMadeByDmb(GlobalUserDocsFolder + '\' + '{#EarlierFilesFolder}') then
        Caption := Caption + #13#10 + GlobalUserDocsFolder + '\' + '{#EarlierFilesFolder}';
    end;

    // -- Activate the first page
    InnerNotebook.ActivePage := Page;

    // -- Make InstallingPage the last page
    InstallingPage.PageIndex := InnerNotebook.PageCount - 1;

    // -- Show the form modally; if user clicks Cancel, ShowModal = mrAbort -> Abort uninstallation
    if ShowModal = mrAbort then
      Abort;
  end;
end;
