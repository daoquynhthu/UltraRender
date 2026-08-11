param(
    [string]$BuildDir = "build_modular_x64",
    [string]$Config = "Release",
    [int]$Width = 32,
    [int]$Height = 32,
    [int[]]$CurveSpp = @(1, 4, 8),
    [int]$ReferenceSpp = 32,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$BuildPath = Join-Path $RepoRoot $BuildDir
$ExePath = Join-Path $BuildPath "artifacts\$Config\bin\gpu_phase_r_guiding_benchmark.exe"
$ResultDir = Join-Path $RepoRoot "output\benchmarks"
$ResultPath = Join-Path $ResultDir "phase_r_restir_pt_suite.json"

function Read-FloatImage {
    param([string]$Path)
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 8) { throw "truncated benchmark image: $Path" }
    $width = [BitConverter]::ToInt32($bytes, 0)
    $height = [BitConverter]::ToInt32($bytes, 4)
    $count = $width * $height * 3
    if ($bytes.Length -ne 8 + 4 * $count) { throw "invalid benchmark image payload: $Path" }
    $values = New-Object 'double[]' $count
    for ($i = 0; $i -lt $count; $i++) { $values[$i] = [BitConverter]::ToSingle($bytes, 8 + 4 * $i) }
    [ordered]@{ width = $width; height = $height; values = $values }
}

function Get-ErrorMetrics {
    param($Image, $Reference)
    if ($Image.width -ne $Reference.width -or $Image.height -ne $Reference.height) { throw "image dimensions differ" }
    $sumError = 0.0
    $sumSigned = 0.0
    $sumReference = 0.0
    $sumDeltaSquared = 0.0
    $squared = New-Object 'double[]' $Image.values.Length
    for ($i = 0; $i -lt $Image.values.Length; $i++) {
        $delta = $Image.values[$i] - $Reference.values[$i]
        $squared[$i] = $delta * $delta
        $sumError += $squared[$i]
        $sumSigned += $delta
        $sumDeltaSquared += $delta * $delta
        $sumReference += [Math]::Abs($Reference.values[$i])
    }
    $mse = $sumError / [double]$squared.Length
    $variance = 0.0
    foreach ($value in $squared) { $variance += ($value - $mse) * ($value - $mse) }
    $count = [double]$Image.values.Length
    $meanDelta = $sumSigned / $count
    $deltaVariance = [Math]::Max(0.0, $sumDeltaSquared / $count - $meanDelta * $meanDelta)
    $relativeScale = [Math]::Max($sumReference / $count, 1.0e-9)
    [ordered]@{
        mse = $mse
        error_variance = $variance / [double]$squared.Length
        relative_mean_bias = [Math]::Abs($sumSigned) / [Math]::Max($sumReference, 1.0e-9)
        relative_bias_95_bound = ([Math]::Abs($meanDelta) + 1.96 * [Math]::Sqrt($deltaVariance / $count)) / $relativeScale
    }
}

function Invoke-Render {
    param([string]$Scene, [int]$Mode, [int]$Spp, [string]$Name)
    $path = Join-Path $ResultDir $Name
    $elapsed = Measure-Command {
        & $ExePath $Scene $Mode $Width $Height $Spp $path
        if ($LASTEXITCODE -ne 0) { throw "ReSTIR PT benchmark failed: $Scene mode=$Mode spp=$Spp" }
    }
    $telemetry = [ordered]@{}
    foreach ($line in Get-Content -LiteralPath ($path + ".telemetry")) {
        $parts = $line -split "=", 2
        if ($parts.Count -eq 2) { $telemetry[$parts[0]] = [int64]$parts[1] }
    }
    [ordered]@{
        path = $path
        elapsed_seconds = [Math]::Round($elapsed.TotalSeconds, 6)
        samples_per_second = [Math]::Round(($Width * $Height * $Spp) / [Math]::Max($elapsed.TotalSeconds, 1.0e-9), 3)
        telemetry = $telemetry
    }
}

function Assert-RejectionBoundary {
    $path = Join-Path $ResultDir "phase_r_restir_pt_disabled_boundary.bin"
    & $ExePath "occlusion" 8 $Width $Height 1 $path
    if ($LASTEXITCODE -eq 0) { throw "ReSTIR PT boundary unexpectedly rendered" }
    $message = Get-Content -Raw -LiteralPath ($path + ".error")
    if ($message -notlike "*restir_pt requires restir_pt.enabled*") {
        throw "unexpected ReSTIR PT boundary: $message"
    }
    [ordered]@{ name = "occlusion"; status = "boundary_passed"; rejection = $message }
}

