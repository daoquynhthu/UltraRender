param(
    [string]$BuildDir = "build_modular_x64",
    [string]$Config = "Release",
    [int]$Width = 16,
    [int]$Height = 16,
    [int[]]$CurveSpp = @(256, 1024, 4096),
    [int]$ReferenceSpp = 8192,
    [string[]]$Scenes = @(
        "glass_caustic", "sds", "small_emitter", "mixed_specular"),
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$BuildPath = Join-Path $RepoRoot $BuildDir
$ExePath = Join-Path $BuildPath "artifacts\$Config\bin\gpu_phase_r_guiding_benchmark.exe"
$ResultDir = Join-Path $RepoRoot "output\benchmarks"
$ResultPath = Join-Path $ResultDir "phase_r_manifold_suite.json"

function Read-FloatImage {
    param([string]$Path)
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 8) { throw "truncated benchmark image: $Path" }
    $width = [BitConverter]::ToInt32($bytes, 0)
    $height = [BitConverter]::ToInt32($bytes, 4)
    $count = $width * $height * 3
    if ($bytes.Length -ne 8 + 4 * $count) {
        throw "invalid benchmark image payload: $Path"
    }
    $values = New-Object 'double[]' $count
    for ($index = 0; $index -lt $count; $index++) {
        $values[$index] = [BitConverter]::ToSingle(
            $bytes, 8 + 4 * $index)
    }
    [ordered]@{ width = $width; height = $height; values = $values }
}

function Subtract-Image {
    param($Image, $Subtract)
    if ($Image.values.Length -ne $Subtract.values.Length) {
        throw "image dimensions differ"
    }
    $values = New-Object 'double[]' $Image.values.Length
    for ($index = 0; $index -lt $values.Length; $index++) {
        $values[$index] = $Image.values[$index] - $Subtract.values[$index]
    }
    [ordered]@{ width = $Image.width; height = $Image.height; values = $values }
}

function Get-ErrorMetrics {
    param($Image, $Reference)
    if ($Image.values.Length -ne $Reference.values.Length) {
        throw "image dimensions differ"
    }
    $sumSquared = 0.0
    $sumDelta = 0.0
    $sumReference = 0.0
    $sumAbsolute = 0.0
    foreach ($index in 0..($Image.values.Length - 1)) {
        $delta = $Image.values[$index] - $Reference.values[$index]
        $sumSquared += $delta * $delta
        $sumDelta += $delta
        $sumReference += [Math]::Abs($Reference.values[$index])
        $sumAbsolute += [Math]::Abs($Image.values[$index])
    }
    $count = [double]$Image.values.Length
    $meanDelta = $sumDelta / $count
    $mse = $sumSquared / $count
    $deltaVariance = [Math]::Max(0.0, $mse - $meanDelta * $meanDelta)
    $scale = [Math]::Max($sumReference / $count, 1.0e-12)
    [ordered]@{
        mse = $mse
        relative_mean_bias = [Math]::Abs($sumDelta) /
            [Math]::Max($sumReference, 1.0e-12)
        relative_bias_95_bound =
            ([Math]::Abs($meanDelta) +
             1.96 * [Math]::Sqrt($deltaVariance / $count)) / $scale
        absolute_energy = $sumAbsolute
    }
}

function Invoke-Render {
    param([string]$Scene, [int]$Mode, [int]$Spp, [string]$Name)
    $path = Join-Path $ResultDir $Name
    $elapsed = Measure-Command {
        & $ExePath $Scene $Mode $Width $Height $Spp $path
        if ($LASTEXITCODE -ne 0) {
            throw "manifold benchmark failed: $Scene mode=$Mode spp=$Spp"
        }
    }
    $telemetry = [ordered]@{}
    foreach ($line in Get-Content -LiteralPath ($path + ".telemetry")) {
        $parts = $line -split "=", 2
        if ($parts.Count -eq 2) {
            $telemetry[$parts[0]] = [double]$parts[1]
        }
    }
    [ordered]@{
        path = $path
        full = Read-FloatImage $path
        manifold = Read-FloatImage ($path + ".manifold")
        specular_emitter = Read-FloatImage ($path + ".specular_emitter")
        elapsed_seconds = $elapsed.TotalSeconds
        samples_per_second = ($Width * $Height * $Spp) /
            [Math]::Max($elapsed.TotalSeconds, 1.0e-9)
        telemetry = $telemetry
    }
}

