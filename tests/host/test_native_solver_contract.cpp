#include <cstdlib>
#include <iostream>

#include <ure/native_solver_contract.hpp>
#include <ure/native_scene_ir.hpp>

namespace {
int failures = 0;
void check(bool condition, const char* message) { if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; } }

ure::native_scene::SolverCapabilityRegistry capabilities() {
    using namespace ure;
    using namespace ure::native_scene;
    SolverCapabilityRegistry result;
    result.integrators = {NativeIntegratorMode::Wavefront, NativeIntegratorMode::PathGuided, NativeIntegratorMode::RestirDI, NativeIntegratorMode::SpecularManifold, NativeIntegratorMode::MLT, NativeIntegratorMode::RestirPT};
    result.samplers = {IntegratorSampler::Default, IntegratorSampler::LowDiscrepancy, IntegratorSampler::PrimarySampleSpace};
    result.wave_modes = {WaveOpticsMode::Radiometric, WaveOpticsMode::CameraDiffraction};
    result.backends = {ExecutionBackend::Cuda}; result.acceleration_providers = {AccelerationProvider::SoftwareBvh};
    result.coherent_merge_modes = {CoherentMergeMode::None}; result.validation_metrics.emplace("energy.relative_error", Version{1, 0});
    return result;
}

ure::native_scene::NativeSolverContract contract() {
    using namespace ure;
    using namespace ure::native_scene;
    NativeSolverContract value; value.id = "solver/final"; value.schema_version = {1, 0};
    value.spectral_domain_bins = 1'000'000; value.spectral_packet_lanes = 16; value.spectral_resident_mb = 1024;
    value.spectral_sampling = SpectralSamplingMode::Importance; value.max_trace_depth = 64;
    value.validation.push_back({"energy.relative_error", 0.005});
    value.hints = {128, 256, 4, 2};
    return value;
}
}

int main() {
    using namespace ure;
    using namespace ure::native_scene;
    auto registry = capabilities(); auto value = contract();
    const auto compiled = compile_solver_contract(value, registry);
    check(compiled.ok(), "supported solver contract did not compile");
    check(compiled.config && compiled.config->spectral_domain_bins == 1'000'000 && compiled.config->spectral_packet_lanes == 16, "spectral mapping changed");
    check(compiled.config && compiled.config->wg_size == 128 && compiled.config->num_gpus_to_use == 2, "execution hints did not map");
    const auto binary = write_solver_contract_binary(value); const auto loaded = read_solver_contract_binary(binary);
    check(loaded.ok() && loaded.value->spectral_domain_bins == value.spectral_domain_bins, "URSC binary roundtrip failed");
    auto hint_variant = value; hint_variant.hints = {32, 64, 1, 1};
    check(solver_contract_semantic_hash(hint_variant) == solver_contract_semantic_hash(value), "execution hints changed solver semantic identity");
    const auto text = write_solver_contract_text(value); const auto text_loaded = read_solver_contract_text(text);
    check(text_loaded.ok() && solver_contract_semantic_hash(*text_loaded.value) == solver_contract_semantic_hash(value), "solver text roundtrip failed");
    scene_ir::SceneIR scene; SceneDocument document; document.id = "scene/q7"; document.schema_version = {1, 0}; document.features.push_back({"ure.render.solver", {1, 0}, RequirementLevel::Required, "ure", {}, "{}"}); auto archive = make_native_scene_archive(std::move(document), scene); archive.solver_contract = std::make_shared<const NativeSolverContract>(value); CapabilityRegistry native_registry; native_registry.features.emplace("ure.render.solver", Version{1, 0});
    const auto scene_binary = write_scene_ir_binary(archive); const auto scene_loaded = read_scene_ir_binary(scene_binary, native_registry); check(scene_loaded.ok() && scene_loaded.value->solver_contract, "binary scene lost solver contract");
    const auto exploded = write_scene_ir_text(archive); const auto exploded_loaded = read_scene_ir_text(exploded, native_registry); check(exploded_loaded.ok() && exploded_loaded.value->solver_contract, "text scene lost solver contract");
    auto guided = value; guided.integrator = NativeIntegratorMode::PathGuided; guided.path_guiding.enabled = true;
    check(compile_solver_contract(guided, registry).ok(), "path-guided mapping failed");
    auto restir = value; restir.integrator = NativeIntegratorMode::RestirDI; restir.restir_di.enabled = true;
    check(!compile_solver_contract(restir, registry).ok(), "biased ReSTIR lacked explicit consent"); restir.allow_biased_reuse = true;
    check(compile_solver_contract(restir, registry).ok(), "explicit biased ReSTIR was rejected");
    auto restir_pt = value; restir_pt.integrator = NativeIntegratorMode::RestirPT; restir_pt.restir_pt.enabled = true;
    restir_pt.restir_pt.spatial_reuse = true; restir_pt.restir_pt.candidate_count = 7; restir_pt.restir_pt.max_reuse_depth = 5;
    const auto restir_pt_compiled = compile_solver_contract(restir_pt, registry);
    check(restir_pt_compiled.ok() && restir_pt_compiled.config &&
          restir_pt_compiled.config->integrator.mode == IntegratorMode::RestirPT &&
          restir_pt_compiled.config->restir_pt.candidate_count == 7,
          "ReSTIR PT runtime mapping failed");
    const auto restir_pt_binary = read_solver_contract_binary(write_solver_contract_binary(restir_pt));
    check(restir_pt_binary.ok() && restir_pt_binary.value->restir_pt.enabled &&
          restir_pt_binary.value->restir_pt.spatial_reuse &&
          restir_pt_binary.value->restir_pt.max_reuse_depth == 5,
          "ReSTIR PT binary roundtrip failed");
    auto mlt = value; mlt.integrator = NativeIntegratorMode::MLT; mlt.mlt.enabled = true; mlt.sampler = IntegratorSampler::PrimarySampleSpace;
    check(compile_solver_contract(mlt, registry).ok(), "MLT mapping failed");
    auto bdpt = value; bdpt.integrator = NativeIntegratorMode::BDPT;
    check(!compile_solver_contract(bdpt, registry).ok(), "unsupported BDPT silently degraded");
    auto backend = value; backend.backend = ExecutionBackend::Vulkan;
    check(!compile_solver_contract(backend, registry).ok(), "unsupported backend silently degraded");
    auto coherent = value; coherent.wave_optics.mode = WaveOpticsMode::CoherentField; coherent.wave_optics.coherent_field_enabled = true; coherent.coherent_merge = CoherentMergeMode::ComplexAmplitude;
    check(!compile_solver_contract(coherent, registry).ok(), "unsupported coherent mode silently degraded");
    auto bad_metric = value; bad_metric.validation[0].metric = "unknown.metric";
    check(!compile_solver_contract(bad_metric, registry).ok(), "unsupported validation metric accepted");
    std::cout << "Phase Q.7 solver contract checks: " << (failures ? "FAILED" : "PASSED") << '\n';
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
