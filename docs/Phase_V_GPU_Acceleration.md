# Phase V — GPU Geometry Acceleration

## Status

V.0 through V.2 are complete and the authoritative cursor is V.3. This
document records the initial acceleration audit, configuration contract,
self-compute correctness baseline and the boundaries that later Phase V work
must preserve. It does not describe the current OptiX, Vulkan RT or DXR bridges
as general production acceleration providers.

## Current production path

The complete renderer still uses the private CUDA self-compute path.
`MeshBvhBuilder` builds one object-space binary BVH per mesh during scene
upload. The loader uploads the reordered triangle index buffer and a 32-byte
`GpuBvhNode` array. Closest-hit and any-hit CUDA routines traverse that array
directly. Instances are scanned linearly, tested against their world-space
bounds and then transformed into the referenced mesh BVH. There is no
production TLAS.

The Phase T `AccelerationProvider` contract is the forward boundary. Vulkan and
D3D12 implement bounded triangle/instance fixtures through native ray query or
compute fallback, but the current public renderer does not lower arbitrary
SceneIR to either backend. Phase V must extend the common provider contract; it
must not make the CUDA layout, a legacy host accelerator or an SDK-native handle
the new public model.

## Audit ledger

| ID | Observed implementation | Risk | Owner |
|---|---|---|---|
| V0-PROD | Scene upload calls `MeshBvhBuilder` separately for each mesh and retains raw CUDA vertex/index/node allocations in the private resource registry. | Build policy, statistics and native ownership are CUDA-private and cannot define the portable API. | V.1-V.5 |
| V0-BLD | The builder chooses the largest centroid extent, partitions at its midpoint, falls back to `nth_element` when one side is empty, emits binary preorder nodes and uses leaves of at most four triangles. | This is not SAH/SBVH/LBVH despite a legacy public header claiming “SAH or midpoint”; build quality and degeneracy behavior are unmeasured. | V.2/V.4 |
| V0-TRV | Closest-hit and an unused duplicate any-hit helper each used `int stack[64]`, pushed both children without near/far ordering and exposed no overflow metric. The production shadow kernel actually called `world_hit`, so it shared closest traversal; the duplicate helper made the boundary easy to mis-audit or misuse. | The active traversal could write past the stack or drop work; duplicate visibility code invited future semantic divergence. | V.2 |
| V0-INS | Closest-hit and the production shadow kernel scan every instance and then traverse its mesh BVH. A dead `any_hit` helper omitted `scene.instance_count`. | No TLAS exists and instance cost remains linear; the dead divergent path needed removal before it could become a consumer. | V.2/V.3 |
| V0-LIN | A mesh with no BVH nodes falls back to a full triangle scan in closest-hit and shadow paths. | Missing or invalid acceleration can silently become O(N); fallback policy has no explicit configuration, reason or telemetry. | V.2 |
| V0-UPD | Retained instance descriptors/transforms support hot updates, but no top-level structure is refitted or rebuilt. | Dynamic transforms cannot provide predictable acceleration update cost. | V.3/V.10 |
| V0-HOST | `BVHAccelerator` is a recursive host traversal compiled into `ure_core`; `SimpleAccelerator` and an Embree placeholder remain in installed `ure_types` headers. Repository search finds no renderer/session/CLI consumer. | Extending these classes would create the forbidden second host production traversal path and split hit semantics from GPU providers. | Remove or quarantine in V.2 |
| V0-OPT | Identical installed `OptixAccelerator` placeholders exist in `ure_types` and `ure_core`; build/update are empty, closest-hit always misses and occlusion always returns false. No OptiX SDK target or pipeline exists. | The class name can be mistaken for a functional provider even though using it would produce incorrect visibility. | Freeze until replacement/removal in V.6 |
| V0-API | Phase T exposes SDK-free bounded geometry, instance, ray/hit and capability contracts, but no `AccelerationConfig`, quality preset, update policy, build statistics or scratch/compaction budget. | Provider selection and operational policy cannot yet be expressed consistently through config, ABI, Session or pyure. | V.1 |
| V0-VAL | Existing gates cover a small indexed quad, two transformed instances and native/compute hit metadata parity. | Deep stack, degenerate triangles, shadow instances, dynamic updates, large meshes, build time, trace rate and peak memory are not yet covered. | V.2-V.11 |

## Frozen boundaries

- CUDA `GpuBvhNode`, raw device pointers and traversal functions remain private.
- `runtime::AccelerationProvider`, stable descriptors and hit metadata own the
  portable boundary.
- The host `BVHAccelerator`, `SimpleAccelerator`, Embree placeholder and
  `OptixAccelerator` placeholders are legacy or nonfunctional inventory. They
  may be removed or replaced by their assigned Phase V steps, but may not gain
  production consumers.
- Linear triangle traversal is a diagnosed compatibility path, not an accepted
  silent fallback policy.
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
| provider | `auto`, `self_compute`, `optix`, `vulkan_rt`, `dxr` | `auto` resolves to CUDA `self_compute`; explicit `self_compute` is accepted; native providers reject until V.6 |
| quality | `auto`, `fast_build`, `balanced`, `high_quality` | only `auto`; presets become executable in V.4 |
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
