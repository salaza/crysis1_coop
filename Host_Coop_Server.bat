@echo off
setlocal EnableDelayedExpansion

set "CRYSIS_DIR="

:: 1. Check relative paths (Crysis\Mods\CrysisCoop or Crysis\CrysisCoop)
for %%I in ("%~dp0..\..") do set "REL2=%%~fI"
for %%I in ("%~dp0..") do set "REL1=%%~fI"

if exist "%REL2%\Game\GameData.pak" (
    set "CRYSIS_DIR=%REL2%"
) else if exist "%REL1%\Game\GameData.pak" (
    set "CRYSIS_DIR=%REL1%"
)

:: 2. Search common Steam / GOG paths across drives C..G
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

echo Found Crysis at: "%CRYSIS_DIR%"

:: 3. Ensure mod files are installed/updated in <Crysis>\Mods\CrysisCoop
for %%I in ("%~dp0.") do set "SCRIPT_DIR=%%~fI"
for %%I in ("%CRYSIS_DIR%\Mods\CrysisCoop") do set "TARGET_MOD_DIR=%%~fI"
if /I not "%SCRIPT_DIR%"=="%TARGET_MOD_DIR%" (
    if exist "%~dp0Bin32\CrysisCoop.dll" (
        echo Updating CrysisCoop in "%TARGET_MOD_DIR%"...
        xcopy /E /I /Y "%~dp0*" "%TARGET_MOD_DIR%\" >nul
    )
)

:: 4. Ensure C1-Launcher is installed
if exist "%CRYSIS_DIR%\Mods\CrysisCoop\Launcher\Bin32\Crysis.exe" (
    if exist "%CRYSIS_DIR%\Bin32\Crysis.exe" (
        for %%F in ("%CRYSIS_DIR%\Bin32\Crysis.exe") do set "EXE32_SIZE=%%~zF"
        if !EXE32_SIZE! GTR 500000 (
            echo Installing C1-Launcher into Bin32 ^(backing up original to Crysis_backup.exe^)...
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
                echo Installing C1-Launcher into Bin64 ^(backing up original to Crysis_backup.exe^)...
                copy /Y "%CRYSIS_DIR%\Bin64\Crysis.exe" "%CRYSIS_DIR%\Bin64\Crysis_backup.exe" >nul
                copy /Y "%CRYSIS_DIR%\Mods\CrysisCoop\Launcher\Bin64\Crysis.exe" "%CRYSIS_DIR%\Bin64\Crysis.exe" >nul
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
    start "" /D "%CRYSIS_DIR%" "%EXE%" -dx9 -mod CrysisCoop -devmode +g_skipIntro 1 +sv_gamerules coop +sv_lanonly 1 +map %MAP% s
    exit /b 0
)

echo Launching Crysis Co-op directly into the In-Game Menu ^(Intro movies skipped^)...
start "" /D "%CRYSIS_DIR%" "%EXE%" -dx9 -mod CrysisCoop -devmode +g_skipIntro 1 +sv_gamerules coop +sv_lanonly 1

