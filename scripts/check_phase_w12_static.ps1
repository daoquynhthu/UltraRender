$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

function Require-Text {
    param(
        [string]$Path,
        [string[]]$Patterns
    )
    $resolved = Join-Path $root $Path
    if (-not (Test-Path -LiteralPath $resolved)) {
        throw "Missing W.12 artifact: $Path"
    }
    $text = Get-Content -Raw -LiteralPath $resolved
    foreach ($pattern in $Patterns) {
        if (-not $text.Contains($pattern)) {
            throw "Missing W.12 contract '$pattern' in $Path"
        }
    }
}

Require-Text "tests/host/test_wave_optics.cpp" @(
    "test_circular_airy_oracle",
    "airy_first_zero_angle_rad",
    "test_slit_diffraction_reference",
    "test_grating_order_reference",
    "test_generalized_ray_and_interferometry",
    "test_fluorescence_resource_and_sampling",
    "test_gpu_renderer_rejects_unsupported_wave_combination"
)
Require-Text "tests/gpu/test_gpu_polarization.cu" @(
    "phase_fixture",
    "expected_s",
    "expected_p",
    "test_mueller_thin_film_uses_boundary",
    "test_stokes_sp_convention"
)
Require-Text "tests/gpu/test_wave_optics_gpu.cu" @(
    "test_gpu_diffractive_jones_response",
    "quarter_wave",
    "test_gpu_fluorescence_transport_and_gate",
    "test_gpu_partial_coherence_ensemble_reduction"
)
Require-Text "tests/gpu/test_spectral_pipeline_soa.cu" @(
    "test_rough_dielectric_eval_pdf_visible_to_direct_light",
    "effective_thin_film_thickness",
    "rough_dielectric_reflection_pdf",
    "test_rough_dielectric_pdf_normalizes_over_reflection_and_transmission",
    "test_rough_dielectric_white_furnace_energy_bound"
)
Require-Text "tests/host/test_distributed_wave_io.cpp" @(
    "test_coherent_before_incoherent_reduction",
    "film.resolved_power_at",
    "duplicate coherent realization was accepted",
    "overlapping realization range was accepted"
)
Require-Text "tests/host/test_config.cpp" @(
    "test_wave_optics_json_fields",
    "test_wave_optics_cli_overrides"
)
Require-Text "tests/host/test_session.cpp" @(
    "ure_wave_optics_config_t",
    "URE_WAVE_OPTICS_CAMERA_DIFFRACTION",
    "URE_WAVE_OPTICS_COHERENT_FIELD"
)
Require-Text "tests/host/test_pyure_smoke.py" @(
    "camera_diffraction=True",
    "diffractive_materials=True",
    "fluorescence=True",
    "coherent_field=True"
)
Require-Text "libs/ure_core/include/ure/ure_c_api.h" @(
    "ure_wave_optics_config_t",
    "camera_diffraction_enabled",
    "coherent_field_enabled",
    "partial_coherence_enabled",
    "diffractive_materials_enabled",
    "fluorescence_enabled",
    "local_fullwave_enabled"
)
Require-Text "scripts/run_phase_w_validation_suite.ps1" @(
    "ure.phase_w.validation.v1",
    "gpu_wave_optics",
    "gpu_polarization",
    "gpu_spectral_soa",
    "test_wave_optics",
    "test_distributed_wave_io",
    "check_phase_w12_static.ps1",
    "check_documentation_consistency.ps1"
)
Require-Text "tools/benchmarks/validate_phase_w_validation_report.ps1" @(
    "ure.phase_w.validation.v1",
    "ExpectedEvidence",
    "ExpectedTests",
    "ExpectedStatic",
    "RequireCleanTree"
)
Require-Text "tools/benchmarks/test_phase_w_validation_report_contract.ps1" @(
    "Assert-Rejected",
    "artifact_digest",
    "physical_evidence",
    "static_gate"
)

$rgbPaths = @(
    "libs/ure_core/src/distributed_contract.cpp",
    "libs/ure_core/src/gpu_driver.cu"
)
foreach ($path in $rgbPaths) {
    $text = Get-Content -Raw -LiteralPath (
        Join-Path $root $path)
    if ($text.Contains(
            "resolved_amplitude_at(") -or
        $text.Contains(
            "resolved_density()")) {
        throw "W.12 coherent sufficient statistics entered an RGB output path"
    }
}

Write-Host "Phase W.12 static audit passed."
