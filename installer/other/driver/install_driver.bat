@echo off
setlocal
rem IDR Configurator - CH340 driver / Windows 7 SHA-2 patch wrapper (best practice).
rem Runs at NORMAL privilege from the installer (addOperation); it elevates only
rem the privileged tool (wusa / pnputil) with Start-Process -Verb RunAs inside
rem try/catch, so a canceled UAC prompt NEVER hangs the installer.
rem
rem Usage:  install_driver.bat [--patch | --driver]
rem   --patch : (Windows 7 only) install the bundled KB3033929 SHA-2 patch if it
rem             is missing, then ask for a restart. The driver needs a reboot,
rem             so the driver install is deferred to the next run.
rem   --driver: import the CH340 driver into the DriverStore with /add-driver.
rem             (PnP binds it when the device is plugged in; no confirmation
rem              dialog is shown even if this Windows 7 still lacks SHA-2.)

set "DRVDIR=%~dp0"
set "USERLOG=%TEMP%\IDRConfigurator_driver.log"

> "%USERLOG%" echo === CH340 setup (%date% %time%) ===
echo DRVDIR=%DRVDIR% >> "%USERLOG%"
setlocal enabledelayedexpansion
for /f "tokens=3" %%v in ('reg query "HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion" /v CurrentVersion 2^>nul') do set "OSVER=%%v"
for /f "tokens=3" %%v in ('reg query "HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment" /v PROCESSOR_ARCHITECTURE 2^>nul') do set "CPUARCH=%%v"
echo OS=%OSVER% CPU=%CPUARCH% >> "%USERLOG%"
if "%CPUARCH%"=="" set "CPUARCH=%PROCESSOR_ARCHITECTURE%"

rem ------------- SHA-2 patch present? -------------
set "PATCHED="
if "%OSVER%"=="6.1" (
    reg query "HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\HotFix" /s 2>nul | findstr /i "KB3033929 KB4474419" >nul
    if not errorlevel 1 set "PATCHED=1"
)
echo PATCHED=%PATCHED% >> "%USERLOG%"

rem ------------- mode dispatch -------------
if /I "%~1"=="--patch" goto :doPatch
goto :doDriver

:doPatch
rem Never apply the patch outside Windows 7 (belt and braces).
if not "%OSVER%"=="6.1" (
    echo SKIP_PATCH_NON_WIN7 >> "%USERLOG%"
    exit /b 0
)
rem Patch already present -> nothing to do, fall through to install the driver.
if defined PATCHED (
    echo PATCH_ALREADY_CONTINUE_DRIVER >> "%USERLOG%"
    goto :doDriver
)
rem Locate the bundled .msu for this architecture.
set "PATCH="
for %%f in ("%DRVDIR%windows6.1-kb3033929*.msu") do if exist "%%f" set "PATCH=%%f"
if not defined PATCH (
    echo NO_PATCH_MSU >> "%USERLOG%"
    exit /b 0
)
echo INSTALL_PATCH=%PATCH% >> "%USERLOG%"
if not exist "%PATCH%" (
    echo NO_PATCH_MSU_FOUND >> "%USERLOG%"
    exit /b 0
)
rem wusa is elevated separately; $env:PATCH is inherited from the bat so the
rem path (spaces safe, Start-Process quotes it) needs no escaping in -Command.
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command ^
  "try { $p = Start-Process -FilePath 'wusa.exe' -ArgumentList $env:PATCH,'/quiet','/norestart' -Verb RunAs -Wait -PassThru; 'wusa exit code: ' + $p.ExitCode | Out-File -Append -FilePath $env:windir\Temp\IDRConfigurator_driver.log; exit $p.ExitCode } catch { 'wusa launch failed: ' + $_.Exception.Message | Out-File -Append -FilePath $env:windir\Temp\IDRConfigurator_driver.log; exit 124 }"
echo --- patch install exit %errorlevel% --- >> "%USERLOG%"
rem Inform the user that a restart is required, then finish cleanly.
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command ^
  "Add-Type -AssemblyName PresentationFramework; $w = New-Object System.Windows.Window -Property @{Title='IDR Configurator - Restart Required'; Topmost=$true; Width=520; Height=220; WindowStartupLocation='CenterScreen'}; $s = [System.Windows.StackPanel]::new(); $s.Margin='16'; $t = [System.Windows.Controls.TextBlock]::new(); $t.Text=('The Windows 7 SHA-2 support update (KB3033929) has been installed.`n`nA restart is required for it to take effect. Please restart this PC and then run the IDR Configurator installer again to finish installing the CH340 driver.'); $t.TextWrapping='Wrap'; $s.Children.Add($t); $w.Content=$s; $w.ShowDialog()" >nul 2>&1
exit /b 0

:doDriver
rem Import the CH340 driver package into the DriverStore.
echo --- pnputil (elevated) --- >> "%USERLOG%"
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command ^
  "try { $p = Start-Process -FilePath pnputil.exe -ArgumentList '/add-driver','%DRVDIR%CH341SER.INF' -Verb RunAs -Wait -PassThru; $rc = $p.ExitCode; 'pnputil exit code: ' + $rc | Out-File -Append -FilePath $env:windir\Temp\IDRConfigurator_driver.log; if ($rc -eq 3010) { exit 0 }; exit $rc } catch { 'pnputil launch failed (UAC canceled?): ' + $_.Exception.Message | Out-File -Append -FilePath $env:windir\Temp\IDRConfigurator_driver.log; exit 124 }"
set "rc=%errorlevel%"
echo --- bat exit code: %rc% --- >> "%USERLOG%"
exit /b %rc%