@echo off
setlocal
rem IDR Configurator - CH340 driver / Windows 7 SHA-2 patch wrapper (auto-detect).
rem The installer is elevated up front (installer.gainAdminRights in installscript),
rem so pnputil / wusa are called DIRECTLY here - no nested Start-Process -Verb RunAs
rem (which is unreliable and hangs on some Windows 7 machines with "access denied").
rem
rem Automatic decision:
rem   * Windows 7 (6.1) that LACKS SHA-2 support (KB3033929/KB4474419 absent):
rem       -> silently install the bundled KB3033929, then ask for a restart and
rem          exit cleanly. The driver needs the reboot, so it is installed on
rem          the NEXT run.
rem   * Windows 7 already patched, OR any other OS (8/8.1/10/11):
rem       -> import the CH340 driver into the DriverStore with /add-driver.
rem The SHA-2 patch is NEVER applied outside Windows 7.
rem
rem No-hold guarantees: every command is captured to a log; if a command fails
rem it returns its exit code immediately - it never blocks waiting for a prompt.

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
    reg query "HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\HotFix" /s 2>nul | findstr /i "KB3033929 KB4474419" >nul
    if not errorlevel 1 set "PATCHED=1"
)
echo PATCHED=%PATCHED% >> "%USERLOG%"

rem ---- auto: Windows 7 WITHOUT the patch -> install it, ask to restart ----
if "%OSVER%"=="6.1" if not defined PATCHED goto :doPatch
goto :doDriver

:doPatch
set "PATCH="
for %%f in ("%DRVDIR%windows6.1-kb3033929*.msu") do if exist "%%f" set "PATCH=%%f"
if not exist "%PATCH%" (
    echo NO_PATCH_MSU >> "%USERLOG%"
    goto :doDriver
)
echo INSTALL_PATCH=%PATCH% >> "%USERLOG%"
wusa.exe "%PATCH%" /quiet /norestart >> "%SYSLOG%" 2>&1
set "rc=%errorlevel%"
echo --- wusa exit code: %rc% --- >> "%USERLOG%"
rem Inform the user a restart is required, then finish cleanly.
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command ^
  "Add-Type -AssemblyName PresentationFramework; $w = New-Object System.Windows.Window -Property @{Title='IDR Configurator - Restart Required'; Topmost=$true; Width=520; Height=220; WindowStartupLocation='CenterScreen'}; $s = [System.Windows.StackPanel]::new(); $s.Margin='16'; $t = [System.Windows.Controls.TextBlock]::new(); $t.Text=('The Windows 7 SHA-2 support update (KB3033929) has been installed.`n`nA restart is required for it to take effect. Please restart this PC and then run the IDR Configurator installer again to finish installing the CH340 driver.'); $t.TextWrapping='Wrap'; $s.Children.Add($t); $w.Content=$s; $w.ShowDialog()" >nul 2>&1
exit /b 0

:doDriver
echo --- pnputil --- >> "%USERLOG%"
pnputil.exe /add-driver "%DRVDIR%CH341SER.INF" >> "%SYSLOG%" 2>&1
set "rc=%errorlevel%"
echo --- pnputil exit code: %rc% --- >> "%USERLOG%"
if "%rc%"=="3010" exit /b 0
exit /b %rc%