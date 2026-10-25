@echo off
setlocal
set "CRYSIS_DIR=%~dp0"
if "%CRYSIS_DIR:~-1%"=="\" set "CRYSIS_DIR=%CRYSIS_DIR:~0,-1%"
set "TARGET_MOD_DIR=%CRYSIS_DIR%\Mods\CrysisCoop"

if exist "%TARGET_MOD_DIR%\Update_CrysisCoop.ps1" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%TARGET_MOD_DIR%\Update_CrysisCoop.ps1" -TargetModDir "%TARGET_MOD_DIR%" -CrysisDir "%CRYSIS_DIR%"
) else if exist "%CRYSIS_DIR%\Update_CrysisCoop.ps1" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%CRYSIS_DIR%\Update_CrysisCoop.ps1" -TargetModDir "%TARGET_MOD_DIR%" -CrysisDir "%CRYSIS_DIR%"
)

exit /b 0
