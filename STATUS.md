# UltraRender Current Status

Last reviewed: 2026-09-10

This document is the current capability summary. [`PLAN.md`](PLAN.md) is authoritative for execution order; source code, generated manifests, and fresh verification output take precedence over prose.

## Maturity and cursor

UltraRender remains a research and development renderer. There is no “UltraRender 1.0” or `UltraRender_preview` product release.

Phase PB is complete. The project declares **Core ABI 1.0** and **Worker Protocol 1.0** for the exact Windows x64 profile described below. The declaration freezes a small client interaction grammar, not the renderer as a product and not its algorithms, internal data models, feature set, or cross-platform behavior. The declaration tag is a repository evidence marker; packages have not been publicly distributed and the support clock has not started.

PRV.4 is complete. The next authoritative implementation cursor is `PRV.5 — 训练无关重建与降噪`; implementation has not started. PRV.1R supersedes the former smoke-only ProductE2E interpretation with corrected runtime semantics and retained functional/quality evidence; PRV.2 adds complete archive realization, self-contained packages and unified scene tooling; PRV.3 closes the bounded canonical material, asset, Mie and radiometric-wave color path; PRV.4 closes typed complete-scene measurement and official artifact publication without changing the frozen Core 1.0 prefix. PRV.0-PRV.4, Phase Q, R, T, V, the declared bounded scope of W, U, HO.0-HO.2, HT.0-HT.5, HR.0-HR.2, and PB.0-PB.8 remain complete within their documented contract, component, or evidence boundaries. HR.3, neural systems, new estimator research, the broader high-order physical world, and differentiation remain frozen during the Preview route.

## Preview integration state

The current render-client spine has one product execution authority. `ure_client` selects either an in-process Direct transport or the local Worker transport; both reach the same runtime/ProductJob implementation. The Worker owns isolation and transport only, and the CLI defaults to Worker without implicit Direct fallback. The complete Preview architecture remains larger than this PRV.1 slice and is specified in [`docs/UltraRender_Preview_Architecture.md`](docs/UltraRender_Preview_Architecture.md).

| Product area | Highest current evidence | Preview gap |
|---|---|---|
| Core/Worker scene render | ProductE2E for bounded native/package, material/asset/radiometric-wave color and 45-plane measurement/output workflows; ProductSnapshot realization, preflight and applicability execute before GPU allocation | PRV.5 onward owns reconstruction, full automatic portfolio and portable-backend semantics |
| CLI render/tooling | ProductE2E for bounded Direct/Worker render and official artifact publication; Scene Tool 0.2 authoring produces glTF, preset and MaterialX-derived canonical artifacts rendered by the same ProductJob | Hydra/Python migration and broader semantics remain PRV.5-PRV.10 |
| Native advanced blocks | Product-integrated for deterministic procedural realization, content resources and the supported solver/time-plan subset | Required unsupported dynamic simulation rejects; state advancement/checkpoint remains PRV.7 |
| Automatic transport | Component executable with a bounded CUDA bridge | The product renderer does not yet consume the full HT support, pilot and portfolio authority |
| Measurement/output | ProductE2E for the bounded complete-scene producer, immutable 45-plane frames, partial reads, OpenEXR/checkpoint/display manifest and Direct/Worker/CLI/SDK paths | Spectrum/Stokes reject where no complete producer exists; reconstruction and denoising remain PRV.5 |
| Reconstruction | Component executable | Not yet connected to the product artifact graph or default execution |
| Vulkan/D3D12/OptiX | Component executable runtime/acceleration evidence | No maintained arbitrary-scene radiometric product path |
| Multi-device/farm/cache | Component executable | Not reachable through one canonical product job and artifact workflow |
| Hydra/legacy Python | Client-reachable internal paths | Bypass the canonical product service and require convergence |

PRV.0 preserves the historical product baseline. The live machine ledger records 46 maintained capabilities and entry points. An additive, schema-validated supersession record permanently explains why five PRV.1-era smoke-only `ProductE2E` classifications were withdrawn. PRV.1R now supplies independent functional, quality and visual evidence for six bounded runtime/client/color-output entries; this new evidence does not close any of the twelve final Preview product scenarios.

