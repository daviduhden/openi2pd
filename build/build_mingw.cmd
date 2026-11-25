@REM See the LICENSE file at the top of the project tree for copyright
@REM and license details.
@REM
@REM openi2pd: build mingw.cmd.
@REM
@REM Configures build and manual CI integration.
@REM

@echo off
setlocal enableextensions disabledelayedexpansion
title Building i2pd


REM Install the required packages in the matching MSYS2 environments:
REM Base: git cmake ninja zip
REM HTML Help build tools: Perl, mandoc, Microsoft hhc.exe and 7-Zip.
REM Put custom tool locations on PATH before running this helper.
REM UCRT64:
REM   mingw-w64-ucrt-x86_64-boost
REM   mingw-w64-ucrt-x86_64-openssl
REM   mingw-w64-ucrt-x86_64-gcc
REM   mingw-w64-ucrt-x86_64-miniupnpc
REM MINGW32:
REM   mingw-w64-i686-boost mingw-w64-i686-openssl
REM   mingw-w64-i686-gcc mingw-w64-i686-miniupnpc

REM Set MSYS2_ROOT when bash.exe is not available through PATH.
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

set "BASE_FILES=README.txt contrib/i2pd.conf contrib/tunnels.conf contrib/certificates contrib/tunnels.d tools/LICENSE"

REM detecting number of processors
set /a threads=%NUMBER_OF_PROCESSORS%

REM Work in the repository root regardless of the caller's directory.
pushd "%~dp0.." >nul || exit /b 1
if not exist "build\CMakeLists.txt" (
  echo This script must live inside the openi2pd build directory. >&2
  exit /b 1
)

REM deleting old log files
del /S build_*.log >> nul 2>&1

echo Cleaning the previous build directory...
%xSH% "cmake -E remove_directory build-output" > build\build.log 2>&1
if errorlevel 1 exit /b 1

REM Build once for both architectures. Keep the verified CHM separate
REM from the native directories that are removed after each ZIP.
echo Building the Windows manual...
set MSYSTEM=UCRT64
%xSH% "cmake -S build/windows-manual -B build-output/manual-build -G Ninja" >> build\build.log 2>&1
if errorlevel 1 (
  echo Error: Windows manual configuration failed. See build\build.log. >&2
  exit /b 1
)
%xSH% "cmake --build build-output/manual-build --target windows-manual" >> build\build.log 2>&1
if errorlevel 1 (
  echo Error: Windows manual generation failed. See build\build.log. >&2
  exit /b 1
)
set "manual=%CD%\build-output\manual-build\windows-manual\openi2pd.chm"
if not exist "%manual%" (
  echo Error: Windows manual missing from the build output. >&2
  exit /b 1
)

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

REM Configuration files are distributed with native LF line endings.
REM Modern Windows editors handle LF and the parser is line based, so do
REM not rewrite tracked sources in place merely to normalise Notepad
REM input.

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
if errorlevel 1 exit /b 1

set MSYSTEM=UCRT64
set bitness=64
call :BUILDING
if errorlevel 1 exit /b 1

echo.

REM compile installer
echo Building installer...
if not defined ISCC if exist "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not defined ISCC if exist "%ProgramFiles%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if not defined ISCC for %%I in (ISCC.exe) do set "ISCC=%%~$PATH:I"
if not defined ISCC (
  echo Error: Inno Setup ISCC.exe not found. Set ISCC to its full path.
  exit /b 1
)
"%ISCC%" /dI2Pd_TextVer="%tag%" /dI2Pd_Ver="%reltag%.0" /dI2Pd_Manual="%manual%" build\win_installer.iss >> build\build.log 2>&1
if errorlevel 1 (
  echo Error: Inno Setup compilation failed. See build\build.log. >&2
  exit /b 1
)

REM Sign binary
if defined xSIGNOPTS (
  "%xSIGNTOOL%" %xSIGNOPTS% build\setup_open-i2pd_v%tag%.exe
)

del README.txt i2pd.exe i2pd-tui.exe i2pd_x32.exe i2pd_x64.exe i2pd-tui_x32.exe i2pd-tui_x64.exe >> nul
rmdir /S /Q tools_x32 >> nul 2>&1
rmdir /S /Q tools_x64 >> nul 2>&1

popd
echo Build complete...
pause
exit /b 0

:BUILDING
%xSH% "cmake -S build -B build-output/native -G Ninja -DCMAKE_BUILD_TYPE=Release -DWITH_UPNP=ON" > build\build_win%bitness%_%tag%.log 2>&1
if errorlevel 1 exit /b 1
echo Building i2pd %tag% for win%bitness%...
%xSH% "cmake --build build-output/native --parallel %threads% --target all" >> build\build_win%bitness%_%tag%.log 2>&1
if errorlevel 1 exit /b 1

REM Sign binary
if defined xSIGNOPTS (
  "%xSIGNTOOL%" %xSIGNOPTS% build-output\native\i2pd.exe
)

REM Copy binary for installer and create distribution archive
%xSH% "cp build-output/native/i2pd.exe i2pd.exe && cp build-output/native/i2pd-tui.exe i2pd-tui.exe && mkdir -p tools_x%bitness% && cp build-output/native/tools/*.exe tools_x%bitness%/ && cp i2pd.exe i2pd_x%bitness%.exe && cp i2pd-tui.exe i2pd-tui_x%bitness%.exe && zip -r9 build-output/native/windows.zip i2pd.exe i2pd-tui.exe tools_x%bitness% %BASE_FILES% && zip -j9 build-output/native/windows.zip build-output/manual-build/windows-manual/openi2pd.chm && mv -f build-output/native/windows.zip build/i2pd_%tag%_win%bitness%_mingw.zip" >> build\build_win%bitness%_%tag%.log 2>&1
if errorlevel 1 (
  echo Error: Windows ZIP assembly failed. See build\build_win%bitness%_%tag%.log. >&2
  exit /b 1
)

REM Clean work directory
%xSH% "cmake -E remove_directory build-output/native" >> build\build_win%bitness%_%tag%.log 2>&1
exit /b %errorlevel%
