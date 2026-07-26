param(
    [string]$SlangRoot = ".build/toolchains/slang-2026.14",
    [string]$LinuxDistribution = "Ubuntu-24.04"
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$BuildDir = Join-Path $RepoRoot "build_modular_x64"
$ShaderRoot = Join-Path $RepoRoot "shaders\vulkan"
$ShaderSource =
    Join-Path $ShaderRoot "phase_t8_acceleration.slang"
$ManifestPath =
    Join-Path $ShaderRoot "phase_t8_manifest.json"
$GeneratedRoot = Join-Path $ShaderRoot "generated"
$CodegenA = Join-Path $RepoRoot ".build\phase_t8_codegen_a"
$CodegenB = Join-Path $RepoRoot ".build\phase_t8_codegen_b"
$PortableBuild =
    Join-Path $RepoRoot ".build\phase_t8_windows_sdk_free"
$ReportRoot = Join-Path $RepoRoot ".build\phase_t8_gate"

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

function Compile-Shader {
    param(
        [string]$Compiler,
        [string]$Entry,
        [string]$Profile,
        [string]$OutputRoot,
        [string[]]$Arguments
    )
    $Spirv = Join-Path $OutputRoot "$Entry.spv"
    $Reflection = Join-Path $OutputRoot "$Entry.json"
    & $Compiler $ShaderSource -entry $Entry `
        -profile $Profile @Arguments `
        -reflection-json $Reflection -o $Spirv
    Require-Success "$Entry SPIR-V compilation"
}

Push-Location $RepoRoot
try {
    $Manifest =
        Get-Content -Raw $ManifestPath |
        ConvertFrom-Json
    Require-Hash `
        $ShaderSource `
        $Manifest.source.sha256 `
        "T.8 shader source"
    $Slangc = Join-Path $RepoRoot "$SlangRoot\bin\slangc.exe"
    Require-Hash `
        $Slangc `
        $Manifest.compiler.windows_executable_sha256 `
        "Pinned Slang compiler"
    New-Item -ItemType Directory -Force `
        $CodegenA, $CodegenB, $ReportRoot |
        Out-Null
    $Arguments = [string[]]$Manifest.arguments
    foreach ($EntryProperty in
             $Manifest.entries.PSObject.Properties) {
        $Entry = $EntryProperty.Name
        $Profile = $EntryProperty.Value.profile
        Compile-Shader `
            $Slangc $Entry $Profile $CodegenA $Arguments
        Compile-Shader `
            $Slangc $Entry $Profile $CodegenB $Arguments
        foreach ($Root in @(
                     $CodegenA,
                     $CodegenB,
                     $GeneratedRoot)) {
            Require-Hash `
                (Join-Path $Root "$Entry.spv") `
                $EntryProperty.Value.spirv_sha256 `
                "$Entry SPIR-V artifact"
            Require-Hash `
                (Join-Path $Root "$Entry.json") `
                $EntryProperty.Value.reflection_sha256 `
                "$Entry reflection artifact"
        }
    }

    & cmake --build $BuildDir --config Release --target `
        ure_vulkan test_acceleration_contract `
        test_vulkan_runtime test_vulkan_acceleration `
        gpu_test_acceleration_contract gpu_test_hardware
    Require-Success "T.8 Windows target build"
    $PreviousRequireRt = $env:UR_REQUIRE_VULKAN_RT
    $PreviousCrossVendor =
        $env:UR_REQUIRE_CROSS_VENDOR_VULKAN
    try {
        $env:UR_REQUIRE_VULKAN_RT = "1"
        $env:UR_REQUIRE_CROSS_VENDOR_VULKAN = "1"
        & ctest --test-dir $BuildDir -C Release `
            -R "^(test_acceleration_contract|vulkan_runtime|vulkan_acceleration|gpu_acceleration_contract|gpu_hardware)$" `
            --output-on-failure
        Require-Success "T.8 Windows acceleration parity"
    } finally {
        $env:UR_REQUIRE_VULKAN_RT = $PreviousRequireRt
        $env:UR_REQUIRE_CROSS_VENDOR_VULKAN =
            $PreviousCrossVendor
    }

    & cmake -S $RepoRoot -B $PortableBuild `
        -G "Visual Studio 17 2022" -A x64 `
        -DUR_ENABLE_CUDA=OFF `
        -DUR_ENABLE_VULKAN=ON `
        -DUR_BUILD_TESTS=ON `
        -DUR_BUILD_CLI=OFF `
        -DUR_BUILD_PHYSICS=OFF
    Require-Success "T.8 Windows CUDA-free configure"
    $PortableCache =
        Get-Content -Raw (
            Join-Path $PortableBuild "CMakeCache.txt")
    if ($PortableCache -match "CMAKE_CUDA_COMPILER") {
        throw "T.8 Windows CUDA-free build configured CUDA"
    }
    & cmake --build $PortableBuild --config Release `
        --target test_vulkan_acceleration
    Require-Success "T.8 Windows CUDA-free build"
    $PreviousRequireRt = $env:UR_REQUIRE_VULKAN_RT
    try {
        $env:UR_REQUIRE_VULKAN_RT = "1"
        & ctest --test-dir $PortableBuild -C Release `
            -R "^vulkan_acceleration$" `
            --output-on-failure
        Require-Success "T.8 Windows CUDA-free execution"
    } finally {
        $env:UR_REQUIRE_VULKAN_RT = $PreviousRequireRt
    }

    $RepoPath = $RepoRoot.Path
    $Drive = $RepoPath.Substring(0, 1).ToLowerInvariant()
    $LinuxRepo =
        "/mnt/$Drive/" +
        $RepoPath.Substring(3).Replace("\", "/")
    $LinuxCommand =
        "cd '$LinuxRepo' && " +
        "cmake -S . -B .build/phase_t8_linux -G Ninja " +
        "-DCMAKE_BUILD_TYPE=Release " +
        "-DUR_ENABLE_CUDA=OFF -DUR_ENABLE_VULKAN=ON " +
        "-DUR_BUILD_TESTS=ON -DUR_BUILD_CLI=OFF " +
        "-DUR_BUILD_PHYSICS=OFF && " +
        "cmake --build .build/phase_t8_linux " +
        "--target test_vulkan_acceleration && " +
        "ctest --test-dir .build/phase_t8_linux " +
        "-R '^vulkan_acceleration$' " +
        "--output-on-failure"
    & wsl.exe -d $LinuxDistribution -- bash -lc $LinuxCommand
    Require-Success "T.8 Linux CUDA-free acceleration execution"

    & .\scripts\check_phase_t_static.ps1
    Require-Success "Phase T static audit"

    $Report = [ordered]@{
        schema = "ure.phase_t8.vulkan_acceleration.v1"
        generated_utc =
            [DateTime]::UtcNow.ToString("o")
        slang = $Manifest.compiler.version
        operators = @(
            $Manifest.entries.PSObject.Properties.Name)
        windows = [ordered]@{
            native_ray_query_required = $true
            compute_bvh_fallback = $true
            cross_vendor_execution = $true
            cuda_self_traversal_parity = $true
            cuda_free_build = $true
        }
        linux = [ordered]@{
            distribution = $LinuxDistribution
            cuda_free_build = $true
            capability_fallback_or_native = $true
        }
    }
    $Report |
        ConvertTo-Json -Depth 5 |
        Set-Content -Encoding utf8 (
            Join-Path $ReportRoot "report.json")
    Write-Host (
        "Phase T.8 Vulkan acceleration gate passed: " +
        "native ray query, compute BVH fallback, " +
        "CUDA traversal parity, Windows cross-vendor " +
        "and Linux CUDA-free execution.")
} finally {
    Pop-Location
}
