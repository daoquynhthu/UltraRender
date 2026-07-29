param(
    [Parameter(Mandatory = $true)]
    [string]$ReportPath,
    [switch]$RequireFarm
)

$ErrorActionPreference = "Stop"

function Assert-Condition {
    param(
        [bool]$Condition,
        [string]$Message
    )
    if (-not $Condition) {
        throw $Message
    }
}

function Assert-Digest {
    param(
        [object]$Value,
        [string]$Label
    )
    Assert-Condition (
        "$Value" -match "^[0-9a-f]{64}$"
    ) "$Label is not a SHA-256 digest"
}

function Assert-Commit {
    param([object]$Value)
    Assert-Condition (
        "$Value" -match "^[0-9a-f]{40,64}$"
    ) "source commit is not a Git object identifier"
}

function Assert-PositiveArray {
    param(
        [object[]]$Values,
        [int]$Count,
        [string]$Label
    )
    Assert-Condition (
        $Values.Count -eq $Count
    ) "$Label count is invalid"
    foreach ($value in $Values) {
        Assert-Condition (
            [double]$value -gt 0.0
        ) "$Label contains a non-positive value"
    }
}

$ResolvedReport = Resolve-Path -LiteralPath $ReportPath
$Report =
    Get-Content -Raw -LiteralPath $ResolvedReport |
    ConvertFrom-Json
$ExpectedBuildMax = @(15.0, 75.0, 250.0)
$ExpectedTraceMin = @(10.0, 10.0, 8.0)
$ExpectedVramMax = [uint64](4MB)
$ExpectedDynamicMax = [ordered]@{
    rigid = 5.0
    deforming = 25.0
    topology_change = 25.0
}

Assert-Condition (
    $Report.schema -eq
        "ure.phase_v.validation.v1"
) "invalid Phase V validation schema"
Assert-Condition (
    $Report.status -eq "passed"
) "Phase V validation status is not passed"
Assert-Condition (
    $Report.profile -in @("local", "farm")
) "invalid Phase V validation profile"
Assert-Condition (
    [uint32]$Report.repetitions -gt 0
) "Phase V validation repetition count is invalid"
Assert-Commit $Report.source.commit
Assert-Digest `
    $Report.artifacts.acceleration_executable_sha256 `
    "acceleration executable"
Assert-Digest `
    $Report.artifacts.core_library_sha256 `
    "core library"
Assert-Condition (
    @($Report.thresholds.dense_build_ms_max).Count -eq 3 -and
    @($Report.thresholds.dense_trace_mrays_min).Count -eq 3 -and
    (
        @($Report.thresholds.dense_build_ms_max |
            ForEach-Object { [double]$_ }) -join ","
    ) -eq ($ExpectedBuildMax -join ",") -and
    (
        @($Report.thresholds.dense_trace_mrays_min |
            ForEach-Object { [double]$_ }) -join ","
    ) -eq ($ExpectedTraceMin -join ",") -and
    [uint64]$Report.thresholds.dense_vram_bytes_max -eq
        $ExpectedVramMax -and
    [double]$Report.thresholds.dynamic_update_ms_max.rigid -eq
        $ExpectedDynamicMax.rigid -and
    [double]$Report.thresholds.dynamic_update_ms_max.deforming -eq
        $ExpectedDynamicMax.deforming -and
    [double]$Report.thresholds.dynamic_update_ms_max.topology_change -eq
        $ExpectedDynamicMax.topology_change
) "Phase V performance thresholds changed"

