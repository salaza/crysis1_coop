@echo off
setlocal EnableDelayedExpansion
title Crysis Co-op - Host Server

set "CRYSIS_DIR="

:: ----------------------------------------------------
:: 1. Find Crysis Installation Folder
:: ----------------------------------------------------
for %%I in ("%~dp0..\..") do set "REL2=%%~fI"
for %%I in ("%~dp0..") do set "REL1=%%~fI"

if exist "%~dp0Game\GameData.pak" (
    for %%I in ("%~dp0.") do set "CRYSIS_DIR=%%~fI"
) else if exist "%REL1%\Game\GameData.pak" (
    set "CRYSIS_DIR=%REL1%"
) else if exist "%REL2%\Game\GameData.pak" (
    set "CRYSIS_DIR=%REL2%"
)

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
    set /p "CRYSIS_DIR=Please paste your full Crysis folder path (e.g. C:\Program Files (x86)\Steam\steamapps\common\Crysis): "
)

if not exist "%CRYSIS_DIR%\Game\GameData.pak" (
    echo [ERROR] Crysis was not found at: "%CRYSIS_DIR%"
    pause
    exit /b 1
)

echo [OK] Crysis directory: "%CRYSIS_DIR%"
set "TARGET_MOD_DIR=%CRYSIS_DIR%\Mods\CrysisCoop"
if not exist "%TARGET_MOD_DIR%" mkdir "%TARGET_MOD_DIR%"

:: ----------------------------------------------------
:: 2. Check for Updates from GitHub Releases
:: ----------------------------------------------------
set "UPDATER_SCRIPT="
if exist "%~dp0Update_CrysisCoop.ps1" (
    set "UPDATER_SCRIPT=%~dp0Update_CrysisCoop.ps1"
) else if exist "%TARGET_MOD_DIR%\Update_CrysisCoop.ps1" (
    set "UPDATER_SCRIPT=%TARGET_MOD_DIR%\Update_CrysisCoop.ps1"
)

if not "!UPDATER_SCRIPT!"=="" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "!UPDATER_SCRIPT!" -TargetModDir "%TARGET_MOD_DIR%" -CrysisDir "%CRYSIS_DIR%"
) else (
    echo [Updater] Running release update check...
    powershell -NoProfile -ExecutionPolicy Bypass -Command "& { param(`$m, `$c) try { `$u = 'https://raw.githubusercontent.com/salaza/crysis1_coop/main/Update_CrysisCoop.ps1'; `$dst = Join-Path `$m 'Update_CrysisCoop.ps1'; `$token = ''; try { `$cr = ('protocol=https`nhost=github.com`n`n' | git credential fill 2>`$null); `$token = (`$cr | Where-Object { `$_ -match '^password=' }) -replace '^password=', ''; } catch {}; `$h = @{ 'User-Agent' = 'CrysisCoop-Updater' }; if (`$token) { `$h['Authorization'] = 'Bearer ' + `$token }; Invoke-RestMethod -Uri `$u -Headers `$h -OutFile `$dst; if (Test-Path `$dst) { & `$dst -TargetModDir `$m -CrysisDir `$c } } catch { Write-Warning ('Could not run updater: ' + `$_) } }" -args "%TARGET_MOD_DIR%","%CRYSIS_DIR%"
)
echo.

:: ----------------------------------------------------
:: 3. Verify C1-Launcher is installed
:: ----------------------------------------------------
if exist "%TARGET_MOD_DIR%\Launcher\Bin32\Crysis.exe" (
    if exist "%CRYSIS_DIR%\Bin32\Crysis.exe" (
        for %%F in ("%CRYSIS_DIR%\Bin32\Crysis.exe") do set "EXE32_SIZE=%%~zF"
        if !EXE32_SIZE! GTR 500000 (
            echo Installing C1-Launcher into Bin32...
            copy /Y "%CRYSIS_DIR%\Bin32\Crysis.exe" "%CRYSIS_DIR%\Bin32\Crysis_backup.exe" >nul
            copy /Y "%TARGET_MOD_DIR%\Launcher\Bin32\Crysis.exe" "%CRYSIS_DIR%\Bin32\Crysis.exe" >nul
        )
    )
)
if exist "%TARGET_MOD_DIR%\Launcher\Bin64\Crysis.exe" (
    if exist "%CRYSIS_DIR%\Bin64\CrySystem.dll" (
        if not exist "%CRYSIS_DIR%\Bin64\Crysis.exe" (
            copy /Y "%TARGET_MOD_DIR%\Launcher\Bin64\Crysis.exe" "%CRYSIS_DIR%\Bin64\Crysis.exe" >nul
        ) else (
            for %%F in ("%CRYSIS_DIR%\Bin64\Crysis.exe") do set "EXE64_SIZE=%%~zF"
            if !EXE64_SIZE! GTR 500000 (
                echo Installing C1-Launcher into Bin64...
                copy /Y "%CRYSIS_DIR%\Bin64\Crysis.exe" "%CRYSIS_DIR%\Bin64\Crysis_backup.exe" >nul
                copy /Y "%TARGET_MOD_DIR%\Launcher\Bin64\Crysis.exe" "%CRYSIS_DIR%\Bin64\Crysis.exe" >nul
            )
        )
    )
)

if exist "%CRYSIS_DIR%\Bin64\Crysis.exe" (
    set "EXE=%CRYSIS_DIR%\Bin64\Crysis.exe"
) else (
    set "EXE=%CRYSIS_DIR%\Bin32\Crysis.exe"
)

echo.
echo  Internet play: the game forwards UDP port 64087 via UPnP and copies your
echo  internet address (IP:port) to the clipboard once the map is loaded.
echo  Send it to your friend. Type coop_address in the console to show it again.
echo  If UPnP is disabled on your router, forward UDP 64087 to this PC manually.
echo.

set "MAP=%~1"
if not "%MAP%"=="" (
    echo Starting Crysis Singleplayer Co-op Host on map: %MAP%
    start "" /D "%CRYSIS_DIR%" "%EXE%" -dx9 -mod CrysisCoop +g_skipIntro 1 +sv_gamerules coop +sv_lanonly 1 +map %MAP% s x
    exit /b 0
)

echo Launching Crysis Co-op directly into the In-Game Menu ^(Intro movies skipped^)...
start "" /D "%CRYSIS_DIR%" "%EXE%" -dx9 -mod CrysisCoop +g_skipIntro 1 +sv_gamerules coop +sv_lanonly 1
