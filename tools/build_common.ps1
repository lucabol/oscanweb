function Test-OscaWebWindows {
    return $env:OS -eq 'Windows_NT'
}

function Test-OscaWebLinux {
    return (Get-Variable IsLinux -ValueOnly -ErrorAction SilentlyContinue) -eq $true
}

function Test-OscaWebMacOS {
    return (Get-Variable IsMacOS -ValueOnly -ErrorAction SilentlyContinue) -eq $true
}

function Get-OscaWebOscanCapabilities {
    $help = & oscan --help 2>&1 | Out-String
    [pscustomobject]@{
        SupportsBackend      = $help -match '--backend\s+c\|native'
        SupportsNativeTarget = $help -match '--native-target'
    }
}

function Get-OscaWebDefaultBackend {
    if (Test-OscaWebWindows -or Test-OscaWebLinux) { return 'native' }
    return 'c'
}

function Get-OscaWebBuildConfig {
    param(
        [ValidateSet('auto', 'native', 'c')]
        [string]$Backend = 'auto',
        [string]$NativeTarget = 'host'
    )

    $capabilities = Get-OscaWebOscanCapabilities
    $resolvedBackend = if ($Backend -eq 'auto') { Get-OscaWebDefaultBackend } else { $Backend }

    if ($resolvedBackend -eq 'native' -and -not ($capabilities.SupportsBackend -and $capabilities.SupportsNativeTarget)) {
        if ($Backend -eq 'auto') {
            Write-Warning 'Installed oscan does not expose --backend/--native-target yet; falling back to the legacy C backend. Install Oscan master/newer than v0.0.33 for native builds.'
            $resolvedBackend = 'c'
        } else {
            throw 'Native backend requested, but installed oscan does not expose --backend/--native-target. Install Oscan master/newer than v0.0.33 with native user-extern str shims, then rerun.'
        }
    }

    if ($resolvedBackend -eq 'c' -and -not $capabilities.SupportsBackend) {
        Write-Host '   oscan has no --backend flag; using the legacy C backend default' -ForegroundColor Yellow
    }

    [pscustomobject]@{
        RequestedBackend      = $Backend
        Backend               = $resolvedBackend
        NativeTarget          = $NativeTarget
        SupportsBackend       = $capabilities.SupportsBackend
        SupportsNativeTarget  = $capabilities.SupportsNativeTarget
        UseHostedLibc         = $resolvedBackend -eq 'native'
    }
}

function Add-OscaWebBackendArgs {
    param(
        [object[]]$InputArgs,
        [pscustomobject]$Config
    )

    $out = @($InputArgs)
    if ($Config.Backend -eq 'native') {
        $out += @('--backend', 'native', '--native-target', $Config.NativeTarget, '--libc')
    } elseif ($Config.SupportsBackend) {
        $out += @('--backend', 'c')
    }
    return $out
}

function Add-OscaWebBridgeArgs {
    param(
        [object[]]$InputArgs,
        [string]$ProjectDir
    )

    $jsBridge = Join-Path $ProjectDir 'js_bridge.c'
    $quickJs = Join-Path (Join-Path $ProjectDir 'libs') (Join-Path 'quickjs' 'quickjs.c')
    $gzipBridge = Join-Path $ProjectDir 'gzip_bridge.c'
    $miniz = Join-Path (Join-Path $ProjectDir 'libs') (Join-Path 'miniz' 'miniz.c')
    $required = @($jsBridge, $quickJs, $gzipBridge, $miniz)

    foreach ($path in $required) {
        if (-not (Test-Path $path)) {
            throw "Required bridge input is missing: $path"
        }
    }

    $out = @($InputArgs)
    $out += @(
        '--extra-c', $jsBridge,
        '--extra-c', $quickJs,
        '--extra-c', $gzipBridge,
        '--extra-c', $miniz,
        '--extra-cflags', "-I$ProjectDir"
    )
    return $out
}

function Add-OscaWebPlatformLinkArgs {
    param(
        [object[]]$InputArgs,
        [pscustomobject]$Config,
        [switch]$WindowsGui
    )

    $out = @($InputArgs)
    if (Test-OscaWebWindows) {
        $out += @('--extra-cflags', '-lwinhttp')
        $out += @('--extra-cflags', '-lws2_32')
        if ($WindowsGui) {
            $out += @('--extra-cflags', '-Wl,--subsystem,windows')
            $out += @('--extra-cflags', '-Wl,--entry,mainCRTStartup')
        }
    } elseif ((Test-OscaWebLinux) -and $Config.Backend -eq 'c') {
        $out += @('--extra-cflags', '-Wl,--allow-multiple-definition')
        $out += @('--extra-cflags', '-lc')
    }
    return $out
}

function Add-OscaWebBuildArgs {
    param(
        [object[]]$InputArgs,
        [pscustomobject]$Config,
        [string]$ProjectDir,
        [switch]$WindowsGui
    )

    $out = Add-OscaWebBackendArgs -InputArgs $InputArgs -Config $Config
    $out = Add-OscaWebBridgeArgs -InputArgs $out -ProjectDir $ProjectDir
    $out = Add-OscaWebPlatformLinkArgs -InputArgs $out -Config $Config -WindowsGui:$WindowsGui
    return $out
}
