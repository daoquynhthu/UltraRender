#include "ure/runtime/execution_graph.hpp"
#include "ure/runtime/runtime.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

namespace rt = ure::runtime;

static int failures = 0;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", \
                         __FILE__, __LINE__, #condition); \
            ++failures; \
        } \
    } while (false)

template <typename Fn>
static bool throws_code(Fn&& fn, rt::ErrorCode code) {
    try {
        fn();
    } catch (const rt::Error& error) {
        return error.code() == code;
    }
    return false;
}

static std::vector<rt::StageKind> ordered_stages(
    const rt::ExecutionGraph& graph) {
    std::vector<rt::StageKind> stages;
    for (const auto id : graph.estimator.ordered_nodes) {
        const auto& node = graph.nodes.at(
            static_cast<std::size_t>(id - 1));
        stages.push_back(std::get<rt::DispatchStage>(node.command).stage);
    }
    return stages;
}

static rt::PathExecutionConfig base_path_config() {
    rt::PathExecutionConfig config;
    config.width = 16;
    config.height = 16;
    config.primary_ray_count = 256;
    config.queue_capacity = 256;
    config.samples_per_pass = 2;
    config.render.max_trace_depth = 8;
    config.render.rays_per_block = 64;
    config.pass_epoch = 7;
    config.guiding_epoch = 3;
    config.restir_di_epoch = 5;
    config.restir_pt_epoch = 6;
    config.bidirectional_epoch = 8;
    config.mlt_epoch = 9;
    return config;
}

static void test_wavefront_graph_is_stable() {
    auto config = base_path_config();
    config.render.restir_di.unbiased = true;
    const auto first = rt::make_path_execution_graph(config);
    const auto second = rt::make_path_execution_graph(config);
    CHECK(first == second);
    CHECK(rt::execution_fingerprint(first) ==
          rt::execution_fingerprint(second));
    CHECK(first.kind == rt::ExecutionKind::PathTracing);
    CHECK(first.regions.size() == 4);
    CHECK(first.regions[3].repeat ==
          rt::RepeatKind::UntilQueueEmpty);
    CHECK(first.regions[3].maximum_iterations == 8);
    CHECK(first.regions[3].termination_queue == std::uint32_t{1});

    bool found_iteration_count = false;
    bool found_shadow_count = false;
    bool found_swap = false;
    bool found_2d_raygen = false;
    for (const auto& node : first.nodes) {
        if (const auto* stage =
                std::get_if<rt::DispatchStage>(&node.command)) {
            if (stage->stage == rt::StageKind::RayGenerate) {
                const auto& work =
                    std::get<rt::DirectWork>(stage->work);
                found_2d_raygen =
                    work.item_extent ==
                        std::array<std::uint64_t, 3>{16, 16, 1} &&
                    work.group_size ==
                        std::array<std::uint32_t, 3>{16, 16, 1};
            }
            if (stage->stage == rt::StageKind::Intersect) {
                const auto& work =
                    std::get<rt::IndirectQueueWork>(stage->work);
                found_iteration_count =
                    work.relation ==
                        rt::CountRelation::PreviousIteration &&
                    work.queue_id == 1 &&
                    static_cast<bool>(work.arguments);
            }
            if (stage->stage == rt::StageKind::ShadowIntersect) {
                const auto& work =
                    std::get<rt::IndirectQueueWork>(stage->work);
                found_shadow_count =
                    work.relation ==
                        rt::CountRelation::SameIteration &&
                    work.queue_id == 4;
            }
        } else if (const auto* queue =
                       std::get_if<rt::QueueStage>(&node.command)) {
            found_swap =
                found_swap ||
                (queue->operation == rt::QueueOperation::Swap &&
                 queue->queue_id == 1 &&
                 queue->paired_queue_id == std::uint32_t{2});
        }
    }
    CHECK(found_iteration_count);
    CHECK(found_shadow_count);
    CHECK(found_swap);
    CHECK(found_2d_raygen);
    const auto stages = ordered_stages(first);
    CHECK(std::ranges::find(
              stages,
              rt::StageKind::RestirDiResample) ==
          stages.end());
}

