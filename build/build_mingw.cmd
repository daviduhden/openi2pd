@echo off
setlocal enableextensions enabledelayedexpansion
title Building i2pd

REM Copyright (c) 2013-2022, The PurpleI2P Project
REM Copyright (c) 2026, David Uhden Collado
REM SPDX-License-Identifier: BSD-3-Clause
REM This file is part of openi2pd and licensed under BSD-3-Clause
REM See full license text in LICENSE file at top of project tree

REM To use that script, you must have installed in your MSYS installation these packages:
REM Base: git cmake ninja zip
REM UCRT64: mingw-w64-ucrt-x86_64-boost mingw-w64-ucrt-x86_64-openssl mingw-w64-ucrt-x86_64-gcc
REM MINGW32: mingw-w64-i686-boost mingw-w64-i686-openssl mingw-w64-i686-gcc

REM setting up variables for MSYS
REM Set MSYS2_ROOT when MSYS2 is not on PATH; no hard-coded installation
REM directory is assumed by this script.
set MSYS2_PATH_TYPE=inherit
set CHERE_INVOKING=enabled_from_arguments
set MSYSTEM=MINGW32

if defined MSYS2_ROOT (
  set "WD=%MSYS2_ROOT%\usr\bin\"
) else (
  set "WD="
  for %%I in (bash.exe) do set "WD=%%~dpI"
)
if not defined WD (
  echo Error: bash.exe not found. Set MSYS2_ROOT to the MSYS2 installation root.
  exit /b 1
)
set "xSH=%WD%bash -lc"

set "BASE_FILES=README.txt contrib/i2pd.conf contrib/tunnels.conf contrib/certificates contrib/tunnels.d"

REM detecting number of processors
set /a threads=%NUMBER_OF_PROCESSORS%

REM we must work in root of repo
cd ..

REM deleting old log files
del /S build_*.log >> nul 2>&1

echo Cleaning the previous build directory...
%xSH% "cmake -E remove_directory build-output" > build\build.log 2>&1

REM set to variable current commit hash
for /F "usebackq" %%a in (`%xSH% "git describe --tags"`) DO (
 set tag=%%a
)

REM set to variable latest released tag
for /F "usebackq" %%b in (`%xSH% "git describe --abbrev=0"`) DO (
 set reltag=%%b
)

echo Preparing configuration files and README for packaging...

%xSH% "echo To use configs and certificates, move all files and certificates folder from contrib directory here. > README.txt" >> nul

REM converting configuration files to DOS format (make usable in Windows Notepad)
%xSH% "unix2dos contrib/i2pd.conf contrib/tunnels.conf contrib/tunnels.d/*" >> build\build.log 2>&1

REM Prepare binary signing command if signing key and password provided
if defined SIGN (
  echo Signing enabled

  for %%X in (signtool.exe) do (set xSIGNTOOL=%%~$PATH:X)
  if not defined xSIGNTOOL (
    if not defined SIGNTOOL (
      echo Error: Can't find signtool. Please provide path to binary using SIGNTOOL variable.
      exit /b 1
    ) else (
      set "xSIGNTOOL=%SIGNTOOL%"
    )
  )

  if defined SIGNKEY (
    set "xSIGNKEYOPTS=/f ^"%SIGNKEY%^""
  )

  if defined SIGNPASS (
    set "xSIGNPASSOPTS=/p ^"%SIGNPASS%^""
  )

  set "xSIGNOPTS=sign /tr http://timestamp.digicert.com /td sha256 /fd sha256 %xSIGNKEYOPTS% %xSIGNPASSOPTS%"
)

REM starting building
set MSYSTEM=MINGW32
set bitness=32
call :BUILDING

set MSYSTEM=UCRT64
set bitness=64
call :BUILDING

echo.

REM compile installer
echo Building installer...
set "ISCC="
if exist "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not defined ISCC if exist "%ProgramFiles%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if not defined ISCC for %%I in (ISCC.exe) do set "ISCC=%%~$PATH:I"
if not defined ISCC (
  echo Error: Inno Setup ISCC.exe not found. Set ISCC to its full path.
  exit /b 1
)
"%ISCC%" /dI2Pd_TextVer="%tag%" /dI2Pd_Ver="%reltag%.0" build\win_installer.iss >> build\build.log 2>&1

REM Sign binary
if defined xSIGNOPTS (
  "%xSIGNTOOL%" %xSIGNOPTS% build\setup_open-i2pd_v%tag%.exe
)

del README.txt i2pd.exe i2pd-tui.exe i2pd_x32.exe i2pd_x64.exe i2pd-tui_x32.exe i2pd-tui_x64.exe >> nul
rmdir /S /Q tools_x32 >> nul 2>&1
rmdir /S /Q tools_x64 >> nul 2>&1

echo Build complete...
pause
exit /b 0

:BUILDING
%xSH% "cmake -S build -B build-output -G Ninja -DCMAKE_BUILD_TYPE=Release -DWITH_UPNP=ON" > build\build_win%bitness%_%tag%.log 2>&1
echo Building i2pd %tag% for win%bitness%...
%xSH% "cmake --build build-output --parallel %threads% --target all" >> build\build_win%bitness%_%tag%.log 2>&1

REM Sign binary
if defined xSIGNOPTS (
  "%xSIGNTOOL%" %xSIGNOPTS% build-output\i2pd.exe
)

REM Copy binary for installer and create distribution archive
%xSH% "cp build-output/i2pd.exe i2pd.exe && cp build-output/i2pd-tui.exe i2pd-tui.exe && mkdir -p tools_x%bitness% && cp build-output/tools/*.exe tools_x%bitness%/ && cp i2pd.exe i2pd_x%bitness%.exe && cp i2pd-tui.exe i2pd-tui_x%bitness%.exe && zip -r9 build/i2pd_%tag%_win%bitness%_mingw.zip i2pd.exe i2pd-tui.exe tools_x%bitness% %BASE_FILES%" >> build\build_win%bitness%_%tag%.log 2>&1

REM Clean work directory
%xSH% "cmake -E remove_directory build-output" >> build\build_win%bitness%_%tag%.log 2>&1
goto EOF

:EOF
