@echo off
rem Install the WCH CH340/CH341 USB-to-Serial driver shipped in this folder.
rem %~dp0 resolves to this batch file's directory at runtime, so this works
rem regardless of where the installer placed the component.
rem Results are logged to %TEMP%\IDRConfigurator_driver.log so failures can be diagnosed.

set "DRVDIR=%~dp0"
set "LOG=%TEMP%\IDRConfigurator_driver.log"

> "%LOG%" echo === CH340 driver install (%date% %time%) ===
echo DRVDIR=%DRVDIR% >> "%LOG%"
echo --- files in driver dir --- >> "%LOG%"
dir /b "%DRVDIR%" >> "%LOG%" 2>&1
echo --- pnputil output --- >> "%LOG%"

pnputil.exe /add-driver "%DRVDIR%CH341SER.INF" /install >> "%LOG%" 2>&1
set "rc=%errorlevel%"
echo --- pnputil exit code: %rc% --- >> "%LOG%"

rem 3010 = success but reboot required (treat as success here).
if "%rc%"=="3010" exit /b 0
exit /b %rc%