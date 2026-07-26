#include "ure/runtime/execution_graph.hpp"
#include "ure/runtime/runtime.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace ure::runtime {
namespace {

constexpr std::uint64_t kSemanticNamespace = 0x5552455f45584543ull;
constexpr std::uint64_t kScene = 100;
constexpr std::uint64_t kFilm = 101;
constexpr std::uint64_t kTelemetry = 102;
constexpr std::uint64_t kGuiding = 103;
constexpr std::uint64_t kRestirDI = 104;
constexpr std::uint64_t kRestirPT = 105;
constexpr std::uint64_t kLightPaths = 106;
constexpr std::uint64_t kVcm = 107;
constexpr std::uint64_t kManifold = 108;
constexpr std::uint64_t kMlt = 109;
constexpr std::uint64_t kWaveHostInput = 110;
constexpr std::uint64_t kWaveDeviceInput = 111;
constexpr std::uint64_t kWaveDeviceOutput = 112;
constexpr std::uint64_t kWaveHostOutput = 113;
constexpr std::uint64_t kHostTelemetry = 114;
constexpr std::uint64_t kHostStaging = 115;
constexpr std::uint64_t kRestirDIOutput = 116;
constexpr std::uint64_t kRestirPTOutput = 117;

class GraphBuilder {
public:
    explicit GraphBuilder(ExecutionGraph graph) : graph_(std::move(graph)) {
        if (!graph_.nodes.empty()) {
            last_id_ = graph_.nodes.back().id;
            next_id_ = last_id_ + 1;
        }
    }

    std::uint32_t add(
        std::uint32_t region_id,
        ExecutionCommand command,
        bool chain = true) {
        ExecutionNode node;
        node.id = next_id_++;
        node.region_id = region_id;
        if (chain && last_id_ != 0) node.dependencies.push_back(last_id_);
        if (const auto* dispatch = std::get_if<DispatchStage>(&command);
            dispatch && dispatch->estimator_critical) {
            graph_.estimator.ordered_nodes.push_back(node.id);
        }
        node.command = std::move(command);
        graph_.nodes.push_back(std::move(node));
        last_id_ = graph_.nodes.back().id;
        return last_id_;
    }

    ExecutionNode& node(std::uint32_t id) {
        return graph_.nodes.at(static_cast<std::size_t>(id - 1));
    }

    ExecutionRegion& region(std::uint32_t id) {
        const auto found = std::ranges::find_if(
            graph_.regions,
            [id](const ExecutionRegion& value) {
                return value.id == id;
            });
        if (found == graph_.regions.end()) {
            throw Error(
                ErrorCode::InvalidArgument,
                "execution region is missing");
        }
        return *found;
    }

    ExecutionGraph finish() {
        return std::move(graph_);
    }

private:
    ExecutionGraph graph_;
    std::uint32_t next_id_ = 1;
    std::uint32_t last_id_ = 0;
};

ResourceAccess access(std::uint64_t id, AccessMode mode) {
    return {semantic_resource(id), mode};
}

DirectWork direct(std::uint64_t count, std::uint32_t group_size) {
    return {{count, 1, 1}, {group_size, 1, 1}};
}

DirectWork direct_2d(
    std::uint64_t width,
    std::uint64_t height,
    std::uint32_t group_width,
    std::uint32_t group_height) {
    return {{width, height, 1}, {group_width, group_height, 1}};
}

ChunkedWork chunked(
    std::uint32_t region_id,
    std::uint64_t total_item_count,
    std::uint64_t chunk_size,
    std::uint32_t group_size) {
    return {
        region_id,
        total_item_count,
        chunk_size,
        group_size};
}

DispatchStage dispatch(
    StageKind stage,
    WorkSource work,
    std::vector<ResourceAccess> resources,
    bool estimator_critical = true) {
    return {
        stage,
        std::move(work),
        std::move(resources),
        estimator_critical};
}

BoundaryStage boundary(
    EpochDomain domain,
    BoundaryAction action,
    std::uint64_t epoch) {
    return {domain, action, epoch};
}

QueueContract queue(
    std::uint32_t id,
    QueueRole role,
    std::uint64_t base) {
    return {
        id,
        role,
        semantic_resource(base),
        semantic_resource(base + 1),
        semantic_resource(base + 2)};
}

const QueueContract& require_queue(
    const std::vector<QueueContract>& queues,
    std::uint32_t id) {
    const auto found = std::ranges::find_if(
        queues,
        [id](const QueueContract& value) { return value.id == id; });
    if (found == queues.end()) {
        throw Error(ErrorCode::InvalidArgument, "execution queue is missing");
    }
    return *found;
}

bool depends_on(
    std::uint32_t node_id,
    std::uint32_t dependency_id,
    const std::unordered_map<std::uint32_t, const ExecutionNode*>& nodes) {
    std::unordered_set<std::uint32_t> visited;
    std::function<bool(std::uint32_t)> visit = [&](std::uint32_t id) {
        if (id == dependency_id) return true;
        if (!visited.insert(id).second) return false;
        for (const auto dependency : nodes.at(id)->dependencies) {
            if (visit(dependency)) return true;
        }
        return false;
    };
    return visit(node_id);
}

void validate_direct(const DirectWork& work) {
    std::uint64_t threads = 1;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (work.item_extent[axis] == 0 ||
            work.group_size[axis] == 0) {
            throw Error(
                ErrorCode::InvalidArgument,
                "direct work extent is invalid");
        }
        if (threads >
            std::numeric_limits<std::uint64_t>::max() /
                work.group_size[axis]) {
            throw Error(
                ErrorCode::Overflow,
                "direct workgroup size overflows");
        }
        threads *= work.group_size[axis];
        const auto groups =
            work.item_extent[axis] / work.group_size[axis] +
            (work.item_extent[axis] % work.group_size[axis] != 0
                 ? 1
                 : 0);
        if (groups > std::numeric_limits<std::uint32_t>::max()) {
            throw Error(
                ErrorCode::Overflow,
                "direct dispatch group count overflows");
        }
    }
    if (threads > 1024) {
        throw Error(
            ErrorCode::InvalidArgument,
            "direct workgroup exceeds portable limit");
    }
}

void hash_u64(ExecutionFingerprint& hash, std::uint64_t value) {
    constexpr std::array<std::uint64_t, 4> primes = {
        1099511628211ull,
        14029467366897019727ull,
        1609587929392839161ull,
        9650029242287828579ull};
    for (std::size_t byte = 0; byte < 8; ++byte) {
        const auto part = (value >> (byte * 8)) & 0xffu;
        for (std::size_t lane = 0; lane < hash.size(); ++lane) {
            hash[lane] ^= part + lane * 0x9du;
            hash[lane] *= primes[lane];
        }
    }
}

void hash_resource(
    ExecutionFingerprint& hash,
    resource::ResourceId resource) {
    hash_u64(hash, resource.namespace_id);
    hash_u64(hash, resource.local_id);
}

template <typename Enum>
void hash_enum(ExecutionFingerprint& hash, Enum value) {
    hash_u64(hash, static_cast<std::uint64_t>(value));
}

}

resource::ResourceId semantic_resource(std::uint64_t local_id) {
    return {kSemanticNamespace, local_id};
}

