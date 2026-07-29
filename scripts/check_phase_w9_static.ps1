$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

function Require-Text {
    param(
        [string]$Path,
        [string[]]$Patterns
    )
    $resolved = Join-Path $root $Path
    if (-not (Test-Path -LiteralPath $resolved)) {
        throw "Missing W.9 artifact: $Path"
    }
    $text = Get-Content -Raw -LiteralPath $resolved
    foreach ($pattern in $Patterns) {
        if (-not $text.Contains($pattern)) {
            throw "Missing W.9 contract '$pattern' in $Path"
        }
    }
}

Require-Text "libs/ure_core/include/ure/anisotropic_optics.hpp" @(
    "SymmetricTensor3",
    "AnisotropicMediumSample",
    "AnisotropicMedium",
    "PolarizationEigenmode",
    "ModalSolution",
    "transverse_displacement",
    "make_principal_anisotropic_sample",
    "make_liquid_crystal_sample",
    "make_stress_birefringent_sample",
    "propagate_anisotropic_displacements_gpu",
    "kMaxAnisotropicSpectralSamples",
    "kMaxModalPropagationBatch"
)
Require-Text "libs/ure_core/src/anisotropic_optics.cpp" @(
    "positive_definite",
    "positive_semidefinite",
    "dielectric_impermeability",
    "solve_anisotropic_modes",
    "stress_optic_coefficient_per_pa",
    "matrix.xy * matrix.yx",
    "optical_activity_rad_per_m"
)
Require-Text "libs/ure_core/src/anisotropic_optics_gpu.cu" @(
    "propagate_modal_fields_kernel",
    "anisotropic-optics.input",
    "matrix_exponential",
    "complete_external"
)
Require-Text "tests/host/test_wave_optics.cpp" @(
    "test_anisotropic_spectral_tensor_contract",
    "test_uniaxial_and_liquid_crystal_modes",
    "test_modal_retardance_activity_and_dichroism",
    "test_stress_birefringence_contract"
)
Require-Text "tests/gpu/test_wave_optics_gpu.cu" @(
    "test_gpu_anisotropic_modal_transport",
    "propagate_anisotropic_displacements_gpu"
)
Require-Text "tests/host/test_public_surface_sdk_free.cpp" @(
    "ure/anisotropic_optics.hpp"
)

$anisotropicSources = @(
    "libs/ure_core/include/ure/anisotropic_optics.hpp",
    "libs/ure_core/src/anisotropic_optics.cpp",
    "libs/ure_core/src/anisotropic_optics_gpu.cu"
)
foreach ($relative in $anisotropicSources) {
    $text = Get-Content -Raw -LiteralPath (
        Join-Path $root $relative)
    if ($text.Contains("GpuMaterialData") -or
        $text.Contains("material.ior") -or
        $text.Contains("header.ior")) {
        throw "W.9 anisotropic transport was flattened into the scalar dielectric material path"
    }
}

Write-Host "Phase W.9 static audit passed."
