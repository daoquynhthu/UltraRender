# UltraRender Current Status

Last reviewed: 2026-08-13

This document is the current capability summary. [`PLAN.md`](PLAN.md) is authoritative for execution order; source code, generated manifests, and fresh verification output take precedence over prose.

## Maturity and cursor

UltraRender remains a research and development renderer. There is no “UltraRender 1.0” or `UltraRender_preview` product release.

Phase PB is complete. The project declares **Core ABI 1.0** and **Worker Protocol 1.0** for the exact Windows x64 profile described below. The declaration freezes a small client interaction grammar, not the renderer as a product and not its algorithms, internal data models, feature set, or cross-platform behavior. The declaration tag is a repository evidence marker; packages have not been publicly distributed and the support clock has not started.

The authoritative implementation cursor is `PRV.1R — Product Runtime 阻塞性修复与可信 E2E 基线`. PRV.0 and the PRV.1 structural client spine, Phase Q, R, T, V, the declared bounded scope of W, U, HO.0-HO.2, HT.0-HT.5, HR.0-HR.2, and PB.0-PB.8 are complete within their documented contract, component, or evidence boundaries. PRV.1's former ProductE2E interpretation has been superseded by the runtime audit; PRV.2 and later product work are blocked until PRV.1R closes. HR.3, neural systems, new estimator research, the broader high-order physical world, and differentiation remain frozen during the Preview route.

## Preview integration state

The current render-client spine has one product execution authority. `ure_client` selects either an in-process Direct transport or the local Worker transport; both reach the same runtime/ProductJob implementation. The Worker owns isolation and transport only, and the CLI defaults to Worker without implicit Direct fallback. The complete Preview architecture remains larger than this PRV.1 slice and is specified in [`docs/UltraRender_Preview_Architecture.md`](docs/UltraRender_Preview_Architecture.md).

| Product area | Highest current evidence | Preview gap |
|---|---|---|
| Core/Worker scene render | ClientReachable structural spine; runtime adapter and Worker both delegate ProductJob 0.1 to `ure_product` | Sample/work accounting, persistent execution, long-operation control, resource roots and meaningful product-image evidence are blocking defects |
| CLI render | ClientReachable through `ure_client`; Worker is default and Direct is explicit | Current smoke does not prove trustworthy samples, quality, budget/cancel semantics or self-contained execution |
| Native advanced blocks | Contract / component executable | Procedural, resource, solver and simulation declarations are not uniformly realized by the renderer |
| Automatic transport | Component executable with a bounded CUDA bridge | The product renderer does not yet consume the full HT support, pilot and portfolio authority |
| Measurement/reconstruction | Component executable | No complete-scene product producer/output chain for all required planes |
| Vulkan/D3D12/OptiX | Component executable runtime/acceleration evidence | No maintained arbitrary-scene radiometric product path |
| Multi-device/farm/cache | Component executable | Not reachable through one canonical product job and artifact workflow |
| Hydra/legacy Python | Client-reachable internal paths | Bypass the canonical product service and require convergence |

PRV.0 preserves the historical product baseline. The live machine ledger still records 46 maintained capabilities and entry points with five PRV.1-era `ProductE2E` classifications; the PRV.1R audit has withdrawn that interpretation. PRV.1R.0 must add a supersession record and reclassify those entries to their actually demonstrated `ClientReachable` or `RendererIntegrated` level without rewriting historical reports. None of the twelve final Preview product scenarios is ProductE2E.

The current semantic audit covers 25 maintained inputs: 15 reject outside the executable ProductJob 0.1 subset, 4 execute, 4 are preserved for tooling and 2 execute with explicit semantic debt. No maintained input is accepted-but-ignored. The retained scenario manifest binds twelve required workflows across eight coverage dimensions. See the historical [`PRV.0 baseline`](docs/PRV0_Product_Truth_Baseline.md), its [machine report](docs/reports/ure_preview_baseline_v1.json), and the historical [PRV.1 validation report](docs/reports/phase_prv1_validation_v1.json); the latter remains valid for routing/smoke parity but is no longer sufficient ProductE2E evidence.

PRV.1's architectural slice is retained. ProductJob remains an `UnstableExtension`; Core ABI 1.0 prefixes are unchanged. The shared `ure_client` provides explicit Direct/Worker transports, Worker uses the two bootstrap exports and shared-memory frame leases, and CLI render owns no renderer/SceneIR/image-save implementation. Its two byte-identical 64×64 PFM renders establish smoke-scale transport parity only. The audit found geometric effective work growth from repeated full rerenders, a fixed 60-second Worker wait boundary, incorrect budget/complete semantics, coarse cancellation, current-directory resource dependence, and a default-profile UHD memory/paging cliff without preflight.

Diagnostics are now a continuous Preview workstream. PRV.1R establishes stable result/domain mapping, versioned detail and catalog, correlation/cause/recovery data, terminal operation errors, device reporting and cross-process parity. PRV.2-PRV.10 must add their own scene, material, output, reconstruction, automatic, session, backend, distributed and adapter diagnostics as those product paths are integrated; PRV.11 only performs unified closure.

