param(
    [string]$BuildDir = "build_modular_x64",
    [string]$Config = "Release",
    [ValidateSet("Local", "Farm")]
    [string]$Profile = "Local",
    [ValidateRange(1, 32)]
    [int]$Repetitions = 1,
    [string]$OutputPath =
        "output/benchmarks/phase_v_validation.json",
    [string]$RunId = "",
    [uint32]$ShardIndex = 0,
    [uint32]$ShardCount = 0,
    [uint64]$SampleStart = 0,
    [uint64]$SampleCount = 0,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$BuildPath = Join-Path $RepoRoot $BuildDir
$EvidenceRoot =
    Join-Path $RepoRoot ".build\phase_v_validation"
$InventoryPath =
    Join-Path $EvidenceRoot "inventory.json"
$ResolvedOutput = Join-Path $RepoRoot $OutputPath

function Invoke-Checked {
    param(
        [scriptblock]$Command,
        [string]$Label
    )
    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "$Label failed with exit code $LASTEXITCODE"
    }
}

function Read-Json {
    param(
        [string]$Path,
        [string]$Schema
    )
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "missing Phase V evidence: $Path"
    }
    $Value =
        Get-Content -Raw -LiteralPath $Path |
        ConvertFrom-Json
    if ($Value.schema -ne $Schema) {
        throw "unexpected evidence schema in $Path"
    }
    $Value
}

function File-Digest {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "missing Phase V artifact: $Path"
    }
    (
        Get-FileHash -LiteralPath $Path `
            -Algorithm SHA256
    ).Hash.ToLowerInvariant()
}

if ($Profile -eq "Farm") {
    if ([string]::IsNullOrWhiteSpace($RunId) -or
        $ShardCount -eq 0 -or
        $ShardIndex -ge $ShardCount -or
        $SampleCount -eq 0) {
        throw "Farm profile requires valid run and sample-shard metadata"
    }
}

New-Item -ItemType Directory -Force `
    -Path $EvidenceRoot | Out-Null
New-Item -ItemType Directory -Force `
    -Path (Split-Path -Parent $ResolvedOutput) |
    Out-Null

$Targets = @(
    "gpu_test_acceleration_contract",
    "gpu_test_cuda_runtime",
    "gpu_test_cluster_lod",
    "gpu_test_dynamic_geometry",
    "gpu_test_contract",
    "test_session",
    "test_cluster_lod",
    "test_dynamic_geometry",
    "test_distributed_file_io",
    "test_multi_backend_schedule",
    "test_multi_backend_inventory",
    "test_vulkan_acceleration"
)
$D3dExecutable = Join-Path $BuildPath `
    "tests\d3d12\test_d3d12_runtime.exe"
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
if (Test-Path -LiteralPath $D3dExecutable) {
    $Targets += "test_d3d12_runtime"
    $RequiredTests += "d3d12_runtime"
}
if (-not $SkipBuild) {
    Invoke-Checked {
        & cmake --build $BuildPath `
            --config $Config `
            --target @Targets
    } "Phase V validation build"
}

$BuildTelemetry = @()
$DynamicGeometry = @()
for ($Run = 0; $Run -lt $Repetitions; ++$Run) {
    $RunRoot = Join-Path $EvidenceRoot `
        ("run_{0:D2}" -f $Run)
    New-Item -ItemType Directory -Force `
        -Path $RunRoot | Out-Null
    $BuildReport =
        Join-Path $RunRoot "build_telemetry.json"
    $DynamicReport =
        Join-Path $RunRoot "dynamic_geometry.json"
    Invoke-Checked {
        & (Join-Path $RepoRoot `
            "tools\benchmarks\run_phase_v_build_telemetry.ps1") `
            -BuildDir $BuildDir `
            -Config $Config `
            -OutputPath (
                [IO.Path]::GetRelativePath(
                    $RepoRoot, $BuildReport)) `
            -SkipBuild
    } "Phase V dense geometry telemetry"
    Invoke-Checked {
        & (Join-Path $RepoRoot `
            "tools\benchmarks\run_phase_v10_dynamic_geometry.ps1") `
            -BuildDir $BuildDir `
            -Config $Config `
            -OutputPath (
                [IO.Path]::GetRelativePath(
                    $RepoRoot, $DynamicReport)) `
            -SkipBuild
    } "Phase V dynamic geometry telemetry"
    $BuildTelemetry += Read-Json `
        $BuildReport `
        "ure.phase_v.build_telemetry.v1"
    $DynamicGeometry += Read-Json `
        $DynamicReport `
        "ure.phase_v.dynamic_geometry.v1"
}

$LodReport = Join-Path $EvidenceRoot `
    "cluster_lod.json"
Invoke-Checked {
    & (Join-Path $RepoRoot `
        "tools\benchmarks\run_phase_v9_lod_visibility.ps1") `
        -BuildDir $BuildDir `
        -Config $Config `
        -OutputPath (
            [IO.Path]::GetRelativePath(
                $RepoRoot, $LodReport)) `
        -SkipBuild
} "Phase V cluster LoD visibility"
$ClusterLod = Read-Json `
    $LodReport `
    "ure.phase_v.cluster_lod.v1"

