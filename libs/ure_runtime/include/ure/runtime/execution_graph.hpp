#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include "ure/render_config.hpp"
#include "ure/resource_types.hpp"

namespace ure::runtime {

inline constexpr std::uint32_t kExecutionGraphSchemaVersion = 1;
inline constexpr std::uint32_t kEstimatorContractVersion = 1;

enum class ExecutionKind : std::uint8_t {
    PathTracing,
    Metropolis,
    WaveOperator
};

enum class RegionKind : std::uint8_t {
    Pass,
    SampleLoop,
    CandidateLoop,
    DepthLoop,
    BootstrapLoop,
    MutationLoop,
    ManifoldRootLoop
};

enum class RepeatKind : std::uint8_t {
    Once,
    FixedCount,
    UntilQueueEmpty
};

struct ExecutionRegion {
    std::uint32_t id = 0;
    std::uint32_t parent_id = 0;
    RegionKind kind = RegionKind::Pass;
    RepeatKind repeat = RepeatKind::Once;
    std::uint64_t repeat_count = 1;
    std::uint32_t maximum_iterations = 1;
    std::optional<std::uint32_t> termination_queue;
    std::optional<std::uint32_t> initial_count_producer;
    std::optional<std::uint32_t> iteration_count_producer;

    bool operator==(const ExecutionRegion&) const = default;
};

enum class QueueRole : std::uint8_t {
    RayCurrent,
    RayNext,
    Hit,
    Shadow,
    MltPathCurrent,
    ManifoldRoot,
    MltPathNext
};

struct QueueContract {
    std::uint32_t id = 0;
    QueueRole role = QueueRole::RayCurrent;
    resource::ResourceId payload;
    resource::ResourceId active_count;
    resource::ResourceId indirect_arguments;

    bool operator==(const QueueContract&) const = default;
};

enum class AccessMode : std::uint8_t {
    Read,
    Write,
    ReadWrite,
    IndirectRead,
    TransferRead,
    TransferWrite
};

struct ResourceAccess {
    resource::ResourceId resource;
    AccessMode mode = AccessMode::Read;

    bool operator==(const ResourceAccess&) const = default;
};

struct DirectWork {
    std::array<std::uint64_t, 3> item_extent = {1, 1, 1};
    std::array<std::uint32_t, 3> group_size = {1, 1, 1};

    bool operator==(const DirectWork&) const = default;
};

struct ChunkedWork {
    std::uint32_t region_id = 0;
    std::uint64_t total_item_count = 0;
    std::uint64_t chunk_size = 0;
    std::uint32_t group_size = 1;

    bool operator==(const ChunkedWork&) const = default;
};

enum class CountRelation : std::uint8_t {
    SameIteration,
    PreviousIteration
};

struct IndirectQueueWork {
    std::uint32_t queue_id = 0;
    std::uint32_t producer_node = 0;
    std::uint32_t initial_producer_node = 0;
    CountRelation relation = CountRelation::SameIteration;
    resource::ResourceId arguments;
    std::uint64_t argument_offset = 0;
    std::uint32_t command_stride = 12;

    bool operator==(const IndirectQueueWork&) const = default;
};

using WorkSource =
    std::variant<DirectWork, ChunkedWork, IndirectQueueWork>;

enum class StageKind : std::uint16_t {
    PathGuidingLightDecay,
    PathGuidingSpatialDecay,
    LightSubpathGenerate,
    LightSubpathExtend,
    VcmSurfaceGridBuild,
    VcmVolumeGridBuild,
    RayGenerate,
    Intersect,
    RestirPtCandidatePrepare,
    RestirDiResample,
    Shade,
    ShadowIntersect,
    RestirPtCandidateStream,
    RestirPtFinalize,
    BidirectionalConnect,
    VcmSurfaceMerge,
    VcmVolumeMerge,
    ManifoldTargetGenerate,
    ManifoldRootInitialize,
    ManifoldRootAdvance,
    ManifoldWeightAssign,
    ManifoldContributionEvaluate,
    ManifoldContributionConvert,
    TechniqueContributionCommit,
    MltPrimaryInitialize,
    MltRayGenerate,
    MltBootstrapCollect,
    MltChainSeed,
    MltMutate,
    MltAcceptDeposit,
    MltSampleCountCommit,
    WavePropagate
};

enum class IterationPredicate : std::uint8_t {
    All,
    First,
    Last
};

struct DispatchStage {
    StageKind stage = StageKind::RayGenerate;
    WorkSource work;
    std::vector<ResourceAccess> resources;
    bool estimator_critical = true;
    IterationPredicate iteration = IterationPredicate::All;

    bool operator==(const DispatchStage&) const = default;
};

enum class QueueOperation : std::uint8_t {
    Reset,
    Swap
};

struct QueueStage {
    QueueOperation operation = QueueOperation::Reset;
    std::uint32_t queue_id = 0;
    std::optional<std::uint32_t> paired_queue_id;

    bool operator==(const QueueStage&) const = default;
};

struct BarrierStage {
    std::vector<resource::ResourceId> resources;

    bool operator==(const BarrierStage&) const = default;
};

struct ClearStage {
    resource::ResourceId resource;
    std::uint64_t offset = 0;
    std::uint64_t size_bytes = 0;
    bool whole_resource = false;

