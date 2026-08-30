@echo off
setlocal
rem IDR Configurator - CH340 driver / Windows 7 SHA-2 patch wrapper (auto-detect).
rem Runs at NORMAL privilege from the installer (addOperation); it elevates only
rem the privileged tool (wusa / pnputil) with Start-Process -Verb RunAs inside
rem try/catch, so a canceled UAC prompt NEVER hangs the installer.
rem
rem Automatic decision (no user toggles needed):
rem   * Windows 7 (6.1) that LACKS SHA-2 support (KB3033929/KB4474419 absent):
rem       -> silently install the bundled KB3033929, then ask for a restart and
rem          exit cleanly. The driver needs the reboot, so it is installed on
rem          the NEXT run.
rem   * Windows 7 already patched, OR any other OS (8/8.1/10/11):
rem       -> import the CH340 driver into the DriverStore with /add-driver.
rem The SHA-2 patch is NEVER applied outside Windows 7.

set "DRVDIR=%~dp0"
set "USERLOG=%TEMP%\IDRConfigurator_driver.log"

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
rem Locate the bundled .msu for this architecture.
set "PATCH="
for %%f in ("%DRVDIR%windows6.1-kb3033929*.msu") do if exist "%%f" set "PATCH=%%f"
if not exist "%PATCH%" (
    echo NO_PATCH_MSU >> "%USERLOG%"
    goto :doDriver
)
echo INSTALL_PATCH=%PATCH% >> "%USERLOG%"
rem wusa elevated; $env:PATCH is inherited from the bat (spaces safe via Start-Process).
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command ^
  "try { $p = Start-Process -FilePath 'wusa.exe' -ArgumentList $env:PATCH,'/quiet','/norestart' -Verb RunAs -Wait -PassThru; 'wusa exit code: ' + $p.ExitCode | Out-File -Append -FilePath $env:windir\Temp\IDRConfigurator_driver.log; exit $p.ExitCode } catch { 'wusa launch failed: ' + $_.Exception.Message | Out-File -Append -FilePath $env:windir\Temp\IDRConfigurator_driver.log; exit 124 }"
echo --- patch install exit %errorlevel% --- >> "%USERLOG%"
rem Inform the user a restart is required, then finish cleanly.
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