ExecutionGraph make_path_execution_graph(
    const PathExecutionConfig& config) {
    if (config.width == 0 || config.height == 0 ||
        config.primary_ray_count == 0 || config.samples_per_pass == 0 ||
        config.render.max_trace_depth <= 0 ||
        config.render.rays_per_block <= 0 ||
        config.render.restir_pt.max_reuse_depth < 0 ||
        config.render.restir_pt.candidate_count <= 0) {
        throw Error(
            ErrorCode::InvalidArgument,
            "path execution configuration is invalid");
    }
    if (static_cast<std::uint64_t>(config.width) *
            static_cast<std::uint64_t>(config.height) !=
        config.primary_ray_count) {
        throw Error(
            ErrorCode::InvalidArgument,
            "path execution extent does not match primary ray count");
    }

    const bool mlt =
        config.render.integrator.mode == IntegratorMode::MLT;
    const bool restir_di =
        config.render.restir_di.enabled &&
        (config.render.restir_di.temporal_reuse ||
         config.render.restir_di.spatial_reuse);
    const bool restir_pt = config.render.restir_pt.enabled;
    const bool bidirectional =
        config.render.bidirectional.enabled ||
        config.render.vcm.enabled ||
        config.render.specular_manifold.enabled;
    if ((restir_di && config.restir_di_input_index > 1) ||
        (restir_pt && config.restir_pt_input_index > 1)) {
        throw Error(
            ErrorCode::InvalidArgument,
            "ReSTIR reservoir input index is invalid");
    }
    if (config.render.vcm.enabled &&
        config.vcm_radius_iteration ==
            std::numeric_limits<std::uint64_t>::max()) {
        throw Error(
            ErrorCode::Overflow,
            "VCM radius iteration overflows");
    }
    if (!mlt && bidirectional && config.samples_per_pass != 1) {
        throw Error(
            ErrorCode::InvalidArgument,
            "advanced path estimators require one sample per pass");
    }

    ExecutionGraph graph;
    graph.kind = mlt
        ? ExecutionKind::Metropolis
        : ExecutionKind::PathTracing;
    graph.estimator.mode = config.render.integrator.mode;
    graph.queues = {
        queue(1, QueueRole::RayCurrent, 1),
        queue(2, QueueRole::RayNext, 10),
        queue(3, QueueRole::Hit, 20),
        queue(4, QueueRole::Shadow, 30),
        queue(6, QueueRole::MltPathCurrent, 50),
        queue(7, QueueRole::ManifoldRoot, 60),
        queue(8, QueueRole::MltPathNext, 70)};

    graph.regions.push_back(
        {1, 0, RegionKind::Pass, RepeatKind::Once, 1, 1,
         std::nullopt, std::nullopt, std::nullopt});
    GraphBuilder builder(std::move(graph));
    builder.add(
        1,
        boundary(EpochDomain::Pass, BoundaryAction::Begin,
                 config.pass_epoch));

    if (mlt) {
        const auto& mlt_config = config.render.mlt;
        if (mlt_config.chain_count <= 0 ||
            mlt_config.bootstrap_samples <= 0 ||
            mlt_config.burn_in_mutations < 0 ||
            mlt_config.mutations_per_chain <= 0 ||
            config.mlt_primary_dimension_count == 0 ||
            config.queue_capacity == 0) {
            throw Error(
                ErrorCode::InvalidArgument,
                "MLT execution configuration is invalid");
        }
        const auto mutations =
            static_cast<std::uint64_t>(mlt_config.mutations_per_chain) *
            config.samples_per_pass;
        if (mutations > std::numeric_limits<std::uint32_t>::max()) {
            throw Error(
                ErrorCode::Overflow,
                "MLT mutation iteration count overflows");
        }
        const auto burn_count =
            !config.mlt_initialized
            ? static_cast<std::uint64_t>(
                  mlt_config.burn_in_mutations)
            : 0;
        if (config.mlt_epoch >
            std::numeric_limits<std::uint64_t>::max() -
                burn_count - mutations) {
            throw Error(
                ErrorCode::Overflow,
                "MLT mutation epoch overflows");
        }
        const auto chain_count =
            static_cast<std::uint64_t>(mlt_config.chain_count);
        const auto bootstrap_count =
            static_cast<std::uint64_t>(
                mlt_config.bootstrap_samples);
        if (config.mlt_primary_dimension_count >
                std::numeric_limits<std::uint64_t>::max() /
                    std::max(chain_count, bootstrap_count)) {
            throw Error(
                ErrorCode::Overflow,
                "MLT primary sample work overflows");
        }
        const auto chain_value_count =
            chain_count * config.mlt_primary_dimension_count;
        const auto bootstrap_value_count =
            bootstrap_count * config.mlt_primary_dimension_count;
        const auto bootstrap_batches =
            bootstrap_count / config.queue_capacity +
            (bootstrap_count % config.queue_capacity != 0 ? 1 : 0);
        if (bootstrap_batches >
            std::numeric_limits<std::uint32_t>::max()) {
            throw Error(
                ErrorCode::Overflow,
                "MLT bootstrap batch count overflows");
        }
        auto mlt_graph = builder.finish();
        if (!config.mlt_initialized) {
            mlt_graph.regions.push_back(
                {2, 1, RegionKind::BootstrapLoop,
                 RepeatKind::FixedCount,
                 bootstrap_batches,
                 static_cast<std::uint32_t>(bootstrap_batches),
                 std::nullopt, std::nullopt, std::nullopt});
            mlt_graph.regions.push_back(
                {3, 2, RegionKind::DepthLoop,
                 RepeatKind::UntilQueueEmpty, 0,
                 static_cast<std::uint32_t>(
                     config.render.max_trace_depth),
                 6, std::nullopt, std::nullopt});
        }
        if (!config.mlt_initialized &&
            mlt_config.burn_in_mutations > 0) {
            mlt_graph.regions.push_back(
                {4, 1, RegionKind::MutationLoop, RepeatKind::FixedCount,
                 static_cast<std::uint64_t>(
                     mlt_config.burn_in_mutations),
                 static_cast<std::uint32_t>(
                     mlt_config.burn_in_mutations),
                 std::nullopt, std::nullopt, std::nullopt});
        }
        mlt_graph.regions.push_back(
            {5, 1, RegionKind::MutationLoop, RepeatKind::FixedCount,
             mutations, static_cast<std::uint32_t>(mutations),
             std::nullopt, std::nullopt, std::nullopt});
        if (!config.mlt_initialized &&
            mlt_config.burn_in_mutations > 0) {
            mlt_graph.regions.push_back(
                {6, 4, RegionKind::DepthLoop,
                 RepeatKind::UntilQueueEmpty, 0,
                 static_cast<std::uint32_t>(
                     config.render.max_trace_depth),
                 6, std::nullopt, std::nullopt});
        }
        mlt_graph.regions.push_back(
            {7, 5, RegionKind::DepthLoop, RepeatKind::UntilQueueEmpty, 0,
             static_cast<std::uint32_t>(config.render.max_trace_depth), 6,
             std::nullopt, std::nullopt});
        GraphBuilder mlt_builder(std::move(mlt_graph));
        mlt_builder.add(
            1,
            boundary(
                EpochDomain::Mlt,
                BoundaryAction::Begin,
                config.mlt_epoch));

        const auto group_size =
            static_cast<std::uint32_t>(config.render.rays_per_block);
        auto add_mlt_evaluation =
            [&](std::uint32_t outer_region,
                std::uint32_t depth_region,
                WorkSource ray_generation_work) {
                mlt_builder.add(
                    outer_region,
                    QueueStage{QueueOperation::Reset, 6, std::nullopt});
                const auto raygen = mlt_builder.add(
                    outer_region,
                    dispatch(
                        StageKind::MltRayGenerate,
                        std::move(ray_generation_work),
                        {
                            access(kMlt, AccessMode::Read),
                            access(50, AccessMode::Write)}));
                mlt_builder.add(
                    depth_region,
                    QueueStage{
                        QueueOperation::Reset,
                        8,
                        std::nullopt});
                mlt_builder.add(
                    depth_region,
                    QueueStage{
                        QueueOperation::Reset,
                        4,
                        std::nullopt});
                IndirectQueueWork path_work;
                path_work.queue_id = 6;
                path_work.initial_producer_node = raygen;
                path_work.relation = CountRelation::PreviousIteration;
                path_work.arguments = semantic_resource(52);
                const auto intersect_node = mlt_builder.add(
                    depth_region,
                    dispatch(
                        StageKind::Intersect,
                        path_work,
                        {
                            access(kScene, AccessMode::Read),
                            access(50, AccessMode::Read),
                            access(20, AccessMode::Write)}));
                auto current_work = path_work;
                current_work.producer_node = intersect_node;
                current_work.initial_producer_node = intersect_node;
                current_work.relation = CountRelation::SameIteration;
                const auto shade_node = mlt_builder.add(
                    depth_region,
                    dispatch(
                        StageKind::Shade,
                        current_work,
                        {
                            access(kScene, AccessMode::Read),
                            access(50, AccessMode::Read),
                            access(20, AccessMode::Read),
                            access(70, AccessMode::Write),
                            access(kMlt, AccessMode::ReadWrite),
                            access(30, AccessMode::Write)}));
                auto& carried_work = std::get<IndirectQueueWork>(
                    std::get<DispatchStage>(
                        mlt_builder.node(intersect_node).command).work);
                carried_work.producer_node = shade_node;
                IndirectQueueWork shadow_work;
                shadow_work.queue_id = 4;
                shadow_work.producer_node = shade_node;
                shadow_work.initial_producer_node = shade_node;
                shadow_work.arguments = semantic_resource(32);
                mlt_builder.add(
                    depth_region,
                    dispatch(
                        StageKind::ShadowIntersect,
                        shadow_work,
                        {
                            access(kScene, AccessMode::Read),
                            access(30, AccessMode::Read),
                            access(kMlt, AccessMode::ReadWrite)}));
                const auto swap_node = mlt_builder.add(
                    depth_region,
                    QueueStage{
                        QueueOperation::Swap,
                        6,
                        std::uint32_t{8}});
                auto& depth = mlt_builder.region(depth_region);
                depth.initial_count_producer = raygen;
                depth.iteration_count_producer = swap_node;
            };

        if (!config.mlt_initialized) {
            mlt_builder.add(
                1,
                dispatch(
                    StageKind::MltPrimaryInitialize,
                    direct(
                        bootstrap_value_count,
                        group_size),
                    {access(kMlt, AccessMode::Write)}));
            add_mlt_evaluation(
                2,
                3,
                chunked(
                    2,
                    bootstrap_count,
                    config.queue_capacity,
                    group_size));
            mlt_builder.add(
                2,
                dispatch(
                    StageKind::MltBootstrapCollect,
                    chunked(
                        2,
                        bootstrap_count,
                        config.queue_capacity,
                        group_size),
                    {access(kMlt, AccessMode::ReadWrite)}));
            mlt_builder.add(
                1,
                AsyncTransferStage{
                    TransferKind::Readback,
                    semantic_resource(kMlt),
                    semantic_resource(kHostStaging),
                    static_cast<std::uint64_t>(
                        mlt_config.bootstrap_samples) *
                        sizeof(float)});
            mlt_builder.add(
                1,
                HostStage{
                    HostOperation::MltBootstrapNormalizeCdf,
                    {access(
                        kHostStaging,
                        AccessMode::ReadWrite)}});
            mlt_builder.add(
                1,
                AsyncTransferStage{
                    TransferKind::Upload,
                    semantic_resource(kHostStaging),
                    semantic_resource(kMlt),
                    static_cast<std::uint64_t>(
                        mlt_config.bootstrap_samples) *
                        sizeof(float)});
            mlt_builder.add(
                1,
                dispatch(
                    StageKind::MltChainSeed,
                    direct(
                        static_cast<std::uint64_t>(
                            mlt_config.chain_count),
                        group_size),
                    {access(kMlt, AccessMode::ReadWrite)}));
            if (mlt_config.burn_in_mutations > 0) {
                mlt_builder.add(
                    4,
                    dispatch(
                        StageKind::MltMutate,
                        direct(
                            chain_value_count,
                            group_size),
                        {access(kMlt, AccessMode::ReadWrite)}));
                add_mlt_evaluation(
                    4,
                    6,
                    direct(chain_count, group_size));
                mlt_builder.add(
                    4,
                    dispatch(
                        StageKind::MltAcceptDeposit,
                        direct(
                            static_cast<std::uint64_t>(
                                mlt_config.chain_count),
                            group_size),
                        {
                            access(kMlt, AccessMode::ReadWrite),
                            access(kFilm, AccessMode::ReadWrite)}));
                mlt_builder.add(
                    4,
                    StateStage{
                        StateOperation::MltMutationAdvance,
                        semantic_resource(kMlt),
                        config.mlt_epoch,
                        1,
                        0});
            }
        }
        mlt_builder.add(
            5,
            dispatch(
                StageKind::MltMutate,
                direct(
                    chain_value_count,
                    group_size),
                {access(kMlt, AccessMode::ReadWrite)}));
        add_mlt_evaluation(
            5,
            7,
            direct(chain_count, group_size));
        mlt_builder.add(
            5,
            dispatch(
                StageKind::MltAcceptDeposit,
                direct(
                    static_cast<std::uint64_t>(
                        mlt_config.chain_count),
                    group_size),
                {
                    access(kMlt, AccessMode::ReadWrite),
                    access(kFilm, AccessMode::ReadWrite)}));
        mlt_builder.add(
            5,
            StateStage{
                StateOperation::MltMutationAdvance,
                semantic_resource(kMlt),
                config.mlt_epoch + burn_count,
                1,
                0});
        mlt_builder.add(
            1,
            dispatch(
                StageKind::MltSampleCountCommit,
                direct(config.primary_ray_count, group_size),
                {access(kFilm, AccessMode::ReadWrite)}));
        mlt_builder.add(
            1,
            StateStage{
                StateOperation::SampleCountAdvance,
                semantic_resource(kFilm),
                config.pass_epoch,
                mutations,
                0});
        mlt_builder.add(
            1,
            boundary(
                EpochDomain::Mlt,
                BoundaryAction::End,
                config.mlt_epoch));
        mlt_builder.add(
            1,
            AsyncTransferStage{
                TransferKind::Readback,
                semantic_resource(kTelemetry),
                semantic_resource(kHostTelemetry),
                0,
                true});
        mlt_builder.add(
            1,
            boundary(
                EpochDomain::Pass,
                BoundaryAction::End,
                config.pass_epoch));
        auto result = mlt_builder.finish();
        validate(result);
        return result;
    }

    if (config.path_guiding_decay_due) {
        if (config.path_guiding_light_count == 0 ||
            config.path_guiding_spatial_entry_count == 0) {
            throw Error(
                ErrorCode::InvalidArgument,
                "path-guiding decay work is empty");
        }
        builder.add(
            1,
            boundary(
                EpochDomain::PathGuiding,
                BoundaryAction::Begin,
                config.guiding_epoch));
        builder.add(
            1,
            dispatch(
                StageKind::PathGuidingLightDecay,
                direct(
                    config.path_guiding_light_count,
                    256),
                {access(kGuiding, AccessMode::ReadWrite)}));
        builder.add(
            1,
            dispatch(
                StageKind::PathGuidingSpatialDecay,
                direct(
                    config.path_guiding_spatial_entry_count,
                    256),
                {access(kGuiding, AccessMode::ReadWrite)}));
        builder.add(
            1,
            BarrierStage{{semantic_resource(kGuiding)}});
        builder.add(
            1,
            boundary(
                EpochDomain::PathGuiding,
                BoundaryAction::End,
                config.guiding_epoch));
    }

    auto base_graph = builder.finish();
    base_graph.regions.push_back(
        {2, 1, RegionKind::SampleLoop, RepeatKind::FixedCount,
         config.samples_per_pass, config.samples_per_pass, std::nullopt,
         std::nullopt, std::nullopt});
    const std::uint64_t candidates = restir_pt
        ? static_cast<std::uint64_t>(
              std::max(1, config.render.restir_pt.candidate_count))
        : 1;
    base_graph.regions.push_back(
        {3, 2, RegionKind::CandidateLoop, RepeatKind::FixedCount,
         candidates, static_cast<std::uint32_t>(candidates), std::nullopt,
         std::nullopt, std::nullopt});
    const auto depth_limit = restir_pt
        ? std::min(
              config.render.max_trace_depth,
              config.render.restir_pt.max_reuse_depth + 1)
        : config.render.max_trace_depth;
    base_graph.regions.push_back(
        {4, 3, RegionKind::DepthLoop, RepeatKind::UntilQueueEmpty, 0,
         static_cast<std::uint32_t>(depth_limit), 1,
         std::nullopt, std::nullopt});
    if (config.render.specular_manifold.enabled) {
        base_graph.regions.push_back(
            {5, 1, RegionKind::ManifoldRootLoop,
             RepeatKind::UntilQueueEmpty, 0,
             std::numeric_limits<std::uint32_t>::max(), 7,
             std::nullopt, std::nullopt});
    }
    GraphBuilder path(std::move(base_graph));

    if (restir_di) {
        path.add(
            1,
            boundary(
                EpochDomain::RestirDI,
                BoundaryAction::Begin,
                config.restir_di_epoch));
    }
    if (restir_pt) {
        path.add(
            1,
            boundary(
                EpochDomain::RestirPT,
                BoundaryAction::Begin,
                config.restir_pt_epoch));
    }
    if (bidirectional) {
        path.add(
            1,
            boundary(
                EpochDomain::Bidirectional,
                BoundaryAction::Begin,
                config.bidirectional_epoch));
        path.add(
            1,
            dispatch(
                StageKind::LightSubpathGenerate,
                direct(
                    config.primary_ray_count,
                    static_cast<std::uint32_t>(
                        config.render.rays_per_block)),
                {
                    access(kScene, AccessMode::Read),
                    access(kLightPaths, AccessMode::Write)}));
        path.add(
            1,
            dispatch(
                StageKind::LightSubpathExtend,
                direct(
                    config.primary_ray_count,
                    static_cast<std::uint32_t>(
                        config.render.rays_per_block)),
                {
                    access(kScene, AccessMode::Read),
                    access(kLightPaths, AccessMode::ReadWrite)}));
        if (config.render.vcm.enabled &&
            config.render.vcm.merge_surfaces) {
            path.add(
                1,
                dispatch(
                    StageKind::VcmSurfaceGridBuild,
                    direct(
                        config.primary_ray_count,
                        static_cast<std::uint32_t>(
                            config.render.rays_per_block)),
                    {
                        access(kLightPaths, AccessMode::Read),
                        access(kVcm, AccessMode::Write)}));
        }
        if (config.render.vcm.enabled &&
            config.render.vcm.merge_volumes) {
            path.add(
                1,
                dispatch(
                    StageKind::VcmVolumeGridBuild,
                    direct(
                        config.primary_ray_count,
                        static_cast<std::uint32_t>(
                            config.render.rays_per_block)),
                    {
                        access(kLightPaths, AccessMode::Read),
                        access(kVcm, AccessMode::ReadWrite)}));
        }
    }

    if (restir_pt) {
        path.add(
            2,
            ClearStage{
                semantic_resource(kRestirPTOutput),
                0,
                0,
                true});
    }
    if (restir_di && config.render.restir_di.unbiased) {
        path.add(
            2,
            ClearStage{
                semantic_resource(kRestirDIOutput),
                0,
                0,
                true});
    }
    path.add(3, QueueStage{QueueOperation::Reset, 1, std::nullopt});
    const auto ray_generate = path.add(
        3,
        dispatch(
            StageKind::RayGenerate,
            direct_2d(
                config.width,
                config.height,
                16,
                16),
            {
                access(kScene, AccessMode::Read),
                access(1, AccessMode::Write),
                access(2, AccessMode::Write)}));

    IndirectQueueWork ray_work;
    ray_work.queue_id = 1;
    ray_work.initial_producer_node = ray_generate;
    ray_work.relation = CountRelation::PreviousIteration;
    ray_work.arguments = semantic_resource(3);
    const auto intersect = path.add(
        4,
        dispatch(
            StageKind::Intersect,
            ray_work,
            {
                access(kScene, AccessMode::Read),
                access(1, AccessMode::Read),
                access(20, AccessMode::Write)}));
    path.add(
        4,
        BarrierStage{
            {semantic_resource(20), semantic_resource(21)}});

    auto current_work = ray_work;
    current_work.producer_node = intersect;
    current_work.initial_producer_node = intersect;
    current_work.relation = CountRelation::SameIteration;
    if (restir_pt) {
        auto stage = dispatch(
            StageKind::RestirPtCandidatePrepare,
            current_work,
            {
                access(1, AccessMode::Read),
                access(20, AccessMode::Read),
                access(kRestirPT, AccessMode::ReadWrite)});
        stage.iteration = IterationPredicate::First;
        path.add(4, std::move(stage));
    }
    path.add(4, QueueStage{QueueOperation::Reset, 2, std::nullopt});
    path.add(4, QueueStage{QueueOperation::Reset, 4, std::nullopt});
    if (restir_di && config.render.restir_di.unbiased) {
        auto stage = dispatch(
            StageKind::RestirDiResample,
            current_work,
            {
                access(1, AccessMode::Read),
                access(20, AccessMode::Read),
                access(kRestirDI, AccessMode::ReadWrite)});
        stage.iteration = IterationPredicate::First;
        path.add(4, std::move(stage));
    }

    std::vector<ResourceAccess> shade_resources = {
        access(kScene, AccessMode::Read),
        access(1, AccessMode::Read),
        access(20, AccessMode::Read),
        access(10, AccessMode::Write),
        access(30, AccessMode::Write),
        access(kFilm, AccessMode::ReadWrite)};
    if (restir_di) {
        shade_resources.push_back(
            access(kRestirDI, AccessMode::ReadWrite));
    }
    if (config.render.path_guiding.enabled) {
        shade_resources.push_back(
            access(kGuiding, AccessMode::ReadWrite));
    }
    if (restir_pt) {
        shade_resources.push_back(
            access(kRestirPT, AccessMode::ReadWrite));
    }
    const auto shade = path.add(
        4,
        dispatch(
            StageKind::Shade,
            current_work,
            std::move(shade_resources)));
    auto& intersect_work = std::get<IndirectQueueWork>(
        std::get<DispatchStage>(path.node(intersect).command).work);
    intersect_work.producer_node = shade;

    IndirectQueueWork shadow_work;
    shadow_work.queue_id = 4;
    shadow_work.producer_node = shade;
    shadow_work.initial_producer_node = shade;
    shadow_work.relation = CountRelation::SameIteration;
    shadow_work.arguments = semantic_resource(32);
    path.add(
        4,
        dispatch(
            StageKind::ShadowIntersect,
            shadow_work,
            {
                access(kScene, AccessMode::Read),
                access(30, AccessMode::Read),
                access(kFilm, AccessMode::ReadWrite)}));
    const auto queue_swap = path.add(
        4,
        QueueStage{QueueOperation::Swap, 1, std::uint32_t{2}});
    path.region(4).initial_count_producer = ray_generate;
    path.region(4).iteration_count_producer = queue_swap;

    if (restir_pt) {
        path.add(
            3,
            dispatch(
                StageKind::RestirPtCandidateStream,
                direct(
                    config.primary_ray_count,
                    static_cast<std::uint32_t>(
                        config.render.rays_per_block)),
                {
                    access(kRestirPT, AccessMode::ReadWrite),
                    access(kFilm, AccessMode::Read)}));
        path.add(
            2,
            dispatch(
                StageKind::RestirPtFinalize,
                direct(
                    config.primary_ray_count,
                    static_cast<std::uint32_t>(
                        config.render.rays_per_block)),
                {
                    access(kRestirPT, AccessMode::ReadWrite),
                    access(kFilm, AccessMode::ReadWrite)}));
        path.add(
            2,
            StateStage{
                StateOperation::RestirPTReservoirSwap,
                semantic_resource(kRestirPT),
                config.restir_pt_input_index,
                1,
                2});
    }
    if (restir_di && config.render.restir_di.unbiased) {
        path.add(
            2,
            StateStage{
                StateOperation::RestirDIReservoirSwap,
                semantic_resource(kRestirDI),
                config.restir_di_input_index,
                1,
                2});
    }

    path.add(
        1,
        StateStage{
            StateOperation::SampleCountAdvance,
            semantic_resource(kFilm),
            config.pass_epoch,
            config.samples_per_pass,
            0});

    if (bidirectional) {
        const bool standalone_manifold =
            config.render.integrator.mode ==
            IntegratorMode::SpecularManifold;
        if (!standalone_manifold) {
            path.add(
                1,
                dispatch(
                    StageKind::BidirectionalConnect,
                    direct(
                        config.primary_ray_count,
                        static_cast<std::uint32_t>(
                            config.render.rays_per_block)),
                    {
                        access(kScene, AccessMode::Read),
                        access(kLightPaths, AccessMode::Read),
                        access(kFilm, AccessMode::ReadWrite)}));
        }
        if (config.render.vcm.enabled &&
            config.render.vcm.merge_surfaces) {
            path.add(
                1,
                dispatch(
                    StageKind::VcmSurfaceMerge,
                    direct(
                        config.primary_ray_count,
                        static_cast<std::uint32_t>(
                            config.render.rays_per_block)),
                    {
                        access(kVcm, AccessMode::Read),
                        access(kFilm, AccessMode::ReadWrite)}));
        }
        if (config.render.vcm.enabled &&
            config.render.vcm.merge_volumes) {
            path.add(
                1,
                dispatch(
                    StageKind::VcmVolumeMerge,
                    direct(
                        config.primary_ray_count,
                        static_cast<std::uint32_t>(
                            config.render.rays_per_block)),
                    {
                        access(kVcm, AccessMode::Read),
                        access(kFilm, AccessMode::ReadWrite)}));
        }
        if (config.render.vcm.enabled) {
            path.add(
                1,
                StateStage{
                    StateOperation::VcmRadiusAdvance,
                    semantic_resource(kVcm),
                    config.vcm_radius_iteration,
                    1,
                    0});
        }
        if (config.render.specular_manifold.enabled) {
            path.add(
                1,
                dispatch(
                    StageKind::ManifoldTargetGenerate,
                    direct(
                        config.primary_ray_count,
                        static_cast<std::uint32_t>(
                            config.render.rays_per_block)),
                    {
                        access(kScene, AccessMode::Read),
                        access(kManifold, AccessMode::Write)}));
            const auto root_initialize = path.add(
                1,
                dispatch(
                    StageKind::ManifoldRootInitialize,
                    direct(
                        config.primary_ray_count,
                        static_cast<std::uint32_t>(
                        config.render.rays_per_block)),
                    {access(kManifold, AccessMode::ReadWrite)}));
            const auto root_advance = path.add(
                5,
                dispatch(
                    StageKind::ManifoldRootAdvance,
                    direct(
                        config.primary_ray_count,
                        static_cast<std::uint32_t>(
                            config.render.rays_per_block)),
                    {
                        access(kScene, AccessMode::Read),
                        access(kManifold, AccessMode::ReadWrite)}));
            path.region(5).initial_count_producer =
                root_initialize;
            path.region(5).iteration_count_producer =
                root_advance;
            const std::array stages = {
                StageKind::ManifoldWeightAssign,
                StageKind::ManifoldContributionEvaluate,
                StageKind::ManifoldContributionConvert};
            for (const auto stage : stages) {
                path.add(
                    1,
                    dispatch(
                        stage,
                        direct(
                            config.primary_ray_count,
                            static_cast<std::uint32_t>(
                                config.render.rays_per_block)),
                        {
                            access(kScene, AccessMode::Read),
                            access(kManifold, AccessMode::ReadWrite)}));
            }
        }
        std::vector<ResourceAccess> commit_resources = {
            access(kFilm, AccessMode::ReadWrite),
            access(kLightPaths, AccessMode::Read)};
        if (config.render.vcm.enabled) {
            commit_resources.push_back(
                access(kVcm, AccessMode::Read));
        }
        if (config.render.specular_manifold.enabled) {
            commit_resources.push_back(
                access(kManifold, AccessMode::Read));
        }
        path.add(
            1,
            dispatch(
                StageKind::TechniqueContributionCommit,
                direct(
                    config.primary_ray_count,
                    static_cast<std::uint32_t>(
                        config.render.rays_per_block)),
                std::move(commit_resources)));
        path.add(
            1,
            boundary(
                EpochDomain::Bidirectional,
                BoundaryAction::End,
                config.bidirectional_epoch));
    }
    if (restir_pt) {
        path.add(
            1,
            boundary(
                EpochDomain::RestirPT,
                BoundaryAction::End,
                config.restir_pt_epoch));
    }
    if (restir_di) {
        path.add(
            1,
            boundary(
                EpochDomain::RestirDI,
                BoundaryAction::End,
                config.restir_di_epoch));
    }
    if (restir_di || restir_pt || bidirectional) {
        path.add(
            1,
            AsyncTransferStage{
                TransferKind::Readback,
                semantic_resource(kTelemetry),
                semantic_resource(kHostTelemetry),
                0,
                true});
    }
    path.add(
        1,
        boundary(
            EpochDomain::Pass,
            BoundaryAction::End,
            config.pass_epoch));
    auto result = path.finish();
    validate(result);
    return result;
}

