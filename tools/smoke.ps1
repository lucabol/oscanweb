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
param(
    [ValidateSet('auto', 'native', 'c')]
    [string]$Backend = 'auto',
    [string]$NativeTarget = 'host',
    [switch]$AllowElevatedNativeLink,
    [switch]$CompileOnly
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
. (Join-Path (Join-Path $Root 'tools') 'build_common.ps1')
Push-Location $Root
try {
    $isWin = Test-OscaWebWindows
    $Out = Join-Path $Root $(if ($isWin) { 'build\smoke.exe' } else { 'build/smoke' })
    New-Item -ItemType Directory -Path (Split-Path $Out) -Force | Out-Null

    $config = Get-OscaWebBuildConfig -Backend $Backend -NativeTarget $NativeTarget -AllowElevatedNativeLink:$AllowElevatedNativeLink
    $smokeArgs = @((Join-Path (Join-Path $Root 'tools') 'smoke_urls.osc'), '-o', $Out)
    $smokeArgs = Add-OscaWebBuildArgs -InputArgs $smokeArgs -Config $config -ProjectDir $Root

    Write-Host '>> Compiling smoke runner' -ForegroundColor Cyan
    & oscan @smokeArgs
    if ($LASTEXITCODE -ne 0) { Write-Host 'compile failed' -ForegroundColor Red; exit 1 }

    if ($CompileOnly) { exit 0 }

    Write-Host '>> Running' -ForegroundColor Cyan
    & $Out
    exit $LASTEXITCODE
} finally {
    Pop-Location
}
