#!/usr/bin/env pwsh
#Requires -Version 5.1
# Dev loop: copy the built .asi and the vendored ASI loader into the game folder.

[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')][string]$Config = 'Release',
    [string]$GamePath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$scriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir

Import-Module (Join-Path $projectDir 'cameraunlock-core/powershell/GamePathDetection.psm1') -Force

if (-not $GamePath) {
    $GamePath = Find-GamePath -GameId 'assetto-corsa-rally'
}
if (-not $GamePath -or -not (Test-Path $GamePath)) {
    throw "Assetto Corsa Rally not found. Pass -GamePath explicitly."
}

# Unlike a game whose EXE sits at the install root, Unreal keeps acr.exe several
# levels down. Both the loader and the .asi have to land beside the EXE - that
# is the directory the DLL search order resolves DINPUT8.dll from - so every
# path below hangs off the EXE directory, not off $GamePath.
$exeDir = Join-Path $GamePath 'acr/Binaries/Win64'
if (-not (Test-Path (Join-Path $exeDir 'acr.exe'))) {
    throw "acr.exe not found under $exeDir. Is this an Assetto Corsa Rally install?"
}

$asi = Join-Path $projectDir "build/$Config/AssettoCorsaRallyHeadTracking.asi"
if (-not (Test-Path $asi)) { throw "Build output not found: $asi. Run 'pixi run build' first." }

$loader = Join-Path $projectDir 'vendor/ultimate-asi-loader/dinput8.dll'
if (-not (Test-Path $loader)) { throw "Vendored ASI loader missing. Run 'pixi run update-deps'." }

Copy-Item $asi (Join-Path $exeDir 'AssettoCorsaRallyHeadTracking.asi') -Force
Write-Host "  deployed AssettoCorsaRallyHeadTracking.asi" -ForegroundColor DarkGray

$loaderTarget = Join-Path $exeDir 'dinput8.dll'
if (-not (Test-Path $loaderTarget)) {
    Copy-Item $loader $loaderTarget -Force
    Write-Host "  deployed dinput8.dll (Ultimate ASI Loader)" -ForegroundColor DarkGray
} else {
    Write-Host "  dinput8.dll already present, left alone" -ForegroundColor DarkGray
}

Write-Host "Deployed to $exeDir" -ForegroundColor Green