ExecutionGraph make_wave_execution_graph(
    const WaveExecutionConfig& config) {
    if (config.sample_count == 0 || config.input_bytes == 0 ||
        config.output_bytes == 0 || config.group_size == 0 ||
        config.group_size > 1024) {
        throw Error(
            ErrorCode::InvalidArgument,
            "wave execution configuration is invalid");
    }

    ExecutionGraph graph;
    graph.kind = ExecutionKind::WaveOperator;
    graph.estimator.mode = IntegratorMode::Wavefront;
    graph.regions.push_back(
        {1, 0, RegionKind::Pass, RepeatKind::Once, 1, 1,
         std::nullopt, std::nullopt, std::nullopt});
    GraphBuilder builder(std::move(graph));
    builder.add(
        1,
        boundary(
            EpochDomain::Pass,
            BoundaryAction::Begin,
            config.pass_epoch));
    builder.add(
        1,
        boundary(
            EpochDomain::Wave,
            BoundaryAction::Begin,
            config.pass_epoch));
    builder.add(
        1,
        AsyncTransferStage{
            TransferKind::Upload,
            semantic_resource(kWaveHostInput),
            semantic_resource(kWaveDeviceInput),
            config.input_bytes});
    builder.add(
        1,
        BarrierStage{{semantic_resource(kWaveDeviceInput)}});
    builder.add(
        1,
        dispatch(
            StageKind::WavePropagate,
            direct(config.sample_count, config.group_size),
            {
                access(kWaveDeviceInput, AccessMode::Read),
                access(kWaveDeviceOutput, AccessMode::Write)}));
    builder.add(
        1,
        BarrierStage{{semantic_resource(kWaveDeviceOutput)}});
    builder.add(
        1,
        AsyncTransferStage{
            TransferKind::Readback,
            semantic_resource(kWaveDeviceOutput),
            semantic_resource(kWaveHostOutput),
            config.output_bytes});
    builder.add(
        1,
        boundary(
            EpochDomain::Wave,
            BoundaryAction::End,
            config.pass_epoch));
    builder.add(
        1,
        boundary(
            EpochDomain::Pass,
            BoundaryAction::End,
            config.pass_epoch));
    auto result = builder.finish();
    validate(result);
    return result;
}

