param(
    [string]$SlangRoot = ".build/toolchains/slang-2026.14",
    [string]$Dxc = "C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/dxc.exe"
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$BuildDir = Join-Path $RepoRoot "build_modular_x64"
$ManifestPath =
    Join-Path $RepoRoot "shaders/d3d12/phase_t9_manifest.json"
$GeneratedRoot =
    Join-Path $RepoRoot "shaders/d3d12/generated"
$CodegenA = Join-Path $RepoRoot ".build/phase_t9_codegen_a"
$CodegenB = Join-Path $RepoRoot ".build/phase_t9_codegen_b"
$HlslRoot = Join-Path $RepoRoot ".build/phase_t9_hlsl"
$NoD3D12Build =
    Join-Path $RepoRoot ".build/phase_t9_no_d3d12"
$ReportRoot = Join-Path $RepoRoot ".build/phase_t9_gate"

function Require-Success {
    param([string]$Label)
    if ($LASTEXITCODE -ne 0) {
        throw "$Label failed with exit code $LASTEXITCODE"
    }
}

function File-Hash {
    param([string]$Path)
    return (
        Get-FileHash -LiteralPath $Path -Algorithm SHA256
    ).Hash.ToLowerInvariant()
}

function Require-Hash {
    param(
        [string]$Path,
        [string]$Expected,
        [string]$Label
    )
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Label is missing: $Path"
    }
    if ((File-Hash $Path) -ne $Expected) {
        throw "$Label SHA-256 mismatch: $Path"
    }
}

Push-Location $RepoRoot
try {
    if (-not $IsWindows) {
        throw "Phase T.9 D3D12 gate requires Windows"
    }
    $Manifest =
        Get-Content -Raw $ManifestPath |
        ConvertFrom-Json
    $Slangc =
        Join-Path $RepoRoot "$SlangRoot/bin/slangc.exe"
    Require-Hash `
        $Slangc `
        $Manifest.compiler.slang.windows_executable_sha256 `
        "Pinned Slang compiler"
    Require-Hash `
        $Dxc `
        $Manifest.compiler.dxc.executable_sha256 `
        "Pinned DXC compiler"
    Require-Hash `
        (Join-Path (Split-Path $Dxc) "dxcompiler.dll") `
        $Manifest.compiler.dxc.dxcompiler_sha256 `
        "Pinned DXC runtime"
    Require-Hash `
        (Join-Path (Split-Path $Dxc) "dxil.dll") `
        $Manifest.compiler.dxc.dxil_sha256 `
        "Pinned DXIL validator"
    foreach ($source in
             $Manifest.sources.PSObject.Properties) {
        Require-Hash `
            (Join-Path (
                Split-Path $ManifestPath) `
                $source.Value.path) `
            $source.Value.sha256 `
            "T.9 source $($source.Name)"
    }
    New-Item -ItemType Directory -Force `
        $CodegenA, $CodegenB, $HlslRoot, $ReportRoot |
        Out-Null
    & .\scripts\regenerate_d3d12_shaders.ps1 `
        -OutputDir $CodegenA `
        -IntermediateDir $HlslRoot `
        -SlangRoot $SlangRoot `
        -Dxc $Dxc
    Require-Success "T.9 first shader generation"
    & .\scripts\regenerate_d3d12_shaders.ps1 `
        -OutputDir $CodegenB `
        -IntermediateDir $HlslRoot `
        -SlangRoot $SlangRoot `
        -Dxc $Dxc
    Require-Success "T.9 second shader generation"
    foreach ($entry in
             $Manifest.entries.PSObject.Properties) {
        foreach ($root in @(
                     $GeneratedRoot,
                     $CodegenA,
                     $CodegenB)) {
            Require-Hash `
                (Join-Path $root "$($entry.Name).dxil") `
                $entry.Value.dxil_sha256 `
                "$($entry.Name) DXIL"
            Require-Hash `
                (Join-Path $root "$($entry.Name).json") `
                $entry.Value.reflection_sha256 `
                "$($entry.Name) reflection"
        }
        Require-Hash `
            (Join-Path $HlslRoot "$($entry.Name).hlsl") `
            $entry.Value.hlsl_sha256 `
            "$($entry.Name) normalized HLSL"
    }

    & cmake --build $BuildDir --config Release --target `
        test_d3d12_runtime test_vulkan_acceleration `
        gpu_test_acceleration_contract
    Require-Success "T.9 Windows parity target build"
    $PreviousRequireDxr = $env:UR_REQUIRE_DXR
    try {
        $env:UR_REQUIRE_DXR = "1"
        & ctest --test-dir $BuildDir -C Release `
            -R "^(d3d12_runtime|vulkan_acceleration|gpu_acceleration_contract)$" `
            --output-on-failure
        Require-Success "T.9 D3D12/DXR parity execution"
    } finally {
        $env:UR_REQUIRE_DXR = $PreviousRequireDxr
    }

    & cmake -S $RepoRoot -B $NoD3D12Build -G Ninja `
        -DCMAKE_BUILD_TYPE=Release `
        -DUR_ENABLE_CUDA=OFF `
        -DUR_ENABLE_VULKAN=ON `
        -DUR_ENABLE_D3D12=OFF `
        -DUR_BUILD_TESTS=ON `
        -DUR_BUILD_CLI=OFF `
        -DUR_BUILD_PHYSICS=OFF
    Require-Success "T.9 no-D3D12 configure"
    $NoD3D12Cache =
        Get-Content -Raw (
            Join-Path $NoD3D12Build "CMakeCache.txt")
    if ($NoD3D12Cache -notmatch "UR_ENABLE_D3D12:BOOL=OFF") {
        throw "T.9 no-D3D12 build enabled D3D12"
    }
    & cmake --build $NoD3D12Build --config Release --target `
        test_vulkan_acceleration
    Require-Success "T.9 no-D3D12 Vulkan build"
    & ctest --test-dir $NoD3D12Build -C Release `
        -R "^vulkan_acceleration$" `
        --output-on-failure
    Require-Success "T.9 no-D3D12 Vulkan execution"

    & .\scripts\check_phase_t_static.ps1
    Require-Success "Phase T static audit"

    $Report = [ordered]@{
        schema = "ure.phase_t9.d3d12.v1"
        generated_utc =
            [DateTime]::UtcNow.ToString("o")
        compiler = [ordered]@{
            slang = $Manifest.compiler.slang.version
            dxc = $Manifest.compiler.dxc.version
            windows_sdk =
                $Manifest.compiler.dxc.windows_sdk
        }
        operators =
            @($Manifest.entries.PSObject.Properties.Name)
        windows = [ordered]@{
            optional_build = $true
            deterministic_dxil = $true
            descriptor_heaps = $true
            queue_fence = $true
            dred = $true
            dxr_1_1_required = $true
            compute_bvh_fallback = $true
            image_sampling = $true
        }
        isolation = [ordered]@{
            d3d12_disabled = $true
            cuda_disabled = $true
            vulkan_execution = $true
        }
    }
    $Report |
        ConvertTo-Json -Depth 5 |
        Set-Content -Encoding utf8 (
            Join-Path $ReportRoot "report.json")
    Write-Host (
        "Phase T.9 D3D12 gate passed: deterministic DXIL, " +
        "descriptor/image resources, queue/fence, DRED, " +
        "DXR/compute parity, and no-D3D12 isolation.")
} finally {
    Pop-Location
}
