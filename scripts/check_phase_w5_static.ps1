$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

function Require-Text {
    param(
        [string]$Path,
        [string[]]$Patterns
    )
    $resolved = Join-Path $root $Path
    if (-not (Test-Path -LiteralPath $resolved)) {
        throw "Missing W.5 artifact: $Path"
    }
    $text = Get-Content -Raw -LiteralPath $resolved
    foreach ($pattern in $Patterns) {
        if (-not $text.Contains($pattern)) {
            throw "Missing W.5 contract '$pattern' in $Path"
        }
    }
}

Require-Text "libs/ure_types/include/ure/scene_ir.hpp" @(
    "BsdfGrating",
    "BsdfPhaseMask",
    "BsdfZonePlate",
    "BsdfDoe",
    "BsdfScatteringTable",
    "DiffractiveScatteringEntry",
    "kMaxDiffractiveScatteringEntries"
)
Require-Text "libs/ure_core/src/wave_optics.cpp" @(
    "maximum_jones_power",
    "diffractive_orders",
    "evanescent_decay_per_m",
    "sample_channels.at(item.first)"
)
Require-Text "libs/ure_core/src/path_tracer_diffractive_material.cuh" @(
    "diffraction_table_jones",
    "build_diffractive_candidates",
    "reserve_ray_slot",
    "SpectralRayModeLane"
)
Require-Text "libs/ure_core/src/path_tracer_intersect.cuh" @(
    "mesh_surface_tangent",
    "surface_tangent",
    "transform_vector"
)
Require-Text "libs/ure_core/src/path_tracer_diffractive_jones.cuh" @(
    "apply_diffractive_jones",
    "jones.sp",
    "jones.ps"
)
Require-Text "libs/ure_core/src/gpu_scene_compiler.cpp" @(
    "MaterialType::Diffractive",
    "is_supported_diffractive_material_config",
    "diffraction_operator",
    "diffraction_table.push_back"
)
Require-Text "schemas/ure_scene_ir_v1.fbs" @(
    "BsdfScatteringTable = 19",
    "table DiffractiveOperator",
    "diffraction:DiffractiveOperator"
)
Require-Text "libs/ure_sceneio/src/materialx_io.cpp" @(
    "URE_bsdf_scattering_table",
    "scattering_table_string",
    "parse_scattering_table"
)
Require-Text "tests/gpu/test_wave_optics_gpu.cu" @(
    "test_gpu_diffractive_jones_response",
    "test_gpu_diffractive_material_transport",
    "test_gpu_diffractive_material_requires_gate"
)

$transport = Get-Content -Raw -LiteralPath (
    Join-Path $root "libs/ure_core/src/path_tracer_diffractive_material.cuh")
if ($transport.Contains("next_dielectric_medium_index")) {
    throw "W.5 thin-sheet operators must not mutate dielectric medium state"
}
if ($transport.Contains("470.0f")) {
    throw "W.5 scattering-table interpolation must derive its wavelength scale from the table"
}

Write-Host "Phase W.5 static audit passed."
