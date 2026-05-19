@echo off
rem totp-nds Windows build wrapper. Hands off to devkitPro's MSYS2 bash
rem so PowerShell doesn't route `make` through WSL. devkitARM + libnds
rem must be installed via devkitPro pacman (gba-dev / nds-dev meta-pkg).

setlocal enableextensions enabledelayedexpansion

if "%DEVKITPRO%"=="" set DEVKITPRO=C:/devkitPro
if "%DEVKITARM%"=="" set DEVKITARM=%DEVKITPRO%/devkitARM
set PATH=C:\devkitPro\msys2\usr\bin;C:\devkitPro\devkitARM\bin;C:\devkitPro\tools\bin;%PATH%

if not exist "%DEVKITARM:/=\%\bin\arm-none-eabi-gcc.exe" (
    echo ERROR: devkitARM not found at "%DEVKITARM%".
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
