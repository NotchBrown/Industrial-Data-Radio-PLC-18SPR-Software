# build_mingw32.ps1 - 32-bit release build
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File script\build_mingw32.ps1
# Output: dist\<datetime>  (DRUPPC.exe + Qt runtime + resource/, nothing packed into exe)

$ErrorActionPreference = "Stop"

$QtRoot     = "D:\Qt5\Qt5.14.2\5.14.2\mingw73_32"
$ToolsRoot  = "D:\Qt5\Qt5.14.2\Tools\mingw730_32"
$ProjectDir = Split-Path -Parent $PSScriptRoot
# Intermediate artifacts live in %TEMP%, never in the workspace; removed after build.
$BuildDir   = Join-Path $env:TEMP "DRUPPC_build_mingw32"

$env:PATH = "$ToolsRoot\bin;$QtRoot\bin;" + $env:PATH

# ---- build (fresh intermediate dir, clear stale artifacts first) ----
Remove-Item -Recurse -Force $BuildDir -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
Set-Location $BuildDir
& "$QtRoot\bin\qmake.exe" "$ProjectDir\DRUPPC.pro" -spec win32-g++ "CONFIG+=release"
if ($LASTEXITCODE -ne 0) { throw "qmake failed" }
& "$ToolsRoot\bin\mingw32-make.exe" -j4
if ($LASTEXITCODE -ne 0) { throw "build failed" }

# ---- dist/<datetime> ----
$stamp   = Get-Date -Format "yyyyMMdd_HHmmss"
$DistDir = Join-Path $ProjectDir "dist\$stamp"
New-Item -ItemType Directory -Force -Path $DistDir | Out-Null

Copy-Item (Join-Path $BuildDir "release\DRUPPC.exe") $DistDir

# Qt/MinGW runtime dlls (32-bit gcc runtime has a different name)
$Dlls = @("Qt5Core.dll", "Qt5Gui.dll", "Qt5Widgets.dll", "Qt5SerialPort.dll", "Qt5Xml.dll", "libgcc_s_dw2-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll")
foreach ($d in $Dlls) {
    Copy-Item (Join-Path $QtRoot "bin\$d") $DistDir -ErrorAction SilentlyContinue
}
New-Item -ItemType Directory -Force -Path (Join-Path $DistDir "platforms") | Out-Null
Copy-Item (Join-Path $QtRoot "plugins\platforms\qwindows.dll") (Join-Path $DistDir "platforms")

# resource/ (fonts, icons, translations loaded externally); .qm built at package time
Copy-Item (Join-Path $ProjectDir "src\resource") (Join-Path $DistDir "resource") -Recurse
New-Item -ItemType Directory -Force -Path (Join-Path $DistDir "resource\i18n") | Out-Null
& "$QtRoot\bin\lrelease.exe" (Join-Path $ProjectDir "src\resource\i18n\druppc_zh_CN.ts") -qm (Join-Path $DistDir "resource\i18n\druppc_zh_CN.qm")
# Ship only compiled .qm, not the .ts sources
Remove-Item (Join-Path $DistDir "resource\i18n\*.ts") -ErrorAction SilentlyContinue

# ---- cleanup intermediate artifacts ----
Set-Location $ProjectDir
Remove-Item -Recurse -Force $BuildDir -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "OK -> $DistDir"
