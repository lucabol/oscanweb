<#
.SYNOPSIS
    Refresh the HTML fixtures used by tests/test_pages.osc.

.DESCRIPTION
    Downloads each fixture URL with curl, decoding gzip transparently
    (--compressed) so what lands on disk is plain HTML — what
    html_parse consumes today.  Files land under tests/fixtures/.

    The fixtures are intentionally a *frozen snapshot* of each site at
    capture time — that's the whole point of a regression suite.  Run
    this script when you want to refresh them, not on a schedule.

.EXAMPLE
    .\tools\capture_fixtures.ps1
#>

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Out  = Join-Path $Root 'tests\fixtures'
New-Item -ItemType Directory -Path $Out -Force | Out-Null

$UA = 'Mozilla/5.0 (OscaWeb-fixture-capture/1.0)'

$cases = @(
    @{ slug = 'danluu';   url = 'https://danluu.com/' }
    @{ slug = 'hn';       url = 'https://news.ycombinator.com/' }
    @{ slug = 'bmfw';     url = 'http://bettermotherfuckingwebsite.com/' }
    @{ slug = 'rfc1952';  url = 'https://datatracker.ietf.org/doc/html/rfc1952' }
    @{ slug = 'visudet';  url = 'https://www.w3.org/TR/CSS2/visudet.html' }
    @{ slug = 'wikipedia';url = 'https://en.wikipedia.org/wiki/HTML' }
)

foreach ($c in $cases) {
    $dest = Join-Path $Out "$($c.slug).html"
    Write-Host ">> $($c.slug) <- $($c.url)" -ForegroundColor Cyan
    & curl.exe -sL --compressed --max-time 25 -A $UA -o $dest $c.url
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $dest)) {
        Write-Host "   FAILED" -ForegroundColor Red
        continue
    }
    $size = (Get-Item $dest).Length
    Write-Host ("   ok ({0:N0} bytes)" -f $size) -ForegroundColor Green
}

Write-Host ""
Write-Host "Done. Review changes with: git diff --stat tests/fixtures/" -ForegroundColor Yellow
