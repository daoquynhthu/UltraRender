# UltraRender Current Status

Last reviewed: 2026-07-29

This document summarizes the current repository state for users and integrators. `PLAN.md` remains authoritative for construction order and phase completion criteria. Source, CMake registration, and fresh test output take precedence over prose when they disagree.

## Project maturity

UltraRender is a research and development renderer, not a stable public release. The repository has a tested CUDA execution path and several completed subsystem contracts, but it also exposes configuration and schema vocabulary for future algorithms that are deliberately rejected at runtime.

The authoritative construction cursor is `U.3`. Phase Q, Phase R, Phase T, Phase V and the declared bounded scope of Phase W are complete. U.1 maps a normalized, SDK-free USD authored-stage snapshot into the validated Phase Q native archive. U.2 adds an optional, dynamically discoverable `HdURE : HdRenderDelegate` built and tested against OpenUSD 25.05 from Houdini 21.0.671; it advertises no prim support and reports non-ready until U.3/U.5. Production coherent and partially coherent sessions still reject before GPU allocation.

## Supported execution baseline

| Area | Current baseline |
|---|---|
| Host OS/toolchain | Windows 11, Visual Studio 2022 Build Tools, C++23 |
| GPU toolchain | CUDA 13.0, CUDA C++20 |
| Validated GPU | RTX 5060 Laptop, compute capability 12.0 |
| Build tree | `build_modular_x64` using Ninja |
| Primary executable | `build_modular_x64/apps/ure_cli/ure_cli.exe` |
| Registered tests | 57 CTest entries at this snapshot |

The full renderer baseline remains Windows/CUDA. The full SceneIR renderer remains unavailable on the portable native backends. Vulkan additionally has a Linux GCC/Ninja gate, Windows NVIDIA native ray-query evidence, and Windows NVIDIA/Intel compute-BVH evidence. D3D12 additionally has Windows NVIDIA DXR 1.1, compute fallback, typed texture/descriptor and cross-queue fence evidence. macOS, older CUDA architectures, and complete Linux/non-NVIDIA/D3D12 scene rendering do not have equivalent evidence.

## Current module boundaries

