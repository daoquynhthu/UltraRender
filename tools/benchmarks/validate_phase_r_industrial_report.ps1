param(
    [Parameter(Mandatory = $true)]
    [string]$ReportPath,
    [ValidateSet("LocalQuick", "Closure")]
    [string]$Profile = "LocalQuick"
)

$ErrorActionPreference = "Stop"

function Assert-Condition {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw $Message }
}

function Assert-PositiveNumber {
    param($Value, [string]$Name)
    Assert-Condition ($null -ne $Value -and [double]$Value -gt 0.0) "$Name must be positive"
}

function Test-FarmEvidence {
    param($Farm)
    Assert-Condition ($Farm.schema -eq "ure.phase_r.farm_evidence.v1") "invalid farm evidence schema"
    Assert-Condition ($Farm.status -eq "collected") "farm evidence was not collected"
    Assert-Condition (-not [string]::IsNullOrWhiteSpace($Farm.run_id)) "farm run_id is missing"
    Assert-Condition ([uint64]$Farm.expected_sample_count -ge 4096) "farm evidence is not a long-run"
    Assert-Condition ($Farm.shards.Count -ge 2) "farm evidence requires multiple shards"
    Assert-Condition ($Farm.executable_sha256 -match '^[0-9a-fA-F]{64}$') "farm executable hash is invalid"
    Assert-Condition ($Farm.merged_artifact_sha256 -match '^[0-9a-fA-F]{64}$') "farm merged artifact hash is invalid"
    Assert-Condition ($Farm.reference_artifact_sha256 -match '^[0-9a-fA-F]{64}$') "farm reference artifact hash is invalid"
    Assert-Condition ([double]$Farm.merge_normalized_mse -ge 0.0 -and
        [double]$Farm.merge_normalized_mse -le 1.0e-6) "farm merge error is invalid"
    Assert-Condition ([int]$Farm.width -gt 0 -and [int]$Farm.height -gt 0) "farm dimensions are invalid"

    $ordered = @($Farm.shards | Sort-Object { [uint64]$_.sample_begin })
    $workerIds = @{}
    $artifactHashes = @{}
    [uint64]$cursor = 0
    foreach ($shard in $ordered) {
        Assert-Condition (-not [string]::IsNullOrWhiteSpace($shard.worker_id)) "farm worker_id is missing"
        Assert-Condition (-not $workerIds.ContainsKey($shard.worker_id)) "farm worker_id is duplicated"
        $workerIds[$shard.worker_id] = $true
        Assert-Condition ($shard.artifact_sha256 -match '^[0-9a-fA-F]{64}$') "invalid farm artifact hash"
        Assert-Condition (-not $artifactHashes.ContainsKey($shard.artifact_sha256)) "farm shards reused the same framebuffer"
        $artifactHashes[$shard.artifact_sha256] = $true
        Assert-PositiveNumber $shard.elapsed_seconds "farm shard elapsed_seconds"
        [uint64]$begin = $shard.sample_begin
        [uint64]$end = $shard.sample_end
        Assert-Condition ($begin -eq $cursor) "farm sample ranges contain a gap or overlap at $cursor"
        Assert-Condition ($end -gt $begin) "farm sample range is empty"
        $cursor = $end
    }
    Assert-Condition ($cursor -eq [uint64]$Farm.expected_sample_count) "farm sample coverage is incomplete"
}

function Test-NsightEvidence {
    param($Nsight)
    Assert-Condition ($Nsight.schema -eq "ure.phase_r.nsight_evidence.v1") "invalid Nsight evidence schema"
    Assert-Condition ($Nsight.status -eq "collected") "Nsight evidence was not collected"
    Assert-Condition (-not [string]::IsNullOrWhiteSpace($Nsight.tool_version)) "Nsight tool version is missing"
    Assert-Condition ($Nsight.source_sha256 -match '^[0-9a-fA-F]{64}$') "invalid Nsight source hash"
    Assert-Condition ($Nsight.vram_source_sha256 -match '^[0-9a-fA-F]{64}$') "invalid VRAM source hash"
    Assert-Condition ($Nsight.profiled_executable_sha256 -match '^[0-9a-fA-F]{64}$') "invalid profiled executable hash"
    Assert-Condition ($Nsight.vram_measurement -in @("device_used_delta", "process_used_memory")) "invalid VRAM measurement method"
    Assert-Condition ($Nsight.kernels.Count -gt 0) "Nsight evidence has no kernels"
    Assert-PositiveNumber $Nsight.peak_vram_bytes "peak_vram_bytes"
    Assert-PositiveNumber $Nsight.total_kernel_launches "total_kernel_launches"
    foreach ($kernel in $Nsight.kernels) {
        Assert-Condition (-not [string]::IsNullOrWhiteSpace($kernel.name)) "Nsight kernel name is missing"
        Assert-PositiveNumber $kernel.launches "kernel launches"
        Assert-Condition ($null -ne $kernel.occupancy_pct -and
            [double]$kernel.occupancy_pct -ge 0.0 -and
            [double]$kernel.occupancy_pct -le 100.0) "kernel occupancy is outside [0, 100]"
    }
}

