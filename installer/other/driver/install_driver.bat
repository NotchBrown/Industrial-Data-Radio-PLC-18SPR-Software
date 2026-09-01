@echo off
setlocal
rem IDR Configurator - CH340 driver wrapper.
rem The installer is elevated up front (installer.gainAdminRights), so we call
rem pnputil directly - no nested RunAs, which is unreliable on Windows 7.
rem
rem Strategy (no automatic system-update installs):
rem   1. ALWAYS import the CH340 driver into the DriverStore with /add-driver.
rem      /add-driver does NOT verify/bind a device, so it never pops a "cannot
rem      verify publisher" dialog and never blocks.
rem   2. If this is Windows 7 that LACKS the SHA-2 code-signing patch, show a
rem      NON-BLOCKING notice (started as a detached process we do NOT wait on)
rem      telling the user to install KB4474419, restart, and re-run.
rem   3. The batch always returns promptly - it never waits on any dialog, so
rem      the installer cannot hang.

set "DRVDIR=%~dp0"
set "USERLOG=%TEMP%\IDRConfigurator_driver.log"
set "SYSLOG=%windir%\Temp\IDRConfigurator_driver.log"

> "%USERLOG%" echo === CH340 setup (%date% %time%) ===
echo DRVDIR=%DRVDIR% >> "%USERLOG%"
for /f "tokens=3" %%v in ('reg query "HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion" /v CurrentVersion 2^>nul') do set "OSVER=%%v"
echo OS=%OSVER% >> "%USERLOG%"

rem ---- SHA-2 patch present on Windows 7? ----
set "PATCHED="
if "%OSVER%"=="6.1" (
    reg query "HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\HotFix" /s 2>nul | findstr /i "KB3033929 KB4474419 KB4490628" >nul
    if not errorlevel 1 set "PATCHED=1"
)
echo PATCHED=%PATCHED% >> "%USERLOG%"

rem ---- 1. import the driver package (nothing blocks here) ----
echo --- pnputil --- >> "%USERLOG%"
pnputil.exe /add-driver "%DRVDIR%CH341SER.INF" >> "%SYSLOG%" 2>&1
set "rc=%errorlevel%"
echo --- pnputil exit code: %rc% --- >> "%USERLOG%"

rem ---- 2. if Windows 7 lacks the SHA-2 patch, warn (non-blocking) ----
if "%OSVER%"=="6.1" if not defined PATCHED (
    echo SHA2_PATCH_MISSING_DRIVER_MAY_NOT_ACTIVATE >> "%USERLOG%"
    rem Show a one-off system-message notice via the built-in msg.exe (no script
    rem engine spawned, so it is not flagged as a downloader trojan). Detached:
    rem the installer does NOT wait for this process to return.
    start "" msg.exe * "The CH340 driver was registered, but this Windows 7 is missing the SHA-2 (SHA-256) code-signing patch, so the driver may not activate until it is installed. Please install KB4474419 from the Microsoft Update Catalog, restart this PC, then re-run the IDR Configurator installer."
)

if "%rc%"=="3010" exit /b 0
exit /b %rc%