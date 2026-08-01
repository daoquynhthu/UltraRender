$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$SourceDir = Join-Path $RepoRoot "tests\sdk_free"
$BuildDir = Join-Path $RepoRoot ".build\phase_t5_sdk_free"

& cmake -S $SourceDir -B $BuildDir -G "Visual Studio 18 2026" -A x64
if ($LASTEXITCODE -ne 0) {
    throw "T.5 SDK-free configure failed"
}

& cmake --build $BuildDir --config Release --target `
    test_execution_graph_sdk_free test_public_surface_sdk_free
if ($LASTEXITCODE -ne 0) {
    throw "T.5 SDK-free build failed"
}

$Cache = Get-Content -Raw (Join-Path $BuildDir "CMakeCache.txt")
if ($Cache -match "CMAKE_CUDA_COMPILER") {
    throw "T.5 SDK-free gate configured a CUDA compiler"
}

& ctest --test-dir $BuildDir -C Release `
    -R "^(execution_graph_sdk_free|public_surface_sdk_free)$" `
    --output-on-failure
if ($LASTEXITCODE -ne 0) {
    throw "T.5 SDK-free execution graph tests failed"
}

Write-Host "Phase T.5 SDK-free execution graph gate passed"
