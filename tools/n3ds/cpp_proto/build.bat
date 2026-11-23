@echo off
rem Builds pose_sample.dll (two variants: plain float arithmetic, and fused multiply-add where numpy's BLAS may use it) with Visual Studio's compiler.
setlocal
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSDIR=%%i
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 || exit /b 1
cd /d %~dp0
set OUT=%1
if "%OUT%"=="" set OUT=.
cl /nologo /O2 /EHsc /fp:precise /LD /DUSE_FMA=0 pose_sample.cpp /Fo%OUT%\ /Fe%OUT%\pose_sample_plain.dll || exit /b 1
cl /nologo /O2 /EHsc /fp:precise /LD /DUSE_FMA=1 pose_sample.cpp /Fo%OUT%\ /Fe%OUT%\pose_sample_fma.dll || exit /b 1
