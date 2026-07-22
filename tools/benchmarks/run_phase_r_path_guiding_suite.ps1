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
$ExePath = Join-Path $BuildPath "tests\gpu\gpu_phase_r_guiding_benchmark.exe"
$ResultDir = Join-Path $RepoRoot "output\benchmarks"
$ResultPath = Join-Path $ResultDir "phase_r_path_guiding_suite.json"

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
    $squared = New-Object 'double[]' $Image.values.Length
    $sum = 0.0
    for ($i = 0; $i -lt $Image.values.Length; $i++) {
        $d = $Image.values[$i] - $Reference.values[$i]
        $squared[$i] = $d * $d
        $sum += $squared[$i]
    }
    $mse = $sum / [double]$squared.Length
    $variance = 0.0
    foreach ($v in $squared) { $variance += ($v - $mse) * ($v - $mse) }
    [ordered]@{ mse = $mse; error_variance = $variance / [double]$squared.Length }
}

function Invoke-GuidingRender {
    param([string]$Scene, [bool]$Guided, [int]$Spp, [string]$Name)
    $path = Join-Path $ResultDir $Name
    $elapsed = Measure-Command {
        & $ExePath $Scene ([int]$Guided) $Width $Height $Spp $path
        if ($LASTEXITCODE -ne 0) { throw "path guiding benchmark failed: $Scene guided=$Guided spp=$Spp" }
    }
    [ordered]@{ path = $path; elapsed_seconds = [Math]::Round($elapsed.TotalSeconds, 6) }
}

function Assert-RejectionBoundary {
    param([string]$Scene, [int]$Mode, [string]$Expected, [string]$Name)
    $path = Join-Path $ResultDir $Name
    & $ExePath $Scene $Mode $Width $Height 1 $path
    if ($LASTEXITCODE -eq 0) { throw "path guiding boundary unexpectedly rendered" }
    $message = Get-Content -Raw -LiteralPath ($path + ".error")
    if ($message -notlike "*$Expected*") { throw "unexpected path guiding boundary: $message" }
    [ordered]@{ name = $Scene; status = "boundary_passed"; rejection = $message }
}

if (-not $SkipBuild) {
    & (Join-Path $RepoRoot "scripts\build_x64.ps1") -BuildDir $BuildDir -Config $Config -Targets gpu_phase_r_guiding_benchmark
    if ($LASTEXITCODE -ne 0) { throw "path guiding benchmark build failed" }
}
if (-not (Test-Path $ExePath)) { throw "path guiding benchmark executable not found: $ExePath" }
if ($CurveSpp.Count -lt 2) { throw "path guiding suite requires at least two curve points" }
foreach ($spp in $CurveSpp) { if ($spp -le 0 -or $spp -ge $ReferenceSpp) { throw "CurveSpp must be positive and below ReferenceSpp" } }
New-Item -ItemType Directory -Path $ResultDir -Force | Out-Null

$reports = @()
$benefitScenes = 0
foreach ($scene in @("cornell", "multi_light", "complex_material", "volume")) {
    $referenceRun = Invoke-GuidingRender $scene $false $ReferenceSpp "phase_r_guiding_${scene}_reference.bin"
    $reference = Read-FloatImage $referenceRun.path
    $modeReports = @()
    foreach ($guided in @($false, $true)) {
        $curve = @()
        foreach ($spp in $CurveSpp) {
            $mode = if ($guided) { "guided" } else { "baseline" }
            $run = Invoke-GuidingRender $scene $guided $spp "phase_r_guiding_${scene}_${mode}_spp${spp}.bin"
            $metrics = Get-ErrorMetrics (Read-FloatImage $run.path) $reference
            $curve += [ordered]@{
                spp = $spp
                elapsed_seconds = $run.elapsed_seconds
                mse_to_reference = [Math]::Round($metrics.mse, 12)
                error_variance = [Math]::Round($metrics.error_variance, 12)
            }
        }
        if ([double]$curve[-1].mse_to_reference -gt [double]$curve[0].mse_to_reference * 1.10) {
            throw "path guiding convergence failed: $scene guided=$guided"
        }
        $modeReports += [ordered]@{ mode = if ($guided) { "guided" } else { "baseline" }; curve = $curve }
    }
    $targetMse = 1.05 * [Math]::Min([double]$modeReports[0].curve[-1].mse_to_reference,
                                    [double]$modeReports[1].curve[-1].mse_to_reference)
    foreach ($mode in $modeReports) {
        $timeToError = $null
        foreach ($point in $mode.curve) {
            if ([double]$point.mse_to_reference -le $targetMse) { $timeToError = $point.elapsed_seconds; break }
        }
        $mode["time_to_error_seconds"] = $timeToError
    }
    if ($null -ne $modeReports[1].time_to_error_seconds -and
        ($null -eq $modeReports[0].time_to_error_seconds -or
         $modeReports[1].time_to_error_seconds -lt $modeReports[0].time_to_error_seconds)) {
        ++$benefitScenes
    }
    $reports += [ordered]@{
        name = $scene
        reference_spp = $ReferenceSpp
        reference_seconds = $referenceRun.elapsed_seconds
        target_mse = [Math]::Round($targetMse, 12)
        modes = $modeReports
        status = "passed"
    }
}
$boundary = Assert-RejectionBoundary "cornell" 7 `
    "path_guided requires path_guiding.enabled" "phase_r_guiding_disabled_boundary.bin"
$reports += $boundary

$report = [ordered]@{
    schema = "ure.phase_r.path_guiding_suite.v1"
    phase = "R-P2"
    suite = "path_guiding_variance_mse_time_to_error"
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
Write-Host "Phase R-P2 path guiding suite passed"
Write-Host "Wrote $ResultPath"
