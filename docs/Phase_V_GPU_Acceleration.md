# Phase V — GPU Geometry Acceleration

## Status

V.0 through V.7 are complete and the authoritative cursor is V.8. This
document records the initial acceleration audit, configuration contract,
self-compute construction, optional native-provider lifecycle and the
cross-provider traversal contract. Clustered geometry resources are V.8.

## Current production path

The complete renderer still uses the private CUDA self-compute path.
`MeshBvhBuilder` builds one object-space BLAS per mesh during scene upload.
Automatic and fast-build quality retain the compatible reordered median BVH2.
Balanced builds binned object SAH and a quantized compact BVH4; high-quality
adds budgeted spatial splits and emits quantized BVH8 while leaves preserve
original primitive identity through an immutable reference array. A separate
world-space binary TLAS stores stable instance ordinals and bounds; closest-hit
and production shadow traversal visit it before transforming candidate rays
into the referenced mesh BLAS. Instance transforms can refit the retained TLAS
topology without rebuilding or uploading static mesh BLAS data.

The Phase T `AccelerationProvider` contract is the forward boundary. Vulkan RT
and DXR now implement multi-BLAS/TLAS construction, compaction, transform
refit/rebuild, scratch budgets and build statistics. OptiX implements the same
construction contract when its separately installed SDK is available. A
canonical SceneIR fixture now lowers to CUDA self-compute, OptiX, Vulkan RT and
DXR and freezes traversal/hit/AOV semantics. The current public renderer still
does not lower an arbitrary SceneIR radiometric integrator to native providers;
V.7 does not claim that broader renderer migration.

## Audit ledger

| ID | Observed implementation | Risk | Owner |
|---|---|---|---|
| V0-PROD | Scene upload calls `MeshBvhBuilder` separately for each mesh and retains raw CUDA vertex/index/node allocations in the private resource registry. | Build policy, statistics and native ownership are CUDA-private and cannot define the portable API. | V.1-V.5 |
| V0-BLD | The original builder chooses the largest centroid extent, partitions at its midpoint, falls back to `nth_element` when one side is empty, emits binary preorder nodes and uses leaves of at most four triangles. V.4 retains it as auto/fast-build and adds measured SAH/SBVH wide presets. | Closed for V.4 quality construction; async construction memory and compaction remain separate. | V.2/V.4/V.5 |
| V0-TRV | Closest-hit and an unused duplicate any-hit helper each used `int stack[64]`, pushed both children without near/far ordering and exposed no overflow metric. The production shadow kernel actually called `world_hit`, so it shared closest traversal; the duplicate helper made the boundary easy to mis-audit or misuse. | The active traversal could write past the stack or drop work; duplicate visibility code invited future semantic divergence. | V.2 |
| V0-INS | V.0 found a linear instance scan and a dead divergent `any_hit`; V.3 replaced the active scan with a shared checked instance TLAS and V.2 removed the dead helper. | Closed for the self-compute baseline; quality and wide-node optimization remain V.4. | V.2/V.3 |
| V0-LIN | A mesh with no BVH nodes falls back to a full triangle scan in closest-hit and shadow paths. | Missing or invalid acceleration can silently become O(N); fallback policy has no explicit configuration, reason or telemetry. | V.2 |
| V0-UPD | V.0 found transform hot updates without top-level acceleration maintenance; V.3 now refits retained TLAS topology and uploads only transforms and TLAS nodes. | Rigid transform refit is closed; deformation and topology classification remain V.10. | V.3/V.10 |
| V0-HOST | `BVHAccelerator` is a recursive host traversal compiled into `ure_core`; `SimpleAccelerator` and an Embree placeholder remain in installed `ure_types` headers. Repository search finds no renderer/session/CLI consumer. | Extending these classes would create the forbidden second host production traversal path and split hit semantics from GPU providers. | Remove or quarantine in V.2 |
| V0-OPT | V.0 found two installed `OptixAccelerator` placeholders whose build/update were empty and whose queries always missed. V.6 removed both and added one optional SDK-backed provider; V.7 adds an actual OptiX IR raygen/miss/closest-hit pipeline. | Closed for native construction and the canonical parity fixture; arbitrary-scene integrator lowering remains separate. | V.6/V.7 |
| V0-API | Phase T exposes SDK-free bounded geometry, instance, ray/hit and capability contracts, but no `AccelerationConfig`, quality preset, update policy, build statistics or scratch/compaction budget. | Provider selection and operational policy cannot yet be expressed consistently through config, ABI, Session or pyure. | V.1 |
| V0-VAL | V.2-V.4 now cover robust/deep traversal, transformed instances, dynamic TLAS refit and a fixed large-mesh build/trace/memory benchmark. | Native-provider parity, dense geometry and full-suite aggregation remain later work. | V.2-V.11 |

