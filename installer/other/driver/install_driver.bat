@echo off
setlocal
rem IDR Configurator - CH340 driver install wrapper (best practice).
rem Runs at NORMAL privilege from the installer; elevates only the pnputil call.
rem Never hangs:
rem   * Windows 7 lacking SHA-2 support (WCH .cat is SHA-256 signed) shows a
rem     guidance dialog with the official Microsoft download link and exits
rem     cleanly (exit 0) WITHOUT attempting the driver install.
rem   * pnputil is launched via Start-Process -Verb RunAs -Wait inside try/catch,
rem     so a canceled UAC prompt cannot hang the installer.
rem   * No /install flag: /add-driver only stages the package into DriverStore
rem     (PnP binds it on device plug-in) and never pops a confirmation dialog.

set "DRVDIR=%~dp0"
set "USERLOG=%TEMP%\IDRConfigurator_driver.log"

> "%USERLOG%" echo === CH340 driver install (%date% %time%) ===
echo USER_TEMP=%TEMP% >> "%USERLOG%"
echo DRVDIR=%DRVDIR% >> "%USERLOG%"

rem ---- OS version (Windows 7 = 6.1) ----
set "OSVER="
for /f "tokens=3" %%v in ('reg query "HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion" /v CurrentVersion 2^>nul') do set "OSVER=%%v"
echo OS_VERSION=%OSVER% >> "%USERLOG%"

rem ---- SHA-2 code-signing support present? (needed to validate SHA-256 .cat) ----
rem Detect KB3033929 (2015) or KB4474419 (2019), the two official SHA-2 patches.
set "NEED_PATCH="
if "%OSVER%"=="6.1" (
    reg query "HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\HotFix" /s 2>nul | findstr /i "KB3033929 KB4474419" >nul
    if errorlevel 1 set "NEED_PATCH=1"
)

if defined NEED_PATCH (
    echo SHA2_PATCH_MISSING >> "%USERLOG%"
    powershell.exe -NoProfile -ExecutionPolicy Bypass -Command ^
      "Add-Type -AssemblyName PresentationFramework; [System.Windows.MessageBox]::Show('The CH340 driver uses a SHA-2 (SHA-256) signature, which this Windows 7 PC cannot yet verify.`n`nPlease install the free Microsoft update KB3033929 and restart the PC, then run the IDR Configurator installer again.`n`nOfficial download:`nhttps://www.microsoft.com/en-us/download/details.aspx?id=46148  (64-bit)`nhttps://www.microsoft.com/en-us/download/details.aspx?id=46078  (32-bit)', 'IDR Configurator - CH340 Driver', 'OK', 'Information')" >nul 2>&1
    exit /b 0
)
echo SHA2_OK >> "%USERLOG%"

rem ---- stage the driver package into DriverStore (elevated) ----
echo --- pnputil (elevated) --- >> "%USERLOG%"
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command ^
  "try { $p = Start-Process -FilePath pnputil.exe -ArgumentList '/add-driver','%DRVDIR%CH341SER.INF' -Verb RunAs -Wait -PassThru; $rc = $p.ExitCode; 'pnputil exit code: ' + $rc | Out-File -Append -FilePath $env:windir\Temp\IDRConfigurator_driver.log; if ($rc -eq 3010) { exit 0 }; exit $rc } catch { 'pnputil launch failed (UAC canceled?): ' + $_.Exception.Message | Out-File -Append -FilePath $env:windir\Temp\IDRConfigurator_driver.log; exit 124 }"
set "rc=%errorlevel%"
echo --- bat exit code: %rc% --- >> "%USERLOG%"
exit /b %rc%