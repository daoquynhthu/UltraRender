# Phase T — Portable GPU Runtime

## Status

T.0 through T.9 are complete and the authoritative cursor is T.10. This
document is the migration ledger for T.10 and T.11. CUDA remains the complete
scene rendering backend. Vulkan has a production compute-runtime foundation and
a bounded acceleration bridge on Windows and Linux. D3D12/DXR has an optional
Windows runtime and bounded acceleration bridge. Full SceneIR rendering is not
yet lowered to either portable backend.

## Audit method and boundary

The audit searched CMake, public headers, the C ABI and Python/CLI surfaces,
scene compilation, resources, queues, kernel launches, multi-GPU scheduling,
wave optics, diagnostics, and acceleration. The initial token-based inventory
found 86 coupled files, including tests and CUDA-private implementation. That
number is a discovery snapshot, not a completion metric: private CUDA source is
expected to remain, while backend-neutral contracts must stop exposing it.

The architectural boundary is:

```
SceneIR / MaterialIR / IntegratorIR / WaveIR / Session / C ABI
                              |
                 backend-neutral runtime contract
                              |
          CUDA backend | Vulkan backend | D3D12 backend
                              |
                  acceleration provider
```

Native handles, allocation pointers, launch syntax, streams, events, and SDK
error types belong below the runtime boundary. Physical estimator order,
spectral/PDF semantics, Stokes/Mueller transport, SceneIR, and distributed merge
identity belong above it.

## Coupling ledger

| ID | Coupling and current evidence | Contract owner | Migration batch |
|---|---|---|---|
| T0-BLD | Root project is C++-only until `UR_ENABLE_CUDA` enables the CUDA renderer. `UR_ENABLE_VULKAN` independently builds from pinned vendored headers/Volk, while Windows-only `UR_ENABLE_D3D12` uses the system D3D12/DXGI SDK. Either portable backend can be disabled without changing the other. | Build/backend registration | T.6/T.7/T.9 complete |
| T0-DEV | Backend-neutral adapter/capability identity owns public device semantics. CUDA, Vulkan and D3D12 enumeration stay backend-private. Vulkan and D3D12 inventories record native hardware capability, while executable providers advertise only implemented compute/ray-query paths. | Adapter/capability registry | T.1/T.6-T.9 complete |
| T0-API | `render.hpp`, Session, installed headers, C ABI and pyure contain no CUDA SDK type. Raw CUDA driver/scene/native structs are `.cuh` files under the non-installed `detail/` boundary. | Public runtime/session API | T.1-T.6 complete |
| T0-ABI | The C ABI is handle-opaque and carries backend identity/configuration parity; production creation selects CUDA through the neutral backend contract and unsupported backends fail loudly. | C ABI and language bindings | T.1/T.3 complete |
| T0-CTX | `GpuContext` and `MultiGpuContext` are backend-private. Each CUDA context owns a production runtime Device, queue, timeline fence, lowered-plan metrics and durable submission identity in addition to T.4 resource registries. | Runtime context and resource lifetime | T.3-T.6 complete |
| T0-RES | Stable ResourceId, typed buffer/image/spectral layouts, residency, sparse tiles and upload plans own public semantics. CUDA native resources remain private; Vulkan and D3D12 implement typed buffer/image/sampler descriptors behind SDK-neutral headers. | Resource/descriptor model | T.4/T.7/T.9 complete |
| T0-EXE | Stable execution graphs are validated and lowered against adapter limits before work. CUDA retains its private fast path; Vulkan and D3D12 record dependency-ordered command DAGs with native timelines/fences, transitions and typed descriptor binding. | Dispatch graph and backend executors | T.5-T.7/T.9 complete |
| T0-KRN | Estimator/PDF versions and critical stage order are backend-neutral. Existing optimized `.cu` bodies remain a private CUDA fast path; pinned Slang emits Vulkan SPIR-V directly and normalized HLSL consumed by pinned DXC for D3D12 DXIL. | Kernel toolchain and semantic library | T.2/T.5-T.9 complete |
| T0-MGPU | CUDA multi-GPU still uses private device ordinals and peer copies, but every child submits a compatible runtime schema/node/dispatch contract and fail-loud validation precedes merge. Heterogeneous negotiation remains T.10. | Multi-adapter scheduler | CUDA migration complete; T.10 heterogeneous scheduling |
| T0-WAVE | Fraunhofer CUDA execution and the Vulkan reference propagation operator consume shared wave semantics and runtime-owned resources/timelines; D3D12 foundation validates the same Mueller/Stokes semantic module. | Wave operator/runtime integration | T.2/T.5-T.7/T.9 complete |
| T0-ACC | SDK-free acceleration descriptors, selection policy, stable ray/hit layout and provider lifetime bridge bounded triangle BLAS/TLAS fixtures to Vulkan ray query and DXR inline ray query. Compute fallback and rejection are explicit. CUDA `world_hit` remains the production reference. | Acceleration-provider API | T.8/T.9 bridges complete; production construction/refit/compaction remains Phase V |
| T0-DIAG | Public diagnostics are SDK-free. CUDA, Vulkan and D3D12 map native failures to structured runtime errors; Vulkan retains validation messages and D3D12 retains DRED breadcrumbs/page-fault diagnostics. | Runtime error and diagnostics | T.3/T.6/T.7/T.9 complete |
| T0-SCN | SceneIR and native serialization contain no backend handle. `CompiledGpuScene`, CUDA native resource views and lowering state are private; runtime resource and execution identities define the portable boundary. | Scene compiler/lowering | T.3/T.4/T.6 complete |
| T0-TEST | SDK-free gates cover runtime/resources/execution/acceleration and installed public surfaces. CUDA production traversal, Vulkan native/fallback and D3D12 native/fallback share exact hit fixtures; no-D3D12 isolation preserves Vulkan execution. Full renderer parity remains T.11. | Validation architecture | T.1-T.9 complete; T.11 full parity |

