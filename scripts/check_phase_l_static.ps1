param(
    [string]$RepoRoot = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
} else {
    $RepoRoot = Resolve-Path $RepoRoot
}

$scanRoots = @("libs", "apps", "tests")
$files = foreach ($root in $scanRoots) {
    Get-ChildItem -Path (Join-Path $RepoRoot $root) -Recurse -File |
        Where-Object { $_.Extension -in @(".h", ".hpp", ".cuh", ".cpp", ".cu", ".py") }
}

function Assert-NoMatch {
    param(
        [string]$Name,
        [string]$Pattern
    )
    $matches = $files | Select-String -Pattern $Pattern
    if ($matches) {
        Write-Host "Phase L static audit failed: $Name"
        $matches | ForEach-Object {
            Write-Host ("{0}:{1}: {2}" -f $_.Path, $_.LineNumber, $_.Line.Trim())
        }
        throw $Name
    }
}

Assert-NoMatch "old GpuSpectrum carrier" "\bGpuSpectrum\b"
Assert-NoMatch "old spectral channel cap" "\bk(Max|Min)SpectralChannels\b"
Assert-NoMatch "GpuTexture packet data field" "\bGpuTexture\b[\s\S]*\bSpectralPacket\s*\*\s*data\b"
Assert-NoMatch "texture upload using packet sizeof" "sizeof\s*\(\s*SpectralPacket\s*\).*texture|texture.*sizeof\s*\(\s*SpectralPacket\s*\)"
Assert-NoMatch "texture upload expanded by packet lanes" "pixel_count\s*\*\s*static_cast\s*<\s*size_t\s*>\s*\(\s*ctx->num_spectral_channels\s*\)"
Assert-NoMatch "GpuMaterialData copied as GPU POD" "cudaMemcpy\s*\([^;]*sizeof\s*\(\s*GpuMaterialData\s*\)|sizeof\s*\(\s*GpuMaterialData\s*\)[^;]*cudaMemcpy"
Assert-NoMatch "MaterialGraph Add texture fail-loud regression" "MaterialGraph Add with texture inputs"
Assert-NoMatch "MaterialGraph Mix texture fail-loud regression" "MaterialGraph Mix with texture inputs"
Assert-NoMatch "MaterialGraph texture expression Phase M.2 regression" "texture inputs.*Phase M\.2 compiler"

$coreHostCieCopies = @(
    Join-Path $RepoRoot "libs\ure_core\include\ure\spectral\cie_data.hpp",
    Join-Path $RepoRoot "libs\ure_core\include\ure\spectral\spectral.hpp"
) | Where-Object { Test-Path $_ }
if ($coreHostCieCopies) {
    Write-Host "Phase L static audit failed: duplicate host spectral/CIE headers in ure_core"
    $coreHostCieCopies | ForEach-Object { Write-Host $_ }
    throw "duplicate host spectral/CIE headers in ure_core"
}

$coreSourceFiles = Get-ChildItem -Path (Join-Path $RepoRoot "libs\ure_core\src") -Recurse -File |
    Where-Object { $_.Extension -in @(".h", ".hpp", ".cuh", ".cpp", ".cu") }
$rawLaneModeChecks = $coreSourceFiles | Select-String -Pattern "==\s*SpectralRayModeLane"
if ($rawLaneModeChecks) {
    Write-Host "Phase L static audit failed: sampled/lane estimator split"
    $rawLaneModeChecks | ForEach-Object {
        Write-Host ("{0}:{1}: {2}" -f $_.Path, $_.LineNumber, $_.Line.Trim())
    }
    throw "sampled/lane estimator split"
}

