# Builds FolderTags.dll (x64 Release) with the Visual Studio Build Tools.
param([string]$Config = "Release", [string]$Version = "1.2.0")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw "Visual Studio C++ Build Tools not found." }
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
# Recent vcvars64.bat call vswhere.exe through PATH.
$env:PATH = (Split-Path $vswhere) + ";" + $env:PATH

$build = Join-Path $root "build"
$cmd = "`"$vcvars`" >nul && cmake -S `"$root`" -B `"$build`" -G Ninja -DCMAKE_BUILD_TYPE=$Config -DFOLDERTAGS_VERSION=$Version && cmake --build `"$build`""
cmd /c $cmd
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)" }
Write-Host "OK -> $build\FolderTags.dll"