## Migration order

1. T.1 introduces backend identity, adapter capabilities, limits, memory
   budgets, and compiler/driver identity across config, CLI, ABI, and pyure.
   CUDA remains the default complete scene-rendering backend.
2. T.2 selected Slang using real spectral, Mueller, queue,
   scattering, wave, and traversal prototypes. No source duplication decision is
   allowed before generated-code and debugging evidence exists.
3. T.3 establishes device, queue, synchronization, resource, module, pipeline,
   dispatch, diagnostics, and extensible descriptor interfaces with a host mock.
4. T.4 moves allocation and native handles behind typed descriptors and stable
   resource IDs. SceneIR and distributed metadata remain handle-free.
5. T.5 freezes estimator order and dependencies as an execution graph.
6. T.6 migrates the existing CUDA implementation behind the contracts before a
   second backend is added.
7. T.7 establishes Vulkan compute execution without claiming traversal.
8. T.8 extends that contract with the bounded Vulkan acceleration bridge
   without absorbing
   Phase V production acceleration construction.
9. T.9 adds the optional Windows D3D12/DXR runtime without claiming complete
   SceneIR rendering.
10. T.10 and T.11 add heterogeneous scheduling and full parity/performance
    evidence.

This order prevents a nominal abstraction from being designed around only
trivial buffers while the difficult texture, queue, estimator-state, wave, and
traversal contracts remain CUDA-owned.

## Static regression rules

`scripts/check_phase_t_static.ps1` is the T.0 gate. It enforces:

- no CUDA SDK include or native CUDA handle/type in `ure_types`,
  `ure_sceneio`, `ure_config`, `render_config.hpp`, `scene_ir.hpp`,
  `ure_c_api.h`, or pyure;
- no installed public project header may include `cuda_runtime.h`; the former
  CUDA include debt allowlist is empty;
- the C ABI remains free of `GpuContext`, `GpuScene`, `GpuMaterialData`, and
  `GpuTexture`;
- this ledger retains every coupling ID, owner, and migration batch;
- the PLAN cursor and T.0/T.1 dependency remain explicit.

CUDA SDK types may exist only in backend-private `.cu`/`.cuh` implementation
files and must not re-enter SDK-free modules or installed headers.

## T.0 completion evidence

- Coupling ledger covers build, public API, ABI/bindings, context, resources,
  execution, kernels, multi-GPU, wave optics, acceleration, diagnostics, scene
  lowering, and tests.
- Every coupling has a contract owner and migration batch.
- Static regression rules prevent new CUDA leakage into backend-neutral
  surfaces and freeze the existing public-header debt.
- No Vulkan/D3D12 implementation is claimed by this audit.

## T.1 backend identity and capability contract

