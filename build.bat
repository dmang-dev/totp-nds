@echo off
rem totp-nds Windows build wrapper. Hands off to devkitPro's MSYS2 bash
rem so PowerShell doesn't route `make` through WSL. devkitARM + libnds
rem must be installed via devkitPro pacman (gba-dev / nds-dev meta-pkg).

setlocal enableextensions enabledelayedexpansion

if "%DEVKITPRO%"=="" set DEVKITPRO=C:/devkitPro
if "%DEVKITARM%"=="" set DEVKITARM=%DEVKITPRO%/devkitARM
set PATH=C:\devkitPro\msys2\usr\bin;C:\devkitPro\devkitARM\bin;C:\devkitPro\tools\bin;%PATH%

rem Trust an inherited DEVKITARM only if a compiler is actually there.
rem These vars are often set machine-wide to a POSIX path for a container
rem or WSL toolchain (e.g. /opt/devkitpro), which resolves to nothing on
rem Windows. Because the lines above only fill in defaults when a var is
rem UNSET, that stale value wins and every build dies with a misleading
rem "devkitARM not found" pointing at a path the user never chose.
if not exist "%DEVKITARM:/=\%\bin\arm-none-eabi-gcc.exe" (
    echo NOTE: ignoring inherited DEVKITARM="%DEVKITARM%" - no compiler there.
    set "DEVKITPRO=C:/devkitPro"
    set "DEVKITARM=C:/devkitPro/devkitARM"
)

rem Delayed expansion: read whatever the fallback above settled on.
if not exist "!DEVKITARM:/=\!\bin\arm-none-eabi-gcc.exe" (
    echo ERROR: devkitARM not found at "!DEVKITARM!".
    echo Install devkitPro + nds-dev meta-package from
    echo   https://github.com/devkitPro/installer/releases
    exit /b 1
)

cd /d "%~dp0"

if "%1"=="clean" (
    make clean 2>nul
    exit /b 0
)

make %*
