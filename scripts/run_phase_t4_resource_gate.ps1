$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$SourceDir = Join-Path $RepoRoot "tests\sdk_free"
$BuildDir = Join-Path $RepoRoot ".build\phase_t4_sdk_free"

& cmake -S $SourceDir -B $BuildDir -G "Visual Studio 17 2022" -A x64
if ($LASTEXITCODE -ne 0) {
    throw "T.4 SDK-free configure failed"
}

& cmake --build $BuildDir --config Release
if ($LASTEXITCODE -ne 0) {
    throw "T.4 SDK-free build failed"
}

$Cache = Get-Content -Raw (Join-Path $BuildDir "CMakeCache.txt")
if ($Cache -match "CMAKE_CUDA_COMPILER") {
    throw "T.4 SDK-free gate configured a CUDA compiler"
}

& ctest --test-dir $BuildDir -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) {
    throw "T.4 SDK-free tests failed"
}

Write-Host "Phase T.4 SDK-free resource gate passed"