The backend-neutral contract is owned by `ure_types/backend_types.hpp`.
`BackendKind`, stable adapter identity, feature bits, numeric limits, memory
capacity/budget, and driver/compiler identity contain no CUDA SDK type. The
CUDA implementation lives below that boundary in `backend_cuda.cu`.

Selection is deterministic and fail-loud:

- `Auto` resolves to CUDA, the current production default.
- `Cuda` accepts a stable UUID-derived adapter ID or an ordinal.
- Vulkan adapters are discoverable and report their acceleration capabilities.
  At the T.1 snapshot D3D12 was unavailable; T.9 adds optional Windows adapter
  inventory below. Full render selection remains rejected for both until
  complete SceneIR lowering exists.
- Unknown adapters, missing required features, invalid backend values, and
  memory budgets above current availability are rejected.
- Automatic memory budgeting reserves headroom against both current
  availability and physical capacity.

The same selection fields are available through `RenderConfig`, JSON, render
CLI options, the C ABI, and pyure. `list-devices` now consumes the neutral
adapter registry and reports the stable ID, memory, driver, and compiler
identity without importing the CUDA runtime in the CLI.

T.1 verification covers JSON and CLI precedence, CUDA adapter identity and
limits, explicit selection and rejection boundaries, C ABI-backed pyure
enumeration/session creation, and unsupported backend failure. The
T.1 closure advanced the cursor to T.2.

## T.2 portable kernel toolchain gate

Slang 2026.14 is pinned as the shared-source frontend after direct compilation
of six nontrivial prototypes to CUDA PTX/cubin, SPIR-V, and DXIL. The gate
validates deterministic artifacts and reflection, 64-bit layout, subgroup and
atomic lowering, specialization, debug/source mapping, PTX register/spill
evidence, CUDA occupancy, and actual CUDA numerical execution.

Restricted shared C++ was rejected because it lacks one validated
SPIR-V/DXIL frontend and reflection/debug contract. A custom URE KernelIR shader
compiler was rejected because owning three optimizer/code-generator/debugger
stacks would be avoidable compiler debt. T.5 subsequently defined only the
dispatch/execution contract and does not duplicate shader compiler
responsibilities.

The full decision, measurements, dependency boundary, and reproduction command
are recorded in `docs/Phase_T2_Kernel_Toolchain_Decision.md`. T.2 does not adopt
Slang RHI and does not migrate the production CUDA executor. Those boundaries
remain T.3-T.6.

## T.3 backend-neutral runtime contract

`ure_runtime` is a pure C++ library with no CUDA, Vulkan, or D3D12 SDK
dependency. It owns typed nonzero handles, descriptor and overflow validation,
device/queue/fence/event contracts, buffer/image/sampler/module/pipeline
creation, resource bindings, timeline submission, dispatch DAG validation,
structured error codes, and durable device-loss diagnostics.

Resource lifetime stays explicit: handles are device-owned, stale or foreign
handles fail, modules cannot be destroyed while pipelines depend on them, and
all offset/size calculations use subtraction-based bounds checks. Timeline
signals must increase monotonically. Dispatch graphs reject missing
dependencies, cycles, duplicate binding slots, empty work, invalid handles,
and invalid synchronization objects before backend lowering.

The pure host mock contract gate covers descriptor alignment, extent and
workgroup overflow, allocation budgets, use-after-destroy, module/pipeline
ownership, copy/binding bounds, event ordering, timeline waits/signals, graph
cycles, and fail-loud device loss. T.3 intentionally does not wrap
`GpuContext`; CUDA native objects remain private until T.4/T.6 migration.

## T.4 resource and descriptor migration

`ResourceId` is a semantic 128-bit identity and is distinct from runtime object
handles. `ResourceLayout` is typed as buffer, image, or spectral-table layout.
Image descriptors carry every mip/layer offset and pitch; spectral descriptors
separate source sample count from logical domain bins. Residency declares
minimum and maximum committed bytes, priority and budget group, with explicit
resident, streamed, and sparse-tiled modes.

Upload plans are deterministic resource-and-offset ordered DAG inputs. Before
backend lowering they reject empty or duplicate IDs, missing or cyclic
dependencies, invalid subresource pitches, overlapping ranges, sparse tile
mismatches, integer overflow, incomplete initial residency and memory-budget
excess. A one-million-bin spectral domain with four texels and eight source
samples occupies 128 bytes; domain bins and packet lanes never multiply its
resident footprint.