    bool operator==(const ClearStage&) const = default;
};

enum class TransferKind : std::uint8_t {
    Upload,
    Readback,
    DeviceToDevice
};

struct AsyncTransferStage {
    TransferKind kind = TransferKind::Upload;
    resource::ResourceId source;
    resource::ResourceId destination;
    std::uint64_t size_bytes = 0;
    bool whole_resource = false;

    bool operator==(const AsyncTransferStage&) const = default;
};

enum class HostOperation : std::uint8_t {
    MltBootstrapNormalizeCdf
};

struct HostStage {
    HostOperation operation =
        HostOperation::MltBootstrapNormalizeCdf;
    std::vector<ResourceAccess> resources;

    bool operator==(const HostStage&) const = default;
};

enum class StateOperation : std::uint8_t {
    RestirDIReservoirSwap,
    RestirPTReservoirSwap,
    VcmRadiusAdvance,
    MltMutationAdvance,
    SampleCountAdvance
};

struct StateStage {
    StateOperation operation =
        StateOperation::SampleCountAdvance;
    resource::ResourceId state;
    std::uint64_t initial_value = 0;
    std::uint64_t increment = 0;
    std::uint64_t modulo = 0;

    bool operator==(const StateStage&) const = default;
};

enum class BoundaryAction : std::uint8_t {
    Begin,
    End
};

enum class EpochDomain : std::uint8_t {
    Pass,
    PathGuiding,
    RestirDI,
    RestirPT,
    Bidirectional,
    Mlt,
    Wave
};

struct BoundaryStage {
    EpochDomain domain = EpochDomain::Pass;
    BoundaryAction action = BoundaryAction::Begin;
    std::uint64_t epoch = 0;

    bool operator==(const BoundaryStage&) const = default;
};

using ExecutionCommand = std::variant<
    DispatchStage,
    QueueStage,
    BarrierStage,
    ClearStage,
    AsyncTransferStage,
    HostStage,
    StateStage,
    BoundaryStage>;

struct ExecutionNode {
    std::uint32_t id = 0;
    std::uint32_t region_id = 0;
    std::vector<std::uint32_t> dependencies;
    ExecutionCommand command;

    bool operator==(const ExecutionNode&) const = default;
};

struct PdfSemanticContract {
    std::uint32_t spectral_sampling = 1;
    std::uint32_t scattering_solid_angle = 1;
    std::uint32_t medium_phase_solid_angle = 1;
    std::uint32_t light_selection = 1;
    std::uint32_t restir_target = 1;
    std::uint32_t technique_support_partition = 1;
    std::uint32_t mlt_primary_sampling = 1;

    bool operator==(const PdfSemanticContract&) const = default;
};

struct EstimatorContract {
    std::uint32_t version = kEstimatorContractVersion;
    IntegratorMode mode = IntegratorMode::Wavefront;
    PdfSemanticContract pdf;
    std::vector<std::uint32_t> ordered_nodes;

    bool operator==(const EstimatorContract&) const = default;
};

struct ExecutionGraph {
    std::uint32_t schema_version = kExecutionGraphSchemaVersion;
    ExecutionKind kind = ExecutionKind::PathTracing;
    std::vector<ExecutionRegion> regions;
    std::vector<QueueContract> queues;
    std::vector<ExecutionNode> nodes;
    EstimatorContract estimator;

    bool operator==(const ExecutionGraph&) const = default;
};

struct PathExecutionConfig {
    RenderConfig render;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t primary_ray_count = 0;
    std::uint64_t queue_capacity = 0;
    std::uint64_t path_guiding_light_count = 0;
    std::uint64_t path_guiding_spatial_entry_count = 0;
    std::uint64_t mlt_primary_dimension_count = 0;
    std::uint32_t samples_per_pass = 1;
    bool path_guiding_decay_due = false;
    bool mlt_initialized = false;
    std::uint64_t pass_epoch = 0;
    std::uint64_t guiding_epoch = 0;
    std::uint64_t restir_di_epoch = 0;
    std::uint64_t restir_pt_epoch = 0;
    std::uint32_t restir_di_input_index = 0;
    std::uint32_t restir_pt_input_index = 0;
    std::uint64_t bidirectional_epoch = 0;
    std::uint64_t vcm_radius_iteration = 0;
    std::uint64_t mlt_epoch = 0;
};

struct WaveExecutionConfig {
    std::uint64_t sample_count = 0;
    std::uint64_t input_bytes = 0;
    std::uint64_t output_bytes = 0;
    std::uint32_t group_size = 128;
    std::uint64_t pass_epoch = 0;

    bool operator==(const WaveExecutionConfig&) const = default;
};

using ExecutionFingerprint = std::array<std::uint64_t, 4>;

resource::ResourceId semantic_resource(std::uint64_t local_id);
ExecutionGraph make_path_execution_graph(const PathExecutionConfig& config);
ExecutionGraph make_wave_execution_graph(const WaveExecutionConfig& config);
void validate(const ExecutionGraph& graph);
ExecutionFingerprint execution_fingerprint(const ExecutionGraph& graph);

}
