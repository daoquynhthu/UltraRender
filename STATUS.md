# UltraRender Current Status

Last reviewed: 2026-07-18

This document summarizes the current repository state for users and integrators. `PLAN.md` remains authoritative for construction order and phase completion criteria. Source, CMake registration, and fresh test output take precedence over prose when they disagree.

## Project maturity

UltraRender is a research and development renderer, not a stable public release. The repository has a tested CUDA execution path and several completed subsystem contracts, but it also exposes configuration and schema vocabulary for future algorithms that are deliberately rejected at runtime.

The authoritative construction cursor is `R-P5`. Phase Q, R-P3 production ReSTIR, R-P4 specular-manifold/BDPT/VCM, and R-P6 Mie volume resources are complete. The remaining Phase R sequence is `R-P5 -> R-P7`; Phase T and later backend work must not be treated as implemented.

## Supported execution baseline

| Area | Current baseline |
|---|---|
| Host OS/toolchain | Windows 11, Visual Studio 2022 Build Tools, C++23 |
| GPU toolchain | CUDA 13.0, CUDA C++20 |
| Validated GPU | RTX 5060 Laptop, compute capability 12.0 |
| Build tree | `build_modular_x64` using Ninja |
| Primary executable | `build_modular_x64/apps/ure_cli/ure_cli.exe` |
| Registered tests | 37 CTest entries at this snapshot |

Linux, macOS, non-NVIDIA execution, and older CUDA architectures do not currently have equivalent repository-level verification evidence.

## Current module boundaries

| Module | Responsibility | Status |
|---|---|---|
| `ure_types` | Backend-neutral types, SceneIR, native contracts | Active |
| `ure_core` | CUDA renderer, GPU scene compiler, sessions and C ABI | Active |
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
| Primary-sample-space MLT | In progress under R-P5 | GPU chains, replay, bootstrap/burn-in, normalization, diagnostics and shard identities run end to end; difficult-scene benefit gate is not yet closed |
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
- MLT chain integrator;
- coherent/partial-coherent production transport and film merge;
- production diffraction camera and general propagation backend;
- Vulkan, D3D12/DXR and OptiX render backends;
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

Native scene closure gate:

```powershell
.\scripts\run_phase_q_validation_suite.ps1 -BuildDir build_modular_x64 -Config Release
```

The active R-P5 implementation passes the complete Release build, 37/37 CTest entries, the Phase R static audit, documentation consistency audit, FlatBuffers schema conformance/regeneration check, and `git diff --check`. R-P4 also retains its independent four-scene manifold evidence. These checks validate the current runtime foundation; they do not close R-P5 because the second difficult-scene time-to-error benefit gate remains open.

## Known documentation rule

Files under `docs/superpowers/specs/` and `docs/superpowers/plans/` are archived design and execution records. Their dates, test counts, unchecked boxes, proposed file names and “next step” statements are historical. Use `docs/README.md` to distinguish current references from archives.