static void test_advanced_estimator_order_is_frozen() {
    auto config = base_path_config();
    config.samples_per_pass = 1;
    config.path_guiding_decay_due = true;
    config.path_guiding_light_count = 8;
    config.path_guiding_spatial_entry_count = 1024;
    config.render.integrator.mode = ure::IntegratorMode::RestirPT;
    config.render.path_guiding.enabled = true;
    config.render.restir_di.enabled = true;
    config.render.restir_di.unbiased = true;
    config.render.restir_pt.enabled = true;
    config.render.restir_pt.candidate_count = 3;
    config.render.restir_pt.max_reuse_depth = 4;
    config.render.bidirectional.enabled = true;
    config.render.vcm.enabled = true;
    config.render.specular_manifold.enabled = true;

    const auto graph = rt::make_path_execution_graph(config);
    const std::vector expected = {
        rt::StageKind::PathGuidingLightDecay,
        rt::StageKind::PathGuidingSpatialDecay,
        rt::StageKind::LightSubpathGenerate,
        rt::StageKind::LightSubpathExtend,
        rt::StageKind::VcmSurfaceGridBuild,
        rt::StageKind::VcmVolumeGridBuild,
        rt::StageKind::RayGenerate,
        rt::StageKind::Intersect,
        rt::StageKind::RestirPtCandidatePrepare,
        rt::StageKind::RestirDiResample,
        rt::StageKind::Shade,
        rt::StageKind::ShadowIntersect,
        rt::StageKind::RestirPtCandidateStream,
        rt::StageKind::RestirPtFinalize,
        rt::StageKind::BidirectionalConnect,
        rt::StageKind::VcmSurfaceMerge,
        rt::StageKind::VcmVolumeMerge,
        rt::StageKind::ManifoldTargetGenerate,
        rt::StageKind::ManifoldRootInitialize,
        rt::StageKind::ManifoldRootAdvance,
        rt::StageKind::ManifoldWeightAssign,
        rt::StageKind::ManifoldContributionEvaluate,
        rt::StageKind::ManifoldContributionConvert,
        rt::StageKind::TechniqueContributionCommit};
    CHECK(ordered_stages(graph) == expected);
    CHECK(graph.regions[2].repeat_count == 3);
    CHECK(graph.regions[3].maximum_iterations == 5);
    CHECK(graph.regions[3].initial_count_producer.has_value());
    CHECK(graph.regions[3].iteration_count_producer.has_value());
    CHECK(graph.regions[4].kind ==
          rt::RegionKind::ManifoldRootLoop);
    CHECK(graph.regions[4].initial_count_producer.has_value());
    CHECK(graph.regions[4].iteration_count_producer.has_value());
    const auto& root_node = graph.nodes.at(
        *graph.regions[4].iteration_count_producer - 1);
    CHECK(std::holds_alternative<rt::DirectWork>(
        std::get<rt::DispatchStage>(root_node.command).work));
    bool found_di_swap = false;
    bool found_pt_swap = false;
    bool found_sample_advance = false;
    int reservoir_clears = 0;
    for (const auto& node : graph.nodes) {
        if (std::holds_alternative<rt::ClearStage>(
                node.command)) {
            ++reservoir_clears;
        }
        const auto* state =
            std::get_if<rt::StateStage>(&node.command);
        if (!state) continue;
        found_di_swap =
            found_di_swap ||
            state->operation ==
                rt::StateOperation::RestirDIReservoirSwap;
        found_pt_swap =
            found_pt_swap ||
            state->operation ==
                rt::StateOperation::RestirPTReservoirSwap;
        found_sample_advance =
            found_sample_advance ||
            state->operation ==
                rt::StateOperation::SampleCountAdvance;
    }
    CHECK(found_di_swap);
    CHECK(found_pt_swap);
    CHECK(found_sample_advance);
    CHECK(reservoir_clears == 2);

    auto changed_epoch = config;
    ++changed_epoch.restir_pt_epoch;
    const auto changed = rt::make_path_execution_graph(changed_epoch);
    CHECK(changed != graph);
    CHECK(rt::execution_fingerprint(changed) !=
          rt::execution_fingerprint(graph));
    auto changed_input = config;
    changed_input.restir_pt_input_index = 1;
    CHECK(rt::execution_fingerprint(
              rt::make_path_execution_graph(changed_input)) !=
          rt::execution_fingerprint(graph));
}

