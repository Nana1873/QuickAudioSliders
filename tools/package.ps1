# Packages the release archive for Nexus from the built DLL and the
# translation files. No .pdb ships. Output: dist\QuickAudioSliders-<version>.zip
# Usage: .\tools\package.ps1 [-Preset release]
[CmdletBinding()]
param(
    [string]$Preset = 'release'
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

$version = (Get-Content (Join-Path $root 'vcpkg.json') -Raw | ConvertFrom-Json).'version-semver'
if (-not $version) { throw "No version-semver in vcpkg.json" }

$dll = Join-Path $root "build\$Preset\QuickAudioSliders.dll"
if (-not (Test-Path $dll)) { throw "DLL not built: $dll (run tools\build.ps1 first)" }

$translations = Join-Path $root 'config\Interface\Translations'
if (-not (Test-Path (Join-Path $translations 'QuickAudioSliders_ENGLISH.txt'))) {
    throw "Translation files missing under $translations"
}

$dist = Join-Path $root 'dist'
$stage = Join-Path $dist 'stage'
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path (Join-Path $stage 'SKSE\Plugins') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $stage 'Interface\Translations') | Out-Null

Copy-Item $dll (Join-Path $stage 'SKSE\Plugins\QuickAudioSliders.dll')
Copy-Item (Join-Path $translations 'QuickAudioSliders_ENGLISH.txt') (Join-Path $stage 'Interface\Translations\')
Copy-Item (Join-Path $translations 'QuickAudioSliders_GERMAN.txt') (Join-Path $stage 'Interface\Translations\')

$archive = Join-Path $dist "QuickAudioSliders-$version.zip"
if (Test-Path $archive) { Remove-Item $archive -Force }
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $archive
Remove-Item $stage -Recurse -Force

Write-Host "[package] OK: $archive"
Get-Item $archive | Select-Object Name, Length
