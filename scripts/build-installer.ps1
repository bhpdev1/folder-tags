# Builds the DLL, then the setup: dist\FolderTags-Setup-<version>.exe
#   .\scripts\build-installer.ps1 -Version 1.2.0
#   .\scripts\build-installer.ps1 -Version 1.2.0 -SkipBuild   # keep build\FolderTags.dll (e.g. signed)
# -SignedUninstallerDir <dir> gives unins000.exe the signature of <dir>\uninst-*.e32. The first run
# there only writes that file, unsigned, and exits with code 3: sign it, then run again.
param([string]$Version = "1.2.0", [switch]$SkipBuild, [string]$SignedUninstallerDir)
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

$defines = @("/DAppVersion=$Version")
if ($SignedUninstallerDir) {
    $uninstDir = (New-Item -ItemType Directory -Force $SignedUninstallerDir).FullName
    $defines += "/DSignedUninstallerDir=$uninstDir"
}

# ISCC reports errors on stderr, which Windows PowerShell turns into terminating errors when
# the output is redirected: its exit code decides instead.
$ErrorActionPreference = "Continue"
& $iscc @defines (Join-Path $root "installer\FolderTags.iss")
$ErrorActionPreference = "Stop"
if ($LASTEXITCODE -ne 0) {
    $unsigned = if ($uninstDir) {
        Get-ChildItem $uninstDir -Filter "uninst-*.e32" |
            Where-Object { (Get-AuthenticodeSignature $_.FullName).Status -eq "NotSigned" }
    }
    if ($unsigned) {
        Write-Host "Sign $($unsigned.FullName -join ', '), then run this script again."
        exit 3
    }
    throw "ISCC failed ($LASTEXITCODE)"
}
Write-Host "OK -> $root\dist\FolderTags-Setup-$Version.exe"
