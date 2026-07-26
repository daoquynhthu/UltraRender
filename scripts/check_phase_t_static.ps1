$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")

function Assert-NoMatch {
    param([string[]]$Paths, [string]$Pattern, [string]$Label)
    $matches = & rg -n --glob "*.h" --glob "*.hpp" --glob "*.cpp" --glob "*.py" $Pattern @Paths 2>$null
    if ($LASTEXITCODE -eq 0) {
        $matches | Write-Host
        throw "$Label"
    }
    if ($LASTEXITCODE -ne 1) { throw "rg failed while checking $Label" }
}

function Assert-Contains {
    param([string]$Path, [string]$Pattern, [string]$Label)
    & rg -q $Pattern (Join-Path $RepoRoot $Path)
    if ($LASTEXITCODE -ne 0) { throw $Label }
}

Push-Location $RepoRoot
try {
    $neutralRoots = @(
        "libs/ure_types",
        "libs/ure_runtime",
        "libs/ure_sceneio",
        "libs/ure_config",
        "pyure"
    )
    Assert-NoMatch $neutralRoots '#include[[:space:]]*[<"]cuda|cuda(TextureObject|Array|Stream|Event|Error)_t|CU(deviceptr|context|stream|event)' "backend-neutral modules expose CUDA SDK types"
    Assert-NoMatch @(
        "libs/ure_types/include/ure/render_config.hpp",
        "libs/ure_types/include/ure/scene_ir.hpp",
        "libs/ure_core/include/ure/ure_c_api.h"
    ) 'cuda_runtime|cuda(TextureObject|Array|Stream|Event|Error)_t|CU(deviceptr|context|stream|event)' "public configuration, SceneIR, or C ABI exposes CUDA SDK types"
    Assert-NoMatch @("libs/ure_core/include/ure/ure_c_api.h") 'Gpu(Context|Scene|MaterialData|Texture)' "C ABI exposes CUDA-era implementation structs"

    $allowedCudaHeaders = @()
    $actualCudaHeaders = @(
        & rg -l '#include[[:space:]]*[<"]cuda_runtime\.h' libs -g "*.h" -g "*.hpp" |
            ForEach-Object { $_ -replace '\\', '/' } |
            Sort-Object
    )
    $expectedCudaHeaders = @($allowedCudaHeaders | Sort-Object)
    if (($actualCudaHeaders -join "`n") -ne ($expectedCudaHeaders -join "`n")) {
        "Expected public CUDA include allowlist:" | Write-Host
        $expectedCudaHeaders | Write-Host
        "Actual public CUDA include set:" | Write-Host
        $actualCudaHeaders | Write-Host
        throw "public CUDA include allowlist changed"
    }

    $ledger = "docs/Phase_T_Portable_GPU_Runtime.md"
    foreach ($id in @(
        "T0-BLD", "T0-DEV", "T0-API", "T0-ABI", "T0-CTX",
        "T0-RES", "T0-EXE", "T0-KRN", "T0-MGPU", "T0-WAVE",
        "T0-ACC", "T0-DIAG", "T0-SCN", "T0-TEST"
    )) {
        Assert-Contains $ledger $id "Phase T coupling ledger is missing $id"
    }
    Assert-Contains $ledger "Contract owner" "Phase T ledger lacks contract ownership"
    Assert-Contains $ledger "Migration batch" "Phase T ledger lacks migration batches"
    Assert-Contains "libs/ure_types/include/ure/backend_types.hpp" "enum class BackendKind" "T.1 backend kind contract is missing"
    Assert-Contains "libs/ure_types/include/ure/backend_types.hpp" "BackendFeatureSet" "T.1 backend feature contract is missing"
    Assert-Contains "libs/ure_types/include/ure/backend_types.hpp" "driver_identity" "T.1 backend identity contract is missing"
    Assert-Contains "libs/ure_types/include/ure/render_config.hpp" "BackendSelectionConfig backend" "RenderConfig lacks backend selection"
    Assert-Contains "libs/ure_core/include/ure/ure_c_api.h" "ure_backend_config_t" "C ABI lacks backend configuration"
    Assert-Contains "pyure/__init__.py" "enumerate_backend_adapters" "pyure lacks backend enumeration"
    Assert-NoMatch @("apps/ure_cli/src/main.cpp") '#include[[:space:]]*[<"]cuda' "CLI still imports the CUDA SDK directly"
    Assert-Contains "tests/portable_kernel/phase_t2_prototypes.slang" "spectral_conversion" "T.2 spectral prototype is missing"
    Assert-Contains "tests/portable_kernel/phase_t2_prototypes.slang" "mueller_transport" "T.2 Mueller prototype is missing"
    Assert-Contains "tests/portable_kernel/phase_t2_prototypes.slang" "queue_compaction" "T.2 queue prototype is missing"
    Assert-Contains "tests/portable_kernel/phase_t2_prototypes.slang" "bsdf_sampling" "T.2 BSDF prototype is missing"
    Assert-Contains "tests/portable_kernel/phase_t2_prototypes.slang" "wave_propagation" "T.2 wave prototype is missing"
    Assert-Contains "tests/portable_kernel/phase_t2_prototypes.slang" "traversal_query" "T.2 traversal prototype is missing"
    Assert-Contains "scripts/run_phase_t2_kernel_toolchain_gate.ps1" "36029c50ef0c82f2616ffb02e0ed27d642cb44a2a297d531cc2ad333b85b85b6" "T.2 Slang pin is missing"
    Assert-Contains "scripts/run_phase_t2_kernel_toolchain_gate.ps1" "cuda_numerical_execution" "T.2 CUDA execution evidence is missing"
    Assert-Contains "docs/Phase_T2_Kernel_Toolchain_Decision.md" "Slang as the shared-source frontend" "T.2 decision is missing"
    Assert-Contains "libs/ure_runtime/include/ure/runtime/runtime.hpp" "class Device" "T.3 runtime device contract is missing"
    Assert-Contains "libs/ure_runtime/include/ure/runtime/runtime.hpp" "DispatchGraph" "T.3 dispatch graph contract is missing"
    Assert-Contains "libs/ure_runtime/include/ure/runtime/runtime.hpp" "DeviceLossInfo" "T.3 device-loss contract is missing"
    Assert-Contains "tests/host/test_runtime_contract.cpp" "test_lifetime_overflow_and_sync" "T.3 lifecycle/synchronization tests are missing"
    Assert-Contains "tests/host/test_runtime_contract.cpp" "test_graph_validation_and_device_loss" "T.3 graph/device-loss tests are missing"
    Assert-Contains "libs/ure_types/include/ure/resource_types.hpp" "struct ResourceId" "T.4 stable resource id is missing"
    Assert-Contains "libs/ure_runtime/include/ure/runtime/resource_plan.hpp" "using ResourceLayout" "T.4 typed resource layout is missing"
    Assert-Contains "libs/ure_runtime/include/ure/runtime/resource_plan.hpp" "SparseTileLayout" "T.4 sparse/tiled contract is missing"
    Assert-Contains "libs/ure_runtime/include/ure/runtime/resource_plan.hpp" "struct UploadPlan" "T.4 upload plan is missing"
    Assert-NoMatch @(
        "libs/ure_core/include/ure/gpu_context.hpp"
    ) 'cudaTextureObject_t|cudaArray_t|pointers_to_free|arrays_to_free|tex_objs_to_free|material_resource_tables_to_free' "T.4 CUDA resource ownership leaked into public headers"
    Assert-NoMatch @("libs/ure_core/include/ure/render.hpp") 'GpuInstanceTransform|GpuMaterialData' "T.4 render API still exposes CUDA-era mutation structs"
    Assert-NoMatch @(
        "libs/ure_core/include/ure/gpu_context.hpp"
    ) 'struct[[:space:]]+(GpuContext|MultiGpuContext)[[:space:]]*\{' "T.4 public runtime context still exposes backend allocation state"
    Assert-Contains "libs/ure_core/src/cuda_resource_registry.cuh" "class CudaResourceRegistry" "T.4 CUDA native resource registry is missing"
    Assert-Contains "libs/ure_core/include/ure/detail/cuda_scene_compiler.hpp" "struct CompiledGpuScene" "T.4 CUDA scene lowering was not moved behind the backend boundary"
    Assert-Contains "tests/host/test_resource_plan.cpp" "1'000'000" "T.4 million-domain resource budget gate is missing"
    Assert-Contains "tests/sdk_free/CMakeLists.txt" "LANGUAGES CXX" "T.4 independent SDK-free target is missing"
    Assert-Contains "scripts/run_phase_t4_resource_gate.ps1" "CMAKE_CUDA_COMPILER" "T.4 SDK-free compiler audit is missing"
    Assert-Contains "libs/ure_runtime/include/ure/runtime/execution_graph.hpp" "struct ExecutionRegion" "T.5 execution region contract is missing"
    Assert-Contains "libs/ure_runtime/include/ure/runtime/execution_graph.hpp" "initial_count_producer" "T.5 active-count producer contract is missing"
    Assert-Contains "libs/ure_runtime/include/ure/runtime/execution_graph.hpp" "struct IndirectQueueWork" "T.5 indirect dispatch contract is missing"
    Assert-Contains "libs/ure_runtime/include/ure/runtime/execution_graph.hpp" "struct AsyncTransferStage" "T.5 async transfer contract is missing"
    Assert-Contains "libs/ure_runtime/include/ure/runtime/execution_graph.hpp" "struct ClearStage" "T.5 resource-clear contract is missing"
    Assert-Contains "libs/ure_runtime/include/ure/runtime/execution_graph.hpp" "MltBootstrapNormalizeCdf" "T.5 MLT host-stage contract is missing"
    Assert-Contains "libs/ure_runtime/include/ure/runtime/execution_graph.hpp" "RestirPTReservoirSwap" "T.5 estimator-state transition contract is missing"
    Assert-Contains "libs/ure_runtime/include/ure/runtime/execution_graph.hpp" "struct PdfSemanticContract" "T.5 PDF semantics contract is missing"
    Assert-Contains "libs/ure_runtime/src/execution_graph.cpp" "make_path_execution_graph" "T.5 path graph generator is missing"
    Assert-Contains "libs/ure_runtime/src/execution_graph.cpp" "make_wave_execution_graph" "T.5 wave graph generator is missing"
    Assert-Contains "libs/ure_runtime/src/execution_graph.cpp" "execution_fingerprint" "T.5 stable graph identity is missing"
    Assert-Contains "libs/ure_core/src/path_tracer_host_api.cu" "last_execution_graph_fingerprint" "T.5 CUDA path entry does not validate graph identity"
    Assert-Contains "libs/ure_core/src/wave_optics_gpu.cu" "make_wave_execution_graph" "T.5 CUDA wave entry does not generate a graph"
    Assert-Contains "tests/host/test_execution_graph.cpp" "test_advanced_estimator_order_is_frozen" "T.5 estimator-order test is missing"
    Assert-Contains "tests/host/test_execution_graph.cpp" "test_validation_rejects_semantic_changes" "T.5 semantic-drift rejection test is missing"
    Assert-Contains "scripts/run_phase_t5_execution_gate.ps1" "CMAKE_CUDA_COMPILER" "T.5 SDK-free compiler audit is missing"
    Assert-Contains "libs/ure_core/src/cuda_runtime_device.cuh" "public runtime::Device" "T.6 CUDA runtime device is missing"
    Assert-Contains "libs/ure_core/src/cuda_runtime_device.cu" "cuLaunchKernel" "T.6 portable pipeline dispatch lowering is missing"
    Assert-Contains "libs/ure_core/src/cuda_runtime_device.cu" "complete_external" "T.6 native CUDA fast-path completion bridge is missing"
    Assert-Contains "libs/ure_core/src/path_tracer_host_api.cu" "runtime_device->lower" "T.6 path execution graph is not lowered"
    Assert-Contains "libs/ure_core/src/wave_optics_gpu.cu" "create_buffer" "T.6 wave resources do not use the runtime device"
    Assert-Contains "libs/ure_core/src/gpu_multi_driver.cu" "multi-GPU execution contracts are incompatible" "T.6 multi-GPU runtime compatibility gate is missing"
    Assert-Contains "tests/gpu/test_cuda_runtime_device.cu" "test_cuda_device_executes_runtime_graph" "T.6 production runtime execution test is missing"
    Assert-Contains "tests/host/test_public_surface_sdk_free.cpp" "SDK-free public surface compiled" "T.6 public SDK-free compile test is missing"
    Assert-Contains "tests/sdk_free/package_consumer/CMakeLists.txt" "find_package" "T.6 SDK-free package consumer is missing"
    Assert-Contains "scripts/run_phase_t6_cuda_backend_gate.ps1" "T5VramMiB" "T.6 VRAM regression gate is missing"
    Assert-Contains "scripts/run_phase_t6_cuda_backend_gate.ps1" "MaximumRegression" "T.6 performance regression gate is missing"
    Assert-Contains "CMakeLists.txt" "option\(UR_ENABLE_CUDA" "T.6 CUDA-optional root build is missing"
    Assert-Contains "CMakeLists.txt" "project\(UltraRender VERSION 1\.0\.0 LANGUAGES CXX\)" "T.6 root project still requires CUDA language"
    if (Test-Path "libs/ure_core/include/ure/gpu_structs.hpp") {
        throw "T.6 CUDA structs remain in the installed public surface"
    }
    if (Test-Path "libs/ure_diag/include/ure/check_cuda.hpp") {
        throw "T.6 CUDA diagnostics remain in the installed public surface"
    }
    Assert-Contains "PLAN.md" "当前游标: T\.7" "PLAN cursor did not advance to T.7"
    Assert-Contains "PLAN.md" "T\.6 closure.*权威游标进入 T\.7" "PLAN lacks the T.6 closure and T.7 gate"
    Write-Host "Phase T static audit passed"
} finally {
    Pop-Location
}