## Frozen boundaries

- CUDA `GpuBvhNode`, raw device pointers and traversal functions remain private.
- `runtime::AccelerationProvider`, stable descriptors and hit metadata own the
  portable boundary.
- The host `BVHAccelerator`, `SimpleAccelerator` and Embree placeholder remain
  legacy inventory and may not gain production consumers. The false OptiX
  placeholders have been removed.
- Linear triangle or instance fallback is prohibited in the self-compute
  production path.
- Phase V may change acceleration construction and traversal, but not
  radiometric, spectral, polarization, BSDF, phase-function or estimator
  semantics.
- A provider may advertise only paths it executes. Missing SDKs or hardware
  capabilities must leave other providers buildable and must reject or use an
  explicitly allowed compatible fallback.

## Migration sequence

1. V.1 adds backend-neutral acceleration configuration, quality, update,
   clustering and statistics requests across JSON, CLI, C ABI, Session and
   pyure.
2. V.2 closes current CUDA self-compute correctness gaps, makes stack and
   linear-fallback policy fail-loud, and establishes robust closest/any-hit
   tests and statistics.
3. V.3 separates BLAS from TLAS and connects transform mutation to top-level
   update policy.
4. V.4 adds measured SAH/SBVH and wide-node quality presets.
5. V.5 adds asynchronous build/upload, scratch budgeting, compaction and
   telemetry.
6. V.6 productionizes optional OptiX, Vulkan RT and DXR providers behind the
   same contract.
7. V.7 freezes cross-provider closest-hit, shadow, transform, material and
   interpolation parity.
8. V.8-V.10 add clustered geometry, physically constrained LoD and dynamic
   geometry classification.
9. V.11 aggregates correctness, build/update time, trace throughput, memory
   and distributed provenance evidence.

## V.0 gate

`scripts/check_phase_v_static.ps1` freezes the audited legacy consumer set,
requires the diagnosed CUDA builder/traversal/fallback signatures, prevents the
OptiX placeholders from being described as production and verifies that Phase
V documentation and the authoritative cursor remain aligned. This is a
regression boundary, not evidence that the diagnosed implementation is already
correct.

## V.1 configuration contract

The backend-neutral C++ `AccelerationConfig` contains three independent policy
axes:

| Field | Vocabulary | V.1 executable boundary |
|---|---|---|
| provider | `auto`, `self_compute`, `optix`, `vulkan_rt`, `dxr` | `auto` resolves to CUDA `self_compute`; native construction and canonical traversal parity are complete, while arbitrary-scene native integrator selection remains fail-loud |
| quality | `auto`, `fast_build`, `balanced`, `high_quality` | all four execute on CUDA self-compute since V.4 |
| update policy | `auto`, `static`, `refit`, `rebuild` | `auto` and `static`; refit/rebuild become executable with V.3/V.6/V.10 |
| clustered geometry | enabled/disabled | disabled until V.8 |
| statistics | enabled/disabled | executable on CUDA self-compute since V.2 |
| scratch budget | bytes or MiB at input surfaces | zero/derived until V.5 |

