param(
    [string]$BuildDir = "build_modular_x64",
    [string]$Config = "Release",
    [int]$Width = 16,
    [int]$Height = 16,
    [int]$Spp = 4,
    [ValidateSet("LocalQuick", "Closure")]
    [string]$Profile = "LocalQuick",
    [string]$FarmReportPath,
    [string]$NsightReportPath,
    [switch]$ReuseEvidence,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$BuildPath = Join-Path $RepoRoot $BuildDir
$ReportDir = Join-Path $RepoRoot "output\benchmarks"
$ReportPath = Join-Path $ReportDir "phase_r_industrial_validation.json"
$CtestArtifactPath = Join-Path $ReportDir "phase_r_ctest_evidence.txt"
$CtestRegex = "^(test_config|test_integrator|test_session|test_mie_phase|test_pyure_smoke|gpu_render|gpu_spectral|gpu_volume|gpu_polarization)$"

function Invoke-Step {
    param([string]$Name, [scriptblock]$Body)
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    & $Body
    $timer.Stop()
    [ordered]@{ name = $Name; status = "passed"; elapsed_seconds = [Math]::Round($timer.Elapsed.TotalSeconds, 6) }
}

function Get-Evidence {
    param([string]$Name, [string]$Path, [string]$ExpectedSchema)
    if (-not (Test-Path -LiteralPath $Path)) { throw "missing $Name evidence: $Path" }
    $item = Get-Item -LiteralPath $Path
    $json = if ($item.Extension -eq ".json") { Get-Content -Raw -LiteralPath $Path | ConvertFrom-Json } else { $null }
    if ($null -ne $json) {
        if ($json.status -ne "passed") { throw "$Name evidence did not pass" }
        if ($json.schema -ne $ExpectedSchema) {
            throw "$Name evidence schema is '$($json.schema)', expected '$ExpectedSchema'"
        }
    }
    [ordered]@{
        name = $Name
        status = "passed"
        artifact = $item.FullName
        artifact_sha256 = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        report = $json
    }
}

function Import-ExternalEvidence {
    param([string]$Path, [string]$Kind)
    if ([string]::IsNullOrWhiteSpace($Path)) { return [ordered]@{ status = "not_collected"; reason = "$Kind report was not supplied" } }
    if (-not (Test-Path -LiteralPath $Path)) { throw "$Kind report does not exist: $Path" }
    Get-Content -Raw -LiteralPath $Path | ConvertFrom-Json
}

New-Item -ItemType Directory -Path $ReportDir -Force | Out-Null
if ($Profile -eq "Closure" -and $ReuseEvidence) {
    throw "Closure profile cannot reuse prior benchmark evidence"
}
Push-Location $RepoRoot
try {
    $steps = @()
    if (-not $SkipBuild) {
        $steps += Invoke-Step "build_all" {
            & (Join-Path $RepoRoot "scripts\build_x64.ps1") -BuildDir $BuildDir -Config $Config
            if ($LASTEXITCODE -ne 0) { throw "build failed" }
        }
    }
    $steps += Invoke-Step "phase_r_static_audit" { & (Join-Path $RepoRoot "scripts\check_phase_r_static.ps1") }
    $steps += Invoke-Step "phase_r_ctest" {
        $output = & ctest --test-dir $BuildPath -C $Config -R $CtestRegex --output-on-failure 2>&1
        $output | Set-Content -LiteralPath $CtestArtifactPath -Encoding utf8
        if ($LASTEXITCODE -ne 0) { $output | Write-Host; throw "Phase R CTest gate failed" }
    }

    $jobs = @(
        [ordered]@{ name = "integrator_smoke"; schema = "ure.phase_r.integrator_smoke.v1"; script = "run_phase_r_integrator_smoke.ps1"; report = "phase_r_integrator_smoke.json"; args = @{ Width = $Width; Height = $Height; Spp = $Spp } },
        [ordered]@{ name = "light_sampling"; schema = "ure.phase_r.light_sampling_suite.v1"; script = "run_phase_r_light_sampling_suite.ps1"; report = "phase_r_light_sampling_suite.json"; args = @{ Width = $Width; Height = $Height } },
        [ordered]@{ name = "path_guiding"; schema = "ure.phase_r.path_guiding_suite.v1"; script = "run_phase_r_path_guiding_suite.ps1"; report = "phase_r_path_guiding_suite.json"; args = @{ Width = $Width; Height = $Height } },
        [ordered]@{ name = "restir_pt"; schema = "ure.phase_r.restir_pt_suite.v1"; script = "run_phase_r_restir_pt_suite.ps1"; report = "phase_r_restir_pt_suite.json"; args = @{ Width = $Width; Height = $Height } },
        [ordered]@{ name = "specular_manifold"; schema = "ure.phase_r.manifold_suite.v1"; script = "run_phase_r_manifold_suite.ps1"; report = "phase_r_manifold_suite.json"; args = if ($Profile -eq "LocalQuick") { @{ Scenes = @("glass_caustic") } } else { @{} } },
        [ordered]@{ name = "bidirectional"; schema = "ure.phase_r.bidirectional_suite.v1"; script = "run_phase_r_bidirectional_suite.ps1"; report = "phase_r_bidirectional_suite.json"; args = if ($Profile -eq "LocalQuick") { @{ Scenes = @("glass_caustic") } } else { @{ MinBdptBenefitScenes = 1; MinVcmBenefitScenes = 1 } } },
        [ordered]@{ name = "mlt"; schema = "ure.phase_r.mlt_suite.v2"; script = "run_phase_r_mlt_suite.ps1"; report = "phase_r_mlt_suite.json"; args = if ($Profile -eq "LocalQuick") { @{ Scenes = @("sds", "sds_small_light"); MinBenefitScenes = 0 } } else { @{} } }
    )
    $evidence = @()
    foreach ($job in $jobs) {
        if (-not $ReuseEvidence) {
            $steps += Invoke-Step $job.name {
                $scriptPath = Join-Path $PSScriptRoot $job.script
                $jobArgs = @{ BuildDir = $BuildDir; Config = $Config; SkipBuild = $true }
                foreach ($key in $job.args.Keys) { $jobArgs[$key] = $job.args[$key] }
                & $scriptPath @jobArgs
            }
        }
        $evidence += Get-Evidence $job.name (Join-Path $ReportDir $job.report) $job.schema
    }
    $evidence += Get-Evidence "volume_mie" $CtestArtifactPath $null

    $smoke = ($evidence | Where-Object name -eq "integrator_smoke").report
    $light = ($evidence | Where-Object name -eq "light_sampling").report
    $guiding = ($evidence | Where-Object name -eq "path_guiding").report
    $restir = ($evidence | Where-Object name -eq "restir_pt").report
    $manifold = ($evidence | Where-Object name -eq "specular_manifold").report
    $bidirectional = ($evidence | Where-Object name -eq "bidirectional").report
    $mlt = ($evidence | Where-Object name -eq "mlt").report
    $commit = (& git rev-parse HEAD).Trim()
    $dirty = -not [string]::IsNullOrWhiteSpace((& git status --porcelain) -join "")
    $report = [ordered]@{
        schema = "ure.phase_r.industrial_validation.v1"
        profile = $Profile.ToLowerInvariant()
        status = "passed"
        identity = [ordered]@{
            git_commit = $commit
            dirty = $dirty
            generated_utc = [DateTime]::UtcNow.ToString("o")
            build_config = $Config
            computer = $env:COMPUTERNAME
        }
        suites = @($evidence | ForEach-Object {
            [ordered]@{ name = $_.name; status = $_.status; artifact = $_.artifact; artifact_sha256 = $_.artifact_sha256 }
        })
        metrics = [ordered]@{
            samples_per_second = [ordered]@{ status = "collected"; value = [double]$smoke.samples_per_second; source = "integrator_smoke" }
            spectral_color_error = [ordered]@{ status = "collected"; value = [double]$light.scenes[0].curve[-1].mean_delta_e_76; metric = "CIE76 from linear spectral-render RGB reconstruction"; source = "light_sampling" }
            variance = [ordered]@{ status = "collected"; value = [double]$light.scenes[0].curve[-1].radiance_variance; source = "light_sampling" }
            mse = [ordered]@{ status = "collected"; value = [double]$light.scenes[0].curve[-1].mse_to_reference; source = "light_sampling" }
            time_to_error_seconds = [ordered]@{ status = "collected"; value = [double]$guiding.scenes[0].modes[0].time_to_error_seconds; source = "path_guiding" }
        }
        integrator_gates = @(
            [ordered]@{ mode = "path_guiding"; positive_benefit_count = [int]$guiding.benefit_scene_count; boundary_failure_count = [int]$guiding.boundary_scene_count },
            [ordered]@{ mode = "restir_pt"; positive_benefit_count = [int]$restir.benefit_scene_count; boundary_failure_count = [int]$restir.boundary_scene_count },
            [ordered]@{ mode = "specular_manifold"; positive_benefit_count = [int]$manifold.benefit_scene_count; boundary_failure_count = [int]$manifold.boundary_scene_count },
            [ordered]@{ mode = "bdpt"; positive_benefit_count = [int]$bidirectional.bdpt_benefit_scene_count; boundary_failure_count = [int]$bidirectional.bdpt_boundary_scene_count },
            [ordered]@{ mode = "vcm"; positive_benefit_count = [int]$bidirectional.vcm_benefit_scene_count; boundary_failure_count = [int]$bidirectional.vcm_boundary_scene_count },
            [ordered]@{ mode = "mlt"; positive_benefit_count = [int]$mlt.benefit_scene_count; boundary_failure_count = [int]$mlt.boundary_scene_count }
        )
        farm = Import-ExternalEvidence $FarmReportPath "farm"
        nsight = Import-ExternalEvidence $NsightReportPath "Nsight"
        steps = $steps
    }
    $report | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $ReportPath -Encoding utf8
    & (Join-Path $PSScriptRoot "validate_phase_r_industrial_report.ps1") -ReportPath $ReportPath -Profile $Profile
    Write-Host "Wrote $ReportPath"
} finally {
    Pop-Location
}