The CUDA backend lowers validated texture plans through a ResourceId-keyed RAII
registry. CUDA arrays, texture objects and spectral allocations are created,
looked up and destroyed only there. The device texture view, context allocation
state, multi-GPU context and `CompiledGpuScene` are backend-private and excluded
from installation. The stable render/session mutation boundary consumes
SceneIR, not CUDA-era material or transform structs.

Distributed file format v4 persists a resource-set hash, descriptor count,
logical bytes and minimum resident bytes. Merge compatibility now includes that
identity, preventing shards produced from different resident resource sets from
being combined. `run_phase_t4_resource_gate.ps1` configures an independent
C++-only CMake project and rejects any configured CUDA compiler;
`test_resource_plan` compiles SceneIR, MaterialIR, Session and distributed
headers there and covers mip layout, sparse tiles, dependency cycles, overlap,
overflow and budget rejection. CUDA texture tests retain RGB hardware filtering
and source-sample spectral parity.

## T.5 dispatch and queue execution IR

`ure_runtime/execution_graph.hpp` owns the SDK-free semantic graph above the
backend command DAG. Stable regions represent pass, sample, candidate, bounded
depth, bootstrap, mutation and manifold-root iteration. Queue contracts carry
payload, active-count and indirect-argument resource identities. A
queue-terminated region names both the producer of its initial active count and
the producer carried from the previous iteration; compact ray/shadow work uses
explicit indirect arguments, while the sparse manifold-root scan remains a
fixed dispatch guarded by its pending-count region.

Commands distinguish 3D direct work, region-chunked work, queue-driven indirect
work, queue reset/swap, ranged or whole-resource clear, resource barrier,
ranged or whole-resource asynchronous transfer, and nested pass/estimator epoch
boundaries. Chunked work keeps MLT
bootstrap batching explicit without repeating whole-batch dispatches or
expanding a node per bootstrap sample. A typed host stage freezes bootstrap
target normalization and CDF construction between readback and upload. The
path graph records separate guiding-light and guiding-spatial decay, wavefront
intersection/shading/shadow order, first-depth
ReSTIR DI/PT work, candidate streaming/finalization, BDPT/VCM/manifold
techniques, and PSSMLT bootstrap/burn-in/production mutation regions. Typed
state transitions carry ReSTIR reservoir ping-pong indices, sample-count
advance, VCM radius iteration and MLT mutation sequence, including modular and
overflow rules. The wave
graph records upload, input barrier, propagation, output barrier and readback.
Loop regions remain compact templates; samples, candidates and depth are not
expanded into unbounded node arrays.

The estimator contract carries versioned spectral, scattering-solid-angle,
medium-phase-solid-angle, light-selection, ReSTIR-target,
technique-support-partition and MLT primary-sampling semantics. Every critical
stage appears in one
dependency-closed ordered sequence. Schema-v1 validation requires canonical
IDs and a dependency-closed command order, validates region parents and
active-count producers, rejects invalid indirect layouts and dispatch-count
overflow, checks resource/barrier/transfer contracts, and requires exactly
nested pass/epoch boundaries. A deterministic four-lane fingerprint covers the
complete canonical graph and estimator contract.

The CUDA path entry generates the graph from the live render configuration and
epochs before launching work, then stores its schema and fingerprint in the
backend-private context. The Fraunhofer CUDA entry generates and fingerprints
the same wave contract before allocation and launch. This integration detects
semantic drift but does not claim T.6 lowering: CUDA allocations, streams,
kernel launch syntax, active-count readback and multi-GPU orchestration still
execute through the existing private backend code.

`run_phase_t5_execution_gate.ps1` configures a C++-only project, rejects any
configured CUDA compiler, and builds the graph contract with warnings as
errors. Contract tests cover deterministic generation, exact advanced
estimator order, cold/warm MLT state, wave transfers/barriers, active-count
loops, indirect arguments, epoch identity, cycle/order drift, invalid PDF
versions and dispatch overflow. CUDA render and wave tests verify entry
integration; the Release inventory contains 40 registered tests at this
closure snapshot. The authoritative cursor is T.6.

## T.6 CUDA production backend migration

`CudaRuntimeDevice` is the production implementation of the SDK-free
`runtime::Device` contract. It owns real CUDA streams, timeline checkpoints,
events, device/upload/readback buffers, mipmapped images, sampler-texture
bindings, PTX modules and compute pipelines. Generic dispatch DAG submission
performs a complete preflight before enqueueing work: handles, usage flags,
binding/copy bounds, adapter grid limits, event order and monotonic timeline
signals all fail through structured `runtime::Error`. CUDA Driver API kernels
consume bindings in stable slot order, while resource and module lifetime rules
prevent use-after-destroy.

