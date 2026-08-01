#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <vector>

#include <cuda_runtime.h>

#include "support_measure_device.cuh"
#include "test_framework.cuh"
#include "ure/runtime/multi_backend.hpp"

namespace tr = ure::transport;

struct GpuCompositionResult {
    double expectation = 0.0;
    double weights[4] = {};
    std::uint32_t valid = 0;
};

static ure::semantic::IdentityDigest id(std::string_view value) {
    return ure::runtime::identity_digest(value);
}

static tr::PathEventGrammar grammar(std::uint64_t scatter_mask) {
    tr::PathEventGrammar result;
    result.maximum_path_events = 3;
    result.alternatives.push_back({{
        {tr::path_event_mask(tr::PathEvent::Camera), 1, 1},
        {scatter_mask, 1, 1},
        {tr::path_event_mask(tr::PathEvent::Emitter), 1, 1}}});
    tr::finalize_path_event_grammar(result);
    return result;
}

static tr::TechniqueDescriptor technique(
    std::string_view identity,
    tr::TechniqueFamily family) {
    tr::TechniqueDescriptor result;
    result.family = family;
    result.technique_identity = id(identity);
    result.sample_space_identity = id("gpu.support.samples");
    result.parameter_identity = id("gpu.support.parameters");
    result.resources.scaling = tr::TechniqueResourceScaling::Pixel;
    result.resources.cost_estimate_known = true;
    result.resources.nanoseconds_per_sample = 1;
    result.resources.scratch_bound_known = true;
    result.resources.scratch_bytes_per_work_item = 1;
    result.resources.persistent_budget_bytes = 1;
    result.resources.max_samples_per_pass = 1;
    result.resources.backend_capability_identity = id("gpu.support.cuda");
    auto& estimator = result.estimator;
    estimator.technique_identity = result.technique_identity;
    estimator.observable.kind = tr::ObservableKind::SpectralRadiance;
    estimator.observable.value_domain = tr::ValueDomain::Spectrum;
    estimator.observable.coherence = tr::CoherenceClass::Incoherent;
    estimator.observable.component_count = 1;
    estimator.observable.unit.dimension.length = -1;
    estimator.observable.unit.dimension.mass = 1;
    estimator.observable.unit.dimension.time = -3;
    estimator.measure.integral_identity = id("gpu.support.integral");
    estimator.measure.coordinate_identity = id("gpu.support.path");
    estimator.measure.term_count = 1;
    estimator.measure.terms[0] = {tr::MeasureDomain::Path, 1};
    estimator.support.event_mask =
        tr::path_event_mask(tr::PathEvent::Camera) |
        tr::path_event_mask(tr::PathEvent::Emitter) |
        tr::path_event_mask(tr::PathEvent::Diffuse) |
        tr::path_event_mask(tr::PathEvent::Glossy);
    estimator.support.max_depth = 3;
    estimator.support.overlap_known = true;
    estimator.density = tr::DensityKind::ExplicitPdf;
    estimator.normalization =
        tr::NormalizationKind::MultipleImportanceSampling;
    estimator.correlation = tr::CorrelationModel::Independent;
    estimator.bias = tr::BiasClass::Unbiased;
    return result;
}

static tr::CompiledCompositionPlan plan() {
    tr::TechniqueGraph graph;
    graph.nodes.push_back({0, technique(
        "gpu.support.diffuse",
        tr::TechniqueFamily::WavefrontPathTracing)});
    graph.nodes.push_back({1, technique(
        "gpu.support.general",
        tr::TechniqueFamily::BidirectionalPathTracing)});
    tr::finalize_technique_graph(graph);
    const auto diffuse = tr::path_event_mask(tr::PathEvent::Diffuse);
    const auto general = diffuse |
        tr::path_event_mask(tr::PathEvent::Glossy);
    const std::vector support_bindings{
        tr::TechniqueSupportBinding{0, grammar(diffuse)},
        tr::TechniqueSupportBinding{1, grammar(general)}};
    const auto support = tr::compile_support_partition_graph(
        graph, grammar(general), support_bindings);
    tr::MisFamilyDescriptor mis;
    mis.family_identity = id("gpu.support.balance");
    const auto& measure = graph.nodes[0].descriptor.estimator.measure;
    tr::MeasureTransformDescriptor first;
    first.transform_identity = id("gpu.support.transform-a");
    first.source_coordinate_identity = measure.coordinate_identity;
    first.target_coordinate_identity = measure.coordinate_identity;
    auto second = first;
    second.transform_identity = id("gpu.support.transform-b");
    const std::vector composition_bindings{
        tr::TechniqueCompositionBinding{0, first},
        tr::TechniqueCompositionBinding{1, second}};
    return tr::compile_composition_plan(
        graph, support, measure, mis,
        tr::EstimateLayer::Unbiased, composition_bindings);
}

__global__ void composition_kernel(
    tr::PackedMisProgram program,
    GpuCompositionResult* result) {
    const double functions[2] = {2.0, 5.0};
    const double proposal_a[2] = {0.75, 0.25};
    const double proposal_b[2] = {0.25, 0.75};
    const std::uint64_t samples[2] = {1, 1};
    const double jacobians[2] = {1.0, 1.0};
    double expectation = 0.0;
    for (std::uint32_t point = 0; point < 2; ++point) {
        const double densities[2] = {
            proposal_a[point], proposal_b[point]};
        double weights[2] = {};
        if (!ure::gpu::detail::evaluate_packed_mis_weights(
                program, densities, samples, jacobians, 3, weights)) {
            result->valid = 0;
            return;
        }
        result->weights[point * 2] = weights[0];
        result->weights[point * 2 + 1] = weights[1];
        expectation += proposal_a[point] *
                           functions[point] / proposal_a[point] *
                           weights[0] +
                       proposal_b[point] *
                           functions[point] / proposal_b[point] *
                           weights[1];
    }
    result->expectation = expectation;
    result->valid = 1;
}

static int test_gpu_composition_e2e() {
    REQUIRE_GPU();
    const auto compiled = plan();
    CHECK(compiled.executable());
    const auto group = std::ranges::find_if(
        compiled.groups,
        [](const tr::CompositionGroup& value) {
            return value.technique_mask == 3;
        });
    CHECK(group != compiled.groups.end());
    const auto program = tr::pack_mis_program(compiled, *group);
    GpuCompositionResult* device_result = nullptr;
    CHECK_CUDA(cudaMalloc(&device_result, sizeof(GpuCompositionResult)));
    DeviceMem guard(device_result);
    CHECK_CUDA(cudaMemset(device_result, 0, sizeof(GpuCompositionResult)));
    composition_kernel<<<1, 1>>>(program, device_result);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());
    GpuCompositionResult host;
    CHECK_CUDA(cudaMemcpy(&host, device_result, sizeof(host),
                          cudaMemcpyDeviceToHost));
    CHECK(host.valid == 1);
    CHECK(std::fabs(host.expectation - 7.0) < 1e-12);
    CHECK(std::fabs(host.weights[0] - 0.75) < 1e-12);
    CHECK(std::fabs(host.weights[1] - 0.25) < 1e-12);
    CHECK(std::fabs(host.weights[2] - 0.25) < 1e-12);
    CHECK(std::fabs(host.weights[3] - 0.75) < 1e-12);
    return 0;
}

int main() {
    RUN_TEST(test_gpu_composition_e2e);
    std::printf("Tests passed: %d, failed: %d\n",
                g_tests_passed, g_tests_failed);
    return g_test_result;
}
