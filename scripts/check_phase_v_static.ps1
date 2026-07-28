$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")

function Assert-Contains {
    param(
        [string]$Path,
        [string]$Pattern,
        [string]$Label
    )
    & rg -q $Pattern (Join-Path $RepoRoot $Path)
    if ($LASTEXITCODE -ne 0) {
        throw $Label
    }
}

function Assert-NoMatch {
    param(
        [string[]]$Paths,
        [string]$Pattern,
        [string]$Label
    )
    $matches = & rg -n $Pattern @Paths 2>$null
    if ($LASTEXITCODE -eq 0) {
        $matches | Write-Host
        throw $Label
    }
    if ($LASTEXITCODE -ne 1) {
        throw "rg failed while checking $Label"
    }
    $global:LASTEXITCODE = 0
}

function Assert-Hash {
    param(
        [string]$Path,
        [string]$Expected,
        [string]$Label
    )
    $fullPath = Join-Path $RepoRoot $Path
    if (-not (Test-Path -LiteralPath $fullPath)) {
        throw "$Label is missing"
    }
    $actual = (
        Get-FileHash -LiteralPath $fullPath -Algorithm SHA256
    ).Hash.ToLowerInvariant()
    if ($actual -ne $Expected) {
        throw "$Label changed outside its assigned Phase V migration step"
    }
}

