<#
.SYNOPSIS
    Compile and run the live URL smoke runner.

.DESCRIPTION
    Hits the public internet — flaky by design.  Use to verify
    end-to-end (HTTP fetch + gzip decode + parse + link extraction)
    against the live versions of our manual-test URLs.

    Not part of `build.ps1 -Test` and not run by CI.  Refresh
    fixtures with `tools/capture_fixtures.ps1` to lock down a
    deterministic offline regression instead.

.EXAMPLE
    .\tools\smoke.ps1
#>

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
Push-Location $Root
try {
    $Out = Join-Path $Root 'build\smoke.exe'
    New-Item -ItemType Directory -Path (Split-Path $Out) -Force | Out-Null

    $args = @(
        'tools/smoke_urls.osc', '-o', $Out,
        '--extra-c', 'js_bridge.c',
        '--extra-c', 'libs/quickjs/quickjs.c',
        '--extra-c', 'gzip_bridge.c',
        '--extra-c', 'libs/miniz/miniz.c',
        '--extra-cflags', "-I$Root"
    )
    if ($env:OS -eq 'Windows_NT') {
        $args += @('--extra-cflags', '-lwinhttp')
    }

    Write-Host '>> Compiling smoke runner' -ForegroundColor Cyan
    & oscan @args
    if ($LASTEXITCODE -ne 0) { Write-Host 'compile failed' -ForegroundColor Red; exit 1 }

    Write-Host '>> Running' -ForegroundColor Cyan
    & $Out
    exit $LASTEXITCODE
} finally {
    Pop-Location
}
