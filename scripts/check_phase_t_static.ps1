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
    Assert-Contains "shaders/shared/portable_semantics.slang" "apply_mueller" "Shared portable polarization semantics are missing"
    Assert-Contains "shaders/shared/portable_semantics.slang" "propagate_wave" "Shared portable wave semantics are missing"
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
    Assert-Contains "CMakeLists.txt" "option\(UR_ENABLE_VULKAN" "T.7 Vulkan-optional root build is missing"
    Assert-Contains "libs/ure_vulkan/include/ure/vulkan_runtime.hpp" "class VulkanRuntimeDevice" "T.7 Vulkan runtime Device is missing"
    Assert-NoMatch @(
        "libs/ure_vulkan/include/ure/vulkan_runtime.hpp"
    ) '#include[[:space:]]*[<"]vulkan|Vk[A-Z][A-Za-z0-9_]+' "T.7 public Vulkan surface exposes SDK types"
    Assert-Contains "libs/ure_vulkan/src/vulkan_runtime_device.cpp" "volkLoadDeviceTable" "T.7 per-device Vulkan dispatch table is missing"
    Assert-Contains "libs/ure_vulkan/src/vulkan_runtime_device.cpp" "vkQueueSubmit2" "T.7 Vulkan DAG submission is missing"
    Assert-Contains "libs/ure_vulkan/src/vulkan_runtime_device.cpp" "pipeline_cache_uuid" "T.7 pipeline cache identity validation is missing"
    Assert-Contains "libs/ure_vulkan/src/vulkan_runtime_device.cpp" "VK_ERROR_DEVICE_LOST" "T.7 device-loss mapping is missing"
    Assert-Contains "shaders/vulkan/phase_t7_foundation.slang" "import portable_semantics" "T.7 does not reuse shared shader semantics"
    foreach ($entry in @(
        "raygen",
        "spectral_polarization",
        "queue_compaction",
        "film_aov",
        "wave_reference"
    )) {
        Assert-Contains "shaders/vulkan/phase_t7_foundation.slang" $entry "T.7 shader operator is missing: $entry"
    }
    Assert-Contains "tests/vulkan/test_vulkan_runtime.cpp" "UR_REQUIRE_CROSS_VENDOR_VULKAN" "T.7 cross-vendor execution gate is missing"
    Assert-Contains "tests/vulkan/test_vulkan_runtime.cpp" "validate_cache_rejection" "T.7 cache rejection test is missing"
    Assert-Contains "third_party/vulkan_dependencies.json" "1\.4\.352" "T.7 Vulkan dependency pin is missing"
    Assert-Contains "scripts/run_phase_t7_vulkan_foundation_gate.ps1" "phase_t7_linux" "T.7 Linux build gate is missing"
    Assert-Contains "scripts/run_phase_t7_vulkan_foundation_gate.ps1" "UR_REQUIRE_CROSS_VENDOR_VULKAN" "T.7 cross-vendor gate is missing"
    Assert-Contains "tests/sdk_free/vulkan_package_consumer/CMakeLists.txt" "UltraRender::ure_vulkan" "T.7 installed package consumer is missing"
    Assert-Contains "PLAN.md" "T\.7 closure.*权威游标进入 T\.8" "PLAN lacks the T.7 closure and T.8 gate"
    Assert-Contains "libs/ure_runtime/include/ure/runtime/acceleration.hpp" "class AccelerationProvider" "T.8 acceleration provider contract is missing"
    Assert-Contains "libs/ure_runtime/include/ure/runtime/acceleration.hpp" "AccelerationFallback" "T.8 acceleration fallback contract is missing"
    Assert-Contains "libs/ure_runtime/src/acceleration.cpp" "select_acceleration" "T.8 acceleration selection is missing"
    Assert-Contains "tests/sdk_free/CMakeLists.txt" "src/acceleration\.cpp" "T.8 SDK-free runtime target omits acceleration"
    Assert-NoMatch @(
        "libs/ure_runtime/include/ure/runtime/acceleration.hpp"
    ) '#include[[:space:]]*[<"](cuda|vulkan|d3d12|dxgi)|Vk[A-Z][A-Za-z0-9_]*|ID3D12[A-Za-z0-9_]*' "T.8 acceleration contract exposes backend SDK types"
    Assert-Contains "libs/ure_vulkan/src/vulkan_runtime_device.cpp" "vkCmdBuildAccelerationStructuresKHR" "T.8 Vulkan acceleration build is missing"
    Assert-Contains "libs/ure_vulkan/src/vulkan_runtime_device.cpp" "VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR" "T.8 Vulkan acceleration descriptor is missing"
    Assert-Contains "shaders/vulkan/phase_t8_acceleration.slang" "ray_query_native" "T.8 native ray-query shader is missing"
    Assert-Contains "shaders/vulkan/phase_t8_acceleration.slang" "compute_bvh" "T.8 compute fallback shader is missing"
    Assert-Contains "tests/gpu/test_acceleration_hit_contract.cu" "world_hit" "T.8 CUDA production hit parity test is missing"
    Assert-Contains "tests/vulkan/test_vulkan_acceleration.cpp" "UR_REQUIRE_VULKAN_RT" "T.8 native ray-query gate is missing"
    Assert-Contains "tests/vulkan/test_vulkan_acceleration.cpp" "cross_adapter_compute" "T.8 cross-adapter fallback parity is missing"
    Assert-Contains "scripts/run_phase_t8_vulkan_acceleration_gate.ps1" "phase_t8_linux" "T.8 Linux acceleration gate is missing"
    Assert-Contains "scripts/run_phase_t8_vulkan_acceleration_gate.ps1" "UR_REQUIRE_VULKAN_RT" "T.8 native ray-query execution gate is missing"
    Assert-Contains "PLAN.md" "T\.8 closure.*权威游标进入 T\.9" "PLAN lacks the T.8 closure and T.9 gate"
    Assert-Contains "CMakeLists.txt" "option\(UR_ENABLE_D3D12" "T.9 D3D12-optional root build is missing"
    Assert-Contains "libs/ure_d3d12/include/ure/d3d12_runtime.hpp" "class D3D12RuntimeDevice" "T.9 D3D12 runtime Device is missing"
    Assert-NoMatch @(
        "libs/ure_d3d12/include/ure/d3d12_runtime.hpp"
    ) '#include[[:space:]]*[<"](windows|d3d12|dxgi)|ID3D12[A-Za-z0-9_]*|IDXGI[A-Za-z0-9_]*' "T.9 public D3D12 surface exposes SDK types"
    Assert-Contains "libs/ure_d3d12/src/d3d12_runtime_device.cpp" "CreateDescriptorHeap" "T.9 descriptor heap lowering is missing"
    Assert-Contains "libs/ure_d3d12/src/d3d12_runtime_device.cpp" "ID3D12CommandQueue::Wait" "T.9 cross-queue fence lowering is missing"
    Assert-Contains "libs/ure_d3d12/src/d3d12_runtime_device.cpp" "GetAutoBreadcrumbsOutput1" "T.9 DRED breadcrumbs are missing"
    Assert-Contains "libs/ure_d3d12/src/d3d12_runtime_device.cpp" "BuildRaytracingAccelerationStructure" "T.9 DXR acceleration build is missing"
    Assert-Contains "tests/d3d12/test_d3d12_runtime.cpp" "UR_REQUIRE_DXR" "T.9 native DXR gate is missing"
    Assert-Contains "tests/d3d12/test_d3d12_runtime.cpp" "run_image_contract" "T.9 image descriptor gate is missing"
    Assert-Contains "tests/d3d12/test_d3d12_runtime.cpp" "run_queue_fence_contract" "T.9 queue/fence gate is missing"
    Assert-Contains "scripts/run_phase_t9_d3d12_gate.ps1" "UR_ENABLE_D3D12=OFF" "T.9 no-D3D12 isolation gate is missing"
    Assert-Contains "scripts/run_phase_t9_d3d12_gate.ps1" "deterministic DXIL" "T.9 deterministic DXIL evidence is missing"
    Assert-Contains "shaders/d3d12/phase_t9_manifest.json" "10\.0\.26100\.0" "T.9 DXC pin is missing"
    Assert-Contains "PLAN.md" "T\.9 closure.*权威游标进入 T\.10" "PLAN lacks the T.9 closure and T.10 gate"
    Assert-Contains "libs/ure_runtime/include/ure/runtime/multi_backend.hpp" "struct WorkerCapability" "T.10 worker capability contract is missing"
    Assert-Contains "libs/ure_runtime/include/ure/runtime/multi_backend.hpp" "struct ResourceCacheKey" "T.10 resource cache key is missing"
    Assert-Contains "libs/ure_runtime/include/ure/runtime/multi_backend.hpp" "struct MergeExecutionMetadata" "T.10 merge provenance contract is missing"
    Assert-Contains "libs/ure_runtime/src/multi_backend.cpp" "negotiate_sample_shards" "T.10 sample scheduler is missing"
    Assert-Contains "libs/ure_runtime/src/multi_backend.cpp" "worker lacks required numeric precision" "T.10 precision negotiation is missing"
    Assert-Contains "libs/ure_runtime/src/multi_backend.cpp" "worker lacks required coherence mode" "T.10 coherence negotiation is missing"
    Assert-Contains "libs/ure_core/src/distributed_file_io.cpp" "constexpr int kVersion = 5" "T.10 distributed file version did not advance"
    Assert-Contains "libs/ure_core/src/distributed_file_io.cpp" "kLegacyVersion = 4" "T.10 distributed v4 compatibility is missing"
    Assert-Contains "libs/ure_core/src/distributed_contract.cpp" "RGB distributed framebuffer cannot merge coherent fields" "T.10 coherent merge rejection is missing"
    Assert-Contains "libs/ure_core/src/gpu_multi_driver.cu" "negotiate_sample_shards" "T.10 CUDA multi-GPU path does not use the scheduler"
    Assert-Contains "libs/ure_core/src/backend_cuda.cu" "cudaDevAttrGpuPciDeviceId" "T.10 CUDA device cache identity lacks the PCI model"
    Assert-Contains "libs/ure_d3d12/src/d3d12_runtime_device.cpp" "__uuidof\(IDXGIDevice\)" "T.10 D3D12 driver identity query is not versioned"
    Assert-Contains "tests/host/test_multi_backend_schedule.cpp" "test_heterogeneous_schedule_is_canonical" "T.10 canonical heterogeneous test is missing"
    Assert-Contains "tests/host/test_distributed_file_io.cpp" "test_legacy_v4_framebuffer_read" "T.10 legacy distributed compatibility test is missing"
    Assert-Contains "tests/multi_backend/test_multi_backend_inventory.cpp" "UR_PHASE_T10_REPORT" "T.10 actual adapter inventory evidence is missing"
    Assert-Contains "tests/sdk_free/CMakeLists.txt" "test_multi_backend_sdk_free" "T.10 SDK-free scheduling test is missing"
    Assert-Contains "scripts/run_phase_t10_multi_backend_gate.ps1" "actual CUDA/Vulkan/D3D12 inventory" "T.10 closure gate is missing"
    Assert-Contains "PLAN.md" "当前游标: T\.11" "PLAN cursor did not advance to T.11"
    Assert-Contains "PLAN.md" "T\.10 closure.*权威游标进入 T\.11" "PLAN lacks the T.10 closure and T.11 gate"
    Write-Host "Phase T static audit passed"
} finally {
    Pop-Location
}
