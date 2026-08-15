#!/usr/bin/env pwsh
#Requires -Version 5.1
# Bump vendored Ultimate ASI Loader (dinput8.dll) to the latest upstream
# within the pinned range. Manual; commit the result. CI never refreshes.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue'

$scriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir

$module = Join-Path $projectDir 'cameraunlock-core/powershell/ModLoaderSetup.psm1'
if (-not (Test-Path $module)) {
    throw "ModLoaderSetup.psm1 not found at $module. Run 'git submodule update --init --recursive'."
}
Import-Module $module -Force

$vendorAsiDir = Join-Path $projectDir 'vendor/ultimate-asi-loader'
$vendorAsiDll = Join-Path $vendorAsiDir 'dinput8.dll'
if (-not (Test-Path $vendorAsiDir)) {
    New-Item -ItemType Directory -Path $vendorAsiDir -Force | Out-Null
}

$tempZip = Join-Path $env:TEMP ("asi-update-" + [IO.Path]::GetRandomFileName() + ".zip")
$tempDll = Join-Path $env:TEMP ("asi-update-" + [IO.Path]::GetRandomFileName() + ".dll")
try {
    Write-Host "Refreshing vendor/ultimate-asi-loader from upstream..." -ForegroundColor Cyan
    $meta = Invoke-FetchLatestLoader `
        -OutputPath $tempZip `
        -Owner 'ThirteenAG' -Repo 'Ultimate-ASI-Loader' `
        -VersionPrefix 'v9.' `
        -AssetPattern '^Ultimate-ASI-Loader_x64\.zip$'

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($tempZip)
    try {
        $dllEntry = $zip.Entries | Where-Object { $_.Name -eq 'dinput8.dll' } | Select-Object -First 1
        if (-not $dllEntry) { throw "Upstream zip $($meta.AssetName) does not contain dinput8.dll." }
        $out = [System.IO.File]::Create($tempDll)
        try { $in = $dllEntry.Open(); try { $in.CopyTo($out) } finally { $in.Dispose() } } finally { $out.Dispose() }

        $dllSha = (Get-FileHash -Path $tempDll -Algorithm SHA256).Hash.ToLower()

        # Upstream unchanged: leave the tree alone. Without this the README's
        # FetchedAt line churns on every run, so a routine no-op refresh looks
        # like a loader bump in the diff.
        $readmePath  = Join-Path $vendorAsiDir 'README.md'
        $licensePath = Join-Path $vendorAsiDir 'LICENSE'
        if ((Test-Path $vendorAsiDll) -and (Test-Path $readmePath) -and (Test-Path $licensePath) -and
            (Get-FileHash -Path $vendorAsiDll -Algorithm SHA256).Hash.ToLower() -eq $dllSha) {
            Write-Host "  no change (tag=$($meta.Tag) sha256=$($dllSha.Substring(0,12))... matches vendored copy)" -ForegroundColor DarkGray
            return
        }

        Move-Item -Path $tempDll -Destination $vendorAsiDll -Force

        $licenseEntry = $zip.Entries | Where-Object { $_.Name -match '^(license|LICENSE)(\..+)?$' -and $_.FullName -notmatch '/.+/' } | Select-Object -First 1
        if ($licenseEntry) {
            $out = [System.IO.File]::Create($licensePath)
            try { $in = $licenseEntry.Open(); try { $in.CopyTo($out) } finally { $in.Dispose() } } finally { $out.Dispose() }
        }
    } finally { $zip.Dispose() }

    if (-not (Test-Path (Join-Path $vendorAsiDir 'LICENSE'))) {
        $licenseUrl = "https://raw.githubusercontent.com/ThirteenAG/Ultimate-ASI-Loader/$($meta.Tag)/license"
        Invoke-WebRequest -Uri $licenseUrl -OutFile (Join-Path $vendorAsiDir 'LICENSE') -UseBasicParsing -TimeoutSec 30 -Headers @{ "User-Agent" = "CameraUnlock-HeadTracking" }
    }

    $readme = @(
        '# Ultimate ASI Loader (vendored)',
        '',
        'Bundled copy of Ultimate ASI Loader, the install-time source of truth.',
        'Refresh manually with `pixi run update-deps`, then commit.',
        '',
        '## Snapshot',
        '',
        '- Upstream: https://github.com/ThirteenAG/Ultimate-ASI-Loader',
        "- Tag: ``$($meta.Tag)``",
        "- Commit: ``$($meta.CommitSha)``",
        "- Asset: ``$($meta.AssetName)``",
        "- dinput8.dll SHA-256: ``$dllSha``",
        "- Fetched at: $($meta.FetchedAt)",
        '',
        '`dinput8.dll` is extracted from the upstream asset untouched. It deploys to the game',
        'EXE directory (`acr/Binaries/Win64`) as `dinput8.dll`: acr.exe statically imports DINPUT8.dll, and the',
        'safe DLL search order resolves the application directory before System32, so the',
        'loader proxies that import without a rename.'
    ) -join "`n"
    Set-Content -Path (Join-Path $vendorAsiDir 'README.md') -Value $readme -Encoding UTF8

    # install.cmd records the loader version in .headtracking-state.json, so it
    # is stamped from the same fetch rather than maintained by hand. Left to a
    # human it would silently keep reporting the previous version into every
    # user's state file after the next bump.
    $installCmdPath = Join-Path $projectDir 'scripts/install.cmd'
    $loaderVersion = $meta.Tag -replace '^v', ''
    $installContent = Get-Content $installCmdPath -Raw
    if ($installContent -notmatch '(?m)^set "ASI_LOADER_VERSION=') {
        throw "No ASI_LOADER_VERSION line found in $installCmdPath"
    }
    # -Raw plus a targeted regex keeps the file's CRLF endings, which a .cmd
    # silently fails without.
    $installContent = $installContent -replace '(?m)^(set "ASI_LOADER_VERSION=)[^"]*(")', "`${1}$loaderVersion`${2}"
    Set-Content -Path $installCmdPath -Value $installContent -NoNewline

    # THIRD-PARTY-NOTICES carries the same version for the licence audit.
    # Anchored to the Ultimate ASI Loader heading: every component in that file
    # has a "- **Version:** vX.Y.Z (commit `...`)" line in the same shape, so an
    # unanchored replace would stamp the loader's version over MinHook's too.
    $noticesPath = Join-Path $projectDir 'THIRD-PARTY-NOTICES.md'
    $notices = Get-Content $noticesPath -Raw
    $noticesPattern = '(?s)(## Ultimate ASI Loader.*?- \*\*Version:\*\* )v[0-9][^\s]*( \(commit `)[0-9a-f]+(`\))'
    if ($notices -notmatch $noticesPattern) {
        throw "No Ultimate ASI Loader version line found in $noticesPath"
    }
    $notices = $notices -replace $noticesPattern, "`${1}$($meta.Tag)`${2}$($meta.CommitSha)`${3}"
    Set-Content -Path $noticesPath -Value $notices -NoNewline

    Write-Host "  tag=$($meta.Tag) sha256=$($dllSha.Substring(0,12))..." -ForegroundColor DarkGray
} finally {
    Remove-Item $tempZip -Force -ErrorAction SilentlyContinue
    Remove-Item $tempDll -Force -ErrorAction SilentlyContinue
}

Write-Host ""
Write-Host "vendor/ultimate-asi-loader refreshed. Review and commit." -ForegroundColor Green