void validate(const ExecutionGraph& graph) {
    if (graph.schema_version != kExecutionGraphSchemaVersion) {
        throw Error(
            ErrorCode::Unsupported,
            "execution graph schema version is unsupported");
    }
    if (graph.estimator.version != kEstimatorContractVersion) {
        throw Error(
            ErrorCode::Unsupported,
            "estimator contract version is unsupported");
    }
    const auto& pdf = graph.estimator.pdf;
    if (pdf.spectral_sampling == 0 ||
        pdf.scattering_solid_angle == 0 ||
        pdf.medium_phase_solid_angle == 0 ||
        pdf.light_selection == 0 ||
        pdf.restir_target == 0 ||
        pdf.technique_support_partition == 0 ||
        pdf.mlt_primary_sampling == 0) {
        throw Error(
            ErrorCode::InvalidArgument,
            "PDF semantic contract is incomplete");
    }
    if (graph.regions.empty() || graph.nodes.empty()) {
        throw Error(
            ErrorCode::InvalidArgument,
            "execution graph is empty");
    }

    std::unordered_map<std::uint32_t, const QueueContract*> queues;
    std::unordered_set<std::uint8_t> queue_roles;
    std::uint32_t previous_queue_id = 0;
    for (const auto& queue_desc : graph.queues) {
        if (queue_desc.id == 0 || queue_desc.id <= previous_queue_id ||
            !queues.emplace(queue_desc.id, &queue_desc).second) {
            throw Error(
                ErrorCode::InvalidArgument,
                "execution queues are not canonical");
        }
        previous_queue_id = queue_desc.id;
        if (!queue_roles.insert(
                static_cast<std::uint8_t>(queue_desc.role)).second) {
            throw Error(
                ErrorCode::InvalidArgument,
                "execution queue role is duplicated");
        }
        if (!queue_desc.payload || !queue_desc.active_count ||
            !queue_desc.indirect_arguments ||
            queue_desc.payload == queue_desc.active_count ||
            queue_desc.payload == queue_desc.indirect_arguments ||
            queue_desc.active_count == queue_desc.indirect_arguments) {
            throw Error(
                ErrorCode::InvalidArgument,
                "execution queue resources are invalid");
        }
    }

    std::unordered_map<std::uint32_t, const ExecutionRegion*> regions;
    std::uint32_t previous_region_id = 0;
    std::uint32_t root_count = 0;
    for (const auto& region : graph.regions) {
        if (region.id == 0 || region.id <= previous_region_id ||
            !regions.emplace(region.id, &region).second) {
            throw Error(
                ErrorCode::InvalidArgument,
                "execution regions are not canonical");
        }
        previous_region_id = region.id;
        if (region.parent_id == 0) {
            ++root_count;
            if (region.kind != RegionKind::Pass) {
                throw Error(
                    ErrorCode::InvalidArgument,
                    "root execution region is not a pass");
            }
        } else if (!regions.contains(region.parent_id)) {
            throw Error(
                ErrorCode::InvalidArgument,
                "execution region parent is missing");
        }
        if (region.repeat == RepeatKind::Once) {
            if (region.repeat_count != 1 ||
                region.maximum_iterations != 1 ||
                region.termination_queue ||
                region.initial_count_producer ||
                region.iteration_count_producer) {
                throw Error(
                    ErrorCode::InvalidArgument,
                    "single execution region is invalid");
            }
        } else if (region.repeat == RepeatKind::FixedCount) {
            if (region.repeat_count == 0 ||
                region.repeat_count > region.maximum_iterations ||
                region.termination_queue ||
                region.initial_count_producer ||
                region.iteration_count_producer) {
                throw Error(
                    ErrorCode::InvalidArgument,
                    "fixed execution region is invalid");
            }
        } else {
            if (region.repeat_count != 0 ||
                region.maximum_iterations == 0 ||
                !region.termination_queue ||
                !queues.contains(*region.termination_queue)) {
                throw Error(
                    ErrorCode::InvalidArgument,
                    "queue-terminated region is invalid");
            }
        }
    }
    if (root_count != 1) {
        throw Error(
            ErrorCode::InvalidArgument,
            "execution graph must have one pass root");
    }

    std::unordered_map<std::uint32_t, const ExecutionNode*> nodes;
    std::uint32_t previous_node_id = 0;
    for (const auto& node : graph.nodes) {
        if (node.id == 0 || node.id <= previous_node_id ||
            !nodes.emplace(node.id, &node).second ||
            !regions.contains(node.region_id)) {
            throw Error(
                ErrorCode::InvalidArgument,
                "execution nodes are not canonical");
        }
        previous_node_id = node.id;
        std::uint32_t previous_dependency = 0;
        for (const auto dependency : node.dependencies) {
            if (dependency == 0 || dependency >= node.id ||
                dependency <= previous_dependency ||
                !nodes.contains(dependency)) {
                throw Error(
                    ErrorCode::InvalidArgument,
                    "execution dependency is invalid");
            }
            previous_dependency = dependency;
        }
    }
    for (const auto& region : graph.regions) {
        if (region.repeat != RepeatKind::UntilQueueEmpty) continue;
        if (!region.initial_count_producer ||
            !region.iteration_count_producer ||
            !nodes.contains(*region.initial_count_producer) ||
            !nodes.contains(*region.iteration_count_producer) ||
            nodes.at(*region.iteration_count_producer)->region_id !=
                region.id) {
            throw Error(
                ErrorCode::InvalidArgument,
                "active-count region dependency is invalid");
        }
        const auto first = std::ranges::find_if(
            graph.nodes,
            [&](const ExecutionNode& node) {
                return node.region_id == region.id;
            });
        if (first == graph.nodes.end() ||
            *region.initial_count_producer >= first->id ||
            !depends_on(
                first->id,
                *region.initial_count_producer,
                nodes)) {
            throw Error(
                ErrorCode::InvalidArgument,
                "active-count region input is invalid");
        }
    }
    for (std::size_t index = 1; index < graph.nodes.size(); ++index) {
        if (!depends_on(
                graph.nodes[index].id,
                graph.nodes[index - 1].id,
                nodes)) {
            throw Error(
                ErrorCode::InvalidArgument,
                "execution node order is not dependency-closed");
        }
    }
    const auto* first_boundary =
        std::get_if<BoundaryStage>(&graph.nodes.front().command);
    const auto* last_boundary =
        std::get_if<BoundaryStage>(&graph.nodes.back().command);
    if (!first_boundary ||
        first_boundary->domain != EpochDomain::Pass ||
        first_boundary->action != BoundaryAction::Begin ||
        !last_boundary ||
        last_boundary->domain != EpochDomain::Pass ||
        last_boundary->action != BoundaryAction::End ||
        first_boundary->epoch != last_boundary->epoch) {
        throw Error(
            ErrorCode::InvalidArgument,
            "execution pass boundary is invalid");
    }

    std::vector<std::pair<EpochDomain, std::uint64_t>> boundaries;
    std::vector<std::uint32_t> critical_nodes;
    for (const auto& node : graph.nodes) {
        std::visit(
            [&](const auto& command) {
                using Type = std::decay_t<decltype(command)>;
                if constexpr (std::is_same_v<Type, DispatchStage>) {
                    if (command.iteration != IterationPredicate::All &&
                        regions.at(node.region_id)->repeat ==
                            RepeatKind::Once) {
                        throw Error(
                            ErrorCode::InvalidArgument,
                            "iteration predicate is outside a loop");
                    }
                    if (command.estimator_critical) {
                        critical_nodes.push_back(node.id);
                    }
                    std::vector<resource::ResourceId> accessed;
                    for (const auto& item : command.resources) {
                        if (!item.resource) {
                            throw Error(
                                ErrorCode::InvalidArgument,
                                "dispatch resource is invalid");
                        }
                        if (std::find(
                                accessed.begin(),
                                accessed.end(),
                                item.resource) != accessed.end()) {
                            throw Error(
                                ErrorCode::InvalidArgument,
                                "dispatch resource is duplicated");
                        }
                        accessed.push_back(item.resource);
                    }
                    std::visit(
                        [&](const auto& work) {
                            using Work =
                                std::decay_t<decltype(work)>;
                            if constexpr (
                                std::is_same_v<Work, DirectWork>) {
                                validate_direct(work);
                            } else if constexpr (
                                std::is_same_v<Work, ChunkedWork>) {
                                if (work.region_id != node.region_id ||
                                    work.total_item_count == 0 ||
                                    work.chunk_size == 0) {
                                    throw Error(
                                        ErrorCode::InvalidArgument,
                                        "chunked work is invalid");
                                }
                                const auto& region =
                                    *regions.at(work.region_id);
                                const auto chunks =
                                    work.total_item_count /
                                        work.chunk_size +
                                    (work.total_item_count %
                                                 work.chunk_size !=
                                             0
                                         ? 1
                                         : 0);
                                if (region.repeat !=
                                        RepeatKind::FixedCount ||
                                    region.repeat_count != chunks) {
                                    throw Error(
                                        ErrorCode::InvalidArgument,
                                        "chunked work does not match its region");
                                }
                                validate_direct(
                                    direct(
                                        std::min(
                                            work.total_item_count,
                                            work.chunk_size),
                                        work.group_size));
                            } else {
                                const auto& queue_desc =
                                    require_queue(
                                        graph.queues,
                                        work.queue_id);
                                if (!work.arguments ||
                                    work.arguments !=
                                        queue_desc.indirect_arguments ||
                                    work.argument_offset % 4 != 0 ||
                                    work.command_stride < 12 ||
                                    work.command_stride % 4 != 0 ||
                                    !nodes.contains(
                                        work.producer_node) ||
                                    !nodes.contains(
                                        work.initial_producer_node)) {
                                    throw Error(
                                        ErrorCode::InvalidArgument,
                                        "indirect queue work is invalid");
                                }
                                if (work.relation ==
                                    CountRelation::SameIteration) {
                                    if (work.producer_node >= node.id ||
                                        !depends_on(
                                            node.id,
                                            work.producer_node,
                                            nodes)) {
                                        throw Error(
                                            ErrorCode::InvalidArgument,
                                            "same-iteration count dependency is invalid");
                                    }
                                } else {
                                    const auto& producer =
                                        *nodes.at(work.producer_node);
                                    const auto& region =
                                        *regions.at(node.region_id);
                                    if (producer.region_id !=
                                            node.region_id ||
                                        region.repeat !=
                                            RepeatKind::UntilQueueEmpty ||
                                        work.initial_producer_node >=
                                            node.id ||
                                        !depends_on(
                                            node.id,
                                            work.initial_producer_node,
                                            nodes)) {
                                        throw Error(
                                            ErrorCode::InvalidArgument,
                                            "iteration-carried count dependency is invalid");
                                    }
                                }
                            }
                        },
                        command.work);
                } else if constexpr (
                    std::is_same_v<Type, QueueStage>) {
                    require_queue(graph.queues, command.queue_id);
                    if (command.operation == QueueOperation::Swap) {
                        if (!command.paired_queue_id ||
                            *command.paired_queue_id ==
                                command.queue_id) {
                            throw Error(
                                ErrorCode::InvalidArgument,
                                "queue swap pair is invalid");
                        }
                        require_queue(
                            graph.queues,
                            *command.paired_queue_id);
                    } else if (command.paired_queue_id) {
                        throw Error(
                            ErrorCode::InvalidArgument,
                            "queue reset has a pair");
                    }
                } else if constexpr (
                    std::is_same_v<Type, BarrierStage>) {
                    if (command.resources.empty()) {
                        throw Error(
                            ErrorCode::InvalidArgument,
                            "execution barrier is empty");
                    }
                    std::vector<resource::ResourceId> sorted =
                        command.resources;
                    std::sort(sorted.begin(), sorted.end());
                    if (sorted != command.resources ||
                        std::adjacent_find(
                            sorted.begin(), sorted.end()) !=
                            sorted.end() ||
                        std::any_of(
                            sorted.begin(),
                            sorted.end(),
                            [](resource::ResourceId value) {
                                return !value;
                            })) {
                        throw Error(
                            ErrorCode::InvalidArgument,
                            "execution barrier resources are invalid");
                    }
                } else if constexpr (
                    std::is_same_v<Type, ClearStage>) {
                    if (!command.resource ||
                        (command.whole_resource
                             ? command.offset != 0 ||
                                   command.size_bytes != 0
                             : command.size_bytes == 0 ||
                                   command.offset >
                                       std::numeric_limits<
                                           std::uint64_t>::max() -
                                           command.size_bytes)) {
                        throw Error(
                            ErrorCode::InvalidArgument,
                            "resource clear is invalid");
                    }
                } else if constexpr (
                    std::is_same_v<Type, AsyncTransferStage>) {
                    if (!command.source || !command.destination ||
                        command.source == command.destination ||
                        (command.whole_resource
                             ? command.size_bytes != 0
                             : command.size_bytes == 0)) {
                        throw Error(
                            ErrorCode::InvalidArgument,
                            "async transfer is invalid");
                    }
                } else if constexpr (
                    std::is_same_v<Type, HostStage>) {
                    if (command.resources.empty()) {
                        throw Error(
                            ErrorCode::InvalidArgument,
                            "host stage resources are empty");
                    }
                    std::vector<resource::ResourceId> host_resources;
                    for (const auto& item : command.resources) {
                        if (!item.resource ||
                            std::find(
                                host_resources.begin(),
                                host_resources.end(),
                                item.resource) !=
                                host_resources.end()) {
                            throw Error(
                                ErrorCode::InvalidArgument,
                                "host stage resources are invalid");
                        }
                        host_resources.push_back(item.resource);
                    }
                } else if constexpr (
                    std::is_same_v<Type, StateStage>) {
                    if (!command.state ||
                        command.increment == 0 ||
                        (command.modulo != 0 &&
                         (command.modulo < 2 ||
                          command.initial_value >=
                              command.modulo))) {
                        throw Error(
                            ErrorCode::InvalidArgument,
                            "state transition is invalid");
                    }
                    const bool reservoir_swap =
                        command.operation ==
                            StateOperation::RestirDIReservoirSwap ||
                        command.operation ==
                            StateOperation::RestirPTReservoirSwap;
                    if ((reservoir_swap &&
                         command.modulo != 2) ||
                        (!reservoir_swap &&
                         command.modulo != 0)) {
                        throw Error(
                            ErrorCode::InvalidArgument,
                            "state transition modulo is invalid");
                    }
                    if (command.modulo == 0) {
                        const auto& region =
                            *regions.at(node.region_id);
                        const auto repeats =
                            region.repeat ==
                                    RepeatKind::FixedCount
                                ? region.repeat_count
                                : 1;
                        if (command.increment >
                                std::numeric_limits<
                                    std::uint64_t>::max() /
                                    repeats ||
                            command.initial_value >
                                std::numeric_limits<
                                    std::uint64_t>::max() -
                                    command.increment *
                                        repeats) {
                            throw Error(
                                ErrorCode::Overflow,
                                "state transition overflows");
                        }
                    }
                } else {
                    if (command.action == BoundaryAction::Begin) {
                        boundaries.emplace_back(
                            command.domain,
                            command.epoch);
                    } else {
                        if (boundaries.empty() ||
                            boundaries.back().first !=
                                command.domain ||
                            boundaries.back().second !=
                                command.epoch) {
                            throw Error(
                                ErrorCode::InvalidArgument,
                                "execution boundary nesting is invalid");
                        }
                        boundaries.pop_back();
                    }
                }
            },
            node.command);
    }
    if (!boundaries.empty()) {
        throw Error(
            ErrorCode::InvalidArgument,
            "execution boundary is not closed");
    }
    if (critical_nodes != graph.estimator.ordered_nodes ||
        critical_nodes.empty()) {
        throw Error(
            ErrorCode::InvalidArgument,
            "estimator order does not match critical stages");
    }
    for (std::size_t index = 1;
         index < critical_nodes.size();
         ++index) {
        if (!depends_on(
                critical_nodes[index],
                critical_nodes[index - 1],
                nodes)) {
            throw Error(
                ErrorCode::InvalidArgument,
                "estimator stage order is not dependency-closed");
        }
    }
}

