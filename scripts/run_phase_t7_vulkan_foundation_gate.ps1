param(
    [string]$SlangRoot = ".build/toolchains/slang-2026.14",
    [string]$LinuxDistribution = "Ubuntu-24.04"
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$BuildDir = Join-Path $RepoRoot "build_modular_x64"
$ShaderRoot = Join-Path $RepoRoot "shaders\vulkan"
$ShaderSource = Join-Path $ShaderRoot "phase_t7_foundation.slang"
$ShaderManifestPath = Join-Path $ShaderRoot "manifest.json"
$DependencyManifestPath =
    Join-Path $RepoRoot "third_party\vulkan_dependencies.json"
$GeneratedRoot = Join-Path $ShaderRoot "generated"
$CodegenA = Join-Path $RepoRoot ".build\phase_t7_codegen_a"
$CodegenB = Join-Path $RepoRoot ".build\phase_t7_codegen_b"
$PortableBuild =
    Join-Path $RepoRoot ".build\phase_t7_windows_sdk_free"
$PortableInstall =
    Join-Path $RepoRoot ".build\phase_t7_install"
$ConsumerSource =
    Join-Path $RepoRoot "tests\sdk_free\vulkan_package_consumer"
$ConsumerBuild =
    Join-Path $RepoRoot ".build\phase_t7_package_consumer"
$ReportRoot = Join-Path $RepoRoot ".build\phase_t7_gate"

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
        [string]$OutputRoot,
        [string[]]$Arguments
    )
    $Spirv = Join-Path $OutputRoot "$Entry.spv"
    $Reflection = Join-Path $OutputRoot "$Entry.json"
    & $Compiler $ShaderSource -entry $Entry @Arguments `
        -I "shaders/shared" `
        -reflection-json $Reflection -o $Spirv
    Require-Success "$Entry SPIR-V compilation"
}

Push-Location $RepoRoot
try {
    $Dependencies =
        Get-Content -Raw $DependencyManifestPath |
        ConvertFrom-Json
    foreach ($Property in
             $Dependencies.vulkan_headers.files.PSObject.Properties) {
        Require-Hash `
            (Join-Path $RepoRoot "third_party\$($Property.Name)") `
            $Property.Value `
            "Vulkan-Headers dependency"
    }
    foreach ($Property in
             $Dependencies.volk.files.PSObject.Properties) {
        Require-Hash `
            (Join-Path $RepoRoot "third_party\$($Property.Name)") `
            $Property.Value `
            "Volk dependency"
    }

    $Manifest =
        Get-Content -Raw $ShaderManifestPath |
        ConvertFrom-Json
    Require-Hash `
        $ShaderSource `
        $Manifest.source.sha256 `
        "T.7 shader source"
    Require-Hash `
        (Join-Path $ShaderRoot $Manifest.shared_source.path) `
        $Manifest.shared_source.sha256 `
        "Shared portable shader semantics"
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
        Compile-Shader $Slangc $Entry $CodegenA $Arguments
        Compile-Shader $Slangc $Entry $CodegenB $Arguments
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
        ure_vulkan test_vulkan_runtime gpu_test_hardware ure_cli
    Require-Success "T.7 Windows target build"
    & ctest --test-dir $BuildDir -C Release `
        -R "^(vulkan_runtime|gpu_hardware|test_runtime_contract)$" `
        --output-on-failure
    Require-Success "T.7 Windows contract tests"

    $PreviousCrossVendor =
        $env:UR_REQUIRE_CROSS_VENDOR_VULKAN
    try {
        $env:UR_REQUIRE_CROSS_VENDOR_VULKAN = "1"
        & ctest --test-dir $BuildDir -C Release `
            -R "^vulkan_runtime$" --output-on-failure
        Require-Success "T.7 Windows cross-vendor execution"
    } finally {
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
    Require-Success "T.7 Windows CUDA-free configure"
    $PortableCache =
        Get-Content -Raw (
            Join-Path $PortableBuild "CMakeCache.txt")
    if ($PortableCache -match "CMAKE_CUDA_COMPILER") {
        throw "T.7 Windows CUDA-free build configured CUDA"
    }
    & cmake --build $PortableBuild --config Release
    Require-Success "T.7 Windows CUDA-free Vulkan build"
    & ctest --test-dir $PortableBuild -C Release `
        -R "^vulkan_runtime$" --output-on-failure
    Require-Success "T.7 Windows CUDA-free Vulkan execution"
    & cmake --install $PortableBuild --config Release `
        --prefix $PortableInstall
    Require-Success "T.7 Vulkan package install"
    & cmake -S $ConsumerSource -B $ConsumerBuild `
        -G "Visual Studio 17 2022" -A x64 `
        "-DCMAKE_PREFIX_PATH=$PortableInstall"
    Require-Success "T.7 Vulkan package consumer configure"
    & cmake --build $ConsumerBuild --config Release
    Require-Success "T.7 Vulkan package consumer build"
    & (Join-Path $ConsumerBuild `
        "Release\ultrarender_vulkan_consumer.exe")
    Require-Success "T.7 Vulkan package consumer execution"

    $RepoPath = $RepoRoot.Path
    $Drive = $RepoPath.Substring(0, 1).ToLowerInvariant()
    $LinuxRepo =
        "/mnt/$Drive/" +
        $RepoPath.Substring(3).Replace("\", "/")
    $LinuxCommand =
        "cd '$LinuxRepo' && " +
        "cmake -S . -B .build/phase_t7_linux -G Ninja " +
        "-DCMAKE_BUILD_TYPE=Release " +
        "-DUR_ENABLE_CUDA=OFF -DUR_ENABLE_VULKAN=ON " +
        "-DUR_BUILD_TESTS=ON -DUR_BUILD_CLI=OFF " +
        "-DUR_BUILD_PHYSICS=OFF && " +
        "cmake --build .build/phase_t7_linux " +
        "--target test_vulkan_runtime && " +
        "ctest --test-dir .build/phase_t7_linux " +
        "-R '^vulkan_runtime$' --output-on-failure"
    & wsl.exe -d $LinuxDistribution -- bash -lc $LinuxCommand
    Require-Success "T.7 Linux Vulkan build and execution"

    $Report = [ordered]@{
        schema = "ure.phase_t7.vulkan_foundation.v1"
        generated_utc =
            [DateTime]::UtcNow.ToString("o")
        vulkan_headers =
            $Dependencies.vulkan_headers.version
        volk_commit = $Dependencies.volk.commit
        slang = $Manifest.compiler.version
        operators = @(
            $Manifest.entries.PSObject.Properties.Name)
        windows = [ordered]@{
            cuda_free_build = $true
            cross_vendor_required = $true
            installed_package_consumer = $true
        }
        linux = [ordered]@{
            distribution = $LinuxDistribution
            cuda_free_build = $true
        }
    }
    $Report |
        ConvertTo-Json -Depth 5 |
        Set-Content -Encoding utf8 (
            Join-Path $ReportRoot "report.json")
    Write-Host (
        "Phase T.7 Vulkan foundation gate passed: " +
        "deterministic SPIR-V, Windows cross-vendor, " +
        "Windows CUDA-free and Linux CUDA-free.")
} finally {
    Pop-Location
}
