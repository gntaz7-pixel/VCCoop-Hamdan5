@echo off
setlocal EnableExtensions
cd /d "%~dp0"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo ERROR: Visual Studio Build Tools 2022 not found.
  exit /b 1
)
for /f "usebackq tokens=* delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%I"
if not defined VSINSTALL (
  echo ERROR: Desktop development with C++ is not installed.
  exit /b 1
)
call "%VSINSTALL%\VC\Auxiliary\Build\vcvarsall.bat" x86
if errorlevel 1 exit /b 1
if not exist out mkdir out
cl /nologo /W4 /O2 /EHsc /LD /DWIN32 /D_WINDOWS "bridge\dinput8_proxy.cpp" /link /NOLOGO /MACHINE:X86 /DEF:"bridge\dinput8.def" /OUT:"out\dinput8.dll" /IMPLIB:"out\dinput8.lib" user32.lib
if errorlevel 1 exit /b 1
if not exist "out\dinput8.dll" (
  echo ERROR: DLL was not produced.
  exit /b 1
)
echo SUCCESS: out\dinput8.dll
