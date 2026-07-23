param(
    [string]$BuildDir = "build_modular_x64",
    [string]$Config = "Release",
    [int]$Width = 16,
    [int]$Height = 16,
    [int[]]$CurveSpp = @(64, 128, 256, 1024),
    [int]$ReferenceSpp = 8192,
    [double]$TargetNormalizedMse = 0.05,
    [int]$MinBenefitScenes = 1,
    [int]$ReplicateCount = 4,
    [int]$ReferenceBaseSample = 1000000,
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

function Merge-Images {
    param([object[]]$Images)
    $values = New-Object 'double[]' $Images[0].values.Length
    foreach ($image in $Images) {
        if ($image.width -ne $Images[0].width -or
            $image.height -ne $Images[0].height) {
            throw "MLT reference shard dimensions differ"
        }
        for ($index = 0; $index -lt $values.Length; $index++) {
            $values[$index] += $image.values[$index] / $Images.Count
        }
    }
    [ordered]@{
        width = $Images[0].width
        height = $Images[0].height
        values = $values
    }
}

function Get-Mean {
    param([double[]]$Values)
    if ($Values.Count -eq 0) { throw "cannot average an empty sample" }
    ($Values | Measure-Object -Average).Average
}

function Get-SampleVariance {
    param([double[]]$Values)
    if ($Values.Count -lt 2) { return 0.0 }
    $mean = Get-Mean $Values
    $sum = 0.0
    foreach ($value in $Values) {
        $delta = $value - $mean
        $sum += $delta * $delta
    }
    $sum / ($Values.Count - 1)
}

function Get-Median {
    param([double[]]$Values)
    $sorted = @($Values | Sort-Object)
    $middle = [int][Math]::Floor($sorted.Count / 2)
    if (($sorted.Count % 2) -eq 1) { return $sorted[$middle] }
    0.5 * ($sorted[$middle - 1] + $sorted[$middle])
}

function Get-ImageMean {
    param($Image)
    Get-Mean ([double[]]$Image.values)
}

function Get-ErrorMetrics {
    param($Image, $Reference)
    $sumSquared = 0.0
    for ($index = 0; $index -lt $Image.values.Length; $index++) {
        $delta = $Image.values[$index] - $Reference.values[$index]
        $sumSquared += $delta * $delta
    }
    $sumReferenceSquared = 0.0
    foreach ($value in $Reference.values) {
        $sumReferenceSquared += $value * $value
    }
    $count = [double]$Image.values.Length
    $mse = $sumSquared / $count
    [ordered]@{
        mse = $mse
        normalized_mse = $mse /
            [Math]::Max($sumReferenceSquared / $count, 1.0e-20)
    }
}

function Get-ReplicatedMetrics {
    param(
        [object[]]$Runs,
        $Reference,
        [double]$ReferenceMean,
        [double]$ReferenceMeanVariance
    )
    $mseValues = @()
    $normalizedMseValues = @()
    $imageMeans = @()
    $elapsedValues = @()
    foreach ($run in $Runs) {
        $metrics = Get-ErrorMetrics $run.image $Reference
        $run.mse = [Math]::Round($metrics.mse, 12)
        $run.normalized_mse = [Math]::Round(
            $metrics.normalized_mse, 8)
        $mseValues += $metrics.mse
        $normalizedMseValues += $metrics.normalized_mse
        $imageMeans += Get-ImageMean $run.image
        $elapsedValues += $run.elapsed_seconds
    }
    $meanImage = Get-Mean ([double[]]$imageMeans)
    $standardError = [Math]::Sqrt(
        (Get-SampleVariance ([double[]]$imageMeans)) / $Runs.Count +
        $ReferenceMeanVariance / $ReplicateCount)
    $relativeBias = [Math]::Abs($meanImage - $ReferenceMean) /
        [Math]::Max([Math]::Abs($ReferenceMean), 1.0e-12)
    $relativeBiasBound =
        ([Math]::Abs($meanImage - $ReferenceMean) +
         3.182 * $standardError) /
        [Math]::Max([Math]::Abs($ReferenceMean), 1.0e-12)
    $replicateEvidence = @($Runs | ForEach-Object {
        $entry = [ordered]@{}
        foreach ($field in $_.GetEnumerator()) {
            if ($field.Key -ne "image") {
                $entry[$field.Key] = $field.Value
            }
        }
        $entry
    })
    [ordered]@{
        elapsed_seconds = [Math]::Round(
            (Get-Median ([double[]]$elapsedValues)), 6)
        mse = [Math]::Round((Get-Mean ([double[]]$mseValues)), 12)
        normalized_mse = [Math]::Round(
            (Get-Mean ([double[]]$normalizedMseValues)), 8)
        normalized_mse_variance = [Math]::Round(
            (Get-SampleVariance ([double[]]$normalizedMseValues)), 12)
        relative_mean_bias = [Math]::Round($relativeBias, 8)
        relative_bias_95_bound = [Math]::Round($relativeBiasBound, 8)
        replicates = $replicateEvidence
    }
}

function Invoke-Render {
    param(
        [string]$Scene,
        [int]$Mode,
        [int]$Spp,
        [string]$Name,
        [int]$SampleBegin = 0,
        [uint64]$IdentityOffset = 0
    )
    $path = Join-Path $ResultDir $Name
    $elapsed = Measure-Command {
        & $ExePath $Scene $Mode $Width $Height $Spp $path `
            $SampleBegin $IdentityOffset
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
        image_sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
        elapsed_seconds = [double]$telemetry.render_seconds
        process_elapsed_seconds = $elapsed.TotalSeconds
        telemetry = $telemetry
    }
}

function Assert-RejectionBoundary {
    $path = Join-Path $ResultDir "phase_r_mlt_guiding_boundary.bin"
    & $ExePath "sds" 9 $Width $Height 1 $path
    if ($LASTEXITCODE -eq 0) { throw "MLT adaptive-reuse boundary unexpectedly rendered" }
    $message = Get-Content -Raw -LiteralPath ($path + ".error")
    if ($message -notlike "*cannot be combined with adaptive reuse*") {
        throw "unexpected MLT boundary: $message"
    }
    [ordered]@{ name = "sds"; status = "boundary_passed"; rejection = $message }
}

if (-not $SkipBuild) {
    & (Join-Path $RepoRoot "scripts\build_x64.ps1") `
        -BuildDir $BuildDir -Config $Config -SkipConfigure `
        -Targets gpu_phase_r_guiding_benchmark
    if ($LASTEXITCODE -ne 0) { throw "MLT benchmark build failed" }
}
if (-not (Test-Path $ExePath)) { throw "MLT benchmark executable missing" }
if ($CurveSpp.Count -lt 2) { throw "MLT curve requires at least two points" }
if ($ReplicateCount -lt 4) { throw "MLT suite requires at least four replicates" }
if ($ReferenceBaseSample -lt 0) { throw "ReferenceBaseSample must be nonnegative" }
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
$maxCurveSpp = ($CurveSpp | Measure-Object -Maximum).Maximum
for ($sceneIndex = 0; $sceneIndex -lt $Scenes.Count; $sceneIndex++) {
    $scene = $Scenes[$sceneIndex]
    $sceneReferenceSpp = if ($scene -eq "small_emitter") {
        $ReferenceSpp * 32
    } elseif ($scene -eq "sds" -or $scene -eq "sds_small_light" -or
              $scene -eq "glass_caustic" -or
              $scene -eq "mixed_specular" -or
              $scene -eq "rough_indirect") {
        $ReferenceSpp * 8
    } elseif ($scene -eq "high_occlusion") {
        $ReferenceSpp * 4
    } elseif ($scene -eq "high_occlusion_small_light") {
        $ReferenceSpp * 8
    } elseif ($scene -eq "volume") {
        $ReferenceSpp * 8
    } else {
        $ReferenceSpp
    }
    if (($sceneReferenceSpp % $ReplicateCount) -ne 0) {
        throw "reference SPP must be divisible by replicate count: $scene"
    }
    $referenceShardSpp = [int]($sceneReferenceSpp / $ReplicateCount)
    $referenceRuns = @()
    $referenceEvidence = @()
    for ($replicate = 0; $replicate -lt $ReplicateCount; $replicate++) {
        $sampleBegin = $ReferenceBaseSample +
            $sceneIndex * 10000000 + $replicate * $referenceShardSpp
        $run = Invoke-Render $scene 0 $referenceShardSpp `
            "phase_r_mlt_${scene}_reference_r${replicate}.bin" `
            $sampleBegin 0
        $referenceRuns += $run
        $referenceEvidence += [ordered]@{
            replicate = $replicate
            spp = $referenceShardSpp
            sample_begin = $sampleBegin
            image_sha256 = $run.image_sha256
            elapsed_seconds = [Math]::Round($run.elapsed_seconds, 6)
            image_mean = [Math]::Round((Get-ImageMean $run.image), 12)
        }
    }
    $reference = Merge-Images @($referenceRuns.image)
    $referenceMeans = [double[]]@(
        $referenceRuns | ForEach-Object { Get-ImageMean $_.image })
    $referenceMean = Get-Mean $referenceMeans
    $referenceMeanVariance = Get-SampleVariance $referenceMeans
    $waveCurve = @()
    $mltCurve = @()
    foreach ($spp in $CurveSpp) {
        $waveRuns = @()
        $mltRuns = @()
        for ($replicate = 0; $replicate -lt $ReplicateCount; $replicate++) {
            $sampleBegin = $sceneIndex * 1000000 +
                $replicate * $maxCurveSpp
            $wave = Invoke-Render $scene 0 $spp `
                "phase_r_mlt_${scene}_wave_spp${spp}_r${replicate}.bin" `
                $sampleBegin 0
            $waveRuns += [ordered]@{
                replicate = $replicate
                sample_begin = $sampleBegin
                image = $wave.image
                image_sha256 = $wave.image_sha256
                elapsed_seconds = [double]$wave.elapsed_seconds
            }
            $identityOffset = [uint64](
                ($sceneIndex * $ReplicateCount + $replicate) *
                $Width * $Height)
            $mlt = Invoke-Render $scene 4 $spp `
                "phase_r_mlt_${scene}_mlt_spp${spp}_r${replicate}.bin" `
                0 $identityOffset
            if ($mlt.telemetry.mlt_bootstrap_positive -le 0 -or
                $mlt.telemetry.mlt_invalid -ne 0 -or
                $mlt.telemetry.mlt_deposited -ne $Width * $Height * $spp) {
                throw "MLT chain lifecycle gate failed: $scene spp=$spp replicate=$replicate"
            }
            $mltRuns += [ordered]@{
                replicate = $replicate
                identity_offset = $identityOffset
                image = $mlt.image
                image_sha256 = $mlt.image_sha256
                elapsed_seconds = [double]$mlt.elapsed_seconds
                acceptance_rate = [Math]::Round(
                    $mlt.telemetry.mlt_acceptance_rate, 8)
            }
        }
        $waveCurve += [ordered]@{
            spp = $spp
            metrics = Get-ReplicatedMetrics $waveRuns $reference `
                $referenceMean $referenceMeanVariance
        }
        $mltCurve += [ordered]@{
            spp = $spp
            metrics = Get-ReplicatedMetrics $mltRuns $reference `
                $referenceMean $referenceMeanVariance
        }
    }
    $waveCurve = @($waveCurve | ForEach-Object {
        $point = [ordered]@{ spp = $_.spp }
        foreach ($entry in $_.metrics.GetEnumerator()) {
            $point[$entry.Key] = $entry.Value
        }
        $point
    })
    $mltCurve = @($mltCurve | ForEach-Object {
        $point = [ordered]@{ spp = $_.spp }
        foreach ($entry in $_.metrics.GetEnumerator()) {
            $point[$entry.Key] = $entry.Value
        }
        $point
    })
    if ([double]$mltCurve[-1].normalized_mse -gt
        [double]$mltCurve[0].normalized_mse) {
        throw "MLT convergence gate failed: $scene"
    }
    if ([double]$mltCurve[-1].relative_bias_95_bound -gt 0.35) {
        throw "MLT high-sample replicate bias bound failed: $scene"
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
        reference_shards = $referenceEvidence
        target_normalized_mse = $TargetNormalizedMse
        wavefront = $waveCurve
        mlt = $mltCurve
        time_to_error_benefit = $benefit
    }
}
$reports += Assert-RejectionBoundary
$result = [ordered]@{
    schema = "ure.phase_r.mlt_suite.v2"
    status = "passed"
    generated_utc = [DateTime]::UtcNow.ToString("o")
    width = $Width
    height = $Height
    target_normalized_mse = $TargetNormalizedMse
    replicate_count = $ReplicateCount
    reference_base_sample = $ReferenceBaseSample
    benefit_scene_count = $benefitScenes
    boundary_scene_count = 1
    workloads = $reports
}
$result | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $ResultPath -Encoding utf8
if ($benefitScenes -lt $MinBenefitScenes) {
    throw "MLT time-to-error benefit gate requires at least $MinBenefitScenes scenes; got $benefitScenes; report: $ResultPath"
}
Write-Host "Phase R-P5 MLT suite passed: $ResultPath"