| Module | Responsibility | Status |
|---|---|---|
| `ure_types` | Backend-neutral types, SceneIR, native contracts | Active |
| `ure_runtime` | SDK-free device/resource/synchronization/dispatch/execution/acceleration/scheduling contracts | Active; implemented by CUDA and bounded Vulkan/D3D12 foundations |
| `ure_vulkan` | Vulkan 1.3 adapter/resource/compute/synchronization/acceleration backend | Active foundation; full SceneIR renderer not yet lowered |
| `ure_d3d12` | Windows D3D12/DXR adapter/resource/compute/synchronization/acceleration backend | Active optional foundation; full SceneIR renderer not yet lowered |
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
| Backend identity/capability selection | Implemented and tested | CUDA is Auto/default; Vulkan and D3D12 inventories record hardware capability, while providers advertise only executable native ray query or compute fallback; unsupported requests reject and full portable-backend rendering remains unavailable |
| Portable kernel toolchain | Slang selected and feasibility-tested | Prototypes compile to PTX/SPIR-V/DXIL; Vulkan operators and D3D12 foundation/acceleration operators consume shared semantics; pinned DXC emits deterministic release DXIL plus separate debug artifacts; CUDA production kernels remain a private fast path |
| Backend-neutral execution graph | Implemented and tested | Stable path/guiding/ReSTIR/advanced-integrator/wave graphs freeze estimator and PDF order; the CUDA backend lowers and submits the contract through runtime-owned queues and timelines |
| CUDA runtime backend | Implemented and tested | Real stream/fence/event, buffer/image/sampler, PTX module/pipeline, DAG submission, wave resources, multi-GPU compatibility and device-loss errors |
| Vulkan compute runtime | T.7 implemented and tested | Vulkan 1.3 adapter/queue/timeline, buffer/image/sampler, SPIR-V, uniform/storage/image descriptors, specialization, cache identity, validation/debug-utils and structured loss mapping; Windows NVIDIA/Intel plus Linux build/execution gates |
| Vulkan acceleration bridge | V.7 traversal parity complete | SDK-free provider/selection/hit contract; multi-BLAS/TLAS lifecycle plus shared SceneIR closest/shadow, transform, material, UV/normal/tangent and AOV parity; arbitrary-scene integrator lowering remains unavailable |
| D3D12/DXR optional runtime | V.7 traversal parity complete | Windows-only SDK-neutral public surface; DXR lifecycle plus shared SceneIR inline-ray-query/compute parity; arbitrary-scene integrator lowering remains unavailable |
| Multi-backend scheduling | T.10 implemented and tested | Canonical weighted sample partition, feature/precision/coherence/budget/semantic negotiation, backend-native resource cache identity, versioned distributed provenance and overlap rejection; actual CUDA, NVIDIA/Intel Vulkan and NVIDIA/Intel D3D12 inventory plus SDK-free gate |
| Cross-backend validation | T.11 implemented and tested | Machine-readable physical-unit, hit/framebuffer, CUDA reference, variance/MSE, loss, budget, cache, cold/warm launch, VRAM and throughput report; CUDA/Vulkan required, DXR capability-driven, all differences thresholded and classified |
| GPU geometry acceleration | V.0-V.11 complete | CUDA self-compute, optional OptiX, Vulkan RT and DXR share one canonical traversal fixture. Cluster resource/LoD and dynamic lifecycle contracts classify rigid/deforming/topology changes into refit, rebuild or recluster actions. The `ure.phase_v.validation.v1` local/farm suite freezes dense build/trace/VRAM, provider parity, dynamic updates and distributed resource/worker/cache provenance without claiming arbitrary native SceneIR rendering |
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
| Multi-GPU/farm sample scheduling and merge | Implemented contract | CUDA private multi-GPU uses the shared scheduler; heterogeneous compatible sample shards preserve backend/compiler/cache provenance. Cross-machine transport and worker orchestration remain outside this closure |
| Denoising | A tested GPU target exists | No general quality or production guarantee is claimed |
| Diffraction camera | W.2 implemented and tested | Explicit CUDA `Wavefront` mode; wavelength PSF bank and spectral-film resolve, with geometric AOVs left unfiltered |
| Diffractive materials | W.5 implemented and tested | Grating, sinusoidal phase-mask, ideal zone-plate, blazed DOE and bounded passive Jones scattering-table operators in ordinary CUDA `Wavefront`; no cross-path coherent interference |
| Fluorescence/phosphorescence | W.6 implemented and tested | Bounded excitation-emission surface resource in ordinary CUDA `Wavefront`; camera paths use adjoint wavelength conversion, preserve detector wavelength, depolarize the shifted lane and carry lifetime delay; film output remains steady-state |
| Partial coherence/generalized transport | W.7 reference contract implemented and tested | Bounded Hermitian PSD CSD, Gaussian-Schell sources, deterministic coherent realizations, Jones/OPL generalized rays, temporal/interferometric oracles, host/CUDA ensemble reduction and coherent-before-incoherent raw-field merge; W.11 serializes sufficient statistics, but no production scene transport or worker emission exists |
| Anisotropic/modal media | W.9 reference contract implemented and tested | Bounded spectral dielectric/extinction tensors, transverse displacement eigenmodes, birefringence, dichroism, optical activity, liquid-crystal and stress-optic factories, exact homogeneous complex generator and host/CUDA parity; no scene-integrated anisotropic interface or ray splitting |
| Local full-wave coupling | W.10 provider/cache contract implemented and tested | Bounded binary RCWA/FDTD/FEM/BEM/FMM/DDA/S-matrix exchange, exact capability/version/content identity, solver evidence and deterministic cache; verified W.5 Jones tables enter CUDA, but no solver is bundled and no scene-scale Maxwell solve is claimed |
| Coherent distributed frames | W.11 sufficient-statistics contract implemented and tested | v6 metadata separates radiance, complex field, mutual intensity and coherent realization; phase/layout/source/group/range provenance, content-digested files and transactional coherent-before-power merges are enforced, but production workers do not yet emit these frames |
| Wave-optics validation | W.12 complete | `ure.phase_w.validation.v1` binds source/artifact identities to eleven physical, estimator, merge, fail-loud and API evidence categories plus full Release CTest and static gates; it does not claim scene-integrated coherent transport |
| General wave-optics host/CUDA references | Partially implemented | Scene-integrated coherent/partial-coherent transport and scalable general propagation remain incomplete |

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
| USD/Hydra | U.1 schema adapter and U.2 delegate/plugin foundation implemented | Optional actual-OpenUSD plugin is discoverable but intentionally unsupported for rendering; file/stage integration, mesh RPrims, full USDShade conversion, interactive sync and export remain U.3-U.6 |
| RenderSession / C ABI / pyure | Implemented and tested | ABI/version stability is not promised yet |
| Native procedural graph | Implemented | Deterministic build graph, not runtime GPU interpretation |
| Script build hook | Contract implemented, disabled by default | Requires explicit opt-in and attestable external runner |
| Native procedural plugin | Not implemented | Planned for Phase X |

