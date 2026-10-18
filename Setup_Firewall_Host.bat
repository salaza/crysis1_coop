@echo off
:: Crysis Co-op - allow the game through the Windows Firewall (run once on the HOST PC)
net session >nul 2>&1
if errorlevel 1 (
    echo Requesting administrator rights...
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

echo Adding Windows Firewall rules for Crysis Co-op (UDP 64087)...
netsh advfirewall firewall delete rule name="Crysis Co-op (UDP 64087)" >nul 2>&1
netsh advfirewall firewall add rule name="Crysis Co-op (UDP 64087)" dir=in action=allow protocol=UDP localport=64087 profile=any
echo.
echo Done. Your router still needs UPnP enabled (automatic) or a manual
echo port forward: UDP 64087 -^> this PC's local IP.
pause