$gpuStructs = Get-Content -Raw (Join-Path $RepoRoot "libs\ure_core\include\ure\gpu_structs.hpp")
if ($gpuStructs -notmatch "struct\s+SpectralSample") {
    throw "SpectralSample is missing"
}
if ($gpuStructs -notmatch "struct\s+(alignas\s*\([^)]*\)\s+)?SpectralPacket") {
    throw "SpectralPacket is missing"
}
if ($gpuStructs -notmatch "enum\s+class\s+SpectralTextureResourceKind") {
    throw "SpectralTextureResourceKind is missing"
}
if ($gpuStructs -match "\bspectral_values\b") {
    throw "GpuTexture legacy spectral_values field must not return"
}
if ($gpuStructs -notmatch "spectral_source_values") {
    throw "GpuTexture spectral source sample pointer is missing"
}
if ($gpuStructs -notmatch "spectral_sample_count") {
    throw "GpuTexture spectral source sample count is missing"
}
if ($gpuStructs -notmatch "SpectralRayModeSampled") {
    throw "SpectralRayModeSampled is missing"
}
if ($gpuStructs -notmatch "spectral_mode_is_sampled") {
    throw "spectral_mode_is_sampled helper is missing"
}
if ($gpuStructs -notmatch "valid_packet_lane_count") {
    throw "valid_packet_lane_count helper is missing"
}
if ($gpuStructs -notmatch "enum\s+class\s+SpectralResourceKind") {
    throw "SpectralResourceKind is missing"
}
if ($gpuStructs -notmatch "struct\s+SpectralResource") {
    throw "SpectralResource is missing"
}
if ($gpuStructs -notmatch "struct\s+HostSpectralResource") {
    throw "HostSpectralResource is missing"
}
if ($gpuStructs -notmatch "HostSpectralResource\s+albedo_resource") {
    throw "GpuMaterialData must carry host spectral resources"
}
if ($gpuStructs -notmatch "SpectralResource\s*\*\s*mat_albedo_resources") {
    throw "GpuScene must expose device spectral resource descriptors"
}
if ($gpuStructs -notmatch "enum\s+class\s+SpectralExpressionNodeKind") {
    throw "SpectralExpressionNodeKind is missing"
}
if ($gpuStructs -notmatch "struct\s+HostSpectralExpressionNode") {
    throw "HostSpectralExpressionNode is missing"
}
if ($gpuStructs -notmatch "SpectralExpressionNode\s*\*\s*material_expression_nodes") {
    throw "GpuScene must expose material expression nodes"
}

$materialHelpers = Get-Content -Raw (Join-Path $RepoRoot "libs\ure_core\include\ure\gpu_material_helpers.cuh")
if ($materialHelpers -notmatch "eval_spectral_resource") {
    throw "eval_spectral_resource is missing"
}
if ($materialHelpers -notmatch "load_mat_spectra_6x\s*\([^)]*const\s+float\s*\*\s*wavelengths") {
    throw "resource-first load_mat_spectra_6x overload is missing"
}
if ($materialHelpers -notmatch "load_mat_emission_spectrum") {
    throw "resource-first emission loader is missing"
}

$spectrumUtilsPath = Join-Path $RepoRoot "libs\ure_core\include\ure\gpu_spectrum_utils.cuh"
$spectrumUtils = Get-Content -Raw $spectrumUtilsPath
if ($spectrumUtils -notmatch "spectral_sample_to_xyz") {
    throw "mode-aware spectral_sample_to_xyz is missing"
}
if ($spectrumUtils -notmatch "SpectralRayModeSampled[\s\S]*1\.0f\s*/\s*safe_pdf") {
    throw "sampled wavelength estimator must use continuous pdf density"
}

$raygenPath = Join-Path $RepoRoot "libs\ure_core\src\path_tracer_raygen.cu"
$raygen = Get-Content -Raw $raygenPath
if ($raygen -notmatch "kSpectralLambdaMin\s*\+\s*r_lambda\s*\*\s*domain") {
    throw "sampled raygen must write continuous wavelength samples"
}
if ($raygen -notmatch "wavelength_pdfs\[ray_index\]\s*=\s*spectral_mode\s*==\s*SpectralRayModeSampled[\s\S]*\?\s*wavelength_pdf") {
    throw "sampled raygen must store proposal-aware continuous wavelength pdf density"
}

$wavefrontPath = Join-Path $RepoRoot "libs\ure_core\src\path_tracer_wavefront.cuh"
$wavefront = Get-Content -Raw $wavefrontPath
if ($wavefront -match "sampled_spectrum_to_xyz") {
    throw "wavefront must use mode-aware spectral_sample_to_xyz"
}
if ($wavefront -notmatch "eval_material_expression") {
    throw "MaterialGraph expression evaluator is missing"
}
$materialRuntimePath = Join-Path $RepoRoot "libs\ure_core\src\path_tracer_material_runtime.cuh"
$materialRuntime = Get-Content -Raw $materialRuntimePath
if ($wavefront -notmatch 'path_tracer_material_runtime\.cuh' -or
    $materialRuntime -notmatch "SpectralExpressionNodeKind::Texture") {
    throw "MaterialGraph texture expression evaluation is missing"
}

