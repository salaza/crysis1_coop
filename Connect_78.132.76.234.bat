@echo off
setlocal EnableDelayedExpansion
title Crysis Co-op - 78.132.76.234

set "SERVER_IP=78.132.76.234"

echo ===================================================
echo     Crysis 1 Co-op - Auto-Updater ^& Launcher
echo     Target Server: %SERVER_IP%
echo ===================================================
echo.

:: ----------------------------------------------------
:: 1. Check for Git Updates (if running in/near a repository)
:: ----------------------------------------------------
set "GIT_DIR="
if exist "%~dp0.git" set "GIT_DIR=%~dp0"
if exist "%~dp0..\.git" set "GIT_DIR=%~dp0..\"
if exist "%~dp0..\..\.git" set "GIT_DIR=%~dp0..\..\"

if not "%GIT_DIR%"=="" (
    where git >nul 2>&1
    if !ERRORLEVEL! EQU 0 (
        echo [Updater] Checking for repository updates via git...
        pushd "%GIT_DIR%"
        git pull origin main
        popd
        echo.
    )
)

:: ----------------------------------------------------
:: 2. Find Crysis Installation Folder
:: ----------------------------------------------------
set "CRYSIS_DIR="

:: Check relative paths (e.g. Crysis\Mods\CrysisCoop)
for %%I in ("%~dp0..\..") do set "REL2=%%~fI"
for %%I in ("%~dp0..") do set "REL1=%%~fI"

if exist "%REL2%\Game\GameData.pak" (
    set "CRYSIS_DIR=%REL2%"
) else if exist "%REL1%\Game\GameData.pak" (
    set "CRYSIS_DIR=%REL1%"
)

:: Search common Steam / GOG paths across drives C..G
if "%CRYSIS_DIR%"=="" (
    for %%D in (C D E F G) do (
        if exist "%%D:\SteamLibrary\steamapps\common\Crysis\Game\GameData.pak" set "CRYSIS_DIR=%%D:\SteamLibrary\steamapps\common\Crysis"
        if exist "%%D:\Program Files (x86)\Steam\steamapps\common\Crysis\Game\GameData.pak" set "CRYSIS_DIR=%%D:\Program Files (x86)\Steam\steamapps\common\Crysis"
        if exist "%%D:\Program Files\Steam\steamapps\common\Crysis\Game\GameData.pak" set "CRYSIS_DIR=%%D:\Program Files\Steam\steamapps\common\Crysis"
        if exist "%%D:\GOG Games\Crysis\Game\GameData.pak" set "CRYSIS_DIR=%%D:\GOG Games\Crysis"
    )
)

if "%CRYSIS_DIR%"=="" (
    echo [!] Could not automatically find your Crysis installation folder.
    set /p "CRYSIS_DIR=Please paste your full Crysis folder path: "
)

if not exist "%CRYSIS_DIR%\Game\GameData.pak" (
    echo [ERROR] Crysis was not found at: "%CRYSIS_DIR%"
    pause
    exit /b 1
)

echo [OK] Crysis directory: "%CRYSIS_DIR%"

