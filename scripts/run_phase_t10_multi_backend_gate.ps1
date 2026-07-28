param(
    [string]$BuildDir = "build_modular_x64",
    [string]$SdkFreeBuild =
        ".build/phase_t10_sdk_free"
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (
    Join-Path $PSScriptRoot "..")
$BuildPath = Join-Path $RepoRoot $BuildDir
$SdkFreePath =
    Join-Path $RepoRoot $SdkFreeBuild
$ReportRoot =
    Join-Path $RepoRoot ".build/phase_t10_gate"
$InventoryPath =
    Join-Path $ReportRoot "inventory.json"

function Require-Success {
    param([string]$Label)
    if ($LASTEXITCODE -ne 0) {
        throw "$Label failed with exit code $LASTEXITCODE"
    }
}

Push-Location $RepoRoot
try {
    New-Item -ItemType Directory -Force `
        $ReportRoot |
        Out-Null
    & cmake --build $BuildPath --config Release `
        --target test_multi_backend_schedule `
        test_distributed_file_io `
        test_multi_backend_inventory `
        gpu_test_hardware
    Require-Success "T.10 production-tree build"
    $PreviousReport = $env:UR_PHASE_T10_REPORT
    try {
        $env:UR_PHASE_T10_REPORT =
            $InventoryPath
        & ctest --test-dir $BuildPath -C Release `
            -R "^(test_multi_backend_schedule|test_distributed_file_io|multi_backend_inventory|gpu_hardware)$" `
            --output-on-failure
        Require-Success "T.10 production-tree execution"
    } finally {
        $env:UR_PHASE_T10_REPORT =
            $PreviousReport
    }
    if (-not (Test-Path -LiteralPath $InventoryPath)) {
        throw "T.10 actual inventory report is missing"
    }
    $Inventory =
        Get-Content -Raw $InventoryPath |
        ConvertFrom-Json
    $Backends = @(
        $Inventory.workers |
        ForEach-Object { $_.backend } |
        Sort-Object -Unique)
    if (-not $Inventory.heterogeneous -or
        $Backends -notcontains "cuda" -or
        $Backends -notcontains "vulkan") {
        throw "T.10 actual inventory is not heterogeneous"
    }
    if ($Inventory.semantic_identity -notmatch
            "^[0-9a-f]{64}$" -or
        @($Inventory.workers |
            Where-Object {
                $_.executable -notmatch
                    "^[0-9a-f]{64}$"
            }).Count -ne 0) {
        throw "T.10 executable or semantic identity is invalid"
    }
    $CacheKeys = @(
        $Inventory.workers |
        ForEach-Object { $_.resource_cache })
    if (@($CacheKeys |
            Where-Object {
                $_ -notmatch "^[0-9a-f]{64}$"
            }).Count -ne 0) {
        throw "T.10 actual inventory cache key is invalid"
    }
    foreach ($group in @(
        $Inventory.workers |
        Group-Object resource_cache)) {
        $GroupBackends = @(
            $group.Group.backend |
            Sort-Object -Unique)
        if ($GroupBackends.Count -gt 1) {
            throw "T.10 cache key aliases different backends"
        }
    }
    $RootCache =
        Get-Content -Raw (
            Join-Path $BuildPath "CMakeCache.txt")
    if ($RootCache -match
            "UR_ENABLE_D3D12:BOOL=ON" -and
        $Backends -notcontains "d3d12") {
        throw "T.10 enabled D3D12 backend is absent"
    }

    & cmake -S tests/sdk_free -B $SdkFreePath `
        -G Ninja `
        -DCMAKE_BUILD_TYPE=Release
    Require-Success "T.10 SDK-free configure"
    & cmake --build $SdkFreePath `
        --config Release
    Require-Success "T.10 SDK-free build"
    & ctest --test-dir $SdkFreePath -C Release `
        --output-on-failure
    Require-Success "T.10 SDK-free execution"

    & .\scripts\check_phase_t_static.ps1
    Require-Success "Phase T static audit"

    $Report = [ordered]@{
        schema = "ure.phase_t10.gate.v1"
        generated_utc =
            [DateTime]::UtcNow.ToString("o")
        contract = [ordered]@{
            canonical_weighted_sample_partition = $true
            feature_negotiation = $true
            precision_negotiation = $true
            coherence_negotiation = $true
            resident_budget_negotiation = $true
            semantic_identity = $true
            backend_artifact_identity = $true
            backend_resource_cache_key = $true
        }
        distributed = [ordered]@{
            file_version = 5
            legacy_v4_read = $true
            cross_backend_sample_merge = $true
            canonical_provenance = $true
            overlap_rejection = $true
            coherent_rgb_rejection = $true
        }
        sdk_free = [ordered]@{
            configured = $true
            warnings_as_errors = $true
            tests_passed = $true
        }
        inventory = $Inventory
    }
    $Report |
        ConvertTo-Json -Depth 8 |
        Set-Content -Encoding utf8 (
            Join-Path $ReportRoot "report.json")
    Write-Host (
        "Phase T.10 multi-backend gate passed: " +
        "capability negotiation, deterministic sample shards, " +
        "backend cache identity, distributed provenance, " +
        "actual CUDA/Vulkan/D3D12 inventory, and SDK-free build.")
} finally {
    Pop-Location
}
