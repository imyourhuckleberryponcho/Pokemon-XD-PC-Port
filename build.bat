@echo off
rem Builds the port.  Usage:  build.bat [debug|release|debug-x86|release-x86] [extra cmake --build args]
rem (default: debug; the -x86 presets are the 32-bit build). Output goes to build\<preset>\bin.
setlocal
set PRESET=%~1
if "%PRESET%"=="" set PRESET=debug

rem Load Visual Studio's compiler into this window: 64-bit, or 32-bit for the -x86 presets.
set VCVARS=vcvars64.bat
if /i "%PRESET:~-4%"=="-x86" set VCVARS=vcvarsamd64_x86.bat
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSDIR=%%i
if not defined VSDIR (
  echo Visual Studio with C++ tools was not found.
  exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\%VCVARS%" >nul 2>&1 || exit /b 1

rem Use the CMake and Ninja that come with Visual Studio, so a separately
rem installed (or broken) CMake on PATH can't get in the way.
set VSCMAKE=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake
set PATH=%VSCMAKE%\CMake\bin;%VSCMAKE%\Ninja;%PATH%

rem Rust: builds the disc reader (nod) from source for 32-bit builds.
if exist "%USERPROFILE%\.cargo\bin" set PATH=%USERPROFILE%\.cargo\bin;%PATH%

cd /d "%~dp0"
cmake --preset %PRESET% || exit /b 1
cmake --build --preset %PRESET% %2 %3 %4 %5 %6 %7 %8 %9 || exit /b 1
echo.
echo Build finished: build\%PRESET%\bin
