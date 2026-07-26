# UltraRender Current Status

Last reviewed: 2026-07-26

This document summarizes the current repository state for users and integrators. `PLAN.md` remains authoritative for construction order and phase completion criteria. Source, CMake registration, and fresh test output take precedence over prose when they disagree.

## Project maturity

UltraRender is a research and development renderer, not a stable public release. The repository has a tested CUDA execution path and several completed subsystem contracts, but it also exposes configuration and schema vocabulary for future algorithms that are deliberately rejected at runtime.

The authoritative construction cursor is `T.8`. Phase Q and Phase R are complete; Phase T has completed T.0-T.7, including the shared Slang toolchain, SDK-free runtime/resource/execution contracts, the production CUDA lowering, and a Vulkan compute-runtime foundation. Vulkan does not yet provide the acceleration bridge required for full scene rendering.

## Supported execution baseline

| Area | Current baseline |
|---|---|
| Host OS/toolchain | Windows 11, Visual Studio 2022 Build Tools, C++23 |
| GPU toolchain | CUDA 13.0, CUDA C++20 |
| Validated GPU | RTX 5060 Laptop, compute capability 12.0 |
| Build tree | `build_modular_x64` using Ninja |
| Primary executable | `build_modular_x64/apps/ure_cli/ure_cli.exe` |
| Registered tests | 42 CTest entries at this snapshot |

The full renderer baseline remains Windows/CUDA. The Vulkan compute foundation additionally has a Linux GCC/Ninja build-and-execution gate and Windows execution evidence on NVIDIA and Intel adapters. macOS, older CUDA architectures, and complete Linux/non-NVIDIA scene rendering do not have equivalent evidence.

## Current module boundaries

| Module | Responsibility | Status |
|---|---|---|
| `ure_types` | Backend-neutral types, SceneIR, native contracts | Active |
| `ure_runtime` | SDK-free device/resource/synchronization/dispatch/execution contracts | Active; implemented by CUDA and the bounded Vulkan compute foundation |
| `ure_vulkan` | Vulkan 1.3 adapter/resource/compute/synchronization backend | Active foundation; no T.8 acceleration bridge |
| `ure_core` | Renderer/session/C ABI plus private CUDA backend | Active |
| `ure_sceneio` | Native scene I/O, glTF/MaterialX adapters, image/SPD/Mie I/O | Active |
| `ure_config` | JSON configuration and CLI parsing | Active |
| `ure_diag` | Logging and diagnostics | Active |
| `ure_physics` | Optional physics/acoustic experiments | Experimental |
| `ure_cli` | Offline rendering and native tooling orchestration | Active |
| `pyure` | ctypes wrapper around the C session ABI | Active but not version-stable |

The deleted root `include/` and `src/` trees are not valid development paths.

## Rendering capability matrix

| Capability | Status | Boundary |
|---|---|---|
| CUDA wavefront path tracing | Implemented and tested | Primary runtime path |
| Backend identity/capability selection | Implemented and tested | CUDA is Auto/default; Vulkan adapters are discoverable but full render selection fails on the missing traversal capability; D3D12 is unavailable |
| Portable kernel toolchain | Slang selected and feasibility-tested | Six prototypes compile deterministically to PTX/SPIR-V/DXIL; five Vulkan foundation operators consume the same shared semantic module; existing production kernels remain a private CUDA fast path |
| Backend-neutral execution graph | Implemented and tested | Stable path/guiding/ReSTIR/advanced-integrator/wave graphs freeze estimator and PDF order; the CUDA backend lowers and submits the contract through runtime-owned queues and timelines |
| CUDA runtime backend | Implemented and tested | Real stream/fence/event, buffer/image/sampler, PTX module/pipeline, DAG submission, wave resources, multi-GPU compatibility and device-loss errors |
| Vulkan compute runtime | T.7 implemented and tested | Vulkan 1.3 adapter/queue/timeline, buffer/image/sampler, SPIR-V, uniform/storage/image descriptors, specialization, cache identity, validation/debug-utils and structured loss mapping; Windows NVIDIA/Intel plus Linux build/execution gates; no traversal or full renderer yet |
| Runtime spectral domain / wavelength packets | Implemented and tested | Packet cap 32; sampled lane mode supported |
| Stokes/Mueller polarization | Implemented for covered boundary/transport paths | Not coherent field transport |
| Lambertian/metal/dielectric/cloth | Implemented for tested paths | Material model coverage is not exhaustive |
| Rough dielectric microfacet BTDF | Implemented with eval/PDF/sample tests | Reference-scene coverage remains finite |
| MaterialGraph and finite dielectric layer | Implemented for supported node/model set | Unsupported combinations fail loudly |
| HG/Rayleigh/Mie volume scattering | Implemented for current resource and transport contract | Mie uses precomputed/imported resources at runtime |
| NEE, light tree and path guiding | Implemented for current Phase R-P1/R-P2 scope | Does not imply all advanced integrators are complete |
| Production ReSTIR DI | Implemented and tested | Unbiased temporal/spatial GRIS policy is separate from biased preview metadata |
| ReSTIR PT path reuse | Implemented and verified for bounded diffuse surface and supported volume suffixes | Unsupported suffix classes remain explicit rather than silently approximated |
| GPU BDPT/VCM | Implemented and tested for the R-P4 bounded estimator contract | Does not imply arbitrary path-space techniques or unrestricted merge support |
| Specular-manifold estimator | Implemented for the exact R-P4 support partition with up to four smooth-delta events | Independent technique AOV and four-scene statistical gate; unsupported paths remain wavefront-owned |
| Primary-sample-space MLT | Implemented under R-P5 | GPU chains, production wavefront replay, symmetric Laplace mutation, stratified bootstrap seeding, normalization, diagnostics and shard identities; replicated disjoint-range validation retains SDS small light as the positive time-to-error workload and records the remaining difficult scenes as boundaries. MLT+BDPT is rejected until both subpaths share one spectral primary sample |
| Industrial validation | R-P7 complete | Clean-tree versioned eight-category report, artifact hashes, runtime boundaries, independent BDPT/VCM and MLT benefit/boundary evidence, disjoint 4,096-SPP farm merge, measured Nsight/VRAM evidence, and strict Closure validator passed |
| Multi-GPU sample partition/merge | Implemented | Not a complete distributed render-farm runtime |
| Denoising | A tested GPU target exists | No general quality or production guarantee is claimed |
| Wave-optics host/CUDA references | Partially implemented | Main production path remains radiometric |

