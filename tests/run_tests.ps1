# OscaWeb Test Runner
# Finds all test_*.osc files, compiles and runs each with oscan, then reports results.

$ErrorActionPreference = "Continue"

# Check that oscan is available
$oscan = Get-Command oscan -ErrorAction SilentlyContinue
if (-not $oscan) {
    Write-Host "ERROR: 'oscan' not found in PATH." -ForegroundColor Red
    Write-Host "Please ensure the oscan compiler is installed and available in your PATH."
    Write-Host "You can add it with: `$env:PATH += ';C:\path\to\oscan'"
    exit 1
}

$testDir = $PSScriptRoot
if (-not $testDir) { $testDir = Split-Path -Parent $MyInvocation.MyCommand.Path }
$testFiles = Get-ChildItem -Path $testDir -Filter "test_*.osc" -File

if ($testFiles.Count -eq 0) {
    Write-Host "No test files (test_*.osc) found in $testDir" -ForegroundColor Yellow
    exit 0
}

$totalPass = 0
$totalFail = 0
$failedFiles = @()

foreach ($file in $testFiles) {
    Write-Host ""
    Write-Host "--- Running $($file.Name) ---" -ForegroundColor Cyan

    $output = & oscan $file.FullName --run 2>&1 | Out-String

    if ($LASTEXITCODE -ne 0) {
        Write-Host "ERROR: oscan failed for $($file.Name)" -ForegroundColor Red
        Write-Host $output
        $failedFiles += $file.Name
        continue
    }

    Write-Host $output

    $lines = $output -split "`n"
    $pass = ($lines | Where-Object { $_ -match "^PASS:" }).Count
    $fail = ($lines | Where-Object { $_ -match "^FAIL:" }).Count

    $totalPass += $pass
    $totalFail += $fail

    if ($fail -gt 0) {
        $failedFiles += $file.Name
    }
}

Write-Host ""
Write-Host "========================================" -ForegroundColor White
Write-Host "  Total PASS: $totalPass" -ForegroundColor Green
Write-Host "  Total FAIL: $totalFail" -ForegroundColor $(if ($totalFail -gt 0) { "Red" } else { "Green" })

if ($failedFiles.Count -gt 0) {
    Write-Host "  Failed in: $($failedFiles -join ', ')" -ForegroundColor Red
}

Write-Host "========================================" -ForegroundColor White

if ($totalFail -gt 0 -or $failedFiles.Count -gt 0) {
    exit 1
} else {
    exit 0
}
