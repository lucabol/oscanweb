$ErrorActionPreference = 'Stop'

$ProjectDir = Split-Path -Parent $PSScriptRoot
. (Join-Path (Join-Path $ProjectDir 'tools') 'build_common.ps1')

function Assert-Equal {
    param(
        [string]$Name,
        [object]$Expected,
        [object]$Actual
    )

    if ($Expected -ne $Actual) {
        throw "$Name expected '$Expected' but got '$Actual'"
    }
}

function Assert-ContainsSequence {
    param(
        [string]$Name,
        [object[]]$ActualArgs,
        [object[]]$Sequence
    )

    $lastStart = $ActualArgs.Count - $Sequence.Count
    for ($i = 0; $i -le $lastStart; $i++) {
        $matches = $true
        for ($j = 0; $j -lt $Sequence.Count; $j++) {
            if ($ActualArgs[$i + $j] -ne $Sequence[$j]) {
                $matches = $false
                break
            }
        }
        if ($matches) { return }
    }

    throw "$Name did not contain sequence: $($Sequence -join ' ')"
}

function Assert-NotContainsSequence {
    param(
        [string]$Name,
        [object[]]$ActualArgs,
        [object[]]$Sequence
    )

    try {
        Assert-ContainsSequence -Name $Name -ActualArgs $ActualArgs -Sequence $Sequence
    } catch {
        return
    }

    throw "$Name unexpectedly contained sequence: $($Sequence -join ' ')"
}

function New-TestConfig {
    param(
        [ValidateSet('native', 'c')]
        [string]$Backend,
        [bool]$SupportsExtraLib = $true
    )

    [pscustomobject]@{
        Backend          = $Backend
        NativeTarget     = 'host'
        SupportsBackend  = $true
        SupportsExtraLib = $SupportsExtraLib
        UseHostedLibc    = $Backend -eq 'native'
    }
}

$nativeBackend = Add-OscaWebBackendArgs -InputArgs @('input.osc') -Config (New-TestConfig -Backend native)
Assert-ContainsSequence -Name 'Native backend passes hosted native flags' -ActualArgs $nativeBackend -Sequence @('--backend', 'native', '--native-target', 'host', '--libc')

$linuxNative = @(Add-OscaWebPlatformLinkArgs -InputArgs @('input.osc') -Config (New-TestConfig -Backend native) -Platform linux)
Assert-ContainsSequence -Name 'Linux native adds GNU feature macro' -ActualArgs $linuxNative -Sequence @('--extra-cflags', '-D_GNU_SOURCE')
Assert-NotContainsSequence -Name 'Linux native does not add pthread unless a probe requires it' -ActualArgs $linuxNative -Sequence @('--extra-cflags', '-pthread')
Assert-ContainsSequence -Name 'Linux native statically links bundled musl runtime' -ActualArgs $linuxNative -Sequence @('--extra-cflags', '-static')
Assert-NotContainsSequence -Name 'Linux native does not add libc as an explicit library' -ActualArgs $linuxNative -Sequence @('--extra-lib', 'c')

$linuxC = @(Add-OscaWebPlatformLinkArgs -InputArgs @('input.osc') -Config (New-TestConfig -Backend c) -Platform linux)
Assert-ContainsSequence -Name 'Linux C keeps multiple definition linker flag' -ActualArgs $linuxC -Sequence @('--extra-cflags', '-Wl,--allow-multiple-definition')
Assert-ContainsSequence -Name 'Linux C uses extra-lib for libc when supported' -ActualArgs $linuxC -Sequence @('--extra-lib', 'c')

$windowsNative = @(Add-OscaWebPlatformLinkArgs -InputArgs @('input.osc') -Config (New-TestConfig -Backend native) -Platform windows -WindowsGui)
Assert-ContainsSequence -Name 'Windows uses extra-lib for WinHTTP when supported' -ActualArgs $windowsNative -Sequence @('--extra-lib', 'winhttp')
Assert-ContainsSequence -Name 'Windows uses extra-lib for Winsock when supported' -ActualArgs $windowsNative -Sequence @('--extra-lib', 'ws2_32')
Assert-ContainsSequence -Name 'Windows GUI keeps subsystem driver flag' -ActualArgs $windowsNative -Sequence @('--extra-cflags', '-Wl,--subsystem,windows')
Assert-ContainsSequence -Name 'Windows GUI keeps mainCRTStartup driver flag' -ActualArgs $windowsNative -Sequence @('--extra-cflags', '-Wl,--entry,mainCRTStartup')

$windowsLegacy = @(Add-OscaWebPlatformLinkArgs -InputArgs @('input.osc') -Config (New-TestConfig -Backend native -SupportsExtraLib $false) -Platform windows)
Assert-ContainsSequence -Name 'Windows falls back to extra-cflags library syntax for old oscan' -ActualArgs $windowsLegacy -Sequence @('--extra-cflags', '-lwinhttp')

$macNative = @(Add-OscaWebPlatformLinkArgs -InputArgs @('input.osc') -Config (New-TestConfig -Backend c) -Platform macos)
Assert-Equal 'macOS C backend remains unchanged' 1 $macNative.Count
Assert-Equal 'macOS C backend preserves input' 'input.osc' $macNative[0]

Write-Host 'PASS: build_common argument construction'