if (-not $SkipBuild) {
    & (Join-Path $RepoRoot "scripts\build_x64.ps1") -BuildDir $BuildDir -Config $Config -Targets gpu_phase_r_guiding_benchmark
    if ($LASTEXITCODE -ne 0) { throw "ReSTIR PT benchmark build failed" }
}
if (-not (Test-Path $ExePath)) { throw "ReSTIR PT benchmark executable not found: $ExePath" }
if ($CurveSpp.Count -lt 2) { throw "ReSTIR PT suite requires at least two curve points" }
foreach ($spp in $CurveSpp) { if ($spp -le 0 -or $spp -ge $ReferenceSpp) { throw "CurveSpp must be positive and below ReferenceSpp" } }
New-Item -ItemType Directory -Path $ResultDir -Force | Out-Null

$reports = @()
$benefitScenes = 0
foreach ($scene in @("multi_light", "occlusion", "volume")) {
    $referenceRun = Invoke-Render $scene 0 $ReferenceSpp "phase_r_restir_pt_${scene}_reference.bin"
    $reference = Read-FloatImage $referenceRun.path
    $modes = @()
    foreach ($mode in @(
        [ordered]@{ name = "wavefront"; value = 0 },
        [ordered]@{ name = "restir_pt"; value = 2 })) {
        $curve = @()
        foreach ($spp in $CurveSpp) {
            $run = Invoke-Render $scene $mode.value $spp "phase_r_restir_pt_${scene}_$($mode.name)_spp${spp}.bin"
            $metrics = Get-ErrorMetrics (Read-FloatImage $run.path) $reference
            $curve += [ordered]@{
                spp = $spp
                elapsed_seconds = $run.elapsed_seconds
                mse_to_reference = [Math]::Round($metrics.mse, 12)
                error_variance = [Math]::Round($metrics.error_variance, 12)
                relative_mean_bias = [Math]::Round($metrics.relative_mean_bias, 8)
                relative_bias_95_bound = [Math]::Round($metrics.relative_bias_95_bound, 8)
                samples_per_second = $run.samples_per_second
                reservoir_telemetry = $run.telemetry
            }
        }
        if ([double]$curve[-1].mse_to_reference -gt [double]$curve[0].mse_to_reference * 1.10) {
            throw "ReSTIR PT convergence failed: $scene mode=$($mode.name)"
        }
        if ($mode.name -eq "restir_pt" -and [double]$curve[-1].relative_mean_bias -gt 0.20) {
            throw "ReSTIR PT bias bound failed: $scene bias=$($curve[-1].relative_mean_bias)"
        }
        $modes += [ordered]@{ mode = $mode.name; curve = $curve }
    }
    $targetMse = 1.05 * [Math]::Min([double]$modes[0].curve[-1].mse_to_reference,
                                    [double]$modes[1].curve[-1].mse_to_reference)
    foreach ($mode in $modes) {
        $timeToError = $null
        foreach ($point in $mode.curve) {
            if ([double]$point.mse_to_reference -le $targetMse) { $timeToError = $point.elapsed_seconds; break }
        }
        $mode["time_to_error_seconds"] = $timeToError
    }
    if ($null -ne $modes[1].time_to_error_seconds -and
        ($null -eq $modes[0].time_to_error_seconds -or
         $modes[1].time_to_error_seconds -lt $modes[0].time_to_error_seconds)) {
        ++$benefitScenes
    }
    $reports += [ordered]@{
        name = $scene
        reference_spp = $ReferenceSpp
        reference_seconds = $referenceRun.elapsed_seconds
        target_mse = [Math]::Round($targetMse, 12)
        modes = $modes
        status = "passed"
    }
}
$reports += Assert-RejectionBoundary

$report = [ordered]@{
    schema = "ure.phase_r.restir_pt_suite.v1"
    phase = "R-P3"
    suite = "restir_pt_bias_variance_time_to_error"
    status = "passed"
    width = $Width
    height = $Height
    curve_spp = $CurveSpp
    reference_spp = $ReferenceSpp
    benefit_scene_count = $benefitScenes
    boundary_scene_count = 1
    scenes = $reports
}
$report | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $ResultPath -Encoding UTF8
Write-Host "Phase R-P3 ReSTIR PT suite passed"
Write-Host "Wrote $ResultPath"