The T.5 execution graph lowers to a backend-private `CudaExecutionPlan` before
every path or wave submission. Lowering checks direct/chunked/indirect launch
limits and records stable schema, fingerprint, node, dispatch, barrier,
transfer and state-transition counts. Existing highly optimized `.cu` path
kernels remain a permitted private native fast path: their fixed estimator
order is the T.5 contract, and a runtime-owned blocking stream/timeline
checkpoint encloses completion without exposing CUDA handles above the backend
boundary. The Fraunhofer path goes further and allocates its input/output
through the runtime Device, performs asynchronous transfers and launches on the
runtime stream. CUDA multi-GPU children must report compatible lowered schema,
node and dispatch shapes before merge.

The former public CUDA diagnostic header was removed. Backend-private checks
map allocation, timeout, invalid argument, unsupported operation and device
loss to `runtime::ErrorCode`; the Device retains loss epoch, reason and driver
identity. Raw GPU structs, driver entrypoints, scene lowering and material
helpers are non-installed `.cuh` files under `detail/`. `USE_CUDA` is private
to `ure_core`, and the root project declares only C++ until
`UR_ENABLE_CUDA=ON` activates CUDA. A root `UR_ENABLE_CUDA=OFF` configuration
builds and installs `ure_runtime`, `ure_sceneio` and `ure_config` without
discovering a CUDA compiler. A separate CMake consumer finds the installed
package and links those components. An independent warnings-as-errors
public-surface target includes Render, Session, C ABI, backend and wave headers
under the same SDK-free condition.

The migration baseline is the same committed T.5 executable and Cornell
fixture used by `run_phase_t6_cuda_backend_gate.ps1`:

| Workload | T.5 baseline | T.6 measured | Contract |
|---|---:|---:|---|
| 64×64, 8 SPP, five-process median | 247.062 ms | 252.95 ms | SHA-256 `9e8e27f1bbc1c48384feaec755498dcee33effdcf20092d4abb2dec0bdae9d73` |
| 512×512, 64 SPP | 11857.174 ms | 12611.37 ms | SHA-256 `ff81b8e08386f9b593748cc56ff5b9c3c481f4014658cf41a0795cdd1ed9e935` |
| 512×512 fixed-time VRAM delta | 1753 MiB | 1752 MiB | one scheduled sample, no polling loop |

The final closure gate measured the larger render 6.4% slower and the small
five-process median 2.4% slower. Both remain below the explicit 20% fail-loud
threshold; repeated closure runs varied on both sides of the baseline. Kernel
bodies, launch dimensions, estimator order and reference pixels are unchanged,
and VRAM did not increase. The production Device numerical test executes a PTX
buffer kernel through
copy/event/dispatch/barrier/copy DAG submission and also covers budget, image,
module/pipeline lifetime, invalid handles, unsupported module format and
timeline behavior. The Release inventory is 41 registered tests at this
closure snapshot. The authoritative cursor is T.7.

## T.7 Vulkan compute production foundation

`ure_vulkan` is an independently selectable C++23 library whose installed
header contains no Vulkan SDK type. Its private implementation dynamically
loads Vulkan through pinned Volk and uses per-device dispatch tables, allowing
multiple vendors to execute in one process without global device-function
aliasing. Adapter discovery requires Vulkan 1.3, compute subgroup support,
shader int64, timeline semaphores, and synchronization2. Reported IDs use the
device UUID; memory, queue, workgroup, subgroup, driver, and compiler identity
come from the physical adapter rather than caller-supplied metadata.
The loader and instance environment is process-lifetime because the static
library is also linked into `pyure_native.dll`; unloading Vulkan from a DLL
process-detach callback would run under the Windows loader lock. Devices,
allocations, synchronization objects, pipelines, and command pools still have
deterministic ownership and teardown before that process boundary.

The runtime Device owns:

- compute queues with internal timeline retirement of command and descriptor
  pools;
- timeline fences, GPU events, typed buffers, images, views, samplers, shader
  modules, descriptor layouts, compute pipelines, and pipeline cache;
- device-local, upload, and readback allocations with actual Vulkan allocation
  size budget accounting;
