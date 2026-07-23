function Test-OscaWebWindows {
    return $env:OS -eq 'Windows_NT'
}

function Test-OscaWebLinux {
    return (Get-Variable IsLinux -ValueOnly -ErrorAction SilentlyContinue) -eq $true
}

function Test-OscaWebMacOS {
    return (Get-Variable IsMacOS -ValueOnly -ErrorAction SilentlyContinue) -eq $true
}

function Get-OscaWebPlatform {
    if (Test-OscaWebWindows) { return 'windows' }
    if (Test-OscaWebLinux) { return 'linux' }
    if (Test-OscaWebMacOS) { return 'macos' }
    return 'other'
}

function Get-OscaWebOscanCapabilities {
    $help = & oscan --help 2>&1 | Out-String
    [pscustomobject]@{
        SupportsBackend      = $help -match '--backend\s+c\|native'
        SupportsNativeTarget = $help -match '--native-target'
        SupportsExtraLib     = $help -match '--extra-lib'
        SupportsAllowElevatedNativeLink = $help -match '--allow-elevated-native-link'
    }
}

function Get-OscaWebDefaultBackend {
    $platform = Get-OscaWebPlatform
    if ($platform -eq 'windows' -or $platform -eq 'linux') { return 'native' }
    return 'c'
}

function Get-OscaWebBuildConfig {
    param(
        [ValidateSet('auto', 'native', 'c')]
        [string]$Backend = 'auto',
        [string]$NativeTarget = 'host',
        [switch]$AllowElevatedNativeLink
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

    if ($AllowElevatedNativeLink -and $resolvedBackend -ne 'native') {
        throw '-AllowElevatedNativeLink is only valid for native backend builds.'
    }
    if ($AllowElevatedNativeLink -and -not $capabilities.SupportsAllowElevatedNativeLink) {
        throw 'Installed oscan does not expose --allow-elevated-native-link. Install Oscan v0.0.37 or newer, then rerun.'
    }

    [pscustomobject]@{
        RequestedBackend      = $Backend
        Backend               = $resolvedBackend
        NativeTarget          = $NativeTarget
        SupportsBackend       = $capabilities.SupportsBackend
        SupportsNativeTarget  = $capabilities.SupportsNativeTarget
        SupportsExtraLib      = $capabilities.SupportsExtraLib
        SupportsAllowElevatedNativeLink = $capabilities.SupportsAllowElevatedNativeLink
        AllowElevatedNativeLink = [bool]$AllowElevatedNativeLink
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
        if ($Config.AllowElevatedNativeLink) {
            $out += @('--allow-elevated-native-link')
        }
    } elseif ($Config.SupportsBackend) {
        $out += @('--backend', 'c')
    }
    return $out
}

function Add-OscaWebExtraLibArg {
    param(
        [object[]]$InputArgs,
        [pscustomobject]$Config,
        [string]$Library
    )

    $out = @($InputArgs)
    if ($Config.SupportsExtraLib) {
        $out += @('--extra-lib', $Library)
    } else {
        $out += @('--extra-cflags', "-l$Library")
    }
    return $out
}

function Add-OscaWebExtraCFlagArg {
    param(
        [object[]]$InputArgs,
        [string]$Flag
    )

    $out = @($InputArgs)
    $out += @('--extra-cflags', $Flag)
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
        [ValidateSet('windows', 'linux', 'macos', 'other')]
        [string]$Platform = (Get-OscaWebPlatform),
        [switch]$WindowsGui
    )

    $out = @($InputArgs)
    if ($Platform -eq 'windows') {
        $out = Add-OscaWebExtraLibArg -InputArgs $out -Config $Config -Library 'winhttp'
        $out = Add-OscaWebExtraLibArg -InputArgs $out -Config $Config -Library 'ws2_32'
        if ($WindowsGui) {
            $out = Add-OscaWebExtraCFlagArg -InputArgs $out -Flag '-Wl,--subsystem,windows'
        }
    } elseif ($Platform -eq 'linux' -and $Config.Backend -eq 'native') {
        $out = Add-OscaWebExtraCFlagArg -InputArgs $out -Flag '-D_GNU_SOURCE'
        $out = Add-OscaWebExtraCFlagArg -InputArgs $out -Flag '-static'
    } elseif ($Platform -eq 'linux' -and $Config.Backend -eq 'c') {
        $out = Add-OscaWebExtraCFlagArg -InputArgs $out -Flag '-Wl,--allow-multiple-definition'
        $out = Add-OscaWebExtraLibArg -InputArgs $out -Config $Config -Library 'c'
    }
    return $out
}

function Add-OscaWebBuildArgs {
    param(
        [object[]]$InputArgs,
        [pscustomobject]$Config,
        [string]$ProjectDir,
        [ValidateSet('windows', 'linux', 'macos', 'other')]
        [string]$Platform = (Get-OscaWebPlatform),
        [switch]$WindowsGui
    )

    $out = Add-OscaWebBackendArgs -InputArgs $InputArgs -Config $Config
    $out = Add-OscaWebBridgeArgs -InputArgs $out -ProjectDir $ProjectDir
    $out = Add-OscaWebPlatformLinkArgs -InputArgs $out -Config $Config -Platform $Platform -WindowsGui:$WindowsGui
    return $out
}
