# build_installer.ps1 - build the IDR Configurator offline installers with Qt IFW
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File installer\script\build_installer.ps1
#   powershell ... build_installer.ps1 -Arch x64
#   powershell ... build_installer.ps1 -Arch x86
#   powershell ... build_installer.ps1 -Arch both      (default)
#   powershell ... build_installer.ps1 -SkipBuild      (use existing dist, skip app build)
# Output: installer\dist\IDRConfigurator_Setup_<arch>_<datetime>.exe
#
# Everything installer-related lives under installer/:
#   installer/script     this script
#   installer/source     QtIFW project (config/, packages/)
#   installer/dist       generated setup executables
#   installer/other      license texts, driver and other assets

param(
    [ValidateSet("x64", "x86", "both")]
    [string]$Arch = "both",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$QtIFW     = "D:\Qt5\QtIFW-4.6.1"
$Installer = Split-Path -Parent $PSScriptRoot      # .../installer
$AppRoot   = Split-Path -Parent $Installer         # .../DRUPPC
$Source    = Join-Path $Installer "source"
$Packages  = Join-Path $Source "packages"
$AppMetaDir   = Join-Path $Packages "druppc\meta"
$AppDataDir   = Join-Path $Packages "druppc\data"
$DrvMetaDir   = Join-Path $Packages "ch340driver\meta"
$DrvDataDir   = Join-Path $Packages "ch340driver\data"
$DrvSrcDir    = Join-Path $Installer "other\driver\CH341SER"
$OutDir       = Join-Path $Installer "dist"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# ---- 1. build the application for the requested architectures (unless skipped) ----
if (-not $SkipBuild) {
    $buildFor = @{}
    if ($Arch -eq "both" -or $Arch -eq "x64") { $buildFor["x64"] = "script\build_mingw64.ps1" }
    if ($Arch -eq "both" -or $Arch -eq "x86") { $buildFor["x86"] = "script\build_mingw32.ps1" }
    foreach ($b in $buildFor.GetEnumerator() | Sort-Object Key) {
        Write-Host ""
        Write-Host "== Building $($b.Key) application =="
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $AppRoot $b.Value)
        if ($LASTEXITCODE -ne 0) { throw "Application build failed for $($b.Key)" }
    }
    Write-Host ""
}

# ---- detect an EXE's machine architecture from its PE header ----
# x64 -> 0x8664, x86 -> 0x014c
function Get-ExeArch([string]$exe) {
    $fs = [System.IO.File]::OpenRead($exe)
    try {
        $buf = New-Object byte[] 1024
        $fs.Read($buf, 0, $buf.Length) | Out-Null
        # e_lfanew at 0x3C
        $pe = [BitConverter]::ToInt32($buf, 0x3C)
        $machine = [BitConverter]::ToUInt16($buf, $pe + 4)
        if ($machine -eq 0x8664) { return "x64" }
        if ($machine -eq 0x014c) { return "x86" }
        return "unknown"
    } finally {
        $fs.Dispose()
    }
}

# ---- driver files ----
# pnputil validates that EVERY file listed in the INF's [SourceDisksFiles] is
# present, otherwise the package is rejected ("invalid driver package", exit 3).
# Both architectures share the same INF, so ship the complete small (~300 KB)
# file set for every installer; the total is negligible next to the setup size.
$DrvFiles = @(
    "CH341SER.INF", "CH341SER.CAT",
    "CH341SER.SYS",        # x86 WDM driver
    "CH341S64.SYS",        # x64 WDM driver
    "CH341M64.SYS",        # ARM64 WDM driver
    "CH341PT.DLL", "CH341PORTS.DLL",        # x86
    "CH341PTA64.DLL", "CH341PORTSA64.DLL"   # x64
)

$stamp = Get-Date -Format "yyyyMMdd_HHmmss"

$arches = if ($Arch -eq "both") { @("x64", "x86") } else { @($Arch) }

foreach ($a in $arches) {

    # ---- pick the newest application build for THIS architecture ----
    $LatestDist = Get-ChildItem (Join-Path $AppRoot "dist") -Directory |
        Sort-Object Name -Descending |
        Where-Object { (Test-Path (Join-Path $_.FullName "IDRConfigurator.exe")) -and
                       (Get-ExeArch (Join-Path $_.FullName "IDRConfigurator.exe")) -eq $a } |
        Select-Object -First 1
    if (-not $LatestDist) {
        Write-Warning "No $a application build found; skipping."
        continue
    }
    Write-Host "Packaging $a app: $($LatestDist.Name)"

    # ---- stage the application package data ----
    Remove-Item -Recurse -Force $AppDataDir -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $AppDataDir | Out-Null
    Copy-Item (Join-Path $LatestDist.FullName "*") $AppDataDir -Recurse

    # ---- stage the driver package data (complete INF file set, shared by both) ----
    Remove-Item -Recurse -Force $DrvDataDir -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $DrvDataDir | Out-Null
    foreach ($f in $DrvFiles) {
        $src = Join-Path $DrvSrcDir $f
        if (-not (Test-Path $src)) { throw "Missing driver file: $src" }
        Copy-Item $src $DrvDataDir
    }
    # driver install wrapper batch (lives next to the driver source folder)
    $batSrc = Join-Path (Split-Path $DrvSrcDir -Parent) "install_driver.bat"
    if (-not (Test-Path $batSrc)) { throw "Missing driver wrapper: $batSrc" }
    Copy-Item $batSrc $DrvDataDir -Force

    # ---- stage the architecture-matched KB3033929 SHA-2 patch (Win7) ----
    $patchDir = Join-Path $Installer "other\KB3033929"
    $patchGlob = if ($a -eq "x64") { "windows6.1-kb3033929-x64*.msu" }
                 else { "windows6.1-kb3033929-x86*.msu" }
    $patch = Get-ChildItem (Join-Path $patchDir $patchGlob) -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($patch) {
        Copy-Item $patch.FullName $DrvDataDir -Force
    } else {
        Write-Warning "No $a KB3033929 .msu found under $patchDir; Win7 SHA-2 patch will be skipped."
    }

    # ---- refresh licenses and installer icon ----
    foreach ($lic in @("license-gpl3.txt", "license-qt.txt", "license-font.txt")) {
        Copy-Item (Join-Path $Installer "other\license\$lic") $AppMetaDir -Force
    }
    Copy-Item (Join-Path $Installer "other\license\license-driver.txt") $DrvMetaDir -Force
    Copy-Item (Join-Path $AppRoot "src\resource\icon\main.ico") `
        (Join-Path $Source "config\druppc_setup.ico") -Force

    # ---- run binarycreator (offline installer) ----
    $Setup = Join-Path $OutDir "IDRConfigurator_Setup_${a}_$stamp.exe"
    & "$QtIFW\bin\binarycreator.exe" --offline-only `
        -c (Join-Path $Source "config\config.xml") `
        -p $Packages `
        $Setup
    if ($LASTEXITCODE -ne 0) { throw "binarycreator failed for $a" }

    Write-Host "OK -> $Setup"
    Write-Host ""
}

Write-Host "All requested installers generated under $OutDir"