The current semantic audit covers 25 maintained inputs: 15 reject outside the executable ProductJob 0.4 subset, 8 execute and 2 execute with explicit semantic debt. No maintained input is accepted-but-ignored; archive, material and output declarations enter an executed/rejected applicability boundary. The retained scenario manifest binds twelve required workflows across eight coverage dimensions; Spectrum/Stokes scenarios remain open where the complete-scene producer explicitly rejects them rather than synthesizing data. See the historical [`PRV.0 baseline`](docs/PRV0_Product_Truth_Baseline.md), its [machine report](docs/reports/ure_preview_baseline_v1.json), and the historical [PRV.1 validation report](docs/reports/phase_prv1_validation_v1.json); the latter remains valid for routing/smoke parity but is no longer sufficient ProductE2E evidence.

PRV.1's architectural slice is retained. ProductJob remains an exact-build `UnstableExtension` and is now version 0.4; Core ABI 1.0 prefixes are unchanged. PRV.1R advances one canonical production item per quantum over persistent candidate executors, records pilot/production/realization/executor counts, exposes separate requested/accepted/completed work, and refuses complete Frame publication until accepted work closes. Worker wait, status polling and cancel remain serviceable concurrently; one session is serial and the client defaults to one concurrent Worker session with structured `Backpressure` beyond that bound. Worker observation no longer imposes a 60-second job failure and CLI has no independent ten-minute product deadline. Session close joins the terminal render worker before runtime unload, preventing operation/frame handles from surviving into DLL teardown. Wall-budget exhaustion remains failed/incomplete; file scenes resolve resources against an explicit root from isolated working directories. Memory planning estimates retained queue ping-pong residency and selects the largest applicable unbiased automatic candidate subset before allocation; the selected plan remains identity-bound and does not reduce spectral, precision, output or reconstruction semantics. Direct and Worker expose coalesced monotonic progress and latest immutable frames while bounded generic multi-plane leases avoid blocking the renderer. ProductJob sample precedence no longer multiplies scene or simulation samples, and repeated starts reject instead of resetting accumulation. Version 0.4 additionally reports eligible, qualified and executed estimator masks as observations of the product plan. Device/Execution 0.1 enumerates product adapters through Direct/Worker, consumes stable device constraints before allocation, and reports the actual device in the plan. The exact-build SDK stages pre-generated C++ protocol headers, FlatBuffers runtime headers, generator identity, `UltraRender::Client`, and out-of-tree Direct/Worker real-render examples without publishing renderer-private C++ ABI.

The maintained functional matrix renders 854×480 at 16 spp through `ure_client` Direct/Worker and CLI Direct/Worker from an isolated working directory. Milestone quality evidence renders 1280×720 Direct and 1920×1080 Worker at 128 spp on the production profile; the Worker run exceeds the former 60-second boundary. Authoritative float PFM, deterministic PNG views, finite/energy/spatial checks, nested-sample convergence, hardware/build identities and a retained visual review all pass within the bounded native color scope. These records do not declare `UltraRender_preview` or complete scene/material/measurement/reconstruction quality.

PRV.2 establishes one internal Scene Realizer from native, package or adapted archive to an immutable ProductSnapshot. It executes deterministic procedural graphs, revalidates the result, resolves content-addressed resources without ambient CWD or author-machine absolute paths, compiles the supported solver contract and bounded time plan, and classifies every retained Q.3-Q.12 semantic as Executed, Rejected or PreservedForTooling. `.urepkg` embeds required local payloads, provenance and optional rebuildable caches; byte identity is deterministic, cache removal leaves semantic identity unchanged, and source deletion does not prevent validation, realization or rendering. A separate 854×480, 16 spp procedural-package matrix produces matching authoritative PFM/PNG artifacts through `ure_client` Direct/Worker and CLI Direct/Worker after deleting the author directory.

