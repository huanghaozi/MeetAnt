@echo off
echo ========================================
echo MeetAnt vcpkg Setup Script for Windows
echo ========================================

REM Check if vcpkg is already installed
if exist "%VCPKG_ROOT%\vcpkg.exe" (
    echo vcpkg is already installed at %VCPKG_ROOT%
    goto :install_deps
)

REM Check common vcpkg locations
if exist "C:\vcpkg\vcpkg.exe" (
    set VCPKG_ROOT=C:\vcpkg
    echo Found vcpkg at C:\vcpkg
    goto :install_deps
)

if exist "C:\tools\vcpkg\vcpkg.exe" (
    set VCPKG_ROOT=C:\tools\vcpkg
    echo Found vcpkg at C:\tools\vcpkg
    goto :install_deps
)

REM vcpkg not found, clone and install
echo vcpkg not found. Installing vcpkg...
cd /d %USERPROFILE%
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
call bootstrap-vcpkg.bat
set VCPKG_ROOT=%cd%

echo.
echo Please add the following to your system environment variables:
echo VCPKG_ROOT=%VCPKG_ROOT%
echo.

:install_deps
echo.
echo Installing dependencies...
cd /d "%VCPKG_ROOT%"

echo Installing wxWidgets...
vcpkg install wxwidgets:x64-windows

echo Installing PortAudio...
vcpkg install portaudio:x64-windows

echo Installing CURL...
vcpkg install curl:x64-windows

echo.
echo ========================================
echo Setup complete!
echo ========================================
echo.
echo To build the project, run:
echo   mkdir build
echo   cd build
echo   cmake .. --preset windows-x64-release
echo   cmake --build . --config Release
echo.
pause 