JSON uses an `acceleration` object. CLI exposes provider, quality, update,
cluster, stats and scratch-budget flags. The C ABI adds separate
`ure_acceleration_config_t` and execution-config creation functions instead of
growing the existing backend structure, so callers compiled against the prior
layout remain safe. Pyure mirrors the enums and creation arguments.

Selection validates provider/backend/capability compatibility before the
renderer or session is created. Vocabulary reserved for later Phase V steps is
therefore representable but never silently downgraded. Default construction
continues to select the existing CUDA self-compute renderer.

Host config tests cover JSON/CLI propagation, CUDA tests cover vocabulary and
selection rejection, and C ABI/pyure tests cover versioned entry points plus
unsupported requests. The V.1 closure passed the Release build and all 48
registered CTest entries without changing traversal kernels or estimator
behavior.

## V.2 self-compute correctness baseline

The CUDA reference builder now validates packed vertex/index shape, finite
coordinates, index bounds and device-index count before constructing a BVH. It
returns aggregate mesh, triangle, node, leaf and depth statistics and refuses a
tree whose depth exceeds the traversal stack contract. Leaf offsets are
explicitly triangle ordinals into the reordered index buffer.

The active closest-hit and production shadow paths share one checked traversal.
Every node, child and leaf range is validated before dereference. A two-slot
capacity check occurs before either child is pushed, returning a typed
`StackOverflow` result instead of writing out of bounds or silently dropping
work. Nonempty meshes without valid BVH data return `InvalidAcceleration`; the
former O(N) triangle fallback is removed. The unused divergent `any_hit`
implementation was deleted.

AABB slabs handle zero direction components without producing NaN and reject
invalid bounds. Triangle intersection uses a geometry-scale-aware determinant
threshold so tiny valid triangles remain hittable while degenerate geometry is
rejected. The normal-size numerical path remains unchanged, preserving both
Cornell reference hashes exactly.

`AccelerationStats` exposes build and traversal telemetry through C++, the C ABI
and pyure. Node and triangle counting is enabled only when statistics are
requested, while overflow and invalid-acceleration detection remain mandatory
and abort the pass regardless of that setting. GPU gates cover builder
validation, transformed-instance closest/shadow parity, robust AABB and triangle
cases, stack overflow and missing acceleration.

## V.3 TLAS and BLAS separation

Each static mesh retains its object-space BLAS. `InstanceTlasBuilder` constructs
a separate world-space binary TLAS over validated instance transforms and
bounds, with leaves referring through a stable instance-index permutation
rather than reordering public instance identity. The active closest-hit and
production shadow paths share checked TLAS traversal before entering the
selected mesh BLAS; there is no linear instance fallback.

Transform mutation validates finite affine matrices and inverse consistency,
derives conservative world bounds from the referenced BLAS bounds and all eight
transformed corners, refits the retained TLAS topology on the host construction
path, and uploads only the new transform array and TLAS nodes. Caller-supplied
bounds therefore cannot create false-negative culling. The device BLAS
allocations and TLAS allocation remain stable. Automatic and explicit `refit`
policy execute this path; `static` rejects mutation and the not-yet-implemented
`rebuild` policy remains fail-loud.

Scene compilation normalizes input quaternions before producing the paired
forward/inverse matrices. This closes a pre-existing case where a non-unit
authoring quaternion could produce an internally inconsistent inverse and is
covered by the instance update gate.

The C++ `AccelerationStats` now distinguishes BLAS bytes, TLAS nodes, leaves,
depth, resident bytes, construction/update time and TLAS traversal visits.
Versioned C ABI v2 getters extend telemetry without enlarging the V.2 output
structure, and pyure consumes that versioned surface. Multi-instance gates
exercise a multi-level TLAS, closest/shadow transformed hit parity, stable
instance identity, topology-preserving refit, root-bound updates and unchanged
BLAS pointers.