## Explicitly incomplete algorithms

The following must not be described as production capabilities merely because enums, configuration fields, schemas, tests for rejection, or host references exist:

- ReSTIR PT suffix classes outside the bounded production replay contract;
- MLT combined with BDPT/VCM/manifold or adaptive reuse schedulers;
- coherent/partial-coherent production scene transport and film output;
- production worker emission of coherent/partial-coherent distributed frames and scalable general propagation backends;
- anisotropic interface boundary matching, walk-off/ray splitting and SceneIR material integration;
- bundled local full-wave solvers, engine-owned process discovery/execution and a stable Phase X dynamic provider ABI;
- transient fluorescence film output, anti-Stokes resources, fluorescent participating media and advanced-integrator fluorescence;
- Vulkan/D3D12/OptiX arbitrary-scene radiometric integrator lowering and DispatchRays;
- OpenUSD file/stage ingestion, Hydra mesh/material/render execution, complete USDShade conversion, interactive USD synchronization and the general plugin ecosystem;
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

Vulkan acceleration, deterministic ray-query/compute-BVH SPIR-V, CUDA traversal parity, capability fallback/rejection, Windows cross-vendor, and Linux/CUDA-free gate:

```powershell
.\scripts\run_phase_t8_vulkan_acceleration_gate.ps1
```

D3D12/DXR deterministic DXIL, descriptor/image resources, queue/fence/DRED, CUDA/Vulkan parity, native DXR and no-D3D12 isolation gate:

```powershell
.\scripts\run_phase_t9_d3d12_gate.ps1
```

Native RT multi-BLAS build, compaction, refit/rebuild, scratch rejection and optional OptiX lifecycle gate:

```powershell
.\scripts\run_phase_v6_native_provider_gate.ps1 -OptixRoot <optix-sdk-or-optix-dev-root>
```

Cross-provider SceneIR shadow/closest-hit, transform, material, interpolation and AOV parity gate:

```powershell
.\scripts\run_phase_v7_cross_provider_parity.ps1
```

Native scene closure gate:

```powershell
.\scripts\run_phase_q_validation_suite.ps1 -BuildDir build_modular_x64 -Config Release
```

R-P5 closure includes deterministic chain replay, replicated fixed-NMSE evidence with disjoint reference/sample ranges and non-overlapping chain-identity intervals, standalone BDPT energy regression, and an explicit MLT+BDPT rejection contract. The earlier two-workload claim used a reference-correlated wavefront prefix and is superseded; the hardened gate retains one reproducible SDS small-light benefit workload plus explicit non-benefit boundaries, matching the R-P7 per-mode criterion. R-P4 retains its independent four-scene manifold evidence.

R-P7 `Closure` passes on clean commit `56d1121`. The replicated `rough_indirect` workload gives independent positive time-to-error for BDPT and VCM while `glass_caustic` verifies the camera-delta rejection boundary. The manifold bias gate uses per-SPP technique-energy moments; the 1,048,576-SPP small-emitter wavefront reference remains below the 35% confidence threshold. Farm, Nsight, and the final benchmark binary share SHA-256 `7b32d2a64bc03dd412874075bf3b6df62f128d39319fdfcf69cb451abad7a95d`.

## Known documentation rule

Files under `docs/superpowers/specs/` and `docs/superpowers/plans/` are archived design and execution records. Their dates, test counts, unchecked boxes, proposed file names and “next step” statements are historical. Use `docs/README.md` to distinguish current references from archives.
