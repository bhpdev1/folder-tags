# Installs (or updates) the shell extension for all users from a source build, then restarts
# Explorer. End users should run the FolderTags setup instead (installer/FolderTags.iss).
# Requires administrator rights (self-elevates once).
$ErrorActionPreference = "Stop"
$fr = (Get-UICulture).TwoLetterISOLanguageName -eq "fr"
function T([string]$en, [string]$frText) { if ($fr) { $frText } else { $en } }

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    $p = Start-Process powershell -Verb RunAs -Wait -PassThru `
        -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`""
    exit $p.ExitCode
}

# FolderTags.dll in the same folder (release bundle) or in the build folder (repo)
$src = Join-Path $PSScriptRoot "FolderTags.dll"
if (-not (Test-Path $src)) { $src = Join-Path (Split-Path $PSScriptRoot -Parent) "build\FolderTags.dll" }
if (-not (Test-Path $src)) {
    & (Join-Path $PSScriptRoot "build.ps1")
}

$dest = Join-Path $env:ProgramFiles "FolderTags"
New-Item -ItemType Directory -Force $dest | Out-Null
$dll = Join-Path $dest "FolderTags.dll"

# Overlay handlers are loaded by every app that shows file icons, so the DLL is often locked.
# A loaded DLL can still be renamed: move it aside, copy the new one, restart Explorer.
Get-ChildItem $dest -Filter "FolderTags.*.old" -ErrorAction SilentlyContinue |
    Remove-Item -Force -ErrorAction SilentlyContinue
if (Test-Path $dll) {
    Move-Item $dll (Join-Path $dest ("FolderTags.{0}.old" -f [DateTime]::Now.Ticks)) -Force
}
Copy-Item $src $dll -Force

# Registers the COM classes, menu handlers, overlays and Tags pane under HKLM, and removes
# the per-user registration of v1.x.
$p = Start-Process "$env:SystemRoot\System32\regsvr32.exe" -ArgumentList "/s `"$dll`"" -Wait -PassThru
if ($p.ExitCode -ne 0) { throw "regsvr32 failed ($($p.ExitCode))" }

# v1.x copy of the DLL (per-user install); the tag data next to it is kept.
$legacy = Join-Path $env:LOCALAPPDATA "FolderTags"
Get-ChildItem $legacy -Filter "FolderTags*.dll" -ErrorAction SilentlyContinue |
    Remove-Item -Force -ErrorAction SilentlyContinue
Get-ChildItem $legacy -Filter "FolderTags.*.old" -ErrorAction SilentlyContinue |
    Remove-Item -Force -ErrorAction SilentlyContinue
Write-Host ((T "FolderTags installed: " "FolderTags installe : ") + $dll)

# Windows restarts the shell by itself (AutoRestartShell), unelevated.
Write-Host (T "Restarting Explorer..." "Redemarrage de l'Explorateur...")
Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 3
if (-not (Get-Process explorer -ErrorAction SilentlyContinue)) { Start-Process explorer.exe }
Write-Host (T "Explorer restarted." "Explorateur redemarre.")