## V.4 measured quality presets

Automatic keeps the V.2 reference path unchanged. Fast-build names the same
midpoint/median BVH2 construction explicitly. Balanced performs 16-bin object
SAH construction and collapses the binary build topology into a 72-byte BVH4.
High-quality evaluates object and spatial SAH candidates, clips reference
bounds at selected planes, limits duplicated primitive references to 50% of the
source primitive count and collapses the result into a 116-byte BVH8. Wide
leaves carry stable original triangle ordinals, so spatial duplication does not
alter SceneIR geometry, material, UV, normal or light identity.

Child bounds use conservative 8-bit quantization relative to each wide node.
Host construction verifies the conservative traversal-stack bound before
upload. Device traversal checks node, child and primitive-reference ranges,
uses the same closest/shadow hit implementation as BVH2 and reports typed stack
or invalid-data failures. The default automatic layout and reference render
remain unchanged.

The fixed 96-by-96 wavy grid gate contains 18,432 triangles and 4,096
deterministic rays. It records host build milliseconds, CUDA traversal
milliseconds, resident node/reference bytes, node visits and triangle tests for
fast BVH2, balanced BVH4 and high-quality BVH8, then compares every hit distance
against the BVH2 reference. A separate crossed-sliver fixture requires an
actual spatial split and duplicated reference. The invariant memory results are
393,184 bytes for BVH2, 305,136 bytes for BVH4 and 368,948 bytes for BVH8;
wide traversal reduces node visits and triangle tests on the fixed large mesh.
Timing is reported rather than hidden or converted into a universal speed
claim; later occupancy and traversal tuning belongs to Phase K.

`AccelerationStats` now includes BLAS build time, primitive-reference count,
spatial-split count, pre-collapse binary-node count and selected arity. C ABI
v3 extends the frozen v2 hierarchy layout, and pyure consumes v3.

## V.5 asynchronous construction and memory budgets

Mesh BLAS construction now runs through a bounded host batch. The automatic
policy permits two concurrent builds; an explicit scratch budget permits up to
the host hardware concurrency while deterministic batch packing keeps the sum
of conservative per-build temporary reservations within that budget. A mesh
whose individual reservation exceeds the request is rejected before renderer
allocation. The reservation includes builder topology, primitive working sets
and SAH/SBVH working storage; it is telemetry and a fail-loud upper bound, not
resident acceleration memory.

The builder records canonical binary bytes before wide collapse, final compact
node/reference bytes and compaction time. Only final nodes and immutable
primitive references receive device allocations. Final compact BLAS bytes plus
a conservative TLAS bound are checked against the selected backend budget and
current device availability before acceleration allocation.

BLAS and TLAS transfers use the runtime-owned CUDA transfer stream. A two-entry
pinned staging queue overlaps host staging with H2D work, retires completed
events before reusing its scratch allowance and synchronizes before any
acceleration consumer can execute. Upload bytes, GPU elapsed upload time and
peak pinned/build temporary memory are part of `AccelerationStats`.

C ABI v4 preserves the v3 hierarchy and adds build wall time, upload time and
bytes, temporary peak, uncompacted/compact bytes, compaction time and peak build
concurrency. pyure consumes v4. The reproducible
`tools/benchmarks/run_phase_v_build_telemetry.ps1` report records fixed-mesh
build/trace/compact-memory/VRAM data together with async pipeline telemetry and
the scratch-budget rejection gate.

## V.6 native RT provider construction

The SDK-free runtime contract now accepts multiple indexed-triangle geometries,
maps every instance to a stable geometry index and carries build quality,
static/refit/rebuild policy, compaction and scratch budget. Provider-owned
statistics report geometry and instance counts, wall-clock build/update time,
scratch peak, uncompacted and compact bytes, and refit/rebuild counts. Instance
updates preserve topology: count, instance identity, material binding and
geometry binding cannot change through the transform-only update entry point.

