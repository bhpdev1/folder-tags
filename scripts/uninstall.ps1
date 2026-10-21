# Removes the shell extension installed by install.ps1 (tags already set on folders are kept).
# Requires administrator rights (self-elevates once).
$ErrorActionPreference = "Stop"
$fr = (Get-UICulture).TwoLetterISOLanguageName -eq "fr"

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    $p = Start-Process powershell -Verb RunAs -Wait -PassThru `
        -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`""
    exit $p.ExitCode
}

# Current install (HKLM), then a leftover v1.x per-user install.
foreach ($dll in (Join-Path $env:ProgramFiles "FolderTags\FolderTags.dll"),
                 (Join-Path $env:LOCALAPPDATA "FolderTags\FolderTags.dll")) {
    if (Test-Path $dll) {
        Start-Process "$env:SystemRoot\System32\regsvr32.exe" -ArgumentList "/s /u `"$dll`"" -Wait
    }
}

Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 3
Remove-Item (Join-Path $env:ProgramFiles "FolderTags") -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $env:LOCALAPPDATA "FolderTags") -Recurse -Force -ErrorAction SilentlyContinue
if (-not (Get-Process explorer -ErrorAction SilentlyContinue)) { Start-Process explorer.exe }
Write-Host $(if ($fr) { "FolderTags desinstalle." } else { "FolderTags uninstalled." })