$resolved = Resolve-Path -LiteralPath $ReportPath
$report = Get-Content -Raw -LiteralPath $resolved | ConvertFrom-Json
Assert-Condition ($report.schema -eq "ure.phase_r.industrial_validation.v1") "invalid industrial report schema"
Assert-Condition ($report.status -eq "passed") "industrial validation did not pass"
Assert-Condition ($report.profile -eq $Profile.ToLowerInvariant()) "report profile does not match requested profile"
Assert-Condition ($report.identity.git_commit -match '^[0-9a-f]{40}$') "git commit identity is invalid"
if ($Profile -eq "Closure") {
    Assert-Condition ($report.identity.dirty -eq $false) "closure evidence must originate from a clean worktree"
}
Assert-Condition ($report.suites.Count -ge 8) "industrial report does not cover every Phase R suite"

$requiredSuites = @(
    "integrator_smoke",
    "light_sampling",
    "path_guiding",
    "restir_pt",
    "specular_manifold",
    "bidirectional",
    "mlt",
    "volume_mie"
)
foreach ($name in $requiredSuites) {
    $matches = @($report.suites | Where-Object { $_.name -eq $name })
    Assert-Condition ($matches.Count -eq 1) "suite '$name' must occur exactly once"
    Assert-Condition ($matches[0].status -eq "passed") "suite '$name' did not pass"
    Assert-Condition ($matches[0].artifact_sha256 -match '^[0-9a-fA-F]{64}$') "suite '$name' artifact hash is invalid"
}

$requiredMetrics = @("samples_per_second", "spectral_color_error", "variance", "mse", "time_to_error_seconds")
foreach ($metricName in $requiredMetrics) {
    $metric = $report.metrics.$metricName
    Assert-Condition ($null -ne $metric) "$metricName is missing"
    Assert-Condition ($metric.status -in @("collected", "not_collected", "unavailable")) "$metricName has invalid status"
    if ($metric.status -eq "collected") {
        Assert-Condition ($null -ne $metric.value -and [double]$metric.value -ge 0.0) "$metricName value is invalid"
    } elseif ($Profile -eq "Closure") {
        throw "$metricName must be collected for closure"
    }
}
Assert-PositiveNumber $report.metrics.samples_per_second.value "samples_per_second"

if ($Profile -eq "Closure") {
    Test-FarmEvidence $report.farm
    Test-NsightEvidence $report.nsight
    Assert-Condition ($report.farm.executable_sha256 -eq
        $report.nsight.profiled_executable_sha256) "farm and Nsight executable hashes differ"
    $requiredModes = @("path_guiding", "restir_pt", "specular_manifold", "bdpt", "vcm", "mlt")
    foreach ($mode in $requiredModes) {
        $matches = @($report.integrator_gates | Where-Object { $_.mode -eq $mode })
        Assert-Condition ($matches.Count -eq 1) "integrator gate '$mode' must occur exactly once"
        $gate = $matches[0]
        Assert-Condition ($gate.positive_benefit_count -ge 1) "$($gate.mode) has no positive-benefit scene"
        Assert-Condition ($gate.boundary_failure_count -ge 1) "$($gate.mode) has no boundary-failure scene"
    }
} else {
    Assert-Condition ($report.farm.status -in @("not_collected", "unavailable")) "local quick farm status is invalid"
    Assert-Condition ($report.nsight.status -in @("not_collected", "unavailable")) "local quick Nsight status is invalid"
}

Write-Host "Phase R industrial report validation passed ($Profile): $resolved"
