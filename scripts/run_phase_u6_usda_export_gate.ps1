param(
    [string]$OpenUsdRoot = $env:HFS,
    [string]$BuildDir = ".build/phase_u6_usda"
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($OpenUsdRoot)) {
    throw "Pass -OpenUsdRoot explicitly or set HFS"
}
$openUsd = Resolve-Path -LiteralPath $OpenUsdRoot
$build = Join-Path $root $BuildDir

& (Join-Path $PSScriptRoot "build_x64.ps1") `
    -BuildDir $BuildDir `
    -Config Release `
    -Targets @("ure_hydra", "test_usda_export") `
    -CMakeOptions @(
        "-DUR_ENABLE_CUDA=OFF",
        "-DUR_ENABLE_VULKAN=OFF",
        "-DUR_ENABLE_D3D12=OFF",
        "-DUR_ENABLE_HYDRA=ON",
        "-DUR_OPENUSD_ROOT=`"$openUsd`"",
        "-DUR_BUILD_CLI=OFF",
        "-DUR_BUILD_PHYSICS=OFF",
        "-DUR_BUILD_TESTS=ON")

& ctest --test-dir $build `
    -C Release `
    -R "^test_usda_export$" `
    --output-on-failure
if ($LASTEXITCODE -ne 0) {
    throw "U.6 actual-OpenUSD export gate failed"
}

Write-Host "Phase U.6 USDA export gate passed."
