<#
.SYNOPSIS
    Launch a Windows GUI build and verify it does not silently exit.

.DESCRIPTION
    Used by CI/release jobs after building the Windows artifact. The script
    starts the executable with --verbose, waits briefly for an OscaWeb window,
    then stops the process. If startup exits early, it prints the deterministic
    %TEMP%\oscanweb.log diagnostic before failing.
#>
param(
    [Parameter(Mandatory = $true)]
    [string]$ExePath,
    [int]$TimeoutSeconds = 5
)

$ErrorActionPreference = 'Stop'

if ($env:OS -ne 'Windows_NT') {
    Write-Host 'startup smoke is Windows-only; skipping'
    exit 0
}

$resolved = Resolve-Path $ExePath
$tempLog = Join-Path $env:TEMP 'oscanweb.log'
if (Test-Path $tempLog) { Remove-Item $tempLog -Force }

$proc = Start-Process -FilePath $resolved.Path -ArgumentList '--verbose' -WorkingDirectory (Split-Path -Parent $resolved.Path) -PassThru
$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
$sawWindow = $false

while ((Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 250
    $proc.Refresh()
    if ($proc.HasExited) { break }
    if ($proc.MainWindowHandle -ne 0 -and $proc.MainWindowTitle -eq 'OscaWeb Browser') {
        $sawWindow = $true
        break
    }
}

$proc.Refresh()
$exitedDuringStartup = $proc.HasExited
if (-not $proc.HasExited) {
    Stop-Process -Id $proc.Id -Force
    $proc.WaitForExit(3000) | Out-Null
}

if ($sawWindow) {
    Write-Host 'PASS: OscaWeb window opened'
    exit 0
}

$diagnostic = if (Test-Path $tempLog) { Get-Content $tempLog -Raw } else { '<missing>' }
if ($exitedDuringStartup) {
    throw "OscaWeb exited during startup (exit code $($proc.ExitCode)). Diagnostic: $diagnostic"
}

throw "OscaWeb did not create a window within $TimeoutSeconds seconds. Diagnostic: $diagnostic"
