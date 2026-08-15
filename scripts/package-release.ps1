#!/usr/bin/env pwsh
#Requires -Version 5.1
# Build the release ZIP from whatever is committed under vendor/ and freshly
# built under build/Release. Never touches the network.
#
#   AssettoCorsaRallyHeadTracking-v{version}-installer.zip  (GitHub, install.cmd)
#   AssettoCorsaRallyHeadTracking-v{version}-nexus.zip      (Nexus, extract to game folder)

[CmdletBinding()]
param(
    # Passed through to Copy-SharedBundle in CI, where the submodule is
    # already synced by the workflow checkout.
    [switch]$NoRefresh
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectDir = Split-Path -Parent $PSScriptRoot
$buildDir   = Join-Path $projectDir 'build/Release'
$releaseDir = Join-Path $projectDir 'release'

$asiName = 'AssettoCorsaRallyHeadTracking.asi'
$asi = Join-Path $buildDir $asiName
if (-not (Test-Path $asi)) { throw "Built .asi not found at $asi. Run 'pixi run build' first." }

Import-Module (Join-Path $projectDir 'cameraunlock-core/powershell/ReleaseWorkflow.psm1') -Force

# pixi.toml is the canonical version; release.ps1 mirrors it into
# CMakeLists.txt, install.cmd and launcher-manifest.json.
$pixiContent = Get-Content -Raw (Join-Path $projectDir 'pixi.toml')
if ($pixiContent -notmatch '(?m)^version\s*=\s*"([0-9]+\.[0-9]+\.[0-9]+)"') {
    throw 'Could not read version from pixi.toml'
}
$version = $Matches[1]

if (Test-Path $releaseDir) { Remove-Item $releaseDir -Recurse -Force }
New-Item -ItemType Directory -Path $releaseDir | Out-Null

$stage = Join-Path $env:TEMP "acr-ht-stage-$([Guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path $stage | Out-Null

# Launcher manifest at the ZIP root, stamped with the real release version.
$manifestPath = Join-Path $projectDir 'launcher-manifest.json'
if (-not (Test-Path $manifestPath)) { throw "launcher-manifest.json not found at $manifestPath" }
$manifest = Get-Content -Raw $manifestPath | ConvertFrom-Json
$manifest.mod_info.version = $version
$manifest | ConvertTo-Json -Depth 10 | Set-Content -Path (Join-Path $stage 'launcher-manifest.json') -Encoding UTF8

$plugins = New-Item -ItemType Directory -Path (Join-Path $stage 'plugins')
Copy-Item -Force $asi (Join-Path $plugins.FullName $asiName)

$vendorSrc = Join-Path $projectDir 'vendor/ultimate-asi-loader'
if (-not (Test-Path (Join-Path $vendorSrc 'dinput8.dll'))) {
    throw "vendor/ultimate-asi-loader/dinput8.dll missing. Run 'pixi run update-deps' and commit."
}
$vendorDst = New-Item -ItemType Directory -Path (Join-Path $stage 'vendor/ultimate-asi-loader') -Force
Copy-Item -Force (Join-Path $vendorSrc '*') $vendorDst.FullName -Recurse

Copy-Item -Force (Join-Path $projectDir 'scripts/install.cmd') $stage
Copy-Item -Force (Join-Path $projectDir 'scripts/uninstall.cmd') $stage
Copy-SharedBundle -StagingDir $stage -NoRefresh:$NoRefresh

foreach ($f in @('README.md', 'LICENSE', 'CHANGELOG.md', 'THIRD-PARTY-NOTICES.md')) {
    Copy-Item -Force (Join-Path $projectDir $f) $stage
}

$installerZip = Join-Path $releaseDir "AssettoCorsaRallyHeadTracking-v$version-installer.zip"
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $installerZip -Force
Write-Host "Built $installerZip" -ForegroundColor Green

Remove-Item $stage -Recurse -Force

# Nexus ZIP: extract-to-game-folder, so the .asi sits at its game-relative
# path. Nexus users manage their own ASI loader - never ship dinput8.dll here.
$nexusStage = Join-Path $env:TEMP "acr-ht-nexus-$([Guid]::NewGuid().ToString('N'))"
$nexusExeDir = New-Item -ItemType Directory -Path (Join-Path $nexusStage 'acr/Binaries/Win64') -Force
Copy-Item -Force $asi (Join-Path $nexusExeDir.FullName $asiName)

# MinHook's BSD-2-Clause requires its notice to travel with binary
# redistributions, so the licence text ships with the bare .asi too - beside
# the .asi, not at the ZIP root. This archive is extracted over the game
# folder, so a root-level file lands in the game's install directory, where
# nothing this mod owns would ever remove it again.
foreach ($doc in @('LICENSE', 'THIRD-PARTY-NOTICES.md')) {
    Copy-Item -Force (Join-Path $projectDir $doc) $nexusExeDir.FullName
}

$nexusZip = Join-Path $releaseDir "AssettoCorsaRallyHeadTracking-v$version-nexus.zip"
Compress-Archive -Path (Join-Path $nexusStage '*') -DestinationPath $nexusZip -Force
Write-Host "Built $nexusZip" -ForegroundColor Green

Remove-Item $nexusStage -Recurse -Force
