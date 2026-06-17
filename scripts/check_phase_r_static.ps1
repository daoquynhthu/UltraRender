param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
)

$ErrorActionPreference = "Stop"

function Assert-Contains {
    param(
        [string]$Path,
        [string]$Pattern,
        [string]$Label
    )
    $FullPath = Join-Path $RepoRoot $Path
    if (-not (Test-Path $FullPath)) {
        throw "missing file for $Label`: $Path"
    }
    if (-not (Select-String -LiteralPath $FullPath -Pattern $Pattern -Quiet)) {
        throw "missing Phase R static marker [$Label] in $Path`: $Pattern"
    }
}

Assert-Contains "tools\benchmarks\run_phase_r_integrator_smoke.ps1" "samples_per_second" "R.0 benchmark smoke metric"
Assert-Contains "libs\ure_core\src\path_tracer_host_api.cu" "launch_blocks_for_active_count" "R.1 active-count launch"
Assert-Contains "libs\ure_core\src\path_tracer_host_api.cu" "current_ray_count <= 0" "R.1 empty-queue termination"
Assert-Contains "libs\ure_core\include\ure\gpu_context.hpp" "last_integrator_ray_queue_overflow_count" "R.2 ray overflow telemetry"
Assert-Contains "libs\ure_core\include\ure\gpu_structs.hpp" "overflow_count" "R.2 queue overflow counter"
Assert-Contains "libs\ure_core\include\ure\path_tracer_sampling.cuh" "sample_path_dimension" "R.3 unified path dimensions"
Assert-Contains "libs\ure_core\src\path_tracer_wavefront.cuh" "kPathDimVolumePhaseU" "R.3 volume phase dimensions"
Assert-Contains "libs\ure_core\src\path_tracer_host_api.cu" "build_light_alias_table" "R.4 weighted light alias table"
Assert-Contains "libs\ure_core\src\path_tracer_wavefront.cuh" "sample_light_list_index" "R.4 unified light selection"
Assert-Contains "libs\ure_core\include\ure\gpu_spectrum_utils.cuh" "scene_cie_mixture_wavelength_pdf" "R.5 scene/CIE wavelength mixture PDF"
Assert-Contains "libs\ure_core\src\path_tracer_raygen.cu" "sample_scene_cie_mixture_wavelength" "R.5 proposal-driven raygen"
Assert-Contains "tests\gpu\test_spectral_pipeline.cu" "test_narrowband_scene_proposal_reduces_spd_estimator_error" "R.5 narrowband proposal oracle"
Assert-Contains "libs\ure_core\src\path_tracer_volume.cuh" "Mie = 2" "R.6 Mie enum boundary"
Assert-Contains "libs\ure_core\src\path_tracer_volume.cuh" "is_supported_volume_phase_function" "R.6 phase support gate"
Assert-Contains "tests\gpu\test_gpu_volume.cu" "test_volume_phase_selector_boundary" "R.6 volume phase selector test"
Assert-Contains "tests\gpu\test_spectral_pipeline_soa.cu" "rough_dielectric_energy_bound" "R.6 rough dielectric energy oracle"
Assert-Contains "tests\gpu\test_gpu_polarization.cu" "test_thin_film_sp_energy_grid" "R.6 thin-film energy oracle"
Assert-Contains "libs\ure_types\include\ure\render_config.hpp" "PathGuidingConfig" "R.7 path guiding config"
Assert-Contains "libs\ure_core\src\path_tracer_wavefront.cuh" "guided_mixture_light_selection_pdf" "R.7 guided light PDF"
Assert-Contains "libs\ure_core\src\path_tracer_wavefront.cuh" "path_guiding_light_weights" "R.7 guide cache device path"
Assert-Contains "tests\gpu\test_render_basic.cu" "test_path_guiding_shadow_visibility_updates_light_weight" "R.7 progressive guide update test"
Assert-Contains "tests\host\test_config.cpp" "test_path_guiding_cli_overrides" "R.7 config parity test"
Assert-Contains "libs\ure_types\include\ure\render_config.hpp" "RestirDirectConfig" "R.8 ReSTIR DI config"
Assert-Contains "libs\ure_core\src\path_tracer_wavefront.cuh" "enqueue_restir_di_temporal_replay" "R.8 temporal ReSTIR DI replay path"
Assert-Contains "libs\ure_core\src\path_tracer_wavefront.cuh" "store_restir_di_visible_candidate" "R.8 visible candidate reservoir update"
Assert-Contains "libs\ure_core\include\ure\gpu_structs.hpp" "restir_di_stokes_i" "R.8 Stokes-compatible reservoir metadata"
Assert-Contains "libs\ure_core\src\path_tracer_host_api.cu" "Unbiased ReSTIR DI is not implemented yet" "R.8 unsupported unbiased mode fail-loud"
Assert-Contains "tests\gpu\test_render_basic.cu" "test_restir_di_visible_shadow_updates_reservoir_metadata" "R.8 reservoir metadata test"
Assert-Contains "tests\host\test_config.cpp" "test_restir_di_cli_overrides" "R.8 config parity test"

Write-Host "Phase R static audit passed"