$sceneCompilerPath = Join-Path $RepoRoot "libs\ure_core\src\gpu_scene_compiler.cpp"
$sceneCompiler = Get-Content -Raw $sceneCompilerPath
if ($sceneCompiler -notmatch "GraphExpressionBuilder") {
    throw "MaterialGraph expression builder is missing"
}
if ($sceneCompiler -notmatch "albedo_expression_root") {
    throw "MaterialGraph albedo expression root is missing"
}
if ($sceneCompiler -notmatch "roughness_expression_root") {
    throw "MaterialGraph roughness expression root is missing"
}
if ($sceneCompiler -notmatch "emission_expression_root") {
    throw "MaterialGraph emission expression root is missing"
}

$oracleHeaderPath = Join-Path $RepoRoot "libs\ure_types\include\ure\spectral\spectral_oracle.hpp"
if (-not (Test-Path $oracleHeaderPath)) {
    throw "spectral_oracle.hpp is missing"
}
$oracleHeader = Get-Content -Raw $oracleHeaderPath
if ($oracleHeader -notmatch "bins\s*=\s*1'000'000") {
    throw "million-bin spectral domain default is missing"
}
if ($oracleHeader -notmatch "integrate_xyz") {
    throw "host spectral oracle integration is missing"
}
if ($oracleHeader -notmatch "d65") {
    throw "D65 oracle fixture is missing"
}
if ($oracleHeader -notmatch "make_metamer_pair") {
    throw "metamer oracle fixture is missing"
}
if ($oracleHeader -notmatch "estimate_uniform_sampled_xyz") {
    throw "host sampled estimator is missing"
}

$oracleTestPath = Join-Path $RepoRoot "tests\host\test_spectral_oracle.cpp"
if (-not (Test-Path $oracleTestPath)) {
    throw "test_spectral_oracle.cpp is missing"
}
$oracleTest = Get-Content -Raw $oracleTestPath
if ($oracleTest -notmatch "1'000'000") {
    throw "spectral oracle test must exercise a million-bin resource"
}
if ($oracleTest -notmatch "test_d65_million_bin_chromaticity") {
    throw "D65 million-bin oracle test is missing"
}

$gpuSpectralTestPath = Join-Path $RepoRoot "tests\gpu\test_spectral_pipeline.cu"
$gpuSpectralTest = Get-Content -Raw $gpuSpectralTestPath
if ($gpuSpectralTest -notmatch "test_l7_gpu_sampled_equal_energy_matches_oracle") {
    throw "L.7 equal-energy GPU oracle test is missing"
}
if ($gpuSpectralTest -notmatch "test_l7_gpu_sampled_d65_matches_oracle") {
    throw "L.7 D65 GPU oracle test is missing"
}
if ($gpuSpectralTest -notmatch "test_l7_gpu_sampled_high_res_resource_matches_oracle") {
    throw "L.7 high-res resource GPU oracle test is missing"
}

$gpuSpectralSoaTestPath = Join-Path $RepoRoot "tests\gpu\test_spectral_pipeline_soa.cu"
$gpuSpectralSoaTest = Get-Content -Raw $gpuSpectralSoaTestPath
if ($gpuSpectralSoaTest -notmatch "SpectralTextureResourceKind::SourceSampleGrid") {
    throw "L.8 spectral texture resource sampling test is missing"
}
if ($gpuSpectralSoaTest -notmatch "test_l9_material_expression_texture_add_mix_device_eval") {
    throw "L.9 material expression GPU test is missing"
}

$gpuRenderTestPath = Join-Path $RepoRoot "tests\gpu\test_render_basic.cu"
$gpuRenderTest = Get-Content -Raw $gpuRenderTestPath
if ($gpuRenderTest -notmatch "test_l8_spectral_texture_upload_keeps_source_sample_count") {
    throw "L.8 spectral texture upload source-count test is missing"
}
if ($gpuRenderTest -notmatch "test_l8_rgb_texture_upload_keeps_hardware_filtering") {
    throw "L.8 RGB hardware texture test is missing"
}

$materialGraphTestPath = Join-Path $RepoRoot "tests\host\test_material_graph.cpp"
$materialGraphTest = Get-Content -Raw $materialGraphTestPath
if ($materialGraphTest -notmatch "test_texture_add_and_mix_compile_to_expression_graph") {
    throw "L.9 MaterialGraph texture Add/Mix host test is missing"
}

