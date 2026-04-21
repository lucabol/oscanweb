<#
.SYNOPSIS
    Build script for OscaWeb browser.

.DESCRIPTION
    Compiles the OscaWeb browser using the Oscan compiler.
    TLS is built-in (SChannel on Windows, BearSSL on Linux).

.EXAMPLE
    .\build.ps1              # Build
    .\build.ps1 -Run         # Build and run
    .\build.ps1 -Clean       # Remove build artifacts
    .\build.ps1 -Test        # Compile and run tests
    .\build.ps1 -V           # Verbose build (show oscan/compiler output)
#>
param(
    [switch]$Run,
    [switch]$Clean,
    [switch]$Test,
    [Alias('V')][switch]$Verbose
)

$ErrorActionPreference = 'Stop'

$ProjectDir = $PSScriptRoot
$BuildDir   = Join-Path $ProjectDir 'build'
$MainSource = Join-Path $ProjectDir 'browser.osc'
$_isWin     = $env:OS -eq 'Windows_NT'
$OutputBin  = Join-Path $BuildDir $(if ($_isWin) { 'browser.exe' } else { 'browser' })

function Write-Step { param([string]$msg) Write-Host ">> $msg" -ForegroundColor Cyan }
function Write-Ok   { param([string]$msg) Write-Host "   OK: $msg" -ForegroundColor Green }
function Write-Fail { param([string]$msg) Write-Host "   ERROR: $msg" -ForegroundColor Red; exit 1 }

# ── Clean ──────────────────────────────────────────────────

if ($Clean) {
    Write-Step 'Cleaning build artifacts'
    if (Test-Path $BuildDir) { Remove-Item $BuildDir -Recurse -Force }
    Write-Ok 'Clean complete'
    exit 0
}

# ── Prerequisite check ────────────────────────────────────

if (-not (Get-Command 'oscan' -ErrorAction SilentlyContinue)) {
    Write-Fail 'oscan not found in PATH. Install from https://github.com/lucabol/Oscan'
}

# ── Test mode ─────────────────────────────────────────────

if ($Test) {
    Write-Step 'Running tests'
    $testFiles = Get-ChildItem -Path (Join-Path $ProjectDir 'tests') -Filter '*.osc' -ErrorAction SilentlyContinue
    if (-not $testFiles -or $testFiles.Count -eq 0) {
        Write-Host '   No test files found in tests/' -ForegroundColor Yellow
        exit 0
    }

    # Extra C flags for tests that need the JS engine
    $JsBridge = Join-Path $ProjectDir 'js_bridge.c'
    $QuickJs  = Join-Path $ProjectDir 'libs' 'quickjs' 'quickjs.c'
    $hasJs    = (Test-Path $JsBridge) -and (Test-Path $QuickJs)

    $failed = 0
    foreach ($tf in $testFiles) {
        Write-Host "   Running $($tf.Name) ... " -NoNewline
        $testArgs = @($tf.FullName, '--run')
        if ($hasJs) {
            $testArgs += @('--extra-c', $JsBridge, '--extra-c', $QuickJs,
                           '--extra-cflags', "-I$ProjectDir")
            if ($_isWin) {
                $testArgs += @('--extra-cflags', '-lwinhttp')
            } elseif ($IsLinux) {
                # Match release.yml: Oscan's freestanding musl toolchain doesn't
                # auto-link libc, but js_bridge.c/quickjs.c need it. Allow
                # duplicate definitions because Oscan's runtime provides a few
                # libc symbols that conflict with musl's libc.a.
                $testArgs += @('--extra-cflags', '-Wl,--allow-multiple-definition',
                               '--extra-cflags', '-lc')
            }
        }
        if ($Verbose) {
            & oscan @testArgs
        } else {
            & oscan @testArgs 2>&1 | Out-Null
        }
        if ($LASTEXITCODE -eq 0) {
            Write-Host 'PASS' -ForegroundColor Green
        } else {
            Write-Host 'FAIL' -ForegroundColor Red
            $failed++
        }
    }
    if ($failed -gt 0) { Write-Fail "$failed test(s) failed" }
    Write-Ok 'All tests passed'
    exit 0
}

# ── Build ─────────────────────────────────────────────────

if (-not (Test-Path $MainSource)) {
    Write-Fail "Main source file not found: $MainSource"
}

if (-not (Test-Path $BuildDir)) {
    New-Item -ItemType Directory -Path $BuildDir | Out-Null
}

Write-Step "Compiling $MainSource"
$oscanArgs = @($MainSource, '-o', $OutputBin)

# Link QuickJS-ng JavaScript engine
$JsBridge = Join-Path $ProjectDir 'js_bridge.c'
$QuickJs  = Join-Path $ProjectDir 'libs' 'quickjs' 'quickjs.c'
if ((Test-Path $JsBridge) -and (Test-Path $QuickJs)) {
    $oscanArgs += @('--extra-c', $JsBridge, '--extra-c', $QuickJs,
                    '--extra-cflags', "-I$ProjectDir",
                    '--extra-cflags', '-lwinhttp')
}

if ($Verbose) { $oscanArgs += @('--warnings', '--verbose') }
if ($Verbose) {
    Write-Host "   oscan $($oscanArgs -join ' ')" -ForegroundColor DarkGray
    & oscan @oscanArgs
} else {
    & oscan @oscanArgs 2>&1 | Out-Null
}
if ($LASTEXITCODE -ne 0) { Write-Fail 'Compilation failed' }
Write-Ok "Built $OutputBin"

# ── Run ───────────────────────────────────────────────────

if ($Run) {
    Write-Step "Running $OutputBin"
    & $OutputBin @args
    exit $LASTEXITCODE
}
