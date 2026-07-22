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
    .\build.ps1 -Backend c   # Deliberate C backend fallback
    .\build.ps1 -V           # Verbose build (show oscan/compiler output)
#>
param(
    [switch]$Run,
    [switch]$Clean,
    [switch]$Test,
    [ValidateSet('auto', 'native', 'c')]
    [string]$Backend = 'auto',
    [string]$NativeTarget = 'host',
    [string]$Output,
    [switch]$WindowsGui,
    [switch]$AllowElevatedNativeLink,
    [Alias('V')][switch]$Verbose
)

$ErrorActionPreference = 'Stop'

$ProjectDir = $PSScriptRoot
$BuildDir   = Join-Path $ProjectDir 'build'
$MainSource = Join-Path $ProjectDir 'browser.osc'
$_isWin     = $env:OS -eq 'Windows_NT'
$OutputBin  = if ($Output) { $Output } else { Join-Path $BuildDir $(if ($_isWin) { 'browser.exe' } else { 'browser' }) }
. (Join-Path (Join-Path $ProjectDir 'tools') 'build_common.ps1')

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

$BuildConfig = Get-OscaWebBuildConfig -Backend $Backend -NativeTarget $NativeTarget -AllowElevatedNativeLink:$AllowElevatedNativeLink
Write-Step "Using $($BuildConfig.Backend) backend"
if ($BuildConfig.Backend -eq 'native') {
    Write-Host "   Native target: $($BuildConfig.NativeTarget), runtime: hosted libc" -ForegroundColor DarkGray
}

# ── Test mode ─────────────────────────────────────────────

if ($Test) {
    Write-Step 'Running tests'
    $testFiles = Get-ChildItem -Path (Join-Path $ProjectDir 'tests') -Filter '*.osc' -ErrorAction SilentlyContinue
    if (-not $testFiles -or $testFiles.Count -eq 0) {
        Write-Host '   No test files found in tests/' -ForegroundColor Yellow
        exit 0
    }

    # Compile tests to a stable folder (build\tests\*.exe) and then execute
    # them as separate steps. Using `oscan --run` compiles to an unpredictable
    # %TEMP% path, which means antivirus scanners (Windows Defender, etc.)
    # see a brand-new executable every invocation and stop to scan it — this
    # made a full test run take 40+ minutes on some Windows machines. With a
    # fixed output folder you can add ONE exclusion rule for
    # <repo>\build\tests\ and get fast, predictable runs.
    $TestBuildDir = Join-Path $BuildDir 'tests'
    New-Item -ItemType Directory -Path $TestBuildDir -Force | Out-Null

    $failed = 0
    foreach ($tf in $testFiles) {
        # The legacy C backend on Linux still links QuickJS through the
        # freestanding musl path, which is known to crash. Native builds use
        # --libc hosted mode and should run this coverage.
        if ($IsLinux -and $BuildConfig.Backend -eq 'c' -and $tf.Name -eq 'test_js.osc') {
            Write-Host "   Skipping $($tf.Name) on Linux C backend (known freestanding QuickJS issue)" -ForegroundColor Yellow
            continue
        }
        Write-Host "   Running $($tf.Name) ... " -NoNewline
        $testExeName = if ($_isWin) { $tf.BaseName + '.exe' } else { $tf.BaseName }
        $testExe = Join-Path $TestBuildDir $testExeName
        $testArgs = @($tf.FullName, '-o', $testExe)
        $testArgs = Add-OscaWebBuildArgs -InputArgs $testArgs -Config $BuildConfig -ProjectDir $ProjectDir
        # Compile only (no --run) so the binary lands in $TestBuildDir.
        if ($Verbose) {
            & oscan @testArgs
        } else {
            & oscan @testArgs 2>&1 | Out-Null
        }
        if ($LASTEXITCODE -ne 0) {
            Write-Host 'FAIL (compile)' -ForegroundColor Red
            $failed++
            continue
        }
        # Execute the compiled test binary.
        if ($Verbose) {
            & $testExe
        } else {
            & $testExe 2>&1 | Out-Null
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
$oscanArgs = Add-OscaWebBuildArgs -InputArgs $oscanArgs -Config $BuildConfig -ProjectDir $ProjectDir -WindowsGui:$WindowsGui

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
