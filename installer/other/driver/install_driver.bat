@echo off
rem Install the WCH CH340/CH341 USB-to-Serial driver shipped in this folder.
rem %~dp0 resolves to this batch file's directory at runtime.
rem
rem Flow (best practice):
rem   1. This bat runs at NORMAL privilege from the installer (addOperation, not
rem      addElevatedOperation) so %TEMP% is the user's real temp and the
rem      diagnostic log is visible to the user.
rem   2. pnputil is launched via Start-Process -Verb RunAs, which triggers a single
rem      UAC elevation prompt (driver install needs admin, as the user requested).
rem   3. The elevated child's exit code is propagated back; 3010 (success but a
rem      reboot is required) is treated as success.

setlocal
set "DRVDIR=%~dp0"
set "USERLOG=%TEMP%\IDRConfigurator_driver.log"

> "%USERLOG%" echo === CH340 driver install (%date% %time%) ===
echo USER_TEMP=%TEMP% >> "%USERLOG%"
echo DRVDIR=%DRVDIR% >> "%USERLOG%"
echo --- files in driver dir --- >> "%USERLOG%"
dir /b "%DRVDIR%" >> "%USERLOG%" 2>&1

rem pnputil package "add" also stages the driver into DriverStore even if no
rem device is currently attached; /install additionally binds it to a present
rem CH340. Start-Process handles argument quoting natively (spaces safe).
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command ^
  "try { $p = Start-Process -FilePath pnputil.exe -ArgumentList '/add-driver','%DRVDIR%CH341SER.INF','/install' -Verb RunAs -Wait -PassThru; $rc = $p.ExitCode; 'pnputil elevated exit code: ' + $rc | Out-File -Append -FilePath $env:windir\Temp\IDRConfigurator_driver.log; if ($rc -eq 3010) { exit 0 }; exit $rc } catch { 'elevation failed: ' + $_.Exception.Message | Out-File -Append -FilePath $env:windir\Temp\IDRConfigurator_driver.log; exit 1 }"
set "rc=%errorlevel%"

echo --- outer bat exit code: %rc% --- >> "%USERLOG%"
if "%rc%"=="3010" exit /b 0
exit /b %rc%