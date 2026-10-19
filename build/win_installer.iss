; Copyright (c) 2013-2026, The PurpleI2P Project
; Copyright (c) 2026, David Uhden Collado
; SPDX-License-Identifier: BSD-3-Clause
#define I2Pd_AppName "openi2pd"
#define I2Pd_Publisher "openi2pd contributors"

[Setup]
AppName={#I2Pd_AppName}
AppVersion={#I2Pd_TextVer}
AppPublisher={#I2Pd_Publisher}

; DefaultDirName is retained for upgrade compatibility; the visible product name
; and all new artwork use openi2pd.
DefaultDirName={pf}\Open I2Pd
DefaultGroupName=openi2pd
UninstallDisplayIcon={app}\I2Pd.exe
OutputDir=.
OutputBaseFilename=setup_open-i2pd_v{#I2Pd_TextVer}

LicenseFile=..\LICENSE
SetupIconFile=..\Win32\openi2pd.ico

InternalCompressLevel=ultra64
Compression=lzma/ultra64
SolidCompression=true

ArchitecturesInstallIn64BitMode=x64
ExtraDiskSpaceRequired=15

AppID={{09F80FCB-ED87-4A5E-8539-16C38DFEC7F8}
AppVerName={#I2Pd_AppName}
AppCopyright=Copyright (c) 2013-2026, The PurpleI2P Project; Copyright (c) 2026, David Uhden Collado
AppPublisherURL=https://github.com/daviduhden/openi2pd
AppSupportURL=https://github.com/daviduhden/openi2pd/issues
AppUpdatesURL=https://github.com/daviduhden/openi2pd/releases

VersionInfoProductVersion={#I2Pd_Ver}
VersionInfoVersion={#I2Pd_Ver}

CloseApplications=yes

[Files]
Source: ..\i2pd_x32.exe; DestDir: {app}; DestName: i2pd.exe; Flags: ignoreversion; Check: not IsWin64; MinVersion: 10.0
Source: ..\i2pd_x64.exe; DestDir: {app}; DestName: i2pd.exe; Flags: ignoreversion; Check: IsWin64; MinVersion: 10.0
Source: ..\i2pd-tui_x32.exe; DestDir: {app}; DestName: i2pd-tui.exe; Flags: ignoreversion; Check: not IsWin64; MinVersion: 10.0
Source: ..\i2pd-tui_x64.exe; DestDir: {app}; DestName: i2pd-tui.exe; Flags: ignoreversion; Check: IsWin64; MinVersion: 10.0
Source: ..\tools_x32\*.exe; DestDir: {app}; Flags: ignoreversion; Check: not IsWin64; MinVersion: 10.0
Source: ..\tools_x64\*.exe; DestDir: {app}; Flags: ignoreversion; Check: IsWin64; MinVersion: 10.0
Source: ..\README.md; DestDir: {app}; DestName: Readme.txt; Flags: onlyifdoesntexist
Source: ..\contrib\i2pd.conf; DestDir: {userappdata}\i2pd; Flags: onlyifdoesntexist
Source: ..\contrib\subscriptions.txt; DestDir: {userappdata}\i2pd; Flags: onlyifdoesntexist
Source: ..\contrib\tunnels.conf; DestDir: {userappdata}\i2pd; Flags: onlyifdoesntexist
Source: ..\contrib\certificates\*; DestDir: {userappdata}\i2pd\certificates; Flags: onlyifdoesntexist recursesubdirs createallsubdirs
Source: ..\contrib\tunnels.d\*; DestDir: {userappdata}\i2pd\tunnels.d; Flags: onlyifdoesntexist recursesubdirs createallsubdirs

[Icons]
Name: {group}\openi2pd; Filename: {app}\i2pd.exe
Name: {group}\Readme; Filename: {app}\Readme.txt

[UninstallDelete]
Type: filesandordirs; Name: {app}
