@echo off
rem Configure and build a Windows preset from any shell: finds Visual Studio, loads
rem its x86 environment, and puts the bundled clang-cl, CMake and Ninja on PATH.
rem
rem   port\tools\win-build.cmd [preset] [extra cmake --build args...]
rem   (default preset: win-x86-clangcl)
setlocal
set "ROOT=%~dp0..\.."
set "PRESET=%~1"
if "%PRESET%"=="" set "PRESET=win-x86-clangcl"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`call "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo Visual Studio with the C++ x86/x64 tools was not found.
    exit /b 1
)
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" amd64_x86 >nul || exit /b 1
set "PATH=%VSDIR%\VC\Tools\Llvm\bin;%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"

cd /d "%ROOT%"
cmake --preset %PRESET% || exit /b 1
shift
cmake --build --preset %PRESET% %1 %2 %3 %4 %5 %6 || exit /b 1
