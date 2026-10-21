# Builds the DLL, then the setup: dist\FolderTags-Setup-<version>.exe
#   .\scripts\build-installer.ps1 -Version 1.2.0
#   .\scripts\build-installer.ps1 -Version 1.2.0 -SkipBuild   # keep build\FolderTags.dll (e.g. signed)
param([string]$Version = "1.2.0", [switch]$SkipBuild)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent

if (-not $SkipBuild) { & (Join-Path $PSScriptRoot "build.ps1") -Version $Version }
if (-not (Test-Path (Join-Path $root "build\FolderTags.dll"))) { throw "build\FolderTags.dll not found." }

$iscc = @(
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { throw "Inno Setup 6.7+ not found (winget install JRSoftware.InnoSetup)." }

& $iscc "/DAppVersion=$Version" (Join-Path $root "installer\FolderTags.iss")
if ($LASTEXITCODE -ne 0) { throw "ISCC failed ($LASTEXITCODE)" }
Write-Host "OK -> $root\dist\FolderTags-Setup-$Version.exe"
