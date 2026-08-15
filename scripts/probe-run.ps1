#!/usr/bin/env pwsh
#Requires -Version 5.1
# Launch the game unattended, hold it up for a fixed window, kill it, and print
# the mod's log. This is the discovery loop: build -> deploy -> probe -> read.

[CmdletBinding()]
param(
    [int]$Seconds = 120,
    [string]$GamePath,
    # Keep the previous log instead of clearing it first. Off by default so a
    # run's output is unambiguously that run's.
    [switch]$Append
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$scriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir

Import-Module (Join-Path $projectDir 'cameraunlock-core/powershell/GamePathDetection.psm1') -Force

if (-not $GamePath) { $GamePath = Find-GamePath -GameId 'assetto-corsa-rally' }
if (-not $GamePath -or -not (Test-Path $GamePath)) {
    throw "Assetto Corsa Rally not found. Pass -GamePath explicitly."
}

$exeDir = Join-Path $GamePath 'acr/Binaries/Win64'
$exe    = Join-Path $exeDir 'acr.exe'
$log    = Join-Path $exeDir 'AssettoCorsaRallyHeadTracking.log'

if (-not (Test-Path $exe)) { throw "acr.exe not found at $exe" }
if (-not $Append -and (Test-Path $log)) { Remove-Item $log -Force }

# Through Steam rather than the EXE directly: acr.exe is wrapped in Steam DRM
# and a direct launch relaunches itself through the client anyway, which
# detaches the process we would otherwise be holding a handle to.
Write-Host "Launching Assetto Corsa Rally (app 3917090) for $Seconds s..." -ForegroundColor Cyan
Start-Process 'steam://rungameid/3917090'

$deadline = (Get-Date).AddSeconds($Seconds)
$proc = $null
while ((Get-Date) -lt $deadline) {
    if (-not $proc) {
        $proc = Get-Process -Name 'acr' -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($proc) { Write-Host "  acr.exe up (pid $($proc.Id))" -ForegroundColor DarkGray }
    } elseif ($proc.HasExited) {
        Write-Host "  acr.exe exited on its own" -ForegroundColor Yellow
        break
    }
    Start-Sleep -Seconds 2
}

$proc = Get-Process -Name 'acr' -ErrorAction SilentlyContinue
if ($proc) {
    Write-Host "  stopping acr.exe" -ForegroundColor DarkGray
    $proc | Stop-Process -Force
    Start-Sleep -Seconds 2
}

Write-Host ""
if (Test-Path $log) {
    Write-Host "--- $log ---" -ForegroundColor Green
    Get-Content $log
} else {
    Write-Host "No log at $log - the loader did not engage." -ForegroundColor Red
    exit 1
}
