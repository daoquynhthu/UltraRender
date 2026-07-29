$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

function Require-Text {
    param(
        [string]$Path,
        [string[]]$Patterns
    )
    $resolved = Join-Path $root $Path
    if (-not (Test-Path -LiteralPath $resolved)) {
        throw "Missing U.1 artifact: $Path"
    }
    $text = Get-Content -Raw -LiteralPath $resolved
    foreach ($pattern in $Patterns) {
        if (-not $text.Contains($pattern)) {
            throw "Missing U.1 contract '$pattern' in $Path"
        }
    }
}

function Reject-Text {
    param(
        [string[]]$Paths,
        [string[]]$Patterns
    )
    foreach ($path in $Paths) {
        $resolved = Join-Path $root $path
        $text = Get-Content -Raw -LiteralPath $resolved
        foreach ($pattern in $Patterns) {
            if ($text.Contains($pattern)) {
                throw "Forbidden U.1 dependency or weak contract '$pattern' in $path"
            }
        }
    }
}

$adapterFiles = @(
    "libs/ure_sceneio/include/ure/usd_schema_adapter.hpp",
    "libs/ure_sceneio/src/usd_schema_adapter.cpp"
)

Require-Text $adapterFiles[0] @(
    "ure.adapter.usd-schema/1.0",
    "UREPhysicsAPI",
    "URESpectralMaterialAPI",
    "ure:spectral:domainBins",
    "ure:spectral:packetLanes",
    "ure:spectral:resourceUri",
    "ure:spectral:contentHash",
    "ure:spectral:basisCount",
    "ure:spectral:tileBins",
    "UsdStageSnapshot",
    "affine_trs_compatible"
)
Require-Text $adapterFiles[1] @(
    "stage.metres_per_unit",
    "basis_rotation",
    "face_vertex_counts",
    "authored_time_sample_count",
    "supported_schema",
    "make_preview_graph",
    "NativeResourceCatalog",
    "validate_resource_catalog",
    "validate_scene_ir_archive",
    "native_scene::sha256_hex"
)
Require-Text "tests/host/test_usd_schema_adapter.cpp" @(
    "1'000'000",
    "packet_lanes = 16",
    "non-triangulated USD mesh was accepted",
    "unsupported USD xform stack was accepted",
    "unsupported required USD schema was accepted",
    "animated USD stage silently entered static SceneIR",
    "weak USD spectral bands mapping was accepted",
    "duplicate USD prim path was accepted",
    "scene_ir_semantic_hash"
)
Require-Text "tests/host/test_public_surface_sdk_free.cpp" @(
    "ure/usd_schema_adapter.hpp"
)

Reject-Text $adapterFiles @(
    "#include <pxr",
    "pxr::",
    "UsdStageRefPtr",
    "SdfPath",
    "usdcat",
    "spectral_bands"
)

Write-Host "Phase U.1 static audit passed."