PRV.3 advances Scene Tool to 0.2 and makes the canonical material program set the product material authority. Its identity binds MaterialGraph topology, every complete-scene material field still consumed by backend lowering, resources, spectral domain, compiler and backend semantics. glTF build, preset realization and MaterialX export/import produce canonical artifacts through Direct and Worker; retained outputs from all three routes are then rendered by ProductJob, rather than only checked as converter fixtures. Texture, SPD, Mie and medium payloads fail before allocation when missing, malformed or domain-incompatible; accepted SPD covers 400–700 nm with an identity-bound endpoint-clamp policy. Native normal maps are explicitly preserved for tooling, while glTF normal and packed metallic-roughness inputs reject until product lowering exists. Adapter loss/rejection shares a content identity with runtime diagnostics. The main functional matrix renders material combinations, glass, Mie volume, diffraction and fluorescence at 854×480/16 samples through Direct and Worker, with package and CLI parity. Five 1280×720/500-sample production-profile scenes retain raw PFM, deterministic PNG, convergence metrics and visual review. Visible pre-denoise variance is recorded as a boundary; PRV.4 subsequently closes the bounded typed measurement/output path, while training-free denoising remains PRV.5 and full HT portfolio productization remains PRV.6.

PRV.4 makes typed measurement the bounded product output authority. The CUDA complete-scene producer publishes 45 BeautyRaw/AOV/sufficient-statistics/estimator planes with explicit schema, identities, units, measures, uncertainty, provenance and sample ranges. Direct and Worker preserve immutable per-plane extent, stride, digest, generation and true partial reads; stable Core-only clients retain the single Color fallback. Official OpenEXR 3.4.14, Measurement checkpoint v2, derived HDR/PPM/BMP and a content manifest form one atomic artifact graph used by the runtime, `ure_client`, CLI and out-of-tree SDK. OpenEXR/Imath are linked statically so the retained Core 1.0 seed can still load the runtime DLL from an arbitrary working directory. The 854×480/16-sample matrix verifies 45 planes, middle-range reads and artifact parity across Direct/Worker plus CLI and SDK. The 1280×720/500-sample Cornell record has stable energy and about 41.5% lower spatial gradient than the retained 128-sample result; visual review still records fine dark-region pre-denoise variance. This closes measurement/output only, not PRV.5 reconstruction or denoising.

Diagnostics are a continuous Preview workstream. PRV.1R establishes stable result/domain mapping, versioned detail and catalog, correlation/cause/recovery data, terminal operation errors, device reporting and cross-process parity. PRV.2 adds scene/package details 600-628. PRV.3 adds material/adapter/resource/applicability details 700-706 and 711-714. PRV.4 adds measurement/output details 800-828 plus legacy-color and snapshot-backpressure details 320/415, including missing producer, unrepresentable layout/scalar, unsupported semantic, transfer integrity, byte budget, disk/codec and atomic publication failures. PRV.5-PRV.10 must add reconstruction, automatic, session, backend, distributed and remaining adapter diagnostics; PRV.11 only performs unified closure.

## Declared public boundary

