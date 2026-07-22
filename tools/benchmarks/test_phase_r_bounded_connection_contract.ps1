param(
    [string]$BuildDir = "build_modular_x64",
    [int]$Spp = 4096,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$ExePath = Join-Path $RepoRoot "$BuildDir\tests\gpu\gpu_phase_r_guiding_benchmark.exe"
$ResultDir = Join-Path $RepoRoot "output\benchmarks"

function Get-ImageSums {
    param([string]$Path)
    $bytes = [IO.File]::ReadAllBytes($Path)
    $pixelCount = [BitConverter]::ToInt32($bytes, 0) *
        [BitConverter]::ToInt32($bytes, 4)
    $count = $pixelCount * 3
    if ($pixelCount -le 0 -or $bytes.Length -ne 8 + 4 * $count) {
        throw "invalid bounded-connection framebuffer payload: $Path"
    }
    $sums = @(0.0, 0.0, 0.0)
    for ($index = 0; $index -lt $count; ++$index) {
        $value = [BitConverter]::ToSingle($bytes, 8 + 4 * $index)
        if ([double]::IsNaN($value) -or [double]::IsInfinity($value)) {
            throw "non-finite bounded-connection framebuffer value: $Path"
        }
        $sums[$index % 3] += $value
    }
    $sums
}

if (-not $SkipBuild) {
    & (Join-Path $RepoRoot "scripts\build_x64.ps1") -BuildDir $BuildDir `
        -Config Release -SkipConfigure -Targets gpu_phase_r_guiding_benchmark
    if ($LASTEXITCODE -ne 0) { throw "bounded connection contract build failed" }
}
New-Item -ItemType Directory -Force $ResultDir | Out-Null
$boundedPath = Join-Path $ResultDir "phase_r_bdpt_bounded_contract.bin"
$fullPath = Join-Path $ResultDir "phase_r_bdpt_full_contract.bin"
& $ExePath cornell 6 16 16 $Spp $boundedPath
if ($LASTEXITCODE -ne 0) { throw "bounded BDPT render failed" }
& $ExePath cornell 10 16 16 $Spp $fullPath
if ($LASTEXITCODE -ne 0) { throw "full BDPT render failed" }
$boundedChannels = @(Get-ImageSums $boundedPath)
$fullChannels = @(Get-ImageSums $fullPath)
$bounded = ($boundedChannels | Measure-Object -Sum).Sum
$full = ($fullChannels | Measure-Object -Sum).Sum
$relative = [Math]::Abs($bounded - $full) /
    [Math]::Max([Math]::Abs($full), 1.0e-12)
if ($relative -gt 0.005) {
    throw "bounded BDPT strategy expectation differs from full enumeration: $relative"
}
$channelRelative = @()
for ($channel = 0; $channel -lt 3; ++$channel) {
    $channelRelative += [Math]::Abs(
        $boundedChannels[$channel] - $fullChannels[$channel]) /
        [Math]::Max([Math]::Abs($fullChannels[$channel]), 1.0e-12)
}
if (($channelRelative | Measure-Object -Maximum).Maximum -gt 0.01) {
    throw "bounded BDPT spectral-channel expectation differs from full enumeration: $channelRelative"
}
Write-Host "Phase R bounded connection contract passed: relative=$relative channels=$channelRelative"