## Declared public boundary

| Item | Declared scope |
|---|---|
| Core ABI | 1.0, Windows 11 x64, little-endian x86-64, C11-compatible headers, Windows x64 C calling convention |
| Loader | `ultrarender_runtime_1.dll`; exactly `ureGetRuntimeManifest` and `ureQueryInterface` |
| Worker protocol | 1.0; same-user local Named Pipe plus read-only shared-memory leases |
| Worker | `ultrarender_worker_1.exe`; no TCP/UDP listener, firewall exception, or ambient plugin/script/solver/model discovery |
| Core surface | 39 table functions for discovery, lifetime, capabilities/errors, operations/events, scene replacement, sessions, and immutable frames |
| Registry | 192 live identities, 140 reviewed Core identities, 11 pre-release tombstones; ProductJob entries are unstable |
| Stable extensions | None initially |
| Unstable extensions | UUID scene transaction and ProductJob 0.1 tables; exact registry/runtime identity required |
| Stable fallback | Bounded native full-scene validation and atomic replacement |
| Legacy APIs | `ure_c_api.h`, `pyure_native.dll`, and pyure ctypes remain experimental and are not Core ABI 1.0 |

PB.0-PB.7 remain Candidate 0.x history and receive no retroactive compatibility promise. The first stable major has no prior stable runtime, so the current-client/prior-stable-runtime matrix row is honestly `NotApplicable`. The retained final-Candidate layout seed is the oldest Core 1 client that future `runtime_1` builds must continue to execute.

The stable Core deliberately excludes telemetry, spectral/Stokes plane schemas, renderer update strategies, transactions, integrators, MaterialGraph, SceneIR layout, RenderConfig, MeasurementBundle, WorldState, GPU scheduling, models, solvers, providers, Hydra, and distributed/farm internals. Those capabilities evolve through schemas, capabilities, or separately versioned stable/unstable extensions.

## Supported execution baseline

| Area | Current baseline |
|---|---|
| Complete-scene reference backend | CUDA |
| Host | Windows 11, Visual Studio 2026, MSVC 19.52, Windows SDK 10.0.28000, C++23 |
| GPU toolchain | CUDA 13.3, CUDA C++20 |
| Validated GPU | RTX 5060 Laptop, compute capability 12.0 |
| Build | Ninja, `build_modular_x64`, Release gate; final products under `artifacts/<Config>/{bin,lib,symbols,pb8_packages}` |
| Portable backends | Vulkan 1.3 and D3D12/DXR foundations; bounded native acceleration/parity, not full SceneIR rendering |

macOS, ARM64, 32-bit, complete Linux/non-NVIDIA rendering, C++ ABI, COM, static linking, and portable native-handle interop are not promised by Core ABI 1.0.

## Hosted non-GPU CI

The maintained GitHub Actions workflow builds the CUDA-off root project on Ubuntu 24.04 with GCC 13 and Clang 18, and on Windows 2025 with MSVC. Each lane compiles the non-GPU libraries and contract generator, runs 33 root host/contract tests, installs the CMake package, executes an out-of-tree `find_package()` consumer, and independently builds and runs the 15-test SDK-free tree with warnings as errors.

GPU backends, CUDA-coupled renderer/session/product-runtime targets, and optional SDK-coupled adapters remain outside this hosted gate. This is a compile, host-behavior, and package-consumption portability boundary; it is not evidence of complete Linux rendering or an additional Core ABI profile. The exact matrix and cache policy are documented in [`docs/CI.md`](docs/CI.md).

## Subsystem status

| Subsystem | Current state | Important boundary |
|---|---|---|
| Product client/runtime | `ure_product`, ProductJob 0.1 and `ure_client` Direct/Worker transports share one structural execution spine; CLI render uses this path | PRV.1R must repair actual sample/work semantics, persistent execution, async control, resource roots and trusted ProductE2E before further product integration |
| CUDA renderer | Implemented and tested | Complete-scene reference path; no CPU production integrator |
| Spectral/polarization | Runtime spectral domain, packet cap 32, Stokes/Mueller on covered paths | Not general coherent field transport |
| Automatic integration | Technique Graph, support/measure composition, pilot qualification and portfolio contracts plus a bounded CUDA bridge implemented | Current product auto renderer does not yet consume the entire HT authority; manual modes remain for reproduction |
| Advanced estimators | ReSTIR DI, bounded ReSTIR PT, BDPT/VCM, bounded specular manifold and PSSMLT verified | Unsupported combinations such as MLT+BDPT remain rejected |
| Measurement/reconstruction | Typed MeasurementBundle, statistical baseline and sample-level Research boundary implemented as SDK-free components | No complete-scene producer/product output path; no trained model or production model ABI |
| Native scene | `.ure`, `.urescene`, `.urepkg`, `.urecache` contracts and tooling implemented | Advanced blocks are not uniformly consumed by the product renderer; schema versions are independent from Core ABI |
| Materials/assets | MaterialGraph, glTF/GLB, bounded MaterialX, image/SPD/Mie component paths implemented | Authoring adapters and runtime realization are not yet one product path; MaterialGraph C++ layout is internal |
| Portable GPU runtime | SDK-free runtime and multi-backend scheduling contracts implemented | Full arbitrary-scene renderer remains CUDA-only |
| GPU acceleration | CUDA self-compute plus bounded OptiX/Vulkan RT/DXR construction/traversal parity | Native providers do not yet run the complete radiometric renderer |
| Wave optics | Bounded diffraction, fluorescence, partial-coherence, anisotropic and local full-wave contracts/references | No production general coherent scene solver |
| USD/Hydra | Bounded adapter/delegate/export path implemented | The delegate currently reaches internal `RenderSession` directly; OpenUSD ABI is external and Hydra is not a Core extension |
| Physics/acoustics | Optional experimental foundations | Unified time-varying physical world remains future work |

