$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

function Require-Text {
    param(
        [string]$Path,
        [string[]]$Patterns
    )
    $resolved = Join-Path $root $Path
    if (-not (Test-Path -LiteralPath $resolved)) {
        throw "Missing U.2 artifact: $Path"
    }
    $text = Get-Content -Raw -LiteralPath $resolved
    foreach ($pattern in $Patterns) {
        if (-not $text.Contains($pattern)) {
            throw "Missing U.2 contract '$pattern' in $Path"
        }
    }
}

function Reject-Text {
    param(
        [string[]]$Paths,
        [string[]]$Patterns
    )
    foreach ($path in $Paths) {
        $text = Get-Content -Raw -LiteralPath (
            Join-Path $root $path)
        foreach ($pattern in $Patterns) {
            if ($text.Contains($pattern)) {
                throw "Forbidden U.2 dependency '$pattern' in $path"
            }
        }
    }
}

Require-Text "CMakeLists.txt" @(
    "UR_ENABLE_HYDRA",
    "UR_OPENUSD_ROOT",
    "add_subdirectory(libs/ure_hydra)",
    "add_subdirectory(tests/hydra)"
)
Require-Text "libs/ure_hydra/CMakeLists.txt" @(
    "UR_ENABLE_HYDRA requires an explicit UR_OPENUSD_ROOT",
    "find_package(Houdini CONFIG REQUIRED)",
    "pxr_hd",
    "pxr_python",
    "ure_hydra_delegate",
    "add_library(ure_hydra MODULE",
    "MFB_ALT_PACKAGE_NAME=ure_hydra",
    "/Zc:inline-",
    "plugInfo.json",
    "install(TARGETS ure_hydra"
)
Require-Text "libs/ure_hydra/src/render_delegate.hpp" @(
    "class HdURE final : public HdRenderDelegate",
    "GetSupportedRprimTypes",
    "GetResourceRegistry",
    "CreateRenderPass",
    "CommitResources",
    "GetRenderStats"
)
Require-Text "libs/ure_hydra/src/render_delegate.cpp" @(
    "kUsdSchemaAdapterIdentity",
    "renderReady",
    "return empty_types()",
    "unavailable until",
    "unavailable before U.3",
    "resource_epoch"
)
Require-Text "libs/ure_hydra/src/renderer_plugin.cpp" @(
    "class HdURERendererPlugin final",
    "HdRendererPluginRegistry::Define",
    "return false"
)
Require-Text "libs/ure_hydra/resources/plugInfo.json.in" @(
    "HdURERendererPlugin",
    "HdRendererPlugin",
    "UltraRender"
)
Require-Text "tests/hydra/test_hydra_render_delegate.cpp" @(
    "std::is_base_of_v",
    "advertised prim support early",
    "renderReady",
    "resourceEpoch"
)
Require-Text "tests/hydra/test_hydra_plugin_discovery.cpp" @(
    "GetOrCreateRendererPlugin",
    "U.2 skeleton was advertised as render-ready",
    "CreateRenderDelegate"
)

Reject-Text @(
    "libs/ure_hydra/src/render_delegate.cpp",
    "libs/ure_hydra/src/renderer_plugin.cpp"
) @(
    "cuda_runtime",
    "vulkan.h",
    "d3d12.h",
    "wgl",
    "glX",
    "CreateWindow"
)

Write-Host "Phase U.2 static audit passed."
