param(
    [string]$OpenUsdRoot = $env:HFS,
    [string]$BuildDir = ".build/phase_u5_hydra"
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
    -Targets @(
        "ure_hydra",
        "test_hydra_render_delegate",
        "test_hydra_plugin_discovery",
        "test_hydra_mesh_rprim",
        "test_hydra_material_sprim",
        "test_hydra_render_buffer",
        "test_hydra_progressive_render") `
    -CMakeOptions @(
        "-DUR_ENABLE_CUDA=ON",
        "-DUR_ENABLE_VULKAN=OFF",
        "-DUR_ENABLE_D3D12=OFF",
        "-DUR_ENABLE_HYDRA=ON",
        "-DUR_OPENUSD_ROOT=`"$openUsd`"",
        "-DUR_BUILD_CLI=ON",
        "-DUR_BUILD_PHYSICS=OFF",
        "-DUR_BUILD_TESTS=ON")

& ctest --test-dir $build `
    -C Release `
    -R "^test_hydra_(render_delegate|plugin_discovery|mesh_rprim|material_sprim|render_buffer|progressive_render)$" `
    --output-on-failure
if ($LASTEXITCODE -ne 0) {
    throw "U.5 Hydra runtime gate failed"
}

$install = Join-Path $build "install"
& cmake --install $build `
    --config Release `
    --component Hydra `
    --prefix $install
if ($LASTEXITCODE -ne 0) {
    throw "U.5 Hydra install failed"
}

$pluginResources = Join-Path $install `
    "lib/UltraRender/hydra/ure_hydra/resources"
$discoveryTest = Join-Path $build `
    "tests/hydra/test_hydra_plugin_discovery.exe"
$savedPluginPath = $env:PXR_PLUGINPATH_NAME
$savedPath = $env:PATH
try {
    $env:PXR_PLUGINPATH_NAME = $pluginResources
    $env:PATH = "$(Join-Path $openUsd 'bin');$savedPath"
    & $discoveryTest
    if ($LASTEXITCODE -ne 0) {
        throw "Installed U.5 Hydra plugin discovery failed"
    }
}
finally {
    $env:PXR_PLUGINPATH_NAME = $savedPluginPath
    $env:PATH = $savedPath
}

Write-Host "Phase U.5 Hydra progressive render gate passed."
