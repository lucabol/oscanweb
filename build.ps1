<#
.SYNOPSIS
    Build script for OscaWeb browser.

.DESCRIPTION
    Transpiles Oscan source to C and compiles with tls_wrapper.c for HTTPS
    support (SChannel on Windows, OpenSSL on Linux).

.EXAMPLE
    .\build.ps1              # Build
    .\build.ps1 -Run         # Build and run
    .\build.ps1 -Clean       # Remove build artifacts
    .\build.ps1 -Test        # Compile and run tests
#>
param(
    [switch]$Run,
    [switch]$Clean,
    [switch]$Test
)

$ErrorActionPreference = 'Stop'

$ProjectDir  = $PSScriptRoot
$BuildDir    = Join-Path $ProjectDir 'build'
$MainSource  = Join-Path $ProjectDir 'browser.osc'
$GenC        = Join-Path $BuildDir   'browser.c'
$TlsWrapper  = Join-Path $ProjectDir 'tls_wrapper.c'
$_isWin = ($env:OS -eq 'Windows_NT') -or ($PSVersionTable.PSEdition -ne 'Core' -and -not $IsLinux -and -not $IsMacOS)

if ($_isWin) {
    $OutputBin = Join-Path $BuildDir 'browser.exe'
} else {
    $OutputBin = Join-Path $BuildDir 'browser'
}

# ── Helpers ────────────────────────────────────────────────

function Write-Step  { param([string]$msg) Write-Host ">> $msg" -ForegroundColor Cyan }
function Write-Ok    { param([string]$msg) Write-Host "   OK: $msg" -ForegroundColor Green }
function Write-Fail  { param([string]$msg) Write-Host "   ERROR: $msg" -ForegroundColor Red; exit 1 }

function Test-Command { param([string]$name) $null -ne (Get-Command $name -ErrorAction SilentlyContinue) }

function Find-CCompiler {
    # Prefer the Oscan bundled clang first
    $oscanCmd = Get-Command oscan -ErrorAction SilentlyContinue
    if ($oscanCmd) {
        $oscanDir = Split-Path $oscanCmd.Source -Parent
        $bundledClang = Join-Path $oscanDir 'toolchain' 'bin' 'clang.exe'
        if (Test-Path $bundledClang) { return $bundledClang }
        $bundledClang = Join-Path $oscanDir 'toolchain' 'windows' 'bin' 'clang.exe'
        if (Test-Path $bundledClang) { return $bundledClang }
    }
    # Then system compilers
    foreach ($cc in @('clang', 'gcc', 'cl')) {
        if (Test-Command $cc) { return $cc }
    }
    return $null
}

# ── Clean ──────────────────────────────────────────────────

if ($Clean) {
    Write-Step 'Cleaning build artifacts'
    if (Test-Path $BuildDir) { Remove-Item $BuildDir -Recurse -Force }
    Write-Ok 'Clean complete'
    exit 0
}

# ── Prerequisite checks ───────────────────────────────────

if (-not (Test-Command 'oscan')) {
    Write-Fail 'oscan not found in PATH. Install from https://github.com/lucabol/Oscan'
}

$CC = Find-CCompiler
if (-not $CC) {
    Write-Fail 'No C compiler found. Install clang, gcc, or MSVC (cl.exe).'
}
Write-Ok "C compiler: $CC"

if (-not (Test-Path $TlsWrapper)) {
    Write-Fail "tls_wrapper.c not found at $TlsWrapper"
}
if ($_isWin) {
    Write-Ok 'TLS: using built-in SChannel (no external deps)'
} else {
    # Linux/macOS needs OpenSSL installed
    $tempC = Join-Path $env:TEMP 'oscanweb_ssl_check.c'
    Set-Content -Path $tempC -Value '#include <openssl/ssl.h>' -Encoding UTF8
    & $CC -E $tempC 2>&1 | Out-Null
    $sslOk = $LASTEXITCODE -eq 0
    Remove-Item $tempC -ErrorAction SilentlyContinue
    if (-not $sslOk) {
        Write-Fail 'OpenSSL development headers not found. Install OpenSSL and ensure headers are in the include path.'
    }
    Write-Ok 'TLS: using OpenSSL'
}

# ── Ensure build directory ─────────────────────────────────

if (-not (Test-Path $BuildDir)) {
    New-Item -ItemType Directory -Path $BuildDir | Out-Null
}

# ── Test mode ──────────────────────────────────────────────

if ($Test) {
    Write-Step 'Running tests'
    $testFiles = Get-ChildItem -Path (Join-Path $ProjectDir 'tests') -Filter '*.osc' -ErrorAction SilentlyContinue
    if (-not $testFiles -or $testFiles.Count -eq 0) {
        Write-Host '   No test files found in tests/' -ForegroundColor Yellow
        exit 0
    }
    $failed = 0
    foreach ($tf in $testFiles) {
        Write-Host "   Running $($tf.Name) ... " -NoNewline
        & oscan $tf.FullName --run 2>&1 | Out-Null
        if ($LASTEXITCODE -eq 0) {
            Write-Host 'PASS' -ForegroundColor Green
        } else {
            Write-Host 'FAIL' -ForegroundColor Red
            $failed++
        }
    }
    if ($failed -gt 0) {
        Write-Fail "$failed test(s) failed"
    }
    Write-Ok 'All tests passed'
    exit 0
}

# ── Build ──────────────────────────────────────────────────

if (-not (Test-Path $MainSource)) {
    Write-Fail "Main source file not found: $MainSource"
}

if (-not (Test-Path $BuildDir)) {
    New-Item -ItemType Directory -Path $BuildDir | Out-Null
}