Push-Location $RepoRoot
try {
    $ledger = "docs/Phase_V_GPU_Acceleration.md"
    foreach ($id in @(
        "V0-PROD",
        "V0-BLD",
        "V0-TRV",
        "V0-INS",
        "V0-LIN",
        "V0-UPD",
        "V0-HOST",
        "V0-OPT",
        "V0-API",
        "V0-VAL"
    )) {
        Assert-Contains $ledger $id "Phase V audit ledger is missing $id"
    }
    Assert-Contains $ledger "This is a" "V.0 gate lacks its regression-boundary qualification"
    Assert-Contains $ledger "regression boundary" "V.0 gate overstates audited correctness"
    Assert-Contains "PLAN.md" "当前游标: V\.6" "PLAN cursor did not advance to V.6"
    Assert-Contains "PLAN.md" "V\.0 closure.*权威游标进入 V\.1" "PLAN lacks the V.0 closure and V.1 gate"
    Assert-Contains "PLAN.md" "V\.1 closure.*权威游标进入 V\.2" "PLAN lacks the V.1 closure and V.2 gate"
    Assert-Contains "PLAN.md" "V\.2 closure.*权威游标进入 V\.3" "PLAN lacks the V.2 closure and V.3 gate"
    Assert-Contains "PLAN.md" "V\.3 closure.*权威游标进入 V\.4" "PLAN lacks the V.3 closure and V.4 gate"
    Assert-Contains "PLAN.md" "V\.4 closure.*权威游标进入 V\.5" "PLAN lacks the V.4 closure and V.5 gate"
    Assert-Contains "PLAN.md" "V\.5 closure.*权威游标进入 V\.6" "PLAN lacks the V.5 closure and V.6 gate"
    Assert-Contains "README.md" "OptiX production provider.*尚未完成" "README misstates OptiX production status"
    Assert-Contains "STATUS.md" "full SceneIR renderer remains unavailable" "STATUS misstates the portable acceleration boundary"
    Assert-Contains "libs/ure_types/include/ure/render_config.hpp" "struct AccelerationConfig" "V.1 C++ acceleration config is missing"
    Assert-Contains "libs/ure_types/include/ure/render_config.hpp" "AccelerationProviderKind::Automatic" "V.1 default provider changed"
    Assert-Contains "libs/ure_core/include/ure/ure_c_api.h" "ure_acceleration_config_t" "V.1 C acceleration config is missing"
    Assert-Contains "libs/ure_core/include/ure/ure_c_api.h" "ure_session_create_execution_config" "V.1 version-safe session entry point is missing"
    Assert-Contains "pyure/__init__.py" "class AccelerationProvider" "V.1 pyure provider enum is missing"
    Assert-Contains "libs/ure_core/src/backend_cuda.cu" "requested acceleration provider is unavailable" "V.1 provider rejection is missing"
    Assert-Contains "libs/ure_types/include/ure/render_config.hpp" "struct AccelerationStats" "V.2 C++ acceleration stats are missing"
    Assert-Contains "libs/ure_core/include/ure/ure_c_api.h" "ure_session_get_acceleration_stats" "V.2 C acceleration stats are missing"
    Assert-Contains "pyure/__init__.py" "class AccelerationStats" "V.2 pyure acceleration stats are missing"
    Assert-Contains "libs/ure_core/include/ure/detail/cuda_bvh_builder.cuh" "class InstanceTlasBuilder" "V.3 TLAS builder is missing"
    Assert-Contains "libs/ure_core/src/path_tracer_intersect.cuh" "hit_instance_tlas" "V.3 TLAS traversal is missing"
    Assert-Contains "libs/ure_core/src/path_tracer_host_api.cu" "InstanceTlasBuilder::refit" "V.3 TLAS refit is missing"
    Assert-Contains "libs/ure_core/src/backend_cuda.cu" "AccelerationUpdatePolicy::Refit" "V.3 refit policy is unavailable"
    Assert-Contains "libs/ure_core/include/ure/ure_c_api.h" "ure_acceleration_stats_v2_t" "V.3 versioned C statistics are missing"
    Assert-Contains "pyure/__init__.py" "_AccelerationStatsV2" "V.3 versioned pyure statistics are missing"
    Assert-Contains "libs/ure_core/include/ure/detail/cuda_structs.cuh" "struct GpuBvh4Node" "V.4 compact BVH4 layout is missing"
    Assert-Contains "libs/ure_core/include/ure/detail/cuda_structs.cuh" "struct GpuWideBvhNode" "V.4 compact BVH8 layout is missing"
    Assert-Contains "libs/ure_core/src/bvh_builder.cpp" "object_sah_split" "V.4 object SAH builder is missing"
    Assert-Contains "libs/ure_core/src/bvh_builder.cpp" "spatial_sah_split" "V.4 spatial SAH builder is missing"
    Assert-Contains "libs/ure_core/src/path_tracer_intersect.cuh" "hit_wide_bvh_nodes" "V.4 wide traversal is missing"
    Assert-Contains "libs/ure_core/include/ure/ure_c_api.h" "ure_acceleration_stats_v3_t" "V.4 versioned C statistics are missing"
    Assert-Contains "pyure/__init__.py" "_AccelerationStatsV3" "V.4 versioned pyure statistics are missing"
    Assert-Contains "tests/gpu/test_acceleration_hit_contract.cu" "V\.4 large mesh" "V.4 measured large-mesh benchmark is missing"
    Assert-Contains "libs/ure_core/src/acceleration_build_pipeline.cpp" "build_blas_batch" "V.5 async BLAS batch is missing"
    Assert-Contains "libs/ure_core/src/acceleration_upload.cu" "AccelerationUploadBatch" "V.5 pinned upload pipeline is missing"
    Assert-Contains "libs/ure_core/include/ure/ure_c_api.h" "ure_acceleration_stats_v4_t" "V.5 versioned C statistics are missing"
    Assert-Contains "pyure/__init__.py" "_AccelerationStatsV4" "V.5 versioned pyure statistics are missing"
    Assert-Contains "tools/benchmarks/run_phase_v_build_telemetry.ps1" "benchmark_vram_bytes" "V.5 build telemetry report is missing"

    Assert-Contains "libs/ure_core/src/bvh_builder.cpp" "count <= 4" "CUDA BVH leaf-size audit signature changed"
    Assert-Contains "libs/ure_core/src/bvh_builder.cpp" "std::nth_element" "CUDA BVH split fallback audit signature changed"
    Assert-Contains "libs/ure_core/include/ure/detail/cuda_structs.cuh" "Compact BVH Node for GPU \(32 bytes\)" "CUDA BVH node-layout audit signature changed"
    Assert-Contains "libs/ure_core/include/ure/detail/cuda_structs.cuh" "kBvhTraversalStackCapacity" "V.2 stack contract is missing"
    Assert-Contains "libs/ure_core/src/path_tracer_intersect.cuh" "int stack\[kBvhTraversalStackCapacity\]" "V.2 checked traversal stack is missing"
    Assert-Contains "libs/ure_core/src/path_tracer_intersect.cuh" "BvhTraversalResult::StackOverflow" "V.2 stack overflow result is missing"
    Assert-Contains "libs/ure_core/src/path_tracer_intersect.cuh" "BvhTraversalResult::InvalidAcceleration" "V.2 invalid acceleration result is missing"
    Assert-Contains "libs/ure_core/src/path_tracer_intersect.cuh" "scene\.instance_count" "closest-hit instance scan audit signature changed"
    Assert-NoMatch @(
        "libs/ure_core/src/path_tracer_intersect.cuh",
        "libs/ure_core/src/path_tracer_wavefront.cuh"
    ) "for \(int j = 0; j < mesh\.triangle_count" "silent linear triangle fallback returned"
    Assert-NoMatch @(
        "libs/ure_core/src/path_tracer_intersect.cuh",
        "libs/ure_core/src/path_tracer_wavefront.cuh"
    ) "\bany_hit_bvh\b" "duplicate any-hit BVH traversal returned"
    Assert-NoMatch @(
        "libs/ure_core/src/path_tracer_intersect.cuh"
    ) "for \(int i = 0; i < scene\.instance_count" "linear instance traversal returned"
    Assert-Contains "libs/ure_runtime/include/ure/runtime/acceleration.hpp" "class AccelerationProvider" "Phase T acceleration-provider boundary is missing"
    Assert-Contains "libs/ure_runtime/include/ure/runtime/acceleration.hpp" "struct AccelerationSceneDesc" "Phase T acceleration-scene descriptor is missing"

    $legacyPattern =
        "\b(BVHAccelerator|SimpleAccelerator|EmbreeAccelerator|OptixAccelerator)\b"
    $actualConsumers = @(
        & rg -l $legacyPattern libs apps `
            -g "*.h" -g "*.hpp" -g "*.cpp" -g "*.cu" -g "*.cuh" |
            ForEach-Object { $_ -replace "\\", "/" } |
            Sort-Object
    )
    $expectedConsumers = @(
        "libs/ure_core/include/ure/bvh_accelerator.hpp",
        "libs/ure_core/include/ure/gpu_accelerator.hpp",
        "libs/ure_core/src/bvh_accelerator.cpp",
        "libs/ure_types/include/ure/accelerators/bvh_accelerator.hpp",
        "libs/ure_types/include/ure/accelerators/cpu_accelerator.hpp",
        "libs/ure_types/include/ure/accelerators/gpu_accelerator.hpp"
    ) | Sort-Object
    if (($actualConsumers -join "`n") -ne
        ($expectedConsumers -join "`n")) {
        "Expected legacy accelerator consumer set:" | Write-Host
        $expectedConsumers | Write-Host
        "Actual legacy accelerator consumer set:" | Write-Host
        $actualConsumers | Write-Host
        throw "legacy host or placeholder accelerator gained a production consumer"
    }

    Assert-Hash "libs/ure_core/src/bvh_accelerator.cpp" "df466a88134cdc8295017732c33643de3bcf40d21331324f5cc62913ab0b2ca4" "legacy host BVH implementation"
    Assert-Hash "libs/ure_core/include/ure/bvh_accelerator.hpp" "f147dae958cd96597ec459a1d06dbc6fb3337bd6305d51e4afb46afa8c92f8de" "legacy core BVH header"
    Assert-Hash "libs/ure_types/include/ure/accelerators/bvh_accelerator.hpp" "f147dae958cd96597ec459a1d06dbc6fb3337bd6305d51e4afb46afa8c92f8de" "legacy types BVH header"
    Assert-Hash "libs/ure_core/include/ure/gpu_accelerator.hpp" "cf793494c24a22a3b781377b6ea382cf9cb94ad5fcc4314d8a70a372ecf5cab4" "legacy core OptiX placeholder"
    Assert-Hash "libs/ure_types/include/ure/accelerators/gpu_accelerator.hpp" "cf793494c24a22a3b781377b6ea382cf9cb94ad5fcc4314d8a70a372ecf5cab4" "legacy types OptiX placeholder"
    Assert-Hash "libs/ure_types/include/ure/accelerators/cpu_accelerator.hpp" "dead9f2e533a4c87893e4e9f7b19dddc71da44517deeeb1386c6a79e9b899034" "legacy CPU accelerator placeholders"
    Assert-NoMatch @("libs", "apps") '#include[[:space:]]*[<"]optix' "OptiX SDK entered the tree before V.6"

    Write-Host "Phase V static audit passed"
} finally {
    Pop-Location
}