:: ----------------------------------------------------
:: 3. Update mod files in <Crysis>\Mods\CrysisCoop if launched from SDK/source
:: ----------------------------------------------------
for %%I in ("%~dp0.") do set "SCRIPT_DIR=%%~fI"
for %%I in ("%CRYSIS_DIR%\Mods\CrysisCoop") do set "TARGET_MOD_DIR=%%~fI"
if /I not "%SCRIPT_DIR%"=="%TARGET_MOD_DIR%" (
    if exist "%~dp0build\Mods\CrysisCoop\Bin64\Release\CrysisCoop.dll" (
        echo [Updater] Syncing latest compiled 64-bit binaries to "%TARGET_MOD_DIR%\Bin64"...
        if not exist "%TARGET_MOD_DIR%\Bin64" mkdir "%TARGET_MOD_DIR%\Bin64"
        copy /Y "%~dp0build\Mods\CrysisCoop\Bin64\Release\CrysisCoop.dll" "%TARGET_MOD_DIR%\Bin64\CrysisCoop.dll" >nul
    )
    if exist "%~dp0build\Mods\CrysisCoop\Bin32\Release\CrysisCoop.dll" (
        echo [Updater] Syncing latest compiled 32-bit binaries to "%TARGET_MOD_DIR%\Bin32"...
        if not exist "%TARGET_MOD_DIR%\Bin32" mkdir "%TARGET_MOD_DIR%\Bin32"
        copy /Y "%~dp0build\Mods\CrysisCoop\Bin32\Release\CrysisCoop.dll" "%TARGET_MOD_DIR%\Bin32\CrysisCoop.dll" >nul
    )
    if exist "%~dp0Game\Libs\FlowNodes\Substitutions.xml" (
        if not exist "%TARGET_MOD_DIR%\Game\Libs\FlowNodes" mkdir "%TARGET_MOD_DIR%\Game\Libs\FlowNodes"
        copy /Y "%~dp0Game\Libs\FlowNodes\Substitutions.xml" "%TARGET_MOD_DIR%\Game\Libs\FlowNodes\Substitutions.xml" >nul
    )
)

:: ----------------------------------------------------
:: 4. Verify C1-Launcher is installed (prevents modern Win10/11 crashes)
:: ----------------------------------------------------
if exist "%CRYSIS_DIR%\Mods\CrysisCoop\Launcher\Bin32\Crysis.exe" (
    if exist "%CRYSIS_DIR%\Bin32\Crysis.exe" (
        for %%F in ("%CRYSIS_DIR%\Bin32\Crysis.exe") do set "EXE32_SIZE=%%~zF"
        if !EXE32_SIZE! GTR 500000 (
            echo Installing C1-Launcher into Bin32...
            copy /Y "%CRYSIS_DIR%\Bin32\Crysis.exe" "%CRYSIS_DIR%\Bin32\Crysis_backup.exe" >nul
            copy /Y "%CRYSIS_DIR%\Mods\CrysisCoop\Launcher\Bin32\Crysis.exe" "%CRYSIS_DIR%\Bin32\Crysis.exe" >nul
        )
    )
)
if exist "%CRYSIS_DIR%\Mods\CrysisCoop\Launcher\Bin64\Crysis.exe" (
    if exist "%CRYSIS_DIR%\Bin64\CrySystem.dll" (
        if not exist "%CRYSIS_DIR%\Bin64\Crysis.exe" (
            copy /Y "%CRYSIS_DIR%\Mods\CrysisCoop\Launcher\Bin64\Crysis.exe" "%CRYSIS_DIR%\Bin64\Crysis.exe" >nul
        ) else (
            for %%F in ("%CRYSIS_DIR%\Bin64\Crysis.exe") do set "EXE64_SIZE=%%~zF"
            if !EXE64_SIZE! GTR 500000 (
                echo Installing C1-Launcher into Bin64...
                copy /Y "%CRYSIS_DIR%\Bin64\Crysis.exe" "%CRYSIS_DIR%\Bin64\Crysis_backup.exe" >nul
                copy /Y "%CRYSIS_DIR%\Mods\CrysisCoop\Launcher\Bin64\Crysis.exe" "%CRYSIS_DIR%\Bin64\Crysis.exe" >nul
            )
        )
    )
)

:: Pick executable (prefer 64-bit if available)
if exist "%CRYSIS_DIR%\Bin64\Crysis.exe" (
    set "EXE=%CRYSIS_DIR%\Bin64\Crysis.exe"
) else (
    set "EXE=%CRYSIS_DIR%\Bin32\Crysis.exe"
)

:: ----------------------------------------------------
:: 5. Launch Crysis and connect directly to 78.132.76.234
:: ----------------------------------------------------
echo.
echo Connecting to Co-op Server at %SERVER_IP%...
echo (Skipping intro movies, starting directly into game)
echo.

start "" /D "%CRYSIS_DIR%" "%EXE%" -dx9 -mod CrysisCoop -devmode +g_skipIntro 1 +sv_lanonly 1 +cl_AfterLoadScreen "+coop_connect %SERVER_IP%"
