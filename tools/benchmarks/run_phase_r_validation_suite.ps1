param(
    [string]$BuildDir = "build_modular_x64",
    [string]$Config = "Release",
    [int]$Width = 64,
    [int]$Height = 64,
    [int]$Spp = 4,
    [double]$MinSamplesPerSecond = 1.0,
    [double]$MinSppPerSecond = 0.001,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$BuildPath = Join-Path $RepoRoot $BuildDir
$ReportDir = Join-Path $RepoRoot "output\benchmarks"
$ReportPath = Join-Path $ReportDir "phase_r_validation_suite.json"
$SmokeReportPath = Join-Path $ReportDir "phase_r_integrator_smoke.json"
$CtestRegex = "^(test_config|test_integrator|test_session|test_pyure_smoke|gpu_render|gpu_spectral|gpu_volume|gpu_polarization)$"

function Invoke-PhaseRStep {
    param(
        [string]$Name,
        [scriptblock]$Body
    )
    $elapsed = Measure-Command { & $Body }
    [ordered]@{
        name = $Name
        status = "passed"
        elapsed_seconds = [Math]::Round($elapsed.TotalSeconds, 6)
    }
}

New-Item -ItemType Directory -Path $ReportDir -Force | Out-Null
Push-Location $RepoRoot
try {
    $steps = @()

    if (-not $SkipBuild) {
        $steps += Invoke-PhaseRStep "build_all" {
            & (Join-Path $RepoRoot "scripts\build_x64.ps1") -BuildDir $BuildDir -Config $Config
            if ($LASTEXITCODE -ne 0) { throw "build failed" }
        }
    }

    $steps += Invoke-PhaseRStep "phase_r_static_audit" {
        & (Join-Path $RepoRoot "scripts\check_phase_r_static.ps1")
        if ($LASTEXITCODE -ne 0) { throw "Phase R static audit failed" }
    }

    $ctestOutput = $null
    $steps += Invoke-PhaseRStep "phase_r_ctest_subset" {
        $script:ctestOutput = & ctest --test-dir $BuildPath -R $CtestRegex --output-on-failure 2>&1
        if ($LASTEXITCODE -ne 0) {
            $script:ctestOutput | ForEach-Object { Write-Host $_ }
            throw "Phase R CTest subset failed"
        }
    }

    $ctestTotal = 0
    $ctestFailed = 0
    $ctestOutputText = ($ctestOutput -join "`n")
    if ($ctestOutputText -match "(\d+)% tests passed, (\d+) tests failed out of (\d+)") {
        $ctestFailed = [int]$Matches[2]
        $ctestTotal = [int]$Matches[3]
    }
    if ($ctestTotal -lt 8 -or $ctestFailed -ne 0) {
        throw "Phase R CTest subset did not prove the expected local coverage"
    }

    $steps += Invoke-PhaseRStep "integrator_smoke_benchmark" {
        & (Join-Path $RepoRoot "tools\benchmarks\run_phase_r_integrator_smoke.ps1") `
            -BuildDir $BuildDir `
            -Config $Config `
            -Width $Width `
            -Height $Height `
            -Spp $Spp `
            -SkipBuild
        if ($LASTEXITCODE -ne 0) { throw "Phase R integrator smoke benchmark failed" }
    }

    if (-not (Test-Path $SmokeReportPath)) {
        throw "missing integrator smoke report: $SmokeReportPath"
    }
    $smoke = Get-Content -Raw -LiteralPath $SmokeReportPath | ConvertFrom-Json
    if ([double]$smoke.samples_per_second -lt $MinSamplesPerSecond) {
        throw "samples_per_second below validation floor"
    }
    if ([double]$smoke.spp_per_second -lt $MinSppPerSecond) {
        throw "spp_per_second below validation floor"
    }

    $report = [ordered]@{
        phase = "R"
        suite = "industrial_validation_local"
        status = "passed"
        build_dir = (Resolve-Path $BuildPath).Path
        config = $Config
        ctest_regex = $CtestRegex
        ctest_total = $ctestTotal
        ctest_failed = $ctestFailed
        benchmark = $smoke
        thresholds = [ordered]@{
            min_samples_per_second = $MinSamplesPerSecond
            min_spp_per_second = $MinSppPerSecond
        }
        steps = $steps
    }
    $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $ReportPath -Encoding UTF8
    Write-Host "Phase R validation suite passed"
    Write-Host "Wrote $ReportPath"
} finally {
    Pop-Location
}