| Item | Declared scope |
|---|---|
| Core ABI | 1.0, Windows 11 x64, little-endian x86-64, C11-compatible headers, Windows x64 C calling convention |
| Loader | `ultrarender_runtime_1.dll`; exactly `ureGetRuntimeManifest` and `ureQueryInterface` |
| Worker protocol | 1.0; same-user local Named Pipe plus read-only shared-memory leases |
| Worker | `ultrarender_worker_1.exe`; no TCP/UDP listener, firewall exception, or ambient plugin/script/solver/model discovery |
| Core surface | 39 table functions for discovery, lifetime, capabilities/errors, operations/events, scene replacement, sessions, and immutable frames |
| Registry | 275 live identities, 140 reviewed Core identities, 11 pre-release tombstones; ProductJob, Device/Execution, Scene Tool, MeasurementFrame and Output entries are unstable |
| Stable extensions | None initially |
| Unstable extensions | UUID scene transaction, ProductJob 0.4, Device/Execution 0.1 and Scene Tool 0.2 tables; exact registry/runtime identity required |
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
| Product client/runtime | Bounded native/package, material/asset/radiometric-wave and typed measurement/output ProductE2E through one execution spine; immutable ProductSnapshot, applicability, progressive frames and structured diagnostics | PRV.5 onward must converge reconstruction, full automatic execution, portable execution and remaining clients |
| CUDA renderer | Implemented and tested | Complete-scene reference path; no CPU production integrator |
| Spectral/polarization | Runtime spectral domain, packet cap 32, Stokes/Mueller on covered paths | Not general coherent field transport |
| Automatic integration | Technique Graph, support/measure composition, pilot qualification and portfolio contracts plus a bounded CUDA bridge implemented | Current product auto renderer does not yet consume the entire HT authority; manual modes remain for reproduction |
| Advanced estimators | ReSTIR DI, bounded ReSTIR PT, BDPT/VCM, bounded specular manifold and PSSMLT verified | Unsupported combinations such as MLT+BDPT remain rejected |
| Measurement/reconstruction | Typed MeasurementBundle now has a complete-scene product producer and official output path; statistical reconstruction and sample-level Research boundary remain SDK-free components | Reconstruction is not yet in the product execution graph; no trained model or production model ABI |
| Native scene | `.ure`, `.urescene` and self-contained `.urepkg` flow through one realizer and runtime tooling extension; Q.3-Q.12 dispositions are explicit | `.urecache` product lifecycle remains PRV.9; stateful simulation remains PRV.7; schema versions are independent from Core ABI |
| Materials/assets | Canonical MaterialGraph and bounded glTF/MaterialX/preset, texture/SPD/Mie/medium routes execute through Scene Tool/ProductSnapshot/ProductJob with retained Direct/Worker images | MaterialGraph C++ layout remains internal; Hydra viewport is not yet a product client |
| Portable GPU runtime | SDK-free runtime and multi-backend scheduling contracts implemented | Full arbitrary-scene renderer remains CUDA-only |
| GPU acceleration | CUDA self-compute plus bounded OptiX/Vulkan RT/DXR construction/traversal parity | Native providers do not yet run the complete radiometric renderer |
| Wave optics | Bounded radiometric diffraction and fluorescence have ProductJob image evidence; partial-coherence, anisotropic and local full-wave contracts/references remain component scoped | No production general coherent scene solver or coherent Worker session |
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

- stress-scale long-duration progress/backpressure characterization and cancellation bounds on declared hardware classes;
- complete pre-allocation estimates for later output/reconstruction planes and hardware-class applicability evidence beyond the current bounded color workflow;
- separately scheduled QHD/UHD 500+ spp stress evidence across declared hardware classes;
- phase-specific structured diagnostics beyond the PRV.1R common envelope and PRV.2-PRV.4 scene/material/output catalogs; later reconstruction, automatic, session, distributed and adapter failures remain owned by their integration phases;
- migration of Python and Hydra onto the canonical product service already used by CLI, Direct and Worker;
- stateful execution of simulation domains beyond the PRV.2 bounded time-plan/rejection boundary;
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
pwsh -NoProfile -File scripts/check_phase_prv2_static.ps1 -RepoRoot .
pwsh -NoProfile -File scripts/run_prv2_package_e2e.ps1 -RepoRoot . -BuildDir build_modular_x64
pwsh -NoProfile -File scripts/check_phase_prv3_static.ps1 -RepoRoot .
pwsh -NoProfile -File scripts/run_prv3_material_e2e.ps1 -RepoRoot . -BuildDir build_modular_x64
pwsh -NoProfile -File scripts/run_prv3_material_quality.ps1 -RepoRoot . -BuildDir build_modular_x64
```

CTest counts are snapshots. Use `ctest --test-dir build_modular_x64 -C Release -N` for the live inventory.
The current configured `build_modular_x64` inventory contains 125 registered tests; this is an inventory count, not a Preview maturity claim.

## License

Project code is licensed under the [Apache License 2.0](LICENSE). Third-party components retain their own licenses.
