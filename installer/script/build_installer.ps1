# build_installer.ps1 - build the DRUPPC offline installer with Qt Installer Framework
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File installer\script\build_installer.ps1
# Output: installer\dist\DRUPPC_Setup_<datetime>.exe
#
# Everything installer-related lives under installer/:
#   installer/script    this script
#   installer/source    QtIFW project (config/, packages/)
#   installer/dist      generated setup executables
#   installer/other     license texts, icons and other assets

$ErrorActionPreference = "Stop"

$QtIFW     = "D:\Qt5\QtIFW-4.6.1"
$Installer = Split-Path -Parent $PSScriptRoot      # .../installer
$AppRoot   = Split-Path -Parent $Installer         # .../DRUPPC
$Source    = Join-Path $Installer "source"
$Packages  = Join-Path $Source "packages"
$MetaDir   = Join-Path $Packages "druppc\meta"
$DataDir   = Join-Path $Packages "druppc\data"

# ---- pick the newest application build to package ----
$LatestDist = Get-ChildItem (Join-Path $AppRoot "dist") -Directory |
    Sort-Object Name -Descending | Select-Object -First 1
if (-not $LatestDist) {
    throw "No application build found under $AppRoot\dist; run script\build_mingw64.ps1 first."
}
Write-Host "Packaging: $($LatestDist.FullName)"

# ---- stage the package data directory ----
Remove-Item -Recurse -Force $DataDir -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $DataDir | Out-Null
Copy-Item (Join-Path $LatestDist.FullName "*") $DataDir -Recurse

# ---- refresh license texts and installer icon ----
Copy-Item (Join-Path $Installer "other\license\*") $MetaDir -Force
Copy-Item (Join-Path $AppRoot "src\resource\icon\main.ico") `
    (Join-Path $Source "config\druppc_setup.ico") -Force

# ---- run binarycreator (offline installer) ----
$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$OutDir = Join-Path $Installer "dist"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$Setup = Join-Path $OutDir "IDRConfigurator_Setup_$stamp.exe"

& "$QtIFW\bin\binarycreator.exe" --offline-only `
    -c (Join-Path $Source "config\config.xml") `
    -p $Packages `
    $Setup
if ($LASTEXITCODE -ne 0) { throw "binarycreator failed" }

Write-Host ""
Write-Host "OK -> $Setup"
