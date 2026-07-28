param(
    [string]$BuildDir = "build_modular_x64",
    [string]$Config = "Release",
    [string]$OptixRoot = "",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$BuildPath = Join-Path $RepoRoot $BuildDir
$ReportRoot = Join-Path $RepoRoot ".build\phase_v6_native_provider"
$ReportPath = Join-Path $ReportRoot "report.json"

Push-Location $RepoRoot
try {
    if (-not [string]::IsNullOrWhiteSpace($OptixRoot)) {
        $ResolvedOptix = Resolve-Path -LiteralPath $OptixRoot
        & cmake -S . -B $BuildPath `
            -DUR_ENABLE_OPTIX=ON `
            "-DUR_OPTIX_ROOT=$ResolvedOptix"
        if ($LASTEXITCODE -ne 0) {
            throw "OptiX-enabled configure failed"
        }
    }
    if (-not $SkipBuild) {
        & (Join-Path $PSScriptRoot "build_x64.ps1") `
            -BuildDir $BuildDir `
            -Config $Config `
            -SkipConfigure `
            -Targets @(
                "gpu_test_cuda_runtime",
                "test_vulkan_acceleration",
                "test_d3d12_runtime",
                "test_acceleration_contract")
        if ($LASTEXITCODE -ne 0) {
            throw "native provider build failed"
        }
    }
    $CudaTest = Join-Path $BuildPath "tests\gpu\gpu_test_cuda_runtime.exe"
    $CudaOutput = & $CudaTest 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) {
        throw "CUDA/OptiX provider test failed:`n$CudaOutput"
    }
    $SavedVulkan = $env:UR_REQUIRE_VULKAN_RT
    $SavedDxr = $env:UR_REQUIRE_DXR
    try {
        $env:UR_REQUIRE_VULKAN_RT = "1"
        $env:UR_REQUIRE_DXR = "1"
        $NativeOutput = & ctest `
            --test-dir $BuildPath `
            -C $Config `
            -R "^(vulkan_acceleration|d3d12_runtime|test_acceleration_contract)$" `
            --output-on-failure 2>&1 | Out-String
        if ($LASTEXITCODE -ne 0) {
            throw "Vulkan RT/DXR provider tests failed:`n$NativeOutput"
        }
    } finally {
        $env:UR_REQUIRE_VULKAN_RT = $SavedVulkan
        $env:UR_REQUIRE_DXR = $SavedDxr
    }
    New-Item -ItemType Directory -Path $ReportRoot -Force |
        Out-Null
    $Report = [ordered]@{
        schema = "ure.phase_v.native_provider.v1"
        config = $Config
        optix_available =
            $CudaOutput.Contains(
                "OptiX acceleration provider lifecycle passed")
        vulkan_rt_required = $true
        dxr_required = $true
        assertions = [ordered]@{
            multi_blas = "pass"
            compaction = "pass"
            refit = "pass"
            rebuild = "pass"
            scratch_rejection = "pass"
            missing_sdk_isolation = "pass"
        }
    }
    $Report | ConvertTo-Json -Depth 5 |
        Set-Content -LiteralPath $ReportPath -Encoding utf8
    Write-Host "Phase V.6 native provider gate passed"
    Write-Host "Wrote $ReportPath"
} finally {
    Pop-Location
}
