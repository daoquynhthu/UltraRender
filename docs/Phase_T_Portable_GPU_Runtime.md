# Phase T — Portable GPU Runtime

## Status

T.0 through T.3 are complete and the authoritative cursor is T.4. This document
is the migration ledger for T.4 through T.11. CUDA remains the only production
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
| T0-API | `render.hpp` exposes `gpu::GpuInstanceTransform` and `gpu::GpuMaterialData`; its factory is named `create_gpu_renderer`. `gpu_driver.hpp`, `gpu_multi_driver.hpp`, and `gpu_scene_compiler.hpp` expose CUDA-era context and scene vocabulary. | Public runtime/session API | T.1, T.3, T.4 |
| T0-ABI | The C ABI is handle-opaque and contains no CUDA SDK type, but creation still routes directly to the GPU/CUDA factory. pyure inherits that single-backend behavior and has no backend identity metadata. | C ABI and language bindings | T.1, T.3 |
| T0-CTX | `gpu_context.hpp` is a monolithic CUDA allocation owner containing device pointers, CUDA arrays, texture objects, estimator state, queues, AOVs, and diagnostics. | Runtime context and resource lifetime | T.3, T.4, T.5, T.6 |
| T0-RES | `gpu_structs.hpp::GpuTexture` stores `cudaTextureObject_t`; texture upload and retained cleanup own CUDA arrays/objects directly. Queue and scene structs use raw device pointers as both semantic views and allocation handles. | Resource/descriptor model | T.4 |
| T0-EXE | `path_tracer_host_api.cu` directly allocates every queue/resource and launches the complete pass sequence. Active-count reads, synchronization, copies, barriers, and estimator epoch order are implicit CUDA host control flow. | Dispatch graph and CUDA executor | T.5, T.6 |
| T0-KRN | Spectral, Mueller, BSDF/phase, traversal, guiding, ReSTIR, BDPT/VCM, manifold, MLT, denoise, and wave kernels use CUDA qualifiers/intrinsics and CUDA compilation units. Shared physical semantics are not yet expressed as a portable kernel contract. | Kernel toolchain and semantic library | T.2, T.5, T.6-T.9 |
| T0-MGPU | `MultiGpuContext` publicly owns raw device pointers and `GpuContext**`; scheduling uses device ordinals, `cudaSetDevice`, peer copies, and global synchronization. Capability negotiation is CUDA-only. | Multi-adapter scheduler | T.3, T.4, T.10 |
| T0-WAVE | The Fraunhofer CUDA reference directly allocates, launches, synchronizes, and copies through the runtime API. Host oracles are portable, but production wave execution has no backend-neutral operator contract. | Wave operator/runtime integration | T.2, T.5, T.6-T.9 |
| T0-ACC | Production traversal is embedded in CUDA kernels and consumes `GpuScene`. `gpu_accelerator.hpp::OptixAccelerator` is a nonfunctional host stub and must not be treated as a provider. There is no BLAS/TLAS provider contract. | Acceleration-provider API | T.3, T.5, then Phase V |
| T0-DIAG | `ure_diag/check_cuda.hpp` exposes `cudaError_t` in a public project header and can reset the device. Nsight/VRAM evidence is backend-specific and lacks a neutral diagnostic event model. | Runtime error and diagnostics | T.3, T.6 |
| T0-SCN | SceneIR and native serialization contain no CUDA handle, but `CompiledGpuScene` lowers directly into `Gpu*` storage and `SpectralPacket`, binding scene compilation to the CUDA layout before runtime selection. | Scene compiler/lowering | T.3, T.4 |
| T0-TEST | Host tests are mostly portable; GPU tests are CUDA translation units and the registered gate assumes a CUDA compiler/device. No mock runtime contract or cross-backend parity fixture exists. | Validation architecture | T.1-T.4, T.11 |

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
- no new public project header may include `cuda_runtime.h` beyond the frozen
  T.0 allowlist (`gpu_context.hpp`, `gpu_hardware.hpp`, `gpu_structs.hpp`, and
  `check_cuda.hpp`);
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
authoritative next cursor is T.3.

## T.2 portable kernel toolchain gate

Slang 2026.14 is pinned as the shared-source frontend after direct compilation
of six nontrivial prototypes to CUDA PTX/cubin, SPIR-V, and DXIL. The gate
validates deterministic artifacts and reflection, 64-bit layout, subgroup and
atomic lowering, specialization, debug/source mapping, PTX register/spill
evidence, CUDA occupancy, and actual CUDA numerical execution.

Restricted shared C++ was rejected because it lacks one validated
SPIR-V/DXIL frontend and reflection/debug contract. A custom URE KernelIR shader
compiler was rejected because owning three optimizer/code-generator/debugger
stacks would be avoidable compiler debt. T.5 may still define a small
dispatch/execution IR; it must not duplicate shader compiler responsibilities.

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
