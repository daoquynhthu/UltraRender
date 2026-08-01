param(
    [string]$OpenUsdRoot = $env:HFS,
    [string]$BuildDir = ".build/phase_u3_hydra"
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($OpenUsdRoot)) {
    throw "Pass -OpenUsdRoot explicitly or set HFS"
}
$openUsd = Resolve-Path -LiteralPath $OpenUsdRoot
$build = Join-Path $root $BuildDir
$cmake = Get-Command cmake -ErrorAction Stop

& $cmake.Source `
    -S $root `
    -B $build `
    -G "Visual Studio 18 2026" `
    -A x64 `
    -DUR_ENABLE_CUDA=OFF `
    -DUR_ENABLE_VULKAN=OFF `
    -DUR_ENABLE_D3D12=OFF `
    -DUR_ENABLE_HYDRA=ON `
    -DUR_OPENUSD_ROOT="$openUsd" `
    -DUR_BUILD_CLI=OFF `
    -DUR_BUILD_PHYSICS=OFF `
    -DUR_BUILD_TESTS=ON
if ($LASTEXITCODE -ne 0) {
    throw "U.3 Hydra configuration failed"
}

& $cmake.Source --build $build `
    --config Release `
    --target ure_hydra `
        test_hydra_render_delegate `
        test_hydra_plugin_discovery `
        test_hydra_mesh_rprim
if ($LASTEXITCODE -ne 0) {
    throw "U.3 Hydra build failed"
}

& ctest --test-dir $build `
    -C Release `
    -R "^test_hydra_(render_delegate|plugin_discovery|mesh_rprim)$" `
    --output-on-failure
if ($LASTEXITCODE -ne 0) {
    throw "U.3 Hydra runtime gate failed"
}

$install = Join-Path $build "install"
& $cmake.Source --install $build `
    --config Release `
    --component Hydra `
    --prefix $install
if ($LASTEXITCODE -ne 0) {
    throw "U.3 Hydra install failed"
}

$pluginResources = Join-Path $install `
    "lib/UltraRender/hydra/ure_hydra/resources"
$discoveryTest = Join-Path $build `
    "tests/hydra/Release/test_hydra_plugin_discovery.exe"
$savedPluginPath = $env:PXR_PLUGINPATH_NAME
$savedPath = $env:PATH
try {
    $env:PXR_PLUGINPATH_NAME = $pluginResources
    $env:PATH = "$(Join-Path $openUsd 'bin');$savedPath"
    & $discoveryTest
    if ($LASTEXITCODE -ne 0) {
        throw "Installed U.3 Hydra plugin discovery failed"
    }
}
finally {
    $env:PXR_PLUGINPATH_NAME = $savedPluginPath
    $env:PATH = $savedPath
}

Write-Host "Phase U.3 Hydra mesh gate passed."