$distributedContractPath = Join-Path $RepoRoot "libs\ure_core\include\ure\distributed_contract.hpp"
$distributedContract = Get-Content -Raw $distributedContractPath
if ($distributedContract -notmatch "struct\s+DistributedSpectralDomainShard") {
    throw "L.10 distributed spectral shard metadata is missing"
}
if ($distributedContract -notmatch "wavelength_pdf_integral") {
    throw "L.10 distributed wavelength pdf metadata is missing"
}
if ($distributedContract -notmatch "struct\s+DistributedFrameShard") {
    throw "L.10 distributed frame shard metadata is missing"
}
if ($distributedContract -notmatch "compatible_shard_metadata_for_merge") {
    throw "L.10 shard metadata merge compatibility check is missing"
}

$distributedContractImplPath = Join-Path $RepoRoot "libs\ure_core\src\distributed_contract.cpp"
$distributedContractImpl = Get-Content -Raw $distributedContractImplPath
if ($distributedContractImpl -notmatch "make_spectral_domain_shard") {
    throw "L.10 spectral domain shard partition implementation is missing"
}
if ($distributedContractImpl -notmatch "make_aggregate_spectral_domain") {
    throw "L.10 aggregate spectral domain metadata is missing"
}
if ($distributedContractImpl -notmatch "distributed framebuffer shard metadata must be compatible") {
    throw "L.10 merge must reject incompatible shard metadata"
}

$distributedFileIoPath = Join-Path $RepoRoot "libs\ure_core\src\distributed_file_io.cpp"
$distributedFileIo = Get-Content -Raw $distributedFileIoPath
if ($distributedFileIo -notmatch "write_shard_metadata") {
    throw "L.10 distributed file writer must persist shard metadata"
}
if ($distributedFileIo -notmatch "read_shard_metadata") {
    throw "L.10 distributed file reader must restore shard metadata"
}
if ($distributedFileIo -notmatch "constexpr\s+int\s+kVersion\s*=\s*3") {
    throw "L.10 distributed file format version must preserve current metadata schema"
}

$distributedContractTestPath = Join-Path $RepoRoot "tests\gpu\test_distributed_contract.cu"
$distributedContractTest = Get-Content -Raw $distributedContractTestPath
if ($distributedContractTest -notmatch "test_spectral_domain_shard_partition") {
    throw "L.10 spectral domain shard partition test is missing"
}
if ($distributedContractTest -notmatch "test_spectral_shard_merge_contract") {
    throw "L.10 spectral shard merge test is missing"
}
if ($distributedContractTest -notmatch "test_shard_metadata_mismatch_rejected") {
    throw "L.10 shard mismatch rejection test is missing"
}

$distributedFileIoTestPath = Join-Path $RepoRoot "tests\host\test_distributed_file_io.cpp"
$distributedFileIoTest = Get-Content -Raw $distributedFileIoTestPath
if ($distributedFileIoTest -notmatch "test_shard_metadata_file_roundtrip") {
    throw "L.10 shard metadata file roundtrip test is missing"
}
if ($distributedFileIoTest -notmatch "test_framebuffer_file_merge_rejects_bad_shard_metadata") {
    throw "L.10 file merge mismatch rejection test is missing"
}

$gpuAutoConfigPath = Join-Path $RepoRoot "libs\ure_core\include\ure\gpu_auto_config.hpp"
$gpuAutoConfig = Get-Content -Raw $gpuAutoConfigPath
if ($gpuAutoConfig -notmatch "SpectralSamplerPreset") {
    throw "L.11 sampler preset contract is missing"
}
if ($gpuAutoConfig -notmatch "SpectralCachePreset") {
    throw "L.11 cache preset contract is missing"
}
if ($gpuAutoConfig -notmatch "SpectralStreamPreset") {
    throw "L.11 CUDA stream preset contract is missing"
}
if ($gpuAutoConfig -notmatch "estimated_resident_resource_bytes") {
    throw "L.11 resident resource estimate is missing"
}
if ($gpuAutoConfig -notmatch "exceeds_resident_budget") {
    throw "L.11 resident budget reject signal is missing"
}

$hostApiPath = Join-Path $RepoRoot "libs\ure_core\src\path_tracer_host_api.cu"
$hostApi = Get-Content -Raw $hostApiPath
if ($hostApi -notmatch "validate_explicit_spectral_resident_budget") {
    throw "L.11 GPU init resident budget gate is missing"
}
if ($hostApi -notmatch "spectral resident resource budget exceeded") {
    throw "L.11 GPU init must fail loud on resident budget overflow"
}

$hardwareTestPath = Join-Path $RepoRoot "tests\gpu\test_hardware.cu"
$hardwareTest = Get-Content -Raw $hardwareTestPath
if ($hardwareTest -notmatch "test_spectral_runtime_plan_low_end_reject_signal") {
    throw "L.11 low-end reject preset test is missing"
}
if ($hardwareTest -notmatch "test_spectral_runtime_plan_high_end_and_farm_presets") {
    throw "L.11 high-end/farm preset test is missing"
}

