$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

function Require-Text {
    param(
        [string]$Path,
        [string[]]$Patterns
    )
    $resolved = Join-Path $root $Path
    if (-not (Test-Path -LiteralPath $resolved)) {
        throw "Missing W.6 artifact: $Path"
    }
    $text = Get-Content -Raw -LiteralPath $resolved
    foreach ($pattern in $Patterns) {
        if (-not $text.Contains($pattern)) {
            throw "Missing W.6 contract '$pattern' in $Path"
        }
    }
}

Require-Text "libs/ure_types/include/ure/scene_ir.hpp" @(
    "BsdfFluorescence",
    "FluorescenceResource",
    "kMaxFluorescenceMatrixEntries",
    "excitation_wavelengths_nm",
    "emission_pdf_per_nm",
    "lifetime_seconds"
)
Require-Text "libs/ure_core/src/wave_optics.cpp" @(
    "sample_fluorescence(",
    "sample_fluorescence_adjoint(",
    ".excitation_wavelengths_nm[row] /",
    "std::log1p(-delay_sample)",
    "density > 0.0"
)
Require-Text "libs/ure_core/src/path_tracer_fluorescence_material.cuh" @(
    "fluorescence_adjoint_weight",
    "transition_pdf",
    "joint_pdf",
    "kRayFlagNeeUnavailable",
    "StokesVector(",
    "fluorescence_delay_seconds",
    "current_medium_idx"
)
Require-Text "libs/ure_core/src/path_tracer_wavefront.cuh" @(
    "path_tracer_fluorescence_material.cuh",
    "copy_film_wavelengths",
    "film_spectrum"
)
Require-Text "libs/ure_core/src/path_tracer_host_api.cu" @(
    "fluorescence tables exceed the bounded scene resource budget",
    "fluorescence operator count exceeds the GPU index range",
    "fluorescence queue state exceeds backend memory budget",
    "full scene reload"
)
Require-Text "libs/ure_core/src/gpu_scene_compiler.cpp" @(
    "MaterialType::Fluorescent",
    "is_supported_fluorescence_config",
    "fluorescence resource contract is invalid"
)
Require-Text "schemas/ure_scene_ir_v1.fbs" @(
    "BsdfFluorescence = 20",
    "table FluorescenceResource",
    "fluorescence:FluorescenceResource"
)
Require-Text "libs/ure_sceneio/src/materialx_io.cpp" @(
    "URE_bsdf_fluorescence",
    "excitation_wavelengths_nm",
    "emission_pdf_per_nm"
)
Require-Text "tests/gpu/test_wave_optics_gpu.cu" @(
    "test_gpu_fluorescence_transport_and_gate",
    "test_gpu_fluorescence_update_requires_reload",
    "center_red > center_blue"
)

$transport = Get-Content -Raw -LiteralPath (
    Join-Path $root "libs/ure_core/src/path_tracer_fluorescence_material.cuh")
if ($transport.Contains("next_dielectric_medium_index")) {
    throw "W.6 fluorescence must preserve the current medium"
}
if ($transport.Contains("emission_wavelength = excitation")) {
    throw "W.6 CUDA camera transport must remain adjoint"
}

Write-Host "Phase W.6 static audit passed."