static void test_mlt_state_graphs_are_explicit() {
    auto config = base_path_config();
    config.render.integrator.mode = ure::IntegratorMode::MLT;
    config.render.mlt.enabled = true;
    config.render.mlt.chain_count = 8;
    config.render.mlt.bootstrap_samples = 1024;
    config.render.mlt.burn_in_mutations = 4;
    config.render.mlt.mutations_per_chain = 16;
    config.mlt_primary_dimension_count = 32;

    const auto initial = rt::make_path_execution_graph(config);
    CHECK(initial.kind == rt::ExecutionKind::Metropolis);
    const auto initial_stages = ordered_stages(initial);
    CHECK(std::ranges::find(
              initial_stages,
              rt::StageKind::MltPrimaryInitialize) !=
          initial_stages.end());
    CHECK(std::ranges::find(
              initial_stages,
              rt::StageKind::MltBootstrapCollect) !=
          initial_stages.end());
    const auto bootstrap_region = std::ranges::find_if(
        initial.regions,
        [](const rt::ExecutionRegion& region) {
            return region.kind == rt::RegionKind::BootstrapLoop;
        });
    CHECK(bootstrap_region != initial.regions.end());
    if (bootstrap_region != initial.regions.end()) {
        CHECK(bootstrap_region->repeat_count == 4);
    }
    bool found_chunked_bootstrap = false;
    bool found_bootstrap_host_stage = false;
    for (const auto& node : initial.nodes) {
        if (const auto* host =
                std::get_if<rt::HostStage>(&node.command)) {
            found_bootstrap_host_stage =
                host->operation ==
                rt::HostOperation::MltBootstrapNormalizeCdf;
        }
        const auto* stage =
            std::get_if<rt::DispatchStage>(&node.command);
        if (!stage ||
            stage->stage != rt::StageKind::MltRayGenerate) {
            continue;
        }
        found_chunked_bootstrap =
            found_chunked_bootstrap ||
            std::holds_alternative<rt::ChunkedWork>(stage->work);
    }
    CHECK(found_chunked_bootstrap);
    CHECK(found_bootstrap_host_stage);

    config.mlt_initialized = true;
    const auto warm = rt::make_path_execution_graph(config);
    const auto warm_stages = ordered_stages(warm);
    CHECK(std::ranges::find(
              warm_stages,
              rt::StageKind::MltPrimaryInitialize) ==
          warm_stages.end());
    CHECK(std::ranges::find(
              warm_stages,
              rt::StageKind::MltMutate) !=
          warm_stages.end());
    CHECK(rt::execution_fingerprint(initial) !=
          rt::execution_fingerprint(warm));
}

static void test_wave_graph_has_transfer_barriers() {
    rt::WaveExecutionConfig config;
    config.sample_count = 64;
    config.input_bytes = 1024;
    config.output_bytes = 1024;
    config.group_size = 32;
    config.pass_epoch = 9;
    const auto graph = rt::make_wave_execution_graph(config);
    CHECK(graph.kind == rt::ExecutionKind::WaveOperator);
    CHECK(graph.nodes.size() == 9);
    CHECK(std::holds_alternative<rt::AsyncTransferStage>(
        graph.nodes[2].command));
    CHECK(std::holds_alternative<rt::BarrierStage>(
        graph.nodes[3].command));
    CHECK(std::get<rt::DispatchStage>(
              graph.nodes[4].command).stage ==
          rt::StageKind::WavePropagate);
    CHECK(std::holds_alternative<rt::BarrierStage>(
        graph.nodes[5].command));
    CHECK(std::get<rt::AsyncTransferStage>(
              graph.nodes[6].command).kind ==
          rt::TransferKind::Readback);
}

