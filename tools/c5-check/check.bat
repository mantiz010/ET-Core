@echo off
setlocal
cd /d "%~dp0"
echo C5 connection check - COM24 at 115200
echo Close Arduino Serial Monitor and browser serial tools first.
echo This resets the C5 and reads its flash ID. It does NOT erase or write flash.
echo Leave the C5 connected. Press any key to start.
pause >nul
set "C5_TOOL=%LOCALAPPDATA%\Arduino15\packages\esp32\tools\esptool_py\5.1.0\esptool.exe"
if not exist "%C5_TOOL%" goto missing
>result.txt echo C5 diagnostic - %DATE% %TIME%
>>result.txt echo Port COM24, baud 115200. No flash writes or erases.
>>result.txt echo === USB port and driver ===
powershell.exe -NoProfile -Command "Get-CimInstance Win32_PnPSignedDriver | Where-Object { $_.DeviceName -match '\(COM24\)' } | Select-Object DeviceName,DeviceID,DriverProviderName,DriverVersion | Format-List" >>result.txt 2>&1
echo Test 1 of 2: ROM loader - no stub.
>>result.txt echo === ROM loader flash-id ===
"%C5_TOOL%" --chip esp32c5 --port COM24 --baud 115200 --before default-reset --after hard-reset --no-stub flash-id >>result.txt 2>&1
set "C5_ROM=%ERRORLEVEL%"
>>result.txt echo ROM exit code: %C5_ROM%
timeout /t 3 /nobreak >nul
echo Test 2 of 2: normal stub loader.
>>result.txt echo === Stub loader flash-id ===
"%C5_TOOL%" --chip esp32c5 --port COM24 --baud 115200 --before default-reset --after hard-reset flash-id >>result.txt 2>&1
set "C5_STUB=%ERRORLEVEL%"
>>result.txt echo Stub exit code: %C5_STUB%
echo.
echo Checks finished. ROM exit=%C5_ROM% Stub exit=%C5_STUB%
echo Send result.txt from this folder. Neither test wrote firmware.
start "" notepad.exe "%~dp0result.txt"
pause
exit /b 0
:missing
echo The esptool 5.1.0 shown in your Arduino log was not found.
echo No board operation performed. Send this message.
pause
exit /b 1