- uniform, storage-buffer, sampled-image, and storage-image descriptors plus
  1/2/4/8-byte specialization constants;
- dependency-ordered DAG recording for copies, dispatches, buffer barriers,
  image transitions, and set/wait events;
- structured native-result classification, durable device-loss metadata, and
  debug-utils messages with `VK_LAYER_KHRONOS_validation` enabled when present.

Submission performs complete preflight before native command allocation. It
rejects stale handles, incorrect usage, out-of-range copies or descriptors,
adapter-limit overflow, duplicate timeline entries, non-monotonic signals,
waits without a prior signal, missing event dependencies, and pipeline binding
type mismatches. Resource destruction is completion-safe. Allocation and
construction failure paths destroy bound objects before freeing memory.
Pipeline cache import validates header size/version, vendor, device, and
pipeline-cache UUID before device creation; cold export and compatible warm
restart are tested, while corrupted and cross-adapter caches fail loudly.

Vulkan-Headers 1.4.352 and Volk commit
`13f95ecee4acd3949c5cceb56cbd43aa9ca6a451` are vendored with a complete
SHA-256 manifest. Slang 2026.14 deterministically compiles ray generation,
spectral Mueller/Stokes transport, atomic wavefront queue compaction, film/AOV,
and complex wave propagation to SPIR-V. The polarization and propagation
operators import `shaders/shared/portable_semantics.slang`, the same semantic
module used by the T.2 PTX/SPIR-V/DXIL gate; the backend does not own a second
math implementation. Reflection, binaries, source, compiler, and arguments
are pinned in `shaders/vulkan/manifest.json`.

`run_phase_t7_vulkan_foundation_gate.ps1` verifies every vendored dependency
hash, performs two deterministic shader builds against committed artifacts,
builds and executes the normal Windows targets, requires two distinct Windows
vendor IDs, configures a separate CUDA-free Windows build, and configures a
CUDA-free Linux GCC/Ninja build with warnings as errors. The CUDA-free Windows
build is installed and consumed by a separate `find_package` project, including
the SDK-neutral public header and exported Vulkan target. The closure machine
executed every operator and the resource/synchronization/cache/lifetime gates
on NVIDIA and Intel Windows adapters; the Linux build and execution gate also
passed. The Release inventory is 42 registered tests at this closure snapshot.

This is deliberately not described as a complete Vulkan scene renderer.
`SelfComputeTraversal` is absent from Vulkan capabilities, `Auto` continues to
select CUDA, and an explicit full Vulkan render configuration fails capability
validation instead of dropping traversal or physical features. T.8 adds the
bounded acceleration bridge below; it does not change that renderer boundary.

## T.8 Vulkan acceleration bridge

`ure_runtime` now owns an SDK-free acceleration contract. It defines provider
capabilities, automatic/compute/ray-query selection, explicit compute fallback
or rejection, validated indexed-triangle and instance descriptors, stable
aligned ray/hit records, typed acceleration handles, and acceleration-resource
descriptor bindings. Native handles, build flags, scratch allocations, and
descriptor extension chains remain private to a backend. CUDA does not
advertise native ray-query or ray-pipeline features merely because the public
feature vocabulary exists. The Vulkan provider likewise withholds
`RayTracingPipeline` until Phase V supplies an executable pipeline path.

Vulkan discovery queries the buffer-device-address, deferred-host-operation,
acceleration-structure, ray-query, and ray-tracing-pipeline extension/feature
chains. Adapter inventory records those hardware facts, while the T.8 device
enables only the ray-query chain used by this bridge, so compute-only adapters
remain usable and unimplemented ray-pipeline execution is not advertised by
the provider. On a ray-query adapter, the runtime builds a single indexed
triangle geometry BLAS and an instanced TLAS, accounts for storage and scratch
allocations against the runtime budget, synchronizes BLAS-to-TLAS and
build-to-query access, retains the input-buffer lifetime, and destroys every
native object behind one opaque acceleration handle. This is intentionally a
bounded bridge: production geometry batching, SAH/wide construction,
refit/update, compaction, build statistics, clustered geometry, and native
ray-tracing-pipeline dispatch remain Phase V work.

Slang 2026.14 deterministically compiles two fixture operators from
`shaders/vulkan/phase_t8_acceleration.slang`:

- `ray_query_native` performs a real Vulkan ray query through the opaque TLAS
  descriptor;
- `compute_bvh` implements the same bounded object-space triangle traversal and
  explicit instance-bound filtering for adapters without ray query.