Invoke-Checked {
    & (Join-Path $RepoRoot `
        "scripts\run_phase_v7_cross_provider_parity.ps1") `
        -BuildDir $BuildDir `
        -Config $Config `
        -SkipBuild
} "Phase V backend parity"
$BackendParity = Read-Json `
    (Join-Path $RepoRoot `
        ".build\phase_v7_parity\report.json") `
    "ure.phase_v.cross_provider_parity.v1"

$PreviousInventory = $env:UR_PHASE_T10_REPORT
try {
    $env:UR_PHASE_T10_REPORT = $InventoryPath
    $InventoryExecutable = Join-Path $BuildPath `
        "tests\multi_backend\test_multi_backend_inventory.exe"
    Invoke-Checked {
        & $InventoryExecutable
    } "Phase V distributed inventory"
} finally {
    $env:UR_PHASE_T10_REPORT = $PreviousInventory
}
$Inventory = Read-Json `
    $InventoryPath `
    "ure.phase_t10.inventory.v1"

$CTestOutput = & ctest `
    --test-dir $BuildPath `
    -C $Config `
    --output-on-failure 2>&1 |
    Out-String
if ($LASTEXITCODE -ne 0) {
    throw "Phase V registered test gate failed`n$CTestOutput"
}
$CTestMatch = [regex]::Match(
    $CTestOutput,
    '100% tests passed, 0 tests failed out of (\d+)')
if (-not $CTestMatch.Success) {
    throw "Phase V registered test summary is missing"
}
$PassedTests = [uint32]$CTestMatch.Groups[1].Value

Invoke-Checked {
    & (Join-Path $RepoRoot `
        "scripts\check_phase_t_static.ps1")
} "Phase T static audit"
Invoke-Checked {
    & (Join-Path $RepoRoot `
        "scripts\check_phase_v_static.ps1")
} "Phase V static audit"
Invoke-Checked {
    & (Join-Path $RepoRoot `
        "scripts\check_documentation_consistency.ps1")
} "Documentation consistency audit"

$Commit = (
    & git -C $RepoRoot rev-parse HEAD
).Trim()
if ($LASTEXITCODE -ne 0 -or
    $Commit -notmatch "^[0-9a-f]{40}$") {
    throw "Phase V source commit is unavailable"
}
$TreeState = if (
    [string]::IsNullOrWhiteSpace(
        (& git -C $RepoRoot status --porcelain |
            Out-String))) {
    "clean"
} else {
    "dirty"
}

$FarmShard = $null
if ($Profile -eq "Farm") {
    $FarmShard = [ordered]@{
        run_id = $RunId
        shard_index = $ShardIndex
        shard_count = $ShardCount
        sample_start = $SampleStart
        sample_count = $SampleCount
    }
}

$Report = [ordered]@{
    schema = "ure.phase_v.validation.v1"
    generated_utc = [DateTime]::UtcNow.ToString("o")
    status = "passed"
    profile = $Profile.ToLowerInvariant()
    repetitions = $Repetitions
    source = [ordered]@{
        commit = $Commit
        tree_state = $TreeState
    }
    artifacts = [ordered]@{
        acceleration_executable_sha256 = File-Digest (
            Join-Path $BuildPath `
                "tests\gpu\gpu_test_acceleration_contract.exe")
        core_library_sha256 = File-Digest (
            Join-Path $BuildPath `
                "libs\ure_core\ure_core.lib")
    }
    thresholds = [ordered]@{
        dense_build_ms_max = @(15.0, 75.0, 250.0)
        dense_trace_mrays_min = @(10.0, 10.0, 8.0)
        dense_vram_bytes_max = 4MB
        dynamic_update_ms_max = [ordered]@{
            rigid = 5.0
            deforming = 25.0
            topology_change = 25.0
        }
    }
    evidence = [ordered]@{
        build_telemetry = $BuildTelemetry
        backend_parity = $BackendParity
        dynamic_geometry = $DynamicGeometry
        cluster_lod = $ClusterLod
        distributed_shards = [ordered]@{
            file_version = 5
            roundtrip = "passed"
            merge_rejection = "passed"
            provenance = "passed"
            inventory = $Inventory
        }
    }
    test_gate = [ordered]@{
        status = "passed"
        passed = $PassedTests
        failed = 0
        required = $RequiredTests
    }
    static_gates = [ordered]@{
        phase_t = $true
        phase_v = $true
        documentation = $true
    }
    farm_shard = $FarmShard
}
$Report | ConvertTo-Json -Depth 24 |
    Set-Content -LiteralPath $ResolvedOutput `
        -Encoding utf8

Invoke-Checked {
    & (Join-Path $RepoRoot `
        "tools\benchmarks\validate_phase_v_validation_report.ps1") `
        -ReportPath $ResolvedOutput `
        -RequireFarm:($Profile -eq "Farm")
} "Phase V validation report"
Invoke-Checked {
    & (Join-Path $RepoRoot `
        "tools\benchmarks\test_phase_v_validation_report_contract.ps1") `
        -ReportPath $ResolvedOutput
} "Phase V validation report negative contract"

Write-Host "Phase V validation suite passed: $ResolvedOutput"
