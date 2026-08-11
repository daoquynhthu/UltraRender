param(
    [string]$BuildDir = "build_modular_x64",
    [string]$Config = "Release",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$BuildPath = Join-Path $RepoRoot $BuildDir
$ArtifactBin = Join-Path $BuildPath "artifacts\$Config\bin"
$OutputDir = Join-Path $RepoRoot ".build\phase_v7_parity"
$InventoryPath = Join-Path $OutputDir "inventory.json"
$ReportPath = Join-Path $OutputDir "report.json"
$RayQueryFeature = [uint64]1 -shl 14

function File-Hash {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        return $null
    }
    (
        Get-FileHash -LiteralPath $Path -Algorithm SHA256
    ).Hash.ToLowerInvariant()
}

function Invoke-Checked {
    param(
        [string]$Path,
        [string[]]$Arguments = @(),
        [hashtable]$Environment = @{}
    )
    $saved = @{}
    foreach ($name in $Environment.Keys) {
        $existing = Get-Item "Env:$name" -ErrorAction SilentlyContinue
        $saved[$name] = if ($null -eq $existing) {
            $null
        } else {
            [string]$existing.Value
        }
        Set-Item "Env:$name" ([string]$Environment[$name])
    }
    try {
        $timer = [Diagnostics.Stopwatch]::StartNew()
        $output = & $Path @Arguments 2>&1 | Out-String
        $exitCode = $LASTEXITCODE
        $timer.Stop()
    } finally {
        foreach ($name in $Environment.Keys) {
            if ($null -eq $saved[$name]) {
                Remove-Item "Env:$name" -ErrorAction SilentlyContinue
            } else {
                Set-Item "Env:$name" $saved[$name]
            }
        }
    }
    if ($exitCode -ne 0) {
        throw "$Path failed with exit code ${exitCode}:`n$output"
    }
    [ordered]@{
        elapsed_ms = [Math]::Round(
            $timer.Elapsed.TotalMilliseconds, 4)
        output = $output.Trim()
    }
}

New-Item -ItemType Directory -Force $OutputDir |
    Out-Null

$targets = @(
    "gpu_test_acceleration_contract",
    "gpu_test_cuda_runtime",
    "test_vulkan_acceleration",
    "test_multi_backend_inventory"
)
$D3dExecutable =
    Join-Path $ArtifactBin "test_d3d12_runtime.exe"
if (Test-Path -LiteralPath $D3dExecutable) {
    $targets += "test_d3d12_runtime"
}
if (-not $SkipBuild) {
    & cmake --build $BuildPath --config $Config --target @targets
    if ($LASTEXITCODE -ne 0) {
        throw "Phase V.7 parity targets failed to build"
    }
}

$InventoryExecutable =
    Join-Path $ArtifactBin "test_multi_backend_inventory.exe"
Invoke-Checked `
    -Path $InventoryExecutable `
    -Environment @{
        UR_PHASE_T10_REPORT = $InventoryPath
    } | Out-Null
$inventory =
    Get-Content -Raw -LiteralPath $InventoryPath |
    ConvertFrom-Json
$cudaWorkers = @(
    $inventory.workers |
        Where-Object backend -eq "cuda")
$vulkanWorkers = @(
    $inventory.workers |
        Where-Object backend -eq "vulkan")
$d3dWorkers = @(
    $inventory.workers |
        Where-Object backend -eq "d3d12")
if ($cudaWorkers.Count -lt 1 -or
    $vulkanWorkers.Count -lt 1) {
    throw "Phase V.7 requires actual CUDA and Vulkan adapters"
}

$cudaCompute = Invoke-Checked -Path (
    Join-Path $ArtifactBin "gpu_test_acceleration_contract.exe")
$cudaOptix = Invoke-Checked -Path (
    Join-Path $ArtifactBin "gpu_test_cuda_runtime.exe")
$optixExecuted =
    $cudaOptix.output -match
        "OptiX acceleration provider traversal and lifecycle passed"

$vulkanRayWorkers = @(
    $vulkanWorkers |
        Where-Object {
            ([uint64]$_.features -band
                $RayQueryFeature) -ne 0
        })
$vulkanEnvironment = @{}
if ($vulkanRayWorkers.Count -gt 0) {
    $vulkanEnvironment.UR_REQUIRE_VULKAN_RT = "1"
}
$vulkan = Invoke-Checked `
    -Path (
        Join-Path $ArtifactBin "test_vulkan_acceleration.exe") `
    -Environment $vulkanEnvironment

$d3dRayWorkers = @(
    $d3dWorkers |
        Where-Object {
            ([uint64]$_.features -band
                $RayQueryFeature) -ne 0
        })