Both consume identical geometry, instance, visibility-mask, ray, hit, and film
records. The fixture covers two non-uniformly transformed instances, closest
hit and miss behavior, visibility masks, primitive and instance identity,
material mapping, UV/barycentric interpolation, shading/geometric normals, and
a four-pixel framebuffer. An independent CUDA test calls the production
`world_hit` implementation with the same oracle rather than reproducing its
intersection logic on the host.

`run_phase_t8_vulkan_acceleration_gate.ps1` performs two warning-as-error shader
builds and verifies committed source, reflection, and SPIR-V hashes. It then
requires native ray query on the Windows NVIDIA adapter, compute fallback on
both NVIDIA and Intel, exact cross-adapter fixture parity, explicit rejection
when fallback is disabled, and CUDA production-hit parity. Separate CUDA-free
Windows MSVC and Linux GCC/Ninja builds execute the Vulkan acceleration test,
and the Phase T SDK-leakage audit runs last. The Release inventory is 45
registered tests at this closure snapshot.

This closure does not claim a complete Vulkan SceneIR renderer or a production
Vulkan acceleration builder. `SelfComputeTraversal` therefore remains absent
from the Vulkan renderer capability set and explicit full-render requests
remain fail-loud.

## T.9 D3D12/DXR optional backend

`ure_d3d12` is enabled by `UR_ENABLE_D3D12` only on Windows. Its installed
header contains backend-neutral handles and contracts but no D3D12, DXGI, COM,
or Windows SDK type. The private implementation enumerates hardware adapters by
DXGI LUID, records driver/compiler identity and current local-memory budgets,
and exposes compute, texture, subgroup, integer and DXR capability only after
native feature queries succeed. CUDA remains `Auto`; neither the Vulkan nor
D3D12 foundation is presented as a complete scene renderer.

The runtime implements:

- device-local, upload and readback buffers plus 1D/2D/3D images and samplers,
  with actual allocation accounting and fail-loud budget checks;
- shader-visible CBV/SRV/UAV and sampler descriptor heaps generated from typed
  pipeline bindings, including structured read-only storage and DXR TLAS;
- compute/copy queues, cross-queue waits, monotonic timeline fences, resource
  transitions, UAV ordering and retirement-owned command/descriptor lifetime;
- structured device-loss mapping and DRED automatic breadcrumbs, page-fault
  diagnostics and breadcrumb contexts configured before device creation;
- a bounded DXR 1.1 triangle BLAS/TLAS provider and inline ray-query path,
  alongside the shared compute-BVH fallback and explicit rejection policy.

The shader path preserves the T.2 decision instead of creating another
independent tracer. Slang 2026.14 consumes
`portable_semantics.slang`, `phase_t9_foundation.slang`, and the same T.8
acceleration source used by Vulkan. A deterministic normalization step flattens
Slang parameter groups to driver-portable HLSL constant buffers, removes
frontend-only attributes, fixes the backend register ABI and emits explicit
root signatures. Pinned Windows SDK 10.0.26100.0 DXC 1.8 then produces
deterministic release DXIL and separate debug-enabled artifacts. Source,
normalized HLSL, reflection, DXIL, compiler and validator hashes are frozen in
`phase_t9_manifest.json`.

The fixture executes spectral/polarization foundation math, storage-image
writes followed by sampled-image reads, cross-queue transfer synchronization,
compute fallback and native DXR traversal. It checks hit distance/type,
primitive/instance/material identity, UV/barycentrics, normals, visibility
masks, non-uniform transforms and framebuffer parity across every enumerated
hardware adapter. The closure machine requires DXR 1.1 rather than silently
skipping it. A separate CUDA-disabled, D3D12-disabled build executes the Vulkan
acceleration fixture, proving that the optional Windows backend does not become
a dependency of the neutral runtime or Vulkan.

`run_phase_t9_d3d12_gate.ps1` regenerates shaders twice, verifies all pinned
hashes, builds and executes CUDA/Vulkan/D3D12 acceleration parity, requires
native DXR, executes the no-D3D12 isolation build, and runs the Phase T static
audit. The Release inventory is 46 registered tests at this closure snapshot.

This closure does not claim complete D3D12 SceneIR lowering, DispatchRays, or
production acceleration construction/refit/compaction. Those remain T.11 and
Phase V responsibilities. The authoritative cursor is T.10.