# Step 1: Transpile Oscan -> C
Write-Step "Transpiling $MainSource -> $GenC"
& oscan $MainSource -o $GenC
if ($LASTEXITCODE -ne 0) { Write-Fail 'oscan transpilation failed' }
Write-Ok 'Transpilation complete'

# Step 2: Extract runtime files from oscan's temp dir
# Compile a trivial program so oscan creates temp dir with runtime
$dummyOsc = Join-Path $BuildDir 'dummy.osc'
Set-Content -Path $dummyOsc -Value 'fn! main() { }' -Encoding UTF8
& oscan $dummyOsc -o (Join-Path $BuildDir 'dummy.exe') 2>&1 | Out-Null
Remove-Item $dummyOsc -ErrorAction SilentlyContinue
Remove-Item (Join-Path $BuildDir 'dummy.exe') -ErrorAction SilentlyContinue

# Find the most recent oscan temp dir and copy ALL runtime files
$tempDirs = Get-ChildItem $env:TEMP -Directory -Filter 'oscan_temp*' -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending
$runtimeCopied = $false
foreach ($td in $tempDirs) {
    $rtH = Join-Path $td.FullName 'osc_runtime.h'
    if (Test-Path $rtH) {
        # Copy all .h files (runtime headers)
        Get-ChildItem $td.FullName -Filter '*.h' | Copy-Item -Destination $BuildDir -Force
        # Copy all .c files except the generated program.c
        Get-ChildItem $td.FullName -Filter '*.c' | Where-Object { $_.Name -ne 'program.c' } |
            Copy-Item -Destination $BuildDir -Force
        $runtimeCopied = $true
        $count = (Get-ChildItem $BuildDir -Include '*.h','*.c' -File).Count
        Write-Ok "Runtime files copied to build/ ($count files)"
        break
    }
}
if (-not $runtimeCopied) {
    Write-Fail 'Could not find Oscan runtime files. Make sure oscan is properly installed.'
}

# Step 3: Compile with TLS wrapper
Write-Step 'Building with TLS support'
$cSources = @($GenC, $TlsWrapper)
$cFlags   = @('-o', $OutputBin, "-I$BuildDir", '-nostdlib', '-lgdi32', '-luser32', '-lkernel32')
if ($_isWin) {
    $cFlags += @('-lws2_32', '-lsecur32', '-lcrypt32')
} else {
    $cFlags += @('-lssl', '-lcrypto')
}
# Link the clang builtins library (provides __chkstk_ms on Windows)
$oscanCmd = Get-Command oscan -ErrorAction SilentlyContinue
if ($oscanCmd) {
    $oscanDir = Split-Path $oscanCmd.Source -Parent
    $builtinsLib = Join-Path $oscanDir 'toolchain' 'lib' 'clang' '22' 'lib' 'windows' 'libclang_rt.builtins-x86_64.a'
    if (Test-Path $builtinsLib) { $cFlags += @($builtinsLib) }
}

if ($CC -eq 'gcc' -or $CC -eq 'clang') {
    $cFlags += @('-lm')
}

# Step 4: Patch generated C
# The Oscan compiler doesn't generate C forward-declarations for extern FFI
# functions, and may not include l_tls.h. We fix both after the preprocessor block.
$patchCode = @"

/* Include TLS header (needed by osc_runtime.c TLS builtins) */
#include "l_tls.h"

/* Forward declarations for TLS FFI functions */
extern int32_t tls_init(void);
extern int32_t tls_connect_to(osc_str host, int32_t port);
extern int32_t tls_send_bytes(int32_t handle, osc_str data, int32_t data_len);
extern int32_t tls_recv_byte(int32_t handle);
extern void    tls_close_conn(int32_t handle);
extern void    tls_cleanup(void);

"@
$genContent = Get-Content $GenC -Raw
# Insert l_tls.h include right before osc_runtime.c include
$genContent = $genContent -replace '(#include "osc_runtime\.c")', "#include `"l_tls.h`"`n`$1"
# Insert extern declarations after the last #endif before code
$genLines = $genContent -split "`n"
$lastEndif = -1
for ($li = 0; $li -lt $genLines.Count; $li++) {
    if ($genLines[$li] -match '^\s*#endif') { $lastEndif = $li }
    if ($genLines[$li] -match '^\s*typedef\s') { break }
}
if ($lastEndif -ge 0) {
    $fwdDecls = @"

/* Forward declarations for TLS FFI functions */
extern int32_t tls_init(void);
extern int32_t tls_connect_to(osc_str host, int32_t port);
extern int32_t tls_send_bytes(int32_t handle, osc_str data, int32_t data_len);
extern int32_t tls_recv_byte(int32_t handle);
extern void    tls_close_conn(int32_t handle);
extern void    tls_cleanup(void);

"@
    $before = $genLines[0..$lastEndif]
    $after  = $genLines[($lastEndif+1)..($genLines.Count-1)]
    $genContent = ($before -join "`n") + "`n" + $fwdDecls + "`n" + ($after -join "`n")
}
Set-Content -Path $GenC -Value $genContent -NoNewline -Encoding UTF8

Write-Step "Compiling with $CC"
$compileArgs = $cSources + $cFlags
Write-Host "   $CC $($compileArgs -join ' ')" -ForegroundColor DarkGray
& $CC @compileArgs
if ($LASTEXITCODE -ne 0) { Write-Fail 'C compilation failed' }
Write-Ok "Built $OutputBin"

# ── Run ────────────────────────────────────────────────────

if ($Run) {
    Write-Step "Running $OutputBin"
    & $OutputBin
    exit $LASTEXITCODE
}
