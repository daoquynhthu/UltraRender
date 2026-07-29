$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

function Require-Text {
    param(
        [string]$Path,
        [string[]]$Patterns
    )
    $resolved = Join-Path $root $Path
    if (-not (Test-Path -LiteralPath $resolved)) {
        throw "Missing U.3 artifact: $Path"
    }
    $text = Get-Content -Raw -LiteralPath $resolved
    foreach ($pattern in $Patterns) {
        if (-not $text.Contains($pattern)) {
            throw "Missing U.3 contract '$pattern' in $Path"
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
                throw "Forbidden U.3 dependency '$pattern' in $path"
            }
        }
    }
}

Require-Text "libs/ure_hydra/CMakeLists.txt" @(
    "Houdini::Dep::pxr_pxOsd",
    "src/mesh_rprim.cpp",
    "src/render_param.cpp"
)
Require-Text "libs/ure_hydra/src/render_delegate.cpp" @(
    "HdPrimTypeTokens->mesh",
    "new HdUREMesh",
    "meshCount",
    "rejectedMeshCount",
    "lastError"
)
Require-Text "libs/ure_hydra/src/render_param.hpp" @(
    "std::shared_ptr<const ure::scene_ir::MeshResource>",
    "std::shared_mutex",
    "FindMesh",
    "revision"
)
Require-Text "libs/ure_hydra/src/mesh_rprim.cpp" @(
    "ComputeTriangleIndices",
    "ComputeTriangulatedFaceVaryingPrimvar",
    "HdInterpolationFaceVarying",
    "resolve_indices",
    "DirtyPoints",
    "DirtyTopology",
    "DirtyTransform",
    "DirtyMaterialId",
    "DirtyVisibility",
    "DirtyDoubleSided",
    "GetInstancerId",
    "subdivision surfaces require an exact tessellation boundary",
    "native index domain",
    "RejectMesh"
)
Require-Text "tests/hydra/test_hydra_mesh_rprim.cpp" @(
    "HdRenderIndex::New",
    "HdInterpolationFaceVarying",
    "VtIntArray{0, 1, 0, 1}",
    "DirtyTransform",
    "DirtyPoints",
    "subdivision rejection fixture",
    "revision"
)
Require-Text "scripts/run_phase_u3_hydra_mesh_gate.ps1" @(
    "test_hydra_mesh_rprim",
    "--component Hydra",
    "PXR_PLUGINPATH_NAME"
)

Reject-Text @(
    "libs/ure_hydra/src/mesh_rprim.cpp",
    "libs/ure_hydra/src/render_param.cpp"
) @(
    "cuda_runtime",
    "vulkan.h",
    "d3d12.h",
    "CreateWindow"
)

Write-Host "Phase U.3 static audit passed."
