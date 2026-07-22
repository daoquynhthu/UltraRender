param(
    [string]$BuildDir = "build_modular_x64",
    [string]$Config = "Release",
    [int]$Width = 16,
    [int]$Height = 16,
    [int[]]$CurveSpp = @(64, 256, 1024),
    [int]$ReferenceSpp = 8192,
    [double]$TargetNormalizedMse = 0.05,
    [int]$MinBenefitScenes = 2,
    [string[]]$Scenes = @(
        "sds", "sds_small_light", "small_emitter", "glass_caustic",
        "high_occlusion"),
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$BuildPath = Join-Path $RepoRoot $BuildDir
$ExePath = Join-Path $BuildPath "tests\gpu\gpu_phase_r_guiding_benchmark.exe"
$ResultDir = Join-Path $RepoRoot "output\benchmarks"
$ResultPath = Join-Path $ResultDir "phase_r_mlt_suite.json"

function Read-FloatImage {
    param([string]$Path)
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 8) { throw "truncated MLT benchmark image: $Path" }
    $width = [BitConverter]::ToInt32($bytes, 0)
    $height = [BitConverter]::ToInt32($bytes, 4)
    $count = $width * $height * 3
    if ($bytes.Length -ne 8 + 4 * $count) {
        throw "invalid MLT benchmark image: $Path"
    }
    $values = New-Object 'double[]' $count
    for ($index = 0; $index -lt $count; $index++) {
        $values[$index] = [BitConverter]::ToSingle($bytes, 8 + 4 * $index)
    }
    [ordered]@{ width = $width; height = $height; values = $values }
}

function Get-ErrorMetrics {
    param($Image, $Reference)
    $sumSquared = 0.0
    $sumDelta = 0.0
    $sumReference = 0.0
    $sumReferenceSquared = 0.0
    for ($index = 0; $index -lt $Image.values.Length; $index++) {
        $delta = $Image.values[$index] - $Reference.values[$index]
        $sumSquared += $delta * $delta
        $sumDelta += $delta
        $sumReference += [Math]::Abs($Reference.values[$index])
        $sumReferenceSquared +=
            $Reference.values[$index] * $Reference.values[$index]
    }
    $count = [double]$Image.values.Length
    $meanDelta = $sumDelta / $count
    $mse = $sumSquared / $count
    $variance = [Math]::Max(0.0, $mse - $meanDelta * $meanDelta)
    $scale = [Math]::Max($sumReference / $count, 1.0e-12)
    [ordered]@{
        mse = $mse
        normalized_mse = $mse /
            [Math]::Max($sumReferenceSquared / $count, 1.0e-20)
        relative_mean_bias = [Math]::Abs($sumDelta) /
            [Math]::Max($sumReference, 1.0e-12)
        relative_bias_95_bound =
            ([Math]::Abs($meanDelta) +
             1.96 * [Math]::Sqrt($variance / $count)) / $scale
    }
}

function Invoke-Render {
    param([string]$Scene, [int]$Mode, [int]$Spp, [string]$Name)
    $path = Join-Path $ResultDir $Name
    $elapsed = Measure-Command {
        & $ExePath $Scene $Mode $Width $Height $Spp $path
        if ($LASTEXITCODE -ne 0) {
            throw "MLT benchmark failed: $Scene mode=$Mode spp=$Spp"
        }
    }
    $telemetry = [ordered]@{}
    foreach ($line in Get-Content -LiteralPath ($path + ".telemetry")) {
        $parts = $line -split "=", 2
        if ($parts.Count -eq 2) { $telemetry[$parts[0]] = [double]$parts[1] }
    }
    [ordered]@{
        image = Read-FloatImage $path
        elapsed_seconds = [double]$telemetry.render_seconds
        process_elapsed_seconds = $elapsed.TotalSeconds
        telemetry = $telemetry
    }
}

if (-not $SkipBuild) {
    & (Join-Path $RepoRoot "scripts\build_x64.ps1") `
        -BuildDir $BuildDir -Config $Config -SkipConfigure `
        -Targets gpu_phase_r_guiding_benchmark
    if ($LASTEXITCODE -ne 0) { throw "MLT benchmark build failed" }
}
if (-not (Test-Path $ExePath)) { throw "MLT benchmark executable missing" }
if ($CurveSpp.Count -lt 2) { throw "MLT curve requires at least two points" }
if (-not [double]::IsFinite($TargetNormalizedMse) -or
    $TargetNormalizedMse -le 0.0) {
    throw "TargetNormalizedMse must be finite and positive"
}
foreach ($spp in $CurveSpp) {
    if ($spp -le 0 -or $spp -ge $ReferenceSpp) {
        throw "CurveSpp must be positive and below ReferenceSpp"
    }
}
New-Item -ItemType Directory -Path $ResultDir -Force | Out-Null

