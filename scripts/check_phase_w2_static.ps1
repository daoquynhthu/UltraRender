$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

function Require-Text {
    param(
        [string]$Path,
        [string[]]$Patterns
    )
    $resolved = Join-Path $root $Path
    if (-not (Test-Path -LiteralPath $resolved)) {
        throw "Missing W.2 artifact: $Path"
    }
    $text = Get-Content -Raw -LiteralPath $resolved
    foreach ($pattern in $Patterns) {
        if (-not $text.Contains($pattern)) {
            throw "Missing W.2 contract '$pattern' in $Path"
        }
    }
}

Require-Text "libs/ure_types/include/ure/render_config.hpp" @(
    "camera_aperture_diameter_m",
    "camera_defocus_waves_at_edge",
    "camera_aperture_blade_count",
    "camera_wavelength_bin_count"
)
Require-Text "libs/ure_core/include/ure/wave_optics.hpp" @(
    "DiffractionPsfBank",
    "make_diffraction_psf_bank",
    "is_valid_diffraction_camera_config"
)
Require-Text "libs/ure_core/src/path_tracer_diffraction.cuh" @(
    "accumulate_diffraction_spectrum",
    "diffraction_spectral_accum",
    "cie_x(spectrum.wavelengths",
    "cie_y(spectrum.wavelengths",
    "cie_z(spectrum.wavelengths"
)
Require-Text "libs/ure_core/src/path_tracer_post.cu" @(
    "resolve_diffraction_framebuffer_kernel",
    "psf_weights",
    "psf_prefix",
    "source_sample_count"
)
Require-Text "libs/ure_core/include/ure/ure_c_api.h" @(
    "ure_wave_optics_config_v2_t",
    "ure_session_create_execution_config_v2"
)
Require-Text "schemas/ure_solver_contract_v1.fbs" @(
    "camera_aperture_diameter_m:double",
    "camera_pupil_sample_count:int=32"
)
Require-Text "pyure/__init__.py" @(
    "_WaveOpticsConfigV2",
    "camera_psf_radius_pixels",
    "ure_session_create_execution_config_v2"
)
Require-Text "tests/gpu/test_wave_optics_gpu.cu" @(
    "test_gpu_diffraction_camera_film_integration",
    "test_disabled_diffraction_parameters_are_inert"
)

$wavefront = Get-Content -Raw -LiteralPath (
    Join-Path $root "libs/ure_core/src/path_tracer_wavefront.cuh")
$callCount = (
    [regex]::Matches(
        $wavefront,
        "accumulate_diffraction_spectrum\(")).Count
if ($callCount -ne 3) {
    throw "Expected three wavelength-aware W.2 film accumulation sites, found $callCount"
}
if ($wavefront.Contains("camera diffraction GPU film is not implemented")) {
    throw "Stale W.2 rejection boundary remains in the production wavefront path"
}

$resolve = Get-Content -Raw -LiteralPath (
    Join-Path $root "libs/ure_core/src/path_tracer_post.cu")
if ($resolve.Contains("cie_x(wavelength_nm)")) {
    throw "W.2 resolve must not approximate the exact-wavelength CIE response at PSF bin centers"
}

Write-Host "Phase W.2 static audit passed."
