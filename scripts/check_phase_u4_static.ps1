$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

function Require-Text {
    param(
        [string]$Path,
        [string[]]$Patterns
    )
    $resolved = Join-Path $root $Path
    if (-not (Test-Path -LiteralPath $resolved)) {
        throw "Missing U.4 artifact: $Path"
    }
    $text = Get-Content -Raw -LiteralPath $resolved
    foreach ($pattern in $Patterns) {
        if (-not $text.Contains($pattern)) {
            throw "Missing U.4 contract '$pattern' in $Path"
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
                throw "Forbidden U.4 dependency '$pattern' in $path"
            }
        }
    }
}

Require-Text "libs/ure_hydra/src/render_delegate.cpp" @(
    "HdPrimTypeTokens->material",
    "new HdUREMaterial",
    "materialCount",
    "rejectedMaterialCount",
    "materialLossCount"
)
Require-Text "libs/ure_hydra/src/material_network.cpp" @(
    "HdMaterialNetwork2",
    "UsdPreviewSurface",
    "UsdUVTexture",
    "UsdPrimvarReader_",
    "URE_constant_color",
    "URE_bsdf_layer",
    "URE_output_surface",
    "URE-U4-LOSS-PREVIEW-BSDF",
    "URE-U4-LOSS-UNUSED-PARAMETER",
    "URE-U4-ERROR-UNSUPPORTED-NODE",
    "URE-U4-ERROR-UNUSED-CONNECTION",
    "graph_.validate()"
)
Require-Text "libs/ure_hydra/src/material_sprim.cpp" @(
    "GetMaterialResource",
    "HdMaterialNetworkMap",
    "HdConvertToHdMaterialNetwork2",
    "UpdateMaterial",
    "RejectMaterial",
    "RemoveMaterial"
)
Require-Text "libs/ure_hydra/src/render_param.hpp" @(
    "HdUREMaterialLossSeverity",
    "HdUREMaterialRecord",
    "FindMaterialLossReport",
    "std::shared_ptr<const ure::scene_ir::MaterialNode>"
)
Require-Text "tests/hydra/test_hydra_material_sprim.cpp" @(
    "URE_constant_color",
    "UsdPreviewSurface",
    "UsdUVTexture",
    "DirtyResource",
    "URE-U4-LOSS-PREVIEW-BSDF",
    "URE-U4-ERROR-UNSUPPORTED-NODE",
    "URE-U4-ERROR-UNUSED-CONNECTION"
)
Require-Text "scripts/run_phase_u4_hydra_material_gate.ps1" @(
    "test_hydra_material_sprim",
    "--component Hydra",
    "PXR_PLUGINPATH_NAME"
)

Reject-Text @(
    "libs/ure_hydra/src/material_network.cpp",
    "libs/ure_hydra/src/material_sprim.cpp"
) @(
    "cuda_runtime",
    "vulkan.h",
    "d3d12.h",
    "CreateWindow",
    "pink checkerboard"
)

Write-Host "Phase U.4 static audit passed."
