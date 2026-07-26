# Phase T — Portable GPU Runtime

## Status

T.0 through T.5 are complete and the authoritative cursor is T.6. This document
is the migration ledger for T.6 through T.11. CUDA remains the only production
backend at this cursor; Vulkan and D3D12 identities are reserved values whose
explicit selection fails loudly.

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
| T0-BLD | Root CMake declares `LANGUAGES CXX CUDA`, globally includes the CUDA toolkit, requires `CUDAToolkit`, and applies CUDA architecture/compiler policy. GPU tests directly link `CUDA::cudart`. | Build/backend registration | T.1, T.2, T.6 |
| T0-DEV | `gpu_hardware.hpp` includes `cuda_runtime.h`; `gpu_hardware.cu` exposes CUDA attributes as the device capability model. CLI `list-devices` calls CUDA directly. | Adapter/capability registry | T.1 |
| T0-API | `render.hpp` mutation methods now consume SceneIR and CUDA scene compilation is backend-private. The legacy factory name and low-level CUDA driver headers remain for T.6 migration. | Public runtime/session API | T.1, T.3, T.4 |
| T0-ABI | The C ABI is handle-opaque and contains no CUDA SDK type, but creation still routes directly to the GPU/CUDA factory. pyure inherits that single-backend behavior and has no backend identity metadata. | C ABI and language bindings | T.1, T.3 |
| T0-CTX | Public `GpuContext` and `MultiGpuContext` are opaque. CUDA context fields remain backend-private; texture and retained-resource native ownership is isolated in the T.4 RAII registry, while queue/executor lowering remains for T.6. | Runtime context and resource lifetime | T.3-T.5 complete; T.6 lowering |
| T0-RES | Stable ResourceId, typed buffer/image/spectral layouts, residency, sparse tiles and upload plans now own public semantics. CUDA arrays, texture objects and spectral allocations exist only in backend-private views/registry; remaining raw device views are CUDA lowering state. | Resource/descriptor model | T.4 |
| T0-EXE | Stable execution regions now expose queue counts, indirect work, barriers, transfers, boundaries and estimator order. `path_tracer_host_api.cu` generates and fingerprints that graph but still performs direct CUDA allocation, count readback and launch lowering. | Dispatch graph and CUDA executor | T.5 complete; T.6 lowering |
| T0-KRN | Estimator/PDF versions and critical stage order are backend-neutral. Spectral, Mueller, BSDF/phase, traversal, guiding, ReSTIR, BDPT/VCM, manifold, MLT, denoise, and wave kernel bodies still use CUDA qualifiers/intrinsics and CUDA compilation units. | Kernel toolchain and semantic library | T.2/T.5 complete; T.6-T.9 lowering |
| T0-MGPU | `MultiGpuContext` allocation state is backend-private; scheduling still uses device ordinals, `cudaSetDevice`, peer copies and global synchronization. Capability negotiation remains CUDA-only. | Multi-adapter scheduler | T.3, T.4, T.10 |
| T0-WAVE | Fraunhofer upload, barrier, dispatch and readback generate a stable backend-neutral wave graph. The current CUDA reference still directly allocates, launches and synchronizes until T.6 lowering. | Wave operator/runtime integration | T.2/T.5 complete; T.6-T.9 lowering |
| T0-ACC | Production traversal is embedded in CUDA kernels and consumes `GpuScene`. `gpu_accelerator.hpp::OptixAccelerator` is a nonfunctional host stub and must not be treated as a provider. T.5 freezes traversal stage order but does not claim a BLAS/TLAS provider contract. | Acceleration-provider API | T.3/T.5 boundary complete; provider work remains Phase V |
| T0-DIAG | `ure_diag/check_cuda.hpp` exposes `cudaError_t` in a public project header and can reset the device. Nsight/VRAM evidence is backend-specific and lacks a neutral diagnostic event model. | Runtime error and diagnostics | T.3, T.6 |
| T0-SCN | SceneIR and native serialization contain no backend handle. `CompiledGpuScene` and CUDA lowering types are private implementation details; complete lowering through the runtime remains T.6. | Scene compiler/lowering | T.3, T.4 |
| T0-TEST | SDK-free host gates cover runtime, resources and execution graphs, including million-domain budgets, order/cycle/overflow rejection and stable fingerprints; GPU parity remains CUDA-only until T.11. | Validation architecture | T.1-T.5 complete; T.11 parity |

## Migration order

1. T.1 introduces backend identity, adapter capabilities, limits, memory
   budgets, and compiler/driver identity across config, CLI, ABI, and pyure.
   CUDA remains the default and only accepted production backend.
2. T.2 selected Slang using real spectral, Mueller, queue,
   scattering, wave, and traversal prototypes. No source duplication decision is
   allowed before generated-code and debugging evidence exists.
3. T.3 establishes device, queue, synchronization, resource, module, pipeline,
   dispatch, diagnostics, and acceleration-provider interfaces with a host mock.
4. T.4 moves allocation and native handles behind typed descriptors and stable
   resource IDs. SceneIR and distributed metadata remain handle-free.
5. T.5 freezes estimator order and dependencies as an execution graph.
6. T.6 migrates the existing CUDA implementation behind the contracts before a
   second backend is added.
7. T.7 through T.11 add Vulkan, acceleration bridges, optional D3D12, scheduling,
   and parity/performance evidence.

This order prevents a nominal abstraction from being designed around only
trivial buffers while the difficult texture, queue, estimator-state, wave, and
traversal contracts remain CUDA-owned.

## Static regression rules

`scripts/check_phase_t_static.ps1` is the T.0 gate. It enforces:

- no CUDA SDK include or native CUDA handle/type in `ure_types`,
  `ure_sceneio`, `ure_config`, `render_config.hpp`, `scene_ir.hpp`,
  `ure_c_api.h`, or pyure;
- no new public project header may include `cuda_runtime.h` beyond the reduced
  allowlist (`gpu_hardware.hpp`, `gpu_structs.hpp`, and `check_cuda.hpp`);
- the C ABI remains free of `GpuContext`, `GpuScene`, `GpuMaterialData`, and
  `GpuTexture`;
- this ledger retains every coupling ID, owner, and migration batch;
- the PLAN cursor and T.0/T.1 dependency remain explicit.

The allowlist records debt to migrate; it is not permission to expand CUDA
types into additional public files. Each later batch must shrink the allowlist
when its owner moves below the backend boundary.

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
- Explicit `Vulkan` and `D3D12` requests are rejected until their production
  backends exist.
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