$reports = @()
$benefitScenes = 0
foreach ($scene in $Scenes) {
    $sceneReferenceSpp = if ($scene -eq "small_emitter") {
        $ReferenceSpp * 32
    } elseif ($scene -eq "sds" -or $scene -eq "sds_small_light" -or
              $scene -eq "glass_caustic") {
        $ReferenceSpp * 8
    } elseif ($scene -eq "high_occlusion") {
        $ReferenceSpp * 4
    } else {
        $ReferenceSpp
    }
    $reference = Invoke-Render $scene 0 $sceneReferenceSpp `
        "phase_r_mlt_${scene}_reference.bin"
    $waveCurve = @()
    $mltCurve = @()
    foreach ($spp in $CurveSpp) {
        $wave = Invoke-Render $scene 0 $spp `
            "phase_r_mlt_${scene}_wave_spp${spp}.bin"
        $mlt = Invoke-Render $scene 4 $spp `
            "phase_r_mlt_${scene}_mlt_spp${spp}.bin"
        $waveMetrics = Get-ErrorMetrics $wave.image $reference.image
        $mltMetrics = Get-ErrorMetrics $mlt.image $reference.image
        if ($mlt.telemetry.mlt_bootstrap_positive -le 0 -or
            $mlt.telemetry.mlt_invalid -ne 0 -or
            $mlt.telemetry.mlt_deposited -ne $Width * $Height * $spp) {
            throw "MLT chain lifecycle gate failed: $scene spp=$spp"
        }
        $waveCurve += [ordered]@{
            spp = $spp
            elapsed_seconds = [Math]::Round($wave.elapsed_seconds, 6)
            mse = [Math]::Round($waveMetrics.mse, 12)
            normalized_mse = [Math]::Round($waveMetrics.normalized_mse, 8)
        }
        $mltCurve += [ordered]@{
            spp = $spp
            elapsed_seconds = [Math]::Round($mlt.elapsed_seconds, 6)
            mse = [Math]::Round($mltMetrics.mse, 12)
            normalized_mse = [Math]::Round($mltMetrics.normalized_mse, 8)
            relative_mean_bias = [Math]::Round($mltMetrics.relative_mean_bias, 8)
            relative_bias_95_bound = [Math]::Round($mltMetrics.relative_bias_95_bound, 8)
            acceptance_rate = [Math]::Round($mlt.telemetry.mlt_acceptance_rate, 8)
        }
    }
    if ([double]$mltCurve[-1].normalized_mse -gt
        [double]$mltCurve[0].normalized_mse) {
        throw "MLT convergence gate failed: $scene"
    }
    if ([double]$mltCurve[-1].relative_bias_95_bound -gt 0.35) {
        throw "MLT high-sample bias bound failed: $scene"
    }
    $waveTime = $null
    $mltTime = $null
    foreach ($point in $waveCurve) {
        if ([double]$point.normalized_mse -le $TargetNormalizedMse) {
            $waveTime = $point.elapsed_seconds
            break
        }
    }
    foreach ($point in $mltCurve) {
        if ([double]$point.normalized_mse -le $TargetNormalizedMse) {
            $mltTime = $point.elapsed_seconds
            break
        }
    }
    $benefit = $null -ne $mltTime -and
        ($null -eq $waveTime -or [double]$mltTime -lt [double]$waveTime)
    if ($benefit) { $benefitScenes++ }
    $reports += [ordered]@{
        scene = $scene
        reference_spp = $sceneReferenceSpp
        target_normalized_mse = $TargetNormalizedMse
        wavefront = $waveCurve
        mlt = $mltCurve
        time_to_error_benefit = $benefit
    }
}
$result = [ordered]@{
    schema = "ure.phase_r.mlt_suite.v1"
    status = "passed"
    generated_utc = [DateTime]::UtcNow.ToString("o")
    width = $Width
    height = $Height
    target_normalized_mse = $TargetNormalizedMse
    benefit_scene_count = $benefitScenes
    boundary_scene_count = 0
    workloads = $reports
}
$result | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $ResultPath -Encoding utf8
if ($benefitScenes -lt $MinBenefitScenes) {
    throw "MLT time-to-error benefit gate requires at least $MinBenefitScenes scenes; got $benefitScenes; report: $ResultPath"
}
Write-Host "Phase R-P5 MLT suite passed: $ResultPath"