ExecutionFingerprint execution_fingerprint(
    const ExecutionGraph& graph) {
    validate(graph);
    ExecutionFingerprint hash = {
        14695981039346656037ull,
        1099511628211ull,
        7809847782465536322ull,
        9650029242287828579ull};
    hash_u64(hash, graph.schema_version);
    hash_enum(hash, graph.kind);
    hash_u64(hash, graph.regions.size());
    for (const auto& region : graph.regions) {
        hash_u64(hash, region.id);
        hash_u64(hash, region.parent_id);
        hash_enum(hash, region.kind);
        hash_enum(hash, region.repeat);
        hash_u64(hash, region.repeat_count);
        hash_u64(hash, region.maximum_iterations);
        hash_u64(hash, region.termination_queue.has_value());
        hash_u64(hash, region.termination_queue.value_or(0));
        hash_u64(hash, region.initial_count_producer.has_value());
        hash_u64(hash, region.initial_count_producer.value_or(0));
        hash_u64(hash, region.iteration_count_producer.has_value());
        hash_u64(hash, region.iteration_count_producer.value_or(0));
    }
    hash_u64(hash, graph.queues.size());
    for (const auto& queue_desc : graph.queues) {
        hash_u64(hash, queue_desc.id);
        hash_enum(hash, queue_desc.role);
        hash_resource(hash, queue_desc.payload);
        hash_resource(hash, queue_desc.active_count);
        hash_resource(hash, queue_desc.indirect_arguments);
    }
    hash_u64(hash, graph.nodes.size());
    for (const auto& node : graph.nodes) {
        hash_u64(hash, node.id);
        hash_u64(hash, node.region_id);
        hash_u64(hash, node.dependencies.size());
        for (const auto dependency : node.dependencies) {
            hash_u64(hash, dependency);
        }
        hash_u64(hash, node.command.index());
        std::visit(
            [&](const auto& command) {
                using Type = std::decay_t<decltype(command)>;
                if constexpr (std::is_same_v<Type, DispatchStage>) {
                    hash_enum(hash, command.stage);
                    hash_u64(hash, command.work.index());
                    std::visit(
                        [&](const auto& work) {
                            using Work =
                                std::decay_t<decltype(work)>;
                            if constexpr (
                                std::is_same_v<Work, DirectWork>) {
                                for (const auto extent :
                                     work.item_extent) {
                                    hash_u64(hash, extent);
                                }
                                for (const auto size :
                                     work.group_size) {
                                    hash_u64(hash, size);
                                }
                            } else if constexpr (
                                std::is_same_v<Work, ChunkedWork>) {
                                hash_u64(hash, work.region_id);
                                hash_u64(
                                    hash,
                                    work.total_item_count);
                                hash_u64(hash, work.chunk_size);
                                hash_u64(hash, work.group_size);
                            } else {
                                hash_u64(hash, work.queue_id);
                                hash_u64(hash, work.producer_node);
                                hash_u64(
                                    hash,
                                    work.initial_producer_node);
                                hash_enum(hash, work.relation);
                                hash_resource(hash, work.arguments);
                                hash_u64(hash, work.argument_offset);
                                hash_u64(hash, work.command_stride);
                            }
                        },
                        command.work);
                    hash_u64(hash, command.resources.size());
                    for (const auto& item : command.resources) {
                        hash_resource(hash, item.resource);
                        hash_enum(hash, item.mode);
                    }
                    hash_u64(hash, command.estimator_critical);
                    hash_enum(hash, command.iteration);
                } else if constexpr (
                    std::is_same_v<Type, QueueStage>) {
                    hash_enum(hash, command.operation);
                    hash_u64(hash, command.queue_id);
                    hash_u64(
                        hash,
                        command.paired_queue_id.has_value());
                    hash_u64(
                        hash,
                        command.paired_queue_id.value_or(0));
                } else if constexpr (
                    std::is_same_v<Type, BarrierStage>) {
                    hash_u64(hash, command.resources.size());
                    for (const auto resource : command.resources) {
                        hash_resource(hash, resource);
                    }
                } else if constexpr (
                    std::is_same_v<Type, ClearStage>) {
                    hash_resource(hash, command.resource);
                    hash_u64(hash, command.offset);
                    hash_u64(hash, command.size_bytes);
                    hash_u64(hash, command.whole_resource);
                } else if constexpr (
                    std::is_same_v<Type, AsyncTransferStage>) {
                    hash_enum(hash, command.kind);
                    hash_resource(hash, command.source);
                    hash_resource(hash, command.destination);
                    hash_u64(hash, command.size_bytes);
                    hash_u64(hash, command.whole_resource);
                } else if constexpr (
                    std::is_same_v<Type, HostStage>) {
                    hash_enum(hash, command.operation);
                    hash_u64(hash, command.resources.size());
                    for (const auto& item : command.resources) {
                        hash_resource(hash, item.resource);
                        hash_enum(hash, item.mode);
                    }
                } else if constexpr (
                    std::is_same_v<Type, StateStage>) {
                    hash_enum(hash, command.operation);
                    hash_resource(hash, command.state);
                    hash_u64(hash, command.initial_value);
                    hash_u64(hash, command.increment);
                    hash_u64(hash, command.modulo);
                } else {
                    hash_enum(hash, command.domain);
                    hash_enum(hash, command.action);
                    hash_u64(hash, command.epoch);
                }
            },
            node.command);
    }
    hash_u64(hash, graph.estimator.version);
    hash_enum(hash, graph.estimator.mode);
    hash_u64(hash, graph.estimator.pdf.spectral_sampling);
    hash_u64(hash, graph.estimator.pdf.scattering_solid_angle);
    hash_u64(hash, graph.estimator.pdf.medium_phase_solid_angle);
    hash_u64(hash, graph.estimator.pdf.light_selection);
    hash_u64(hash, graph.estimator.pdf.restir_target);
    hash_u64(
        hash,
        graph.estimator.pdf.technique_support_partition);
    hash_u64(hash, graph.estimator.pdf.mlt_primary_sampling);
    hash_u64(hash, graph.estimator.ordered_nodes.size());
    for (const auto node : graph.estimator.ordered_nodes) {
        hash_u64(hash, node);
    }
    return hash;
}

}