## Public-boundary evidence

The PB.8 freeze has:

- frozen Core 1.0 structure/table prefixes and a two-symbol export list;
- one generated registry for C ABI and worker semantics;
- deterministic v1 schemas, ABI manifest, tombstones and compatibility records;
- lifetime, lease, backpressure, cancellation, security, malformed-input and crash/restart gates;
- a closed 25-surface PB interaction ledger with no duplicate public contract authority under the PB scope;
- independent C11, C++23 unstable-extension and local-worker consumers;
- all 39 Core calls plus the transaction call exercised;
- six finite, nonzero, spatially nonuniform PFM render artifacts;
- a complete Windows x64 Release build and 101/101 registered CTest snapshot.

The machine-readable report is [`docs/reports/phase_pb_validation_v2.json`](docs/reports/phase_pb_validation_v2.json). Exact promise and non-promise language is in [`docs/Public_API_Support_Policy.md`](docs/Public_API_Support_Policy.md).

This report is a point-in-time public-boundary declaration record. PRV.0 separately established the product execution, semantic-debt and maintained-client baseline; the live ledger has advanced through PRV.1 without reinterpreting or weakening PB.8 compatibility evidence.

## Explicitly incomplete

- trustworthy ProductJob sample/work accounting, persistent incremental execution, bounded cancellation and long-running Worker control;
- self-contained resource execution independent of process current directory, plus pre-allocation GPU memory/applicability decisions;
- a real-product Direct/Worker/CLI/external-client matrix with 480p functional, 720p/1080p quality and separately scheduled QHD/UHD stress evidence;
- monotonic progressive events/latest-frame acquisition, explicit device selection/reporting, unambiguous `sample_budget`/scene/per-frame precedence, and an SDK that carries pre-generated protocol headers plus a renderer-free reference client;
- comprehensive structured diagnostics beyond the current Core lifecycle subset, including domain catalogs, cross-process correlation, terminal-operation errors and actionable recovery guidance;
- migration of Python and Hydra onto the canonical product service already used by CLI, Direct and Worker;
- complete realization or explicit rejection of native procedural/resource/solver/simulation semantics;
- full HT-contract-driven automatic transport in the product renderer;
- automatic production reconstruction with complete-scene measurement producers and multilayer output;
- coherent/partial-coherent production scene sessions and worker frame emission;
- scene-integrated anisotropic interfaces, walk-off and ray splitting;
- bundled general full-wave solvers and engine-owned solver discovery/execution;
- arbitrary-scene radiometric rendering on Vulkan, D3D12/DXR, or OptiX;
- canonical multi-device, farm, cache and checkpoint workflows exposed through the product job;
- a unified dynamic physical world and production-grade general fluid/acoustic solver;
- an in-repository GUI or general plugin ecosystem.

Learned proposals, neural denoisers, new estimator families, broad unified-world research and differentiable workflows are intentionally frozen rather than active incomplete Preview work.

Unsupported capability requests are expected to fail with structured diagnostics. A fail-loud boundary may represent policy, resource limits, missing evidence, or remaining implementation debt; it is not by itself proof of a defect.

## Verification commands

```powershell
.\scripts\build_x64.ps1 -BuildDir build_modular_x64 -Config Release
ctest --test-dir build_modular_x64 -C Release --output-on-failure
.\scripts\run_phase_pb_validation_suite.ps1 -BuildDir build_modular_x64 -Config Release
pwsh -NoProfile -File scripts/check_phase_prv0_static.ps1 -RepoRoot .
pwsh -NoProfile -File scripts/check_phase_prv1_static.ps1 -RepoRoot . -RequireVerifiedReport
pwsh -NoProfile -File scripts/run_phase_prv1_validation.ps1 -RepoRoot . -BuildDir build_modular_x64 -FullGateState Passed
```

CTest counts are snapshots. Use `ctest --test-dir build_modular_x64 -C Release -N` for the live inventory.
The current configured `build_modular_x64` inventory contains 108 registered tests; this is an inventory count, not a Preview maturity claim.

## License

Project code is licensed under the [Apache License 2.0](LICENSE). Third-party components retain their own licenses.
