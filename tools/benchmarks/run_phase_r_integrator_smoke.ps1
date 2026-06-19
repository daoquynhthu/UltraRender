param(
    [string]$BuildDir = "build_modular_x64",
    [string]$Config = "Release",
    [string]$Scene = "scenes\benchmarks\phase_l_spectral_budget.gltf",
    [int]$Width = 64,
    [int]$Height = 64,
    [int]$Spp = 4,
    [string]$Format = "hdr",
    [string]$Output = "phase_r_integrator_smoke.hdr",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$BuildPath = Join-Path $RepoRoot $BuildDir
$ExePath = Join-Path $BuildPath "apps\ure_cli\ure_cli.exe"
$ScenePath = Join-Path $RepoRoot $Scene
$ResultDir = Join-Path $RepoRoot "output\benchmarks"
$ResultPath = Join-Path $ResultDir "phase_r_integrator_smoke.json"

if (-not $SkipBuild) {
    & (Join-Path $RepoRoot "scripts\build_x64.ps1") -BuildDir $BuildDir -Config $Config -Target ure_cli
    if ($LASTEXITCODE -ne 0) { throw "ure_cli build failed" }
}

if (-not (Test-Path $ExePath)) { throw "ure_cli executable not found: $ExePath" }
if (-not (Test-Path $ScenePath)) { throw "scene not found: $ScenePath" }
New-Item -ItemType Directory -Path $ResultDir -Force | Out-Null

$elapsed = Measure-Command {
    & $ExePath --quiet render $ScenePath --width $Width --height $Height --spp $Spp --format $Format --output $Output
    if ($LASTEXITCODE -ne 0) { throw "render command failed" }
}

$pixels = [int64]$Width * [int64]$Height
$sampleCount = $pixels * [int64]$Spp
$sppPerSecond = if ($elapsed.TotalSeconds -gt 0.0) { $sampleCount / $pixels / $elapsed.TotalSeconds } else { 0.0 }
$samplesPerSecond = if ($elapsed.TotalSeconds -gt 0.0) { $sampleCount / $elapsed.TotalSeconds } else { 0.0 }

$result = [ordered]@{
    phase = "R"
    benchmark = "integrator_smoke"
    scene = (Resolve-Path $ScenePath).Path
    width = $Width
    height = $Height
    spp = $Spp
    elapsed_seconds = [Math]::Round($elapsed.TotalSeconds, 6)
    samples_per_second = [Math]::Round($samplesPerSecond, 3)
    spp_per_second = [Math]::Round($sppPerSecond, 6)
    output = $Output
}

$result | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $ResultPath -Encoding UTF8
Write-Host "Wrote $ResultPath"