Vulkan RT builds one BLAS per geometry, reuses a bounded scratch allocation,
queries compact sizes, copies beneficial compact BLAS results and only then
builds the TLAS against final BLAS addresses. TLAS construction retains update
capacity and executes either `UPDATE` refit or an explicit rebuild. DXR follows
the same lifecycle with post-build compact-size data, compact AS copies,
`ALLOW_UPDATE`/`PERFORM_UPDATE`, and a shared peak scratch allocation.

OptiX is an optional CUDA-side construction provider. CMake enables it only
when `optix.h` is found under `UR_OPTIX_ROOT`, `OPTIX_ROOT` or
`OptiX_INSTALL_DIR`; an absent SDK leaves CUDA self-compute, Vulkan and D3D12
unchanged and produces a deterministic `Unsupported` boundary. Its source uses
`optixAccelBuild`, compact-size emission, `optixAccelCompact` and IAS
update/rebuild. The implementation was also configured and executed against
the official NVIDIA `optix-dev` v8.1.0 headers at commit
`50021ea0af6d41609a97777ceebbdf1e1d34efe7`: the registered CUDA runtime gate
passed two-GAS compaction, IAS refit, memory cleanup and one-byte scratch
rejection on the closure GPU.

Vulkan and D3D12 lifecycle tests execute two BLAS, compact memory accounting,
TLAS refit, TLAS rebuild, input lifetime and one-byte scratch rejection on
available native adapters. Compute-only adapters retain the explicit compute
fallback, while a native request with fallback disabled remains fail-loud.
V.6 does not claim framebuffer or complete hit-attribute parity; that is the
V.7 gate. `scripts/run_phase_v6_native_provider_gate.ps1` can accept an
`-OptixRoot`, requires physical Vulkan RT and DXR on the closure machine, and
writes `ure.phase_v.native_provider.v1` evidence.

## V.7 cross-provider traversal parity

`tests/shared/acceleration_parity_fixture.hpp` owns one canonical SceneIR
fixture and its deterministic lowering. It contains one indexed quad, two
instances with different material identities and non-uniform transforms, four
closest-hit or miss queries and one shadow query. CUDA self-compute, OptiX,
Vulkan RT and DXR consume that same source instead of maintaining independent
hand-authored fixtures.

The SDK-free `AccelerationHit` record now carries a 16-byte
tangent/handedness field in addition to position/distance, geometric and
shading normals, UV/barycentrics and material/instance/primitive identity.
Shared Slang compute and inline-ray-query kernels interpolate all attributes.
Tangents are transformed as vectors and Gram-Schmidt orthonormalized against
the shading normal; a deterministic axis fallback handles a degenerate input.
The compact validation AOV is `(u, v, abs(Nz), visibility)`. Shadow parity is
defined as first-hit visibility, so providers need not return an identical
primitive when more than one valid occluder exists.

OptiX now compiles `shaders/optix/phase_v7_acceleration.cu` to CUDA 13 OptiX
IR and creates actual raygen, miss and closest-hit program groups, pipeline,
stack contract and shader binding table without exposing an OptiX handle in an
installed header. Geometry SBT records retain per-geometry attributes while
instance metadata preserves stable material and instance identity. The V.6
two-GAS construction/refit/scratch test remains separate from the one-geometry
SceneIR parity scene.

`scripts/run_phase_v7_cross_provider_parity.ps1` writes
`ure.phase_v.cross_provider_parity.v1`. It records the fixture and executable
artifact hashes, absolute numerical thresholds and actual provider adapter,
driver and compiler identity. CUDA and Vulkan remain required on the closure
machine; OptiX and DXR become mandatory only when the configured SDK or
advertised hardware capability is available. This gate closes acceleration
traversal semantics, not arbitrary-scene BSDF, spectral, polarization or
integrator lowering on the native backends.