$renderTestPath = Join-Path $RepoRoot "tests\gpu\test_render_basic.cu"
$renderTest = Get-Content -Raw $renderTestPath
if ($renderTest -notmatch "test_l11_spectral_texture_cache_budget_rejects_oversized_resident_upload") {
    throw "L.11 oversized resident texture rejection test is missing"
}

$renderConfigPath = Join-Path $RepoRoot "libs\ure_types\include\ure\render_config.hpp"
$renderConfig = Get-Content -Raw $renderConfigPath
if ($renderConfig -notmatch "Legacy alias for spectral_packet_lanes") {
    throw "L.12 num_wavelengths must remain documented as a packet-lane legacy alias"
}
if ($renderConfig -notmatch "spectral_domain_bins") {
    throw "L.12 RenderConfig must expose spectral_domain_bins separately"
}
if ($renderConfig -notmatch "spectral_packet_lanes") {
    throw "L.12 RenderConfig must expose spectral_packet_lanes separately"
}

Assert-NoMatch "domain bins assigned to packet lanes" "spectral_packet_lanes\s*=\s*[^;\r\n]*spectral_domain_bins"
Assert-NoMatch "domain bins assigned to legacy num_wavelengths" "num_wavelengths\s*=\s*[^;\r\n]*spectral_domain_bins"
Assert-NoMatch "million-bin GPU resource arrays" "GpuSpectrum\s*\[|GpuSpectrum\s*\*"

if ($hostApi -match "cudaMalloc\s*\([^;]*spectral_domain_bins") {
    throw "L.12 GPU init must not allocate arrays sized by spectral_domain_bins"
}
if ($hostApi -match "num_channels\s*=\s*[^;\r\n]*spectral_domain_bins") {
    throw "L.12 GPU packet/channel count must not come from spectral_domain_bins"
}
if ($gpuAutoConfig -notmatch "plan\.domain_bins\s*=\s*spectral_domain_bins\(cfg\)") {
    throw "L.12 runtime plan must track domain_bins explicitly"
}
if ($gpuAutoConfig -notmatch "plan\.packet_lanes") {
    throw "L.12 runtime plan must track packet_lanes explicitly"
}
if ($gpuAutoConfig -notmatch "estimate_resident_spectral_resource_bytes") {
    throw "L.12 resident resource estimate helper is missing"
}

$benchmarkScenePath = Join-Path $RepoRoot "scenes\benchmarks\phase_l_spectral_budget.gltf"
if (-not (Test-Path $benchmarkScenePath)) {
    throw "L.12 Phase L benchmark glTF scene is missing"
}
$benchmarkScene = Get-Content -Raw $benchmarkScenePath
if ($benchmarkScene -notmatch "phase_l_emissive_quad") {
    throw "L.12 Phase L benchmark scene must identify the benchmark mesh"
}

$benchmarkScriptPath = Join-Path $RepoRoot "tools\benchmarks\run_phase_l_spectral_smoke.ps1"
if (-not (Test-Path $benchmarkScriptPath)) {
    throw "L.12 Phase L benchmark smoke script is missing"
}
$benchmarkScript = Get-Content -Raw $benchmarkScriptPath
if ($benchmarkScript -notmatch "spectral-domain-bins" -or $benchmarkScript -notmatch "1000000") {
    throw "L.12 benchmark smoke must exercise million-domain CLI config"
}
if ($benchmarkScript -notmatch "spectral-max-resident-mb") {
    throw "L.12 benchmark smoke must pass resident cache budget"
}

$planPath = Join-Path $RepoRoot "PLAN.md"
$plan = Get-Content -Raw $planPath
if ($plan -notmatch "已完成 \(L\.0-L\.12\)") {
    throw "L.12 PLAN overview must mark Phase L complete through L.12"
}

$completionAuditPath = Join-Path $RepoRoot "docs\Phase_L_Completion_Audit.md"
if (-not (Test-Path $completionAuditPath)) {
    throw "L.12 completion audit document is missing"
}
$completionAudit = Get-Content -Raw $completionAuditPath
if ($completionAudit -notmatch "Requirements And Evidence" -or
    $completionAudit -notmatch "Final Gate") {
    throw "L.12 completion audit must record requirements and final gate"
}

Write-Host "Phase L static audit passed."