$TelemetryRuns = @($Report.evidence.build_telemetry)
Assert-Condition (
    $TelemetryRuns.Count -eq
        [uint32]$Report.repetitions
) "build telemetry repetition count is inconsistent"
foreach ($telemetry in $TelemetryRuns) {
    Assert-Condition (
        $telemetry.schema -eq
            "ure.phase_v.build_telemetry.v1"
    ) "invalid build telemetry schema"
    Assert-Condition (
        [uint64]$telemetry.large_mesh.triangle_count -ge
            18432
    ) "dense geometry triangle count is too small"
    Assert-Condition (
        [uint64]$telemetry.large_mesh.ray_count -ge
            4096
    ) "dense geometry ray count is too small"
    Assert-PositiveArray `
        @($telemetry.large_mesh.build_ms) 3 `
        "dense geometry build time"
    Assert-PositiveArray `
        @($telemetry.large_mesh.trace_ms) 3 `
        "dense geometry trace time"
    Assert-PositiveArray `
        @(
            $telemetry.large_mesh.
                trace_mrays_per_second
        ) 3 "dense geometry throughput"
    Assert-PositiveArray `
        @($telemetry.large_mesh.compact_bytes) 3 `
        "dense geometry compact bytes"
    Assert-Condition (
        [uint64]$telemetry.large_mesh.
            benchmark_vram_bytes -gt 0
    ) "dense geometry VRAM is invalid"
    Assert-Condition (
        [uint64]$telemetry.large_mesh.node_visits[1] -lt
            [uint64]$telemetry.large_mesh.node_visits[0] -and
        [uint64]$telemetry.large_mesh.node_visits[2] -lt
            [uint64]$telemetry.large_mesh.node_visits[0]
    ) "wide BVH traversal work did not improve"
    Assert-Condition (
        [uint64]$telemetry.large_mesh.
            spatial_split_count -gt 0
    ) "SBVH stress split was not exercised"
    for ($Preset = 0; $Preset -lt 3; ++$Preset) {
        Assert-Condition (
            [double]$telemetry.large_mesh.build_ms[$Preset] -le
                [double]$Report.thresholds.
                    dense_build_ms_max[$Preset] -and
            [double]$telemetry.large_mesh.
                trace_mrays_per_second[$Preset] -ge
                [double]$Report.thresholds.
                    dense_trace_mrays_min[$Preset]
        ) "dense geometry performance regression threshold failed"
    }
    Assert-Condition (
        [uint64]$telemetry.large_mesh.
            benchmark_vram_bytes -le
            [uint64]$Report.thresholds.
                dense_vram_bytes_max
    ) "dense geometry VRAM regression threshold failed"
    Assert-Condition (
        [double]$telemetry.async_pipeline.
            build_wall_ms -gt 0.0 -and
        [double]$telemetry.async_pipeline.
            upload_ms -gt 0.0 -and
        [uint64]$telemetry.async_pipeline.
            temporary_bytes_peak -gt 0 -and
        [uint64]$telemetry.async_pipeline.
            compacted_bytes -gt 0
    ) "asynchronous construction telemetry is invalid"
}

$DynamicRuns = @($Report.evidence.dynamic_geometry)
Assert-Condition (
    $DynamicRuns.Count -eq
        [uint32]$Report.repetitions
) "dynamic geometry repetition count is inconsistent"
foreach ($dynamic in $DynamicRuns) {
    Assert-Condition (
        $dynamic.schema -eq
            "ure.phase_v.dynamic_geometry.v1"
    ) "invalid dynamic geometry schema"
    Assert-Condition (
        [double]$dynamic.update_ms.rigid -gt 0.0 -and
        [double]$dynamic.update_ms.deforming -gt 0.0 -and
        [double]$dynamic.update_ms.topology_change -gt 0.0 -and
        [double]$dynamic.update_ms.rigid -le
            [double]$Report.thresholds.
                dynamic_update_ms_max.rigid -and
        [double]$dynamic.update_ms.deforming -le
            [double]$Report.thresholds.
                dynamic_update_ms_max.deforming -and
        [double]$dynamic.update_ms.topology_change -le
            [double]$Report.thresholds.
                dynamic_update_ms_max.topology_change
    ) "dynamic update timing is invalid"
    Assert-Condition (
        [uint64]$dynamic.operations.
            rigid_tlas_refit -eq 1 -and
        [uint64]$dynamic.operations.
            blas_rebuild -eq 2 -and
        [uint64]$dynamic.operations.
            topology_rebuild -eq 1
    ) "dynamic geometry operation counts are invalid"
    Assert-Condition (
        $dynamic.gates.depth_aov_correctness -and
        $dynamic.gates.invalid_acceleration_zero -and
        $dynamic.gates.unsupported_refit_rejected
    ) "dynamic geometry correctness gate failed"
}

$Lod = $Report.evidence.cluster_lod
Assert-Condition (
    $Lod.schema -eq
        "ure.phase_v.cluster_lod.v1" -and
    [uint64]$Lod.rays -gt 0 -and
    [uint64]$Lod.shadow.protected_mismatch -eq 0 -and
    [uint64]$Lod.reflection.protected_mismatch -eq 0 -and
    [uint64]$Lod.shadow.preview_proxy_mismatch -eq
        [uint64]$Lod.rays -and
    [uint64]$Lod.reflection.preview_proxy_mismatch -eq
        [uint64]$Lod.rays
) "cluster LoD visibility evidence is invalid"

$Parity = $Report.evidence.backend_parity
Assert-Condition (
    $Parity.schema -eq
        "ure.phase_v.cross_provider_parity.v1" -and
    $Parity.assertions.result -eq "passed"
) "backend parity evidence is invalid"
$SelfCompute = @(
    $Parity.providers |
        Where-Object provider -eq "self_compute")
Assert-Condition (
    $SelfCompute.Count -eq 1 -and
    $SelfCompute[0].status -eq "passed"
) "self-compute parity evidence is missing"
foreach ($provider in $Parity.providers) {
    Assert-Condition (
        $provider.status -in @(
            "passed",
            "unavailable")
    ) "backend parity provider status is invalid"
}

$Distributed = $Report.evidence.distributed_shards
Assert-Condition (
    [uint32]$Distributed.file_version -eq 5 -and
    $Distributed.roundtrip -eq "passed" -and
    $Distributed.merge_rejection -eq "passed" -and
    $Distributed.provenance -eq "passed"
) "distributed shard contract evidence is invalid"
$Inventory = $Distributed.inventory
Assert-Condition (
    $Inventory.schema -eq
        "ure.phase_t10.inventory.v1" -and
    $Inventory.heterogeneous -and
    [uint32]$Inventory.worker_count -eq
        @($Inventory.workers).Count
) "distributed inventory is invalid"
Assert-Digest `
    $Inventory.semantic_identity `
    "distributed semantic identity"
Assert-Digest `
    $Inventory.resource_set.content_hash `
    "distributed resource set"
Assert-Condition (
    [uint64]$Inventory.resource_set.
        descriptor_count -gt 0 -and
    [uint64]$Inventory.resource_set.
        logical_bytes -gt 0 -and
    [uint64]$Inventory.resource_set.
        minimum_resident_bytes -gt 0
) "distributed resource-set metadata is invalid"

$Backends = @()
$Cursor = [uint64]0
foreach ($worker in @(
    $Inventory.workers |
        Sort-Object {
            [uint64]$_.sample_start
        })) {
    Assert-Condition (
        [uint64]$worker.sample_start -eq $Cursor -and
        [uint64]$worker.sample_count -gt 0
    ) "distributed sample shards contain a gap or overlap"
    $Cursor += [uint64]$worker.sample_count
    Assert-Digest `
        $worker.executable `
        "distributed executable"
    Assert-Digest `
        $worker.resource_cache `
        "distributed resource cache"
    Assert-Condition (
        -not [string]::IsNullOrWhiteSpace(
            $worker.adapter_id) -and
        -not [string]::IsNullOrWhiteSpace(
            $worker.driver) -and
        -not [string]::IsNullOrWhiteSpace(
            $worker.compiler)
    ) "distributed worker identity is incomplete"
    $Backends += "$($worker.backend)"
}
Assert-Condition (
    $Cursor -gt 0 -and
    $Backends -contains "cuda" -and
    $Backends -contains "vulkan"
) "distributed schedule lacks required backends"

Assert-Condition (
    $Report.test_gate.status -eq "passed" -and
    [uint32]$Report.test_gate.failed -eq 0 -and
    [uint32]$Report.test_gate.passed -ge 54
) "registered CTest gate is incomplete"
$RequiredTests = @(
    "gpu_acceleration_contract",
    "gpu_clustered_geometry",
    "gpu_cluster_lod",
    "gpu_dynamic_geometry",
    "gpu_contract",
    "test_acceleration_contract",
    "test_clustered_geometry",
    "test_cluster_lod",
    "test_dynamic_geometry",
    "test_distributed_file_io",
    "test_multi_backend_schedule",
    "vulkan_acceleration",
    "multi_backend_inventory"
)
if ($Backends -contains "d3d12") {
    $RequiredTests += "d3d12_runtime"
}
foreach ($test in $RequiredTests) {
    Assert-Condition (
        @($Report.test_gate.required) -contains $test
    ) "required Phase V test is absent: $test"
}
Assert-Condition (
    $Report.static_gates.phase_t -and
    $Report.static_gates.phase_v -and
    $Report.static_gates.documentation
) "static or documentation gate failed"

if ($RequireFarm -or $Report.profile -eq "farm") {
    $Farm = $Report.farm_shard
    Assert-Condition (
        $Report.profile -eq "farm" -and
        -not [string]::IsNullOrWhiteSpace(
            $Farm.run_id) -and
        [uint32]$Farm.shard_count -gt 0 -and
        [uint32]$Farm.shard_index -lt
            [uint32]$Farm.shard_count -and
        [uint64]$Farm.sample_count -gt 0
    ) "farm shard metadata is invalid"
}

Write-Host "Phase V validation report contract passed: $ResolvedReport"
