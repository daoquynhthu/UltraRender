param(
    [string]$BuildDir = "build_modular_x64",
    [string]$Config = "Release",
    [int]$Width = 16,
    [int]$Height = 16,
    [int[]]$CurveSpp = @(4, 16, 64),
    [int]$ReferenceSpp = 256,
    [string[]]$Scenes = @("cornell", "sds", "glass_caustic"),
    [int]$MinBdptBenefitScenes = 0,
    [int]$MinVcmBenefitScenes = 0,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$BuildPath = Join-Path $RepoRoot $BuildDir
$ExePath = Join-Path $BuildPath "tests\gpu\gpu_phase_r_guiding_benchmark.exe"
$ResultDir = Join-Path $RepoRoot "output\benchmarks"
$ResultPath = Join-Path $ResultDir "phase_r_bidirectional_suite.json"

function Read-FloatImage {
    param([string]$Path)
    $bytes = [IO.File]::ReadAllBytes($Path)
    $width = [BitConverter]::ToInt32($bytes, 0)
    $height = [BitConverter]::ToInt32($bytes, 4)
    $values = New-Object 'double[]' ($width * $height * 3)
    for ($index = 0; $index -lt $values.Length; ++$index) {
        $values[$index] = [BitConverter]::ToSingle($bytes, 8 + $index * 4)
    }
    [ordered]@{ width = $width; height = $height; values = $values }
}

function Get-Metrics {
    param($Image, $Reference)
    $sumSquared = 0.0
    $referenceSquared = 0.0
    $energy = 0.0
    for ($index = 0; $index -lt $Image.values.Length; ++$index) {
        $delta = $Image.values[$index] - $Reference.values[$index]
        $sumSquared += $delta * $delta
        $referenceSquared += $Reference.values[$index] * $Reference.values[$index]
        $energy += [Math]::Abs($Image.values[$index])
    }
    [ordered]@{
        normalized_mse = $sumSquared / [Math]::Max($referenceSquared, 1.0e-20)
        absolute_energy = $energy
    }
}

function Invoke-Render {
    param([string]$Scene, [int]$Mode, [int]$Spp, [string]$Name)
    $path = Join-Path $ResultDir $Name
    $elapsed = Measure-Command {
        & $ExePath $Scene $Mode $Width $Height $Spp $path
        if ($LASTEXITCODE -ne 0) { throw "bidirectional benchmark failed: $Scene mode=$Mode spp=$Spp" }
    }
    $telemetry = [ordered]@{}
    foreach ($line in Get-Content -LiteralPath ($path + ".telemetry")) {
        $parts = $line -split "=", 2
        if ($parts.Count -eq 2) { $telemetry[$parts[0]] = [double]$parts[1] }
    }
    [ordered]@{
        image = Read-FloatImage $path
        connection = Read-FloatImage ($path + ".bidirectional_connection")
        surface_merge = Read-FloatImage ($path + ".vcm_surface_merge")
        volume_merge = Read-FloatImage ($path + ".vcm_volume_merge")
        elapsed_seconds = $elapsed.TotalSeconds
        telemetry = $telemetry
    }
}

if (-not $SkipBuild) {
    & (Join-Path $RepoRoot "scripts\build_x64.ps1") -BuildDir $BuildDir -Config $Config -Targets gpu_phase_r_guiding_benchmark
    if ($LASTEXITCODE -ne 0) { throw "bidirectional benchmark build failed" }
}
if (-not (Test-Path $ExePath)) { throw "bidirectional benchmark executable not found" }
New-Item -ItemType Directory -Path $ResultDir -Force | Out-Null

$reports = @()
$benefits = @{ bdpt = 0; vcm = 0 }
$boundaries = @{ bdpt = 0; vcm = 0 }
foreach ($scene in $Scenes) {
    $referenceRun = Invoke-Render $scene 0 $ReferenceSpp "phase_r_bidir_${scene}_reference.bin"
    if ($scene -eq "glass_caustic") {
        $boundaryModes = @([ordered]@{ name = "bdpt"; value = 6 }, [ordered]@{ name = "vcm"; value = 5 })
        $modeReports = @()
        foreach ($mode in $boundaryModes) {
            $run = Invoke-Render $scene $mode.value $ReferenceSpp "phase_r_bidir_${scene}_$($mode.name)_boundary.bin"
            $connectionEnergy = (Get-Metrics $run.connection $run.connection).absolute_energy
            $mergeEnergy = (Get-Metrics $run.surface_merge $run.surface_merge).absolute_energy +
                (Get-Metrics $run.volume_merge $run.volume_merge).absolute_energy
            if ($connectionEnergy -gt 1.0e-8 -or $mergeEnergy -gt 1.0e-8 -or
                (Get-Metrics $run.image $run.image).absolute_energy -le 1.0e-8) {
                throw "$($mode.name) camera-delta boundary was not isolated"
            }
            $boundaries[$mode.name]++
            $modeReports += [ordered]@{
                mode = $mode.name
                connection_energy = $connectionEnergy
                merge_energy = $mergeEnergy
                status = "boundary_passed"
            }
        }
        $reports += [ordered]@{
            name = $scene; status = "boundary_passed"
            boundary = "camera_delta_outside_ordinary_connection_and_photon_merge_support"
            modes = $modeReports
        }
        continue
    }

    $modes = @()
    foreach ($mode in @(
        [ordered]@{ name = "wavefront"; value = 0 },
        [ordered]@{ name = "bdpt"; value = 6 },
        [ordered]@{ name = "vcm"; value = 5 })) {
        $curve = @()
        foreach ($spp in $CurveSpp) {
            $run = Invoke-Render $scene $mode.value $spp "phase_r_bidir_${scene}_$($mode.name)_spp${spp}.bin"
            $metrics = Get-Metrics $run.image $referenceRun.image
            $curve += [ordered]@{
                spp = $spp
                elapsed_seconds = [Math]::Round($run.elapsed_seconds, 6)
                normalized_mse = [Math]::Round($metrics.normalized_mse, 10)
                samples_per_second = [Math]::Round(($Width * $Height * $spp) / $run.elapsed_seconds, 3)
            }
        }
        $modes += [ordered]@{ mode = $mode.name; curve = $curve }
    }
    $target = 1.05 * [Math]::Min(
        [double]$modes[1].curve[-1].normalized_mse,
        [double]$modes[2].curve[-1].normalized_mse)
    foreach ($mode in $modes) {
        $time = $null
        foreach ($point in $mode.curve) {
            if ([double]$point.normalized_mse -le $target) { $time = $point.elapsed_seconds; break }
        }
        $mode["time_to_error_seconds"] = $time
    }
    $waveTime = $modes[0].time_to_error_seconds
    foreach ($mode in $modes[1..2]) {
        if ($null -ne $mode.time_to_error_seconds -and
            ($null -eq $waveTime -or $mode.time_to_error_seconds -lt $waveTime)) {
            $benefits[$mode.mode]++
        }
    }
    $reports += [ordered]@{ name = $scene; status = "passed"; target_normalized_mse = $target; modes = $modes }
}

$report = [ordered]@{
    schema = "ure.phase_r.bidirectional_suite.v1"
    status = "passed"
    width = $Width
    height = $Height
    curve_spp = $CurveSpp
    reference_spp = $ReferenceSpp
    bdpt_benefit_scene_count = $benefits.bdpt
    vcm_benefit_scene_count = $benefits.vcm
    bdpt_boundary_scene_count = $boundaries.bdpt
    vcm_boundary_scene_count = $boundaries.vcm
    scenes = $reports
}
$report | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $ResultPath -Encoding utf8
if ($benefits.bdpt -lt $MinBdptBenefitScenes) { throw "BDPT benefit gate failed" }
if ($benefits.vcm -lt $MinVcmBenefitScenes) { throw "VCM benefit gate failed" }
Write-Host "Phase R bidirectional suite passed: $ResultPath"