function Get-MonteCarloBias {
    param($ImageRun, $ReferenceRun)
    $imageMean = [double]$ImageRun.telemetry.manifold_sample_energy_mean
    $referenceMean = [double]$ReferenceRun.telemetry.specular_emitter_sample_energy_mean
    $standardError = [Math]::Sqrt(
        [double]$ImageRun.telemetry.manifold_sample_energy_variance /
            [double]$ImageRun.telemetry.technique_sample_count +
        [double]$ReferenceRun.telemetry.specular_emitter_sample_energy_variance /
            [double]$ReferenceRun.telemetry.technique_sample_count)
    $scale = [Math]::Max([Math]::Abs($referenceMean), 1.0e-12)
    [ordered]@{
        relative_mean_bias = [Math]::Abs($imageMean - $referenceMean) / $scale
        relative_bias_95_bound =
            ([Math]::Abs($imageMean - $referenceMean) +
             1.96 * $standardError) / $scale
        standard_error = $standardError
    }
}

if (-not $SkipBuild) {
    & (Join-Path $RepoRoot "scripts\build_x64.ps1") `
        -BuildDir $BuildDir -Config $Config `
        -Target gpu_phase_r_guiding_benchmark
    if ($LASTEXITCODE -ne 0) { throw "manifold benchmark build failed" }
}
if (-not (Test-Path $ExePath)) {
    throw "manifold benchmark executable not found: $ExePath"
}
if ($CurveSpp.Count -lt 2) {
    throw "manifold suite requires at least two curve points"
}
foreach ($spp in $CurveSpp) {
    if ($spp -le 0 -or $spp -ge $ReferenceSpp) {
        throw "CurveSpp must be positive and below ReferenceSpp"
    }
}
New-Item -ItemType Directory -Path $ResultDir -Force | Out-Null