## Scene and API capability matrix

| Capability | Status | Boundary |
|---|---|---|
| `.ure` canonical text | Implemented | Review/source-control projection |
| `.urescene` binary scene | Implemented | Native production source container |
| `.urepkg` package | Implemented | Indexed package and embedded scene payloads |
| `.urecache` compiled cache | Implemented contract | Disposable, identity-checked, non-authoritative |
| Native validate/build/pack/unpack/inspect/migrate | Implemented in `ure_cli` | Validation remains capability-aware |
| glTF/GLB import | Implemented through native validation boundary | glTF is an adapter, not core schema |
| MaterialX import/export | Implemented for accepted subset | Unsupported nodes fail; URE graph remains authoritative |
| USD/Hydra | Not implemented | Planned for Phase U; current boundary rejects use |
| RenderSession / C ABI / pyure | Implemented and tested | ABI/version stability is not promised yet |
| Native procedural graph | Implemented | Deterministic build graph, not runtime GPU interpretation |
| Script build hook | Contract implemented, disabled by default | Requires explicit opt-in and attestable external runner |
| Native procedural plugin | Not implemented | Planned for Phase X |

## Explicitly incomplete algorithms

The following must not be described as production capabilities merely because enums, configuration fields, schemas, tests for rejection, or host references exist:

- ReSTIR PT suffix classes outside the bounded production replay contract;
- MLT combined with BDPT/VCM/manifold or adaptive reuse schedulers;
- coherent/partial-coherent production transport and film merge;
- production diffraction camera and general propagation backend;
- Vulkan full scene rendering/RT acceleration, D3D12/DXR and OptiX render backends;
- complete USD/Hydra and plugin ecosystems;
- production-grade general fluid or acoustic simulation.

## Verification

Full Release build:

```powershell
.\scripts\build_x64.ps1 -BuildDir build_modular_x64 -Config Release
```

Full registered test gate:

```powershell
ctest --test-dir build_modular_x64 -C Release --output-on-failure
```

CUDA runtime/reference/performance and SDK-free package gate:

```powershell
.\scripts\run_phase_t6_cuda_backend_gate.ps1
```

Vulkan compute foundation, deterministic SPIR-V, Windows cross-vendor, and Linux/CUDA-free gate:

```powershell
.\scripts\run_phase_t7_vulkan_foundation_gate.ps1
```

Native scene closure gate:

```powershell
.\scripts\run_phase_q_validation_suite.ps1 -BuildDir build_modular_x64 -Config Release
```

R-P5 closure includes deterministic chain replay, replicated fixed-NMSE evidence with disjoint reference/sample ranges and non-overlapping chain-identity intervals, standalone BDPT energy regression, and an explicit MLT+BDPT rejection contract. The earlier two-workload claim used a reference-correlated wavefront prefix and is superseded; the hardened gate retains one reproducible SDS small-light benefit workload plus explicit non-benefit boundaries, matching the R-P7 per-mode criterion. R-P4 retains its independent four-scene manifold evidence.

R-P7 `Closure` passes on clean commit `56d1121`. The replicated `rough_indirect` workload gives independent positive time-to-error for BDPT and VCM while `glass_caustic` verifies the camera-delta rejection boundary. The manifold bias gate uses per-SPP technique-energy moments; the 1,048,576-SPP small-emitter wavefront reference remains below the 35% confidence threshold. Farm, Nsight, and the final benchmark binary share SHA-256 `7b32d2a64bc03dd412874075bf3b6df62f128d39319fdfcf69cb451abad7a95d`.

## Known documentation rule

Files under `docs/superpowers/specs/` and `docs/superpowers/plans/` are archived design and execution records. Their dates, test counts, unchecked boxes, proposed file names and “next step” statements are historical. Use `docs/README.md` to distinguish current references from archives.
