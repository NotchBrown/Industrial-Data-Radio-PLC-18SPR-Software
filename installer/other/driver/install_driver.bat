@echo off
rem Install the WCH CH340/CH341 USB-to-Serial driver shipped in this folder.
rem %~dp0 resolves to this batch file's directory at runtime, so this works
rem regardless of where the installer placed the component.
cd /d "%~dp0"
pnputil.exe /add-driver "%~dp0CH341SER.INF" /install
set "rc=%errorlevel%"
rem 3010 = success but reboot required (treat as success here).
if "%rc%"=="3010" exit /b 0
exit /b %rc%