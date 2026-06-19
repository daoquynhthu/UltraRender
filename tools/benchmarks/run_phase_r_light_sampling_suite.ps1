param(
    [string]$BuildDir = "build_modular_x64",
    [string]$Config = "Release",
    [int]$Width = 32,
    [int]$Height = 32,
    [int[]]$CurveSpp = @(1, 4),
    [int]$ReferenceSpp = 16,
    [double]$MaxMidMseRatio = 1.05,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$BuildPath = Join-Path $RepoRoot $BuildDir
$ExePath = Join-Path $BuildPath "apps\ure_cli\ure_cli.exe"
$ResultDir = Join-Path $RepoRoot "output\benchmarks"
$ResultPath = Join-Path $ResultDir "phase_r_light_sampling_suite.json"

function Read-RgbeImage {
    param([string]$Path)
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $pos = 0
    $width = 0
    $height = 0
    while ($pos -lt $bytes.Length) {
        $lineStart = $pos
        while ($pos -lt $bytes.Length -and $bytes[$pos] -ne 10) { $pos++ }
        $lineBytes = $bytes[$lineStart..([Math]::Max($lineStart, $pos - 1))]
        $line = [System.Text.Encoding]::ASCII.GetString($lineBytes).Trim()
        $pos++
        if ($line -match "^-Y\s+(\d+)\s+\+X\s+(\d+)$") {
            $height = [int]$Matches[1]
            $width = [int]$Matches[2]
            break
        }
    }
    if ($width -le 0 -or $height -le 0) {
        throw "invalid HDR header: $Path"
    }
    $expectedBytes = $width * $height * 4
    if ($bytes.Length - $pos -lt $expectedBytes) {
        throw "truncated HDR pixel data: $Path"
    }
    $values = New-Object 'double[]' ($width * $height * 3)
    for ($i = 0; $i -lt ($width * $height); $i++) {
        $r = [int]$bytes[$pos++]
        $g = [int]$bytes[$pos++]
        $b = [int]$bytes[$pos++]
        $e = [int]$bytes[$pos++]
        if ($e -ne 0) {
            $scale = [Math]::Pow(2.0, $e - 128) / 256.0
            $values[$i * 3 + 0] = $r * $scale
            $values[$i * 3 + 1] = $g * $scale
            $values[$i * 3 + 2] = $b * $scale
        }
    }
    [ordered]@{
        width = $width
        height = $height
        values = $values
    }
}

function Get-ImageMse {
    param($A, $B)
    if ($A.width -ne $B.width -or $A.height -ne $B.height) {
        throw "image dimensions differ"
    }
    $sum = 0.0
    for ($i = 0; $i -lt $A.values.Length; $i++) {
        $d = $A.values[$i] - $B.values[$i]
        $sum += $d * $d
    }
    $sum / [double]$A.values.Length
}

function Get-ImageVariance {
    param($Image)
    $mean = 0.0
    foreach ($v in $Image.values) { $mean += $v }
    $mean /= [double]$Image.values.Length
    $sum = 0.0
    foreach ($v in $Image.values) {
        $d = $v - $mean
        $sum += $d * $d
    }
    $sum / [double]$Image.values.Length
}

function Invoke-Render {
    param(
        [string]$Scene,
        [int]$Spp,
        [string]$OutputName
    )
    $elapsed = Measure-Command {
        & $ExePath --quiet render $Scene --width $Width --height $Height --spp $Spp --format hdr --output $OutputName | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "render failed: $Scene spp=$Spp" }
    }
    [ordered]@{
        output = (Join-Path $RepoRoot "output\$OutputName")
        elapsed_seconds = [Math]::Round($elapsed.TotalSeconds, 6)
    }
}

if (-not $SkipBuild) {
    & (Join-Path $RepoRoot "scripts\build_x64.ps1") -BuildDir $BuildDir -Config $Config -Target ure_cli
    if ($LASTEXITCODE -ne 0) { throw "ure_cli build failed" }
}
if (-not (Test-Path $ExePath)) { throw "ure_cli executable not found: $ExePath" }
New-Item -ItemType Directory -Path $ResultDir -Force | Out-Null

$scenes = @(
    [ordered]@{
        name = "spectral_emissive_quad"
        path = Join-Path $RepoRoot "scenes\benchmarks\phase_l_spectral_budget.gltf"
    },
    [ordered]@{
        name = "multi_emissive_quad"
        path = Join-Path $RepoRoot "scenes\benchmarks\phase_r_multi_light_sampling.gltf"
    }
)

$sceneReports = @()
foreach ($scene in $scenes) {
    if (-not (Test-Path $scene.path)) { throw "missing benchmark scene: $($scene.path)" }
    $referenceName = "phase_r_$($scene.name)_spp$ReferenceSpp.hdr"
    $referenceRun = Invoke-Render -Scene $scene.path -Spp $ReferenceSpp -OutputName $referenceName
    $referenceImage = Read-RgbeImage -Path $referenceRun.output

    $curve = @()
    foreach ($spp in $CurveSpp) {
        if ($spp -ge $ReferenceSpp) { throw "CurveSpp values must be below ReferenceSpp" }
        $outputName = "phase_r_$($scene.name)_spp$spp.hdr"
        $run = Invoke-Render -Scene $scene.path -Spp $spp -OutputName $outputName
        $image = Read-RgbeImage -Path $run.output
        $mse = Get-ImageMse -A $image -B $referenceImage
        $variance = Get-ImageVariance -Image $image
        $pixels = [int64]$Width * [int64]$Height
        $samplesPerSecond = if ($run.elapsed_seconds -gt 0.0) {
            ($pixels * [int64]$spp) / [double]$run.elapsed_seconds
        } else {
            0.0
        }
        $curve += [ordered]@{
            spp = $spp
            output = $run.output
            elapsed_seconds = $run.elapsed_seconds
            mse_to_reference = [Math]::Round($mse, 12)
            radiance_variance = [Math]::Round($variance, 12)
            samples_per_second = [Math]::Round($samplesPerSecond, 3)
        }
    }

    if ($curve.Count -lt 2) { throw "light sampling suite requires at least two curve SPP values" }
    $firstMse = [double]$curve[0].mse_to_reference
    $lastMse = [double]$curve[$curve.Count - 1].mse_to_reference
    if ($firstMse -gt 0.0 -and $lastMse -gt $firstMse * $MaxMidMseRatio) {
        throw "MSE convergence check failed for $($scene.name): first=$firstMse last=$lastMse"
    }

    $sceneReports += [ordered]@{
        name = $scene.name
        scene = (Resolve-Path $scene.path).Path
        reference = [ordered]@{
            spp = $ReferenceSpp
            output = $referenceRun.output
            elapsed_seconds = $referenceRun.elapsed_seconds
            radiance_variance = [Math]::Round((Get-ImageVariance -Image $referenceImage), 12)
        }
        curve = $curve
        convergence = [ordered]@{
            first_mse = $firstMse
            last_mse = $lastMse
            max_mid_mse_ratio = $MaxMidMseRatio
            status = "passed"
        }
    }
}

$report = [ordered]@{
    phase = "R-P1"
    suite = "light_sampling_variance_mse_local"
    status = "passed"
    width = $Width
    height = $Height
    curve_spp = $CurveSpp
    reference_spp = $ReferenceSpp
    scenes = $sceneReports
}
$report | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $ResultPath -Encoding UTF8
Write-Host "Phase R-P1 light sampling suite passed"
Write-Host "Wrote $ResultPath"