$reports = @()
$benefitScenes = 0
$boundaryScenes = 0
$positiveScenes = 0
foreach ($scene in $Scenes) {
    $waveReferenceSpp = if ($scene -eq "small_emitter") {
        $ReferenceSpp * 128
    } elseif ($scene -eq "sds") {
        $ReferenceSpp * 8
    } elseif ($scene -eq "mixed_specular") {
        $ReferenceSpp * 4
    } else {
        $ReferenceSpp
    }
    $smsReferenceSpp = if ($scene -eq "mixed_specular") {
        $ReferenceSpp * 16
    } elseif ($scene -eq "small_emitter") {
        $ReferenceSpp * 16
    } elseif ($scene -eq "sds") {
        $ReferenceSpp * 8
    } else {
        $ReferenceSpp
    }
    $waveReferenceRun = Invoke-Render $scene 0 $waveReferenceSpp `
        "phase_r_manifold_${scene}_wave_reference.bin"
    $smsReferenceRun = Invoke-Render $scene 3 $smsReferenceSpp `
        "phase_r_manifold_${scene}_sms_reference.bin"
    $manifoldReference = $waveReferenceRun.specular_emitter
    $baseReference = Subtract-Image `
        $waveReferenceRun.full $waveReferenceRun.specular_emitter
    $referenceEnergy = (Get-ErrorMetrics `
        $manifoldReference $manifoldReference).absolute_energy
    if ($referenceEnergy -le 1.0e-8) {
        if ($scene -ne "glass_caustic") {
            throw "manifold reference is zero: $scene"
        }
        $fullEnergy = (Get-ErrorMetrics `
            $waveReferenceRun.full $waveReferenceRun.full).absolute_energy
        $smsEnergy = (Get-ErrorMetrics `
            $smsReferenceRun.manifold $smsReferenceRun.manifold).absolute_energy
        if ($fullEnergy -le 1.0e-8 -or $smsEnergy -gt 1.0e-8 -or
            $smsReferenceRun.telemetry.manifold_converged -le 0) {
            throw "glass camera-delta support boundary was not isolated"
        }
        ++$boundaryScenes
        $reports += [ordered]@{
            name = $scene
            status = "boundary_passed"
            boundary = "camera_delta_outside_anchored_sms_support"
            reference_spp = $ReferenceSpp
            wavefront_full_energy = $fullEnergy
            wavefront_anchored_delta_energy = $referenceEnergy
            manifold_energy = $smsEnergy
            manifold_telemetry = $smsReferenceRun.telemetry
        }
        continue
    }
    ++$positiveScenes
    if ($smsReferenceRun.telemetry.manifold_converged -le 0 -or
        $smsReferenceRun.telemetry.manifold_root_matches -ne
            $smsReferenceRun.telemetry.manifold_converged) {
        throw "manifold reference root lifecycle failed: $scene"
    }
    $referenceBias = Get-MonteCarloBias `
        $smsReferenceRun $waveReferenceRun
    if ($referenceBias.relative_bias_95_bound -gt 0.35) {
        throw "manifold high-SPP bias bound failed: $scene " +
            "bias=$($referenceBias.relative_mean_bias) " +
            "bound=$($referenceBias.relative_bias_95_bound)"
    }

    $waveCurve = @()
    $smsCurve = @()
    foreach ($spp in $CurveSpp) {
        $wave = Invoke-Render $scene 0 $spp `
            "phase_r_manifold_${scene}_wave_spp${spp}.bin"
        $sms = Invoke-Render $scene 3 $spp `
            "phase_r_manifold_${scene}_sms_spp${spp}.bin"
        $waveMetrics = Get-ErrorMetrics `
            $wave.specular_emitter $manifoldReference
        $smsMetrics = Get-ErrorMetrics $sms.manifold $manifoldReference
        $baseMetrics = Get-ErrorMetrics `
            (Subtract-Image $sms.full $sms.manifold) $baseReference
        $waveCurve += [ordered]@{
            spp = $spp
            elapsed_seconds = [Math]::Round($wave.elapsed_seconds, 6)
            manifold_mse = [Math]::Round($waveMetrics.mse, 12)
            samples_per_second = [Math]::Round($wave.samples_per_second, 3)
        }
        $smsCurve += [ordered]@{
            spp = $spp
            elapsed_seconds = [Math]::Round($sms.elapsed_seconds, 6)
            manifold_mse = [Math]::Round($smsMetrics.mse, 12)
            manifold_relative_mean_bias =
                [Math]::Round($smsMetrics.relative_mean_bias, 8)
            manifold_relative_bias_95_bound =
                [Math]::Round($smsMetrics.relative_bias_95_bound, 8)
            base_relative_mean_bias =
                [Math]::Round($baseMetrics.relative_mean_bias, 8)
            samples_per_second = [Math]::Round($sms.samples_per_second, 3)
            manifold_telemetry = $sms.telemetry
        }
    }
    if ([double]$smsCurve[-1].manifold_mse -gt
        [double]$smsCurve[0].manifold_mse) {
        throw "manifold convergence failed: $scene"
    }
    if ([double]$smsCurve[-1].base_relative_mean_bias -gt 0.15) {
        throw "manifold base-energy partition failed: $scene"
    }
    if ($smsCurve[-1].manifold_telemetry.manifold_converged -le 0 -or
        $smsCurve[-1].manifold_telemetry.manifold_root_matches -ne
            $smsCurve[-1].manifold_telemetry.manifold_converged) {
        throw "manifold target/root drain failed: $scene"
    }
    if ($scene -eq "mixed_specular" -and
        $smsCurve[-1].manifold_telemetry.manifold_rejected_non_delta -le 0) {
        throw "rough/specular rejection boundary was not exercised"
    }
    $targetMse = [double]$smsCurve[-1].manifold_mse * 1.05
    $waveTime = $null
    $smsTime = $null
    for ($index = 0; $index -lt $CurveSpp.Count; $index++) {
        if ($null -eq $waveTime -and
            [double]$waveCurve[$index].manifold_mse -le $targetMse) {
            $waveTime = $waveCurve[$index].elapsed_seconds
        }
        if ($null -eq $smsTime -and
            [double]$smsCurve[$index].manifold_mse -le $targetMse) {
            $smsTime = $smsCurve[$index].elapsed_seconds
        }
    }
    if ($null -ne $smsTime -and
        ($null -eq $waveTime -or $smsTime -lt $waveTime)) {
        ++$benefitScenes
    }
    $reports += [ordered]@{
        name = $scene
        reference_spp = $ReferenceSpp
        wavefront_reference_spp = $waveReferenceSpp
        manifold_reference_spp = $smsReferenceSpp
        manifold_reference_energy = $referenceEnergy
        manifold_reference_relative_mean_bias =
            [Math]::Round($referenceBias.relative_mean_bias, 8)
        manifold_reference_relative_bias_95_bound =
            [Math]::Round($referenceBias.relative_bias_95_bound, 8)
        manifold_reference_standard_error = $referenceBias.standard_error
        wavefront = $waveCurve
        specular_manifold = $smsCurve
        target_mse = $targetMse
        wavefront_time_to_error_seconds = $waveTime
        manifold_time_to_error_seconds = $smsTime
        status = "passed"
    }
}
if ($positiveScenes -gt 0 -and $benefitScenes -lt 1) {
    throw "manifold suite found no positive time-to-error workload"
}

$report = [ordered]@{
    schema = "ure.phase_r.manifold_suite.v1"
    phase = "R-P4"
    suite = "specular_manifold_bias_variance_time_to_error"
    status = "passed"
    width = $Width
    height = $Height
    curve_spp = $CurveSpp
    reference_spp = $ReferenceSpp
    benefit_scene_count = $benefitScenes
    boundary_scene_count = $boundaryScenes
    scenes = $reports
}
$report | ConvertTo-Json -Depth 12 |
    Set-Content -LiteralPath $ResultPath -Encoding UTF8
Write-Host "Phase R-P4 manifold suite passed"
Write-Host "Wrote $ResultPath"