$d3d = $null
if ($d3dWorkers.Count -gt 0) {
    $d3dEnvironment = @{}
    if ($d3dRayWorkers.Count -gt 0) {
        $d3dEnvironment.UR_REQUIRE_DXR = "1"
    }
    $d3d = Invoke-Checked `
        -Path $D3dExecutable `
        -Environment $d3dEnvironment
}

$OptixRoot = $null
$cachePath = Join-Path $BuildPath "CMakeCache.txt"
$cacheLine = Get-Content -LiteralPath $cachePath |
    Where-Object {
        $_ -like "UR_OPTIX_ROOT:PATH=*"
    } |
    Select-Object -First 1
if ($cacheLine) {
    $OptixRoot = $cacheLine.Substring(
        $cacheLine.IndexOf("=") + 1)
}
$OptixCommit = $null
if ($OptixRoot -and
    (Test-Path -LiteralPath (
        Join-Path $OptixRoot ".git"))) {
    $OptixCommit = (
        & git -C $OptixRoot rev-parse HEAD
    ).Trim()
}
$OptixModule = Join-Path $ArtifactBin "shaders\optix\phase_v7_acceleration.optixir"
$OptixCompiler = $null
if ($optixExecuted) {
    $nvccVersion = (& nvcc.exe --version 2>&1 | Out-String)
    if ($LASTEXITCODE -ne 0 -or
        $nvccVersion -notmatch
            'release\s+([0-9]+\.[0-9]+)') {
        throw "Unable to determine the CUDA compiler used for OptiX IR"
    }
    $OptixCompiler =
        "CUDA $($Matches[1]) OptiX IR"
}

$providers = @(
    [ordered]@{
        provider = "self_compute"
        status = "passed"
        execution = "cuda_bvh_tlas"
        workers = $cudaWorkers
        elapsed_ms = $cudaCompute.elapsed_ms
    },
    [ordered]@{
        provider = "optix"
        status = if ($optixExecuted) {
            "passed"
        } else {
            "unavailable"
        }
        execution = "optix_pipeline_gas_ias"
        workers = @(
            $cudaWorkers |
                Where-Object { $optixExecuted })
        compiler = $OptixCompiler
        sdk_commit = $OptixCommit
        module_sha256 = File-Hash $OptixModule
        elapsed_ms = $cudaOptix.elapsed_ms
    },
    [ordered]@{
        provider = "vulkan_rt"
        status = if ($vulkanRayWorkers.Count -gt 0) {
            "passed"
        } else {
            "unavailable"
        }
        execution = "vulkan_inline_ray_query"
        workers = $vulkanRayWorkers
        shader_sha256 = File-Hash (
            Join-Path $RepoRoot "shaders\vulkan\generated\ray_query_native.spv")
        elapsed_ms = $vulkan.elapsed_ms
    },
    [ordered]@{
        provider = "dxr"
        status = if ($d3dRayWorkers.Count -gt 0) {
            "passed"
        } else {
            "unavailable"
        }
        execution = "dxr_inline_ray_query"
        workers = $d3dRayWorkers
        shader_sha256 = File-Hash (
            Join-Path $RepoRoot "shaders\d3d12\generated\ray_query_native.dxil")
        elapsed_ms = if ($null -ne $d3d) {
            $d3d.elapsed_ms
        } else {
            $null
        }
    }
)

$Report = [ordered]@{
    schema = "ure.phase_v.cross_provider_parity.v1"
    generated_utc = [DateTime]::UtcNow.ToString("o")
    fixture = [ordered]@{
        source = "SceneIR"
        path = "tests/shared/acceleration_parity_fixture.hpp"
        sha256 = File-Hash (
            Join-Path $RepoRoot "tests\shared\acceleration_parity_fixture.hpp")
        geometry_count = 1
        instance_count = 2
        ray_count = 5
        closest_hit_rays = 4
        shadow_rays = 1
    }
    tolerance = [ordered]@{
        hit_metadata_absolute = 0.00002
        framebuffer_absolute = 0.00002
        tangent_unit_absolute = 0.00001
        tangent_normal_dot_absolute = 0.00001
    }
    assertions = [ordered]@{
        framebuffer_aov = "uv,abs_shading_normal_z,visibility"
        hit_metadata = @(
            "distance",
            "position",
            "shading_normal",
            "geometric_normal",
            "tangent_handedness",
            "uv",
            "barycentrics",
            "material_index",
            "instance_index",
            "primitive_index"
        )
        shadow_visibility = "first_hit_boolean"
        instance_transform = "non_uniform_scale_and_translation"
        result = "passed"
    }
    providers = $providers
}
$Report |
    ConvertTo-Json -Depth 10 |
    Set-Content -Encoding utf8 $ReportPath
Write-Host (
    "Phase V.7 cross-provider parity gate passed: " +
    "self-compute, OptiX when available, Vulkan RT " +
    "and DXR when available.")