static void test_validation_rejects_semantic_changes() {
    auto graph = rt::make_path_execution_graph(base_path_config());
    std::swap(
        graph.estimator.ordered_nodes[0],
        graph.estimator.ordered_nodes[1]);
    CHECK(throws_code(
        [&] { rt::validate(graph); },
        rt::ErrorCode::InvalidArgument));

    graph = rt::make_path_execution_graph(base_path_config());
    graph.nodes.back().dependencies = {graph.nodes.back().id};
    CHECK(throws_code(
        [&] { rt::validate(graph); },
        rt::ErrorCode::InvalidArgument));

    graph = rt::make_path_execution_graph(base_path_config());
    graph.estimator.pdf.restir_target = 0;
    CHECK(throws_code(
        [&] { rt::validate(graph); },
        rt::ErrorCode::InvalidArgument));

    graph = rt::make_path_execution_graph(base_path_config());
    graph.estimator.pdf.mlt_primary_sampling = 0;
    CHECK(throws_code(
        [&] { rt::validate(graph); },
        rt::ErrorCode::InvalidArgument));

    graph = rt::make_path_execution_graph(base_path_config());
    graph.regions[3].iteration_count_producer.reset();
    CHECK(throws_code(
        [&] { rt::validate(graph); },
        rt::ErrorCode::InvalidArgument));

    graph = rt::make_path_execution_graph(base_path_config());
    for (auto& node : graph.nodes) {
        auto* state = std::get_if<rt::StateStage>(&node.command);
        if (!state) continue;
        state->increment = 0;
        break;
    }
    CHECK(throws_code(
        [&] { rt::validate(graph); },
        rt::ErrorCode::InvalidArgument));

    graph = rt::make_path_execution_graph(base_path_config());
    for (auto& node : graph.nodes) {
        auto* stage = std::get_if<rt::DispatchStage>(&node.command);
        if (!stage || stage->stage != rt::StageKind::Intersect) continue;
        std::get<rt::IndirectQueueWork>(stage->work).arguments =
            rt::semantic_resource(9999);
        break;
    }
    CHECK(throws_code(
        [&] { rt::validate(graph); },
        rt::ErrorCode::InvalidArgument));

    rt::WaveExecutionConfig wave;
    wave.sample_count = 16;
    wave.input_bytes = 128;
    wave.output_bytes = 128;
    auto wave_graph = rt::make_wave_execution_graph(wave);
    wave_graph.nodes[3].dependencies.clear();
    CHECK(throws_code(
        [&] { rt::validate(wave_graph); },
        rt::ErrorCode::InvalidArgument));

    rt::WaveExecutionConfig overflow;
    overflow.sample_count =
        static_cast<std::uint64_t>(
            std::numeric_limits<std::uint32_t>::max()) +
        1;
    overflow.input_bytes = 1;
    overflow.output_bytes = 1;
    overflow.group_size = 1;
    CHECK(throws_code(
        [&] { static_cast<void>(
            rt::make_wave_execution_graph(overflow)); },
        rt::ErrorCode::Overflow));

    auto state_overflow = base_path_config();
    state_overflow.pass_epoch =
        std::numeric_limits<std::uint64_t>::max();
    CHECK(throws_code(
        [&] { static_cast<void>(
            rt::make_path_execution_graph(state_overflow)); },
        rt::ErrorCode::Overflow));
}

int main() {
    test_wavefront_graph_is_stable();
    test_advanced_estimator_order_is_frozen();
    test_mlt_state_graphs_are_explicit();
    test_wave_graph_has_transfer_barriers();
    test_validation_rejects_semantic_changes();
    if (failures == 0) {
        std::printf("Execution graph contract tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
