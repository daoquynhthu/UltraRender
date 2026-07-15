# R-P6 Mie and Volume Phase Resources Implementation Plan

> Archive status: completed execution record. Checkboxes, counts and next steps below are not a live plan.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver a validated Lorenz-Mie host solver and external table adapter whose common resource drives unbiased wavelength-dependent Mie phase evaluation, PDF, sampling, NEE, and continuation on CUDA.

**Architecture:** `ure_types` owns pure SceneIR-facing resource data, `ure_sceneio` owns versioned table import/export and shared validation, and `ure_core` owns the host solver, compilation/upload, and GPU execution. Both generated and imported data become the same immutable `MiePhaseResource`; GPU code consumes flattened scene-owned tables and never solves the Mie series during rendering.

**Tech Stack:** C++23 host code, CUDA C++20 device code, nlohmann JSON, existing SceneIR/GpuScene/SpectralPacket infrastructure, CMake/Ninja, direct executable tests and CTest.

## Global Constraints

- Follow `AGENTS.md` and the R-P6 completion contract in `PLAN.md`.
- No production code is written before its corresponding failing test has been observed.
- No step changes more than approximately 50 lines; split mechanical declarations, validation, solver kernels, upload, and dispatch into separate edits.
- Physical units are metres, nanometres, square metres, inverse cubic metres, inverse metres, and inverse steradians as defined by the design spec.
- Mie never falls back to HG or Rayleigh; invalid or missing resources fail before GPU launch.
- GPU-heavy targets build serially.
- Do not commit until the user approves the completed implementation report.

---

### Task 1: SceneIR Mie Resource Types and Structural Validation

**Files:**
- Create: `libs/ure_types/include/ure/mie_phase.hpp`
- Create: `libs/ure_types/include/ure/mie_phase_validation.hpp`
- Modify: `libs/ure_types/include/ure/scene_ir.hpp`
- Create: `libs/ure_sceneio/include/ure/mie_phase_io.hpp`
- Create: `libs/ure_sceneio/src/mie_phase_io.cpp`
- Modify: `libs/ure_sceneio/CMakeLists.txt`
- Create: `tests/host/test_mie_phase.cpp`
- Modify: `tests/host/CMakeLists.txt`

**Interfaces:**
- Produces: `scene_ir::VolumePhaseFunction`, `MieRadiusSample`, `MieRadiusDistribution`, `MieOpticalSample`, `MieGenerationConfig`, and immutable SceneIR ownership of `MiePhaseResource`.
- Produces: header-only canonical validation/hash in `ure_types`, with compatibility wrappers in namespace `ure::sceneio`; this keeps `ure_core` independent of scene I/O.

- [ ] **Step 1: Add a failing host test for valid and invalid resources**

Construct a two-wavelength, three-angle isotropic resource with phase value `1/(4*pi)`. Assert validation creates two CDF rows ending at one and a stable non-empty hash. Add independent cases for non-monotone wavelengths, missing `[-1,1]` coverage, negative phase values, and inconsistent dimensions. Renderer wavelength-domain coverage belongs to the compiler tests in Task 4.

- [ ] **Step 2: Configure and run the new target to verify RED**

Run:

```powershell
.\scripts\build_x64.ps1 -BuildDir build_modular_x64 -Config Release -Targets test_mie_phase
```

Expected: compilation fails because `ure/mie_phase.hpp` and validation APIs do not exist.

- [ ] **Step 3: Add the pure data types in edits below 50 lines**

Use these exact semantic fields:

```cpp
enum class VolumePhaseFunction { HenyeyGreenstein = 0, Rayleigh = 1, Mie = 2 };

struct MieRadiusSample {
    double radius_m = 0.0;
    double number_weight = 0.0;
};

struct MieOpticalSample {
    double wavelength_nm = 0.0;
    std::complex<double> particle_ior = {1.0, 0.0};
    double host_ior = 1.0;
};

struct MiePhaseResource {
    std::vector<float> wavelengths_nm;
    std::vector<float> cos_theta;
    std::vector<float> phase;
    std::vector<float> cdf;
    std::vector<float> scattering_cross_section_m2;
    std::vector<float> extinction_cross_section_m2;
    std::vector<float> absorption_cross_section_m2;
    std::vector<float> asymmetry;
    std::string provenance;
    std::string content_hash;
};
```

- [ ] **Step 4: Implement common validation and canonical hashing**

Validation must use checked size multiplication, exact cell integration of non-negative piecewise-linear density, independent exact first-moment integration, and exact dimension checks. It must reject a normalization error larger than the supplied tolerance rather than silently renormalizing imported phase rows. Canonical physical hashing excludes provenance and stored hashes, canonicalizes signed zero, and uses defined fixed-width byte serialization. Solver output may normalize before calling this validator.

- [ ] **Step 5: Add phase fields to SceneIR global and material media**

Add `medium_phase` defaulting to HG and `std::shared_ptr<const MiePhaseResource> medium_mie_resource` to `MaterialNode` and `SceneIR`. Add compile-time ownership assertions and reject alias-based mutation by construction.

- [ ] **Step 6: Run the host test to verify GREEN**

Run `test_mie_phase.exe`; expected all validation cases pass with no warning output.

### Task 2: Deterministic Radius Quadrature and Lorenz-Mie Solver

**Files:**
- Create: `libs/ure_core/include/ure/mie_solver.hpp`
- Create: `libs/ure_core/src/mie_solver.cpp`
- Modify: `libs/ure_core/CMakeLists.txt`
- Modify: `tests/host/test_mie_phase.cpp`

**Interfaces:**
- Produces: `std::vector<MieRadiusSample> compile_mie_radius_distribution(const MieRadiusDistribution&)`.
- Produces: `std::shared_ptr<MiePhaseResource> generate_mie_phase_resource(const MieGenerationConfig&)`.

- [ ] **Step 1: Add failing distribution tests**

Assert monodisperse output contains one unit-weight radius, explicit samples reject negative/zero radii and negative weights, normalized weights sum to one, and a log-normal distribution produces deterministic strictly positive samples and weights.

- [ ] **Step 2: Observe RED, then implement distribution compilation**

Use deterministic midpoint quadrature in log-radius space over the configured standard-deviation extent. Normalize weights with double precision and reject zero total weight.

- [ ] **Step 3: Add failing small-particle and energy tests**

For a small real-index sphere, assert the generated angular curve approaches the Rayleigh `1 + mu^2` shape after normalization. For a non-absorbing sphere assert absorption is near zero and extinction equals scattering. For an absorbing sphere assert `0 <= scattering <= extinction`. Use the vacuum-wavelength, host-index, relative-index, and `n + i*kappa` conventions from the design.

- [ ] **Step 4: Observe RED, then implement stable coefficient generation**

Implement the solver as focused private functions for truncation order, guarded logarithmic-derivative downward recurrence starting above `max(n_stop, ceil(abs(m)*x))`, Riccati-Bessel forward recurrence, coefficient evaluation, amplitude evaluation, and radius-distribution accumulation. Accumulate differential scattering cross sections before normalization. Use `std::complex<double>` and reject any configured case exceeding the explicit term, angular-sample, or byte budget.

- [ ] **Step 5: Add failing normalization, hash, and repeatability tests**

Generate the same config twice and assert byte-identical canonical float arrays and identical hashes. Independently integrate every phase row and asymmetry value.

- [ ] **Step 6: Normalize solver output, validate it, and verify GREEN**

Before normalization, adaptively refine the angular grid until integrated differential scattering cross section agrees with coefficient-derived `Csca` within tolerance, with special refinement near `mu=1`. Fail if the angular budget is exhausted. Add fixed external-reference `Qsca`, `Qext`, `g`, and angular checks plus a two-radius scattering-weighted mixture identity. Then normalize, validate, and run `test_mie_phase.exe` with zero failures.

### Task 3: Versioned External Mie Table Adapter

**Files:**
- Modify: `libs/ure_sceneio/include/ure/mie_phase_io.hpp`
- Modify: `libs/ure_sceneio/src/mie_phase_io.cpp`
- Modify: `tests/host/test_mie_phase.cpp`
- Create: `tests/assets/mie/isotropic_v1.mie.json`

**Interfaces:**
- Produces: `MiePhaseResource load_mie_phase_table(const std::string&)`.
- Produces: `void save_mie_phase_table(const MiePhaseResource&, const std::string&)`.

- [ ] **Step 1: Add failing JSON round-trip and rejection tests**

Cover version, explicit unit tokens, grids, rows, mandatory scattering/extinction cross sections, derived absorption/CDF/asymmetry, provenance, unknown version, invalid unit, dimension mismatch, and non-finite numeric rejection.

- [ ] **Step 2: Observe RED, implement strict import, and verify GREEN**

Use nlohmann JSON already vendored by the project. Do not accept alternate unit spellings or silently reorder grids.

- [ ] **Step 3: Implement canonical export and byte-repeatability test**

Export with fixed key order and LF output. Save the same resource twice and assert byte-identical files.

### Task 4: Scene Compilation, Deduplication, and Fail-Loud Contracts

**Files:**
- Modify: `libs/ure_core/include/ure/gpu_scene_compiler.hpp`
- Modify: `libs/ure_core/src/gpu_scene_compiler.cpp`
- Modify: `libs/ure_core/include/ure/gpu_structs.hpp`
- Modify: `tests/host/test_mie_phase.cpp`
- Modify: `tests/host/test_material_graph.cpp`

**Interfaces:**
- Produces: `HostMiePhaseResource` flattened host carrier and per-medium phase/resource indices in `CompiledGpuScene` and `GpuMaterialData`.

- [ ] **Step 1: Add failing compiler tests**

Assert missing Mie resource fails; nonzero anisotropy, empirical scattering/absorption, negative/non-finite density, and float-overflowing `density * cross_section` fail; wavelength-domain gaps fail; two shared resources deduplicate; equal-content distinct pointers deduplicate; forged stored hashes are ignored; and HG/Rayleigh reject attached tables while compiling without one.

- [ ] **Step 2: Observe RED and add scalar GPU descriptors**

Add `VolumePhaseFunction` and `medium_phase_resource_index` to `GpuMaterial`; add global equivalents plus the host resource vector to `CompiledGpuScene`.

- [ ] **Step 3: Implement compiler validation and collision-safe deduplication**

Always revalidate and recompute the physical-content hash. Deduplicate first by that hash and then compare canonical physical arrays and typed generation metadata before reusing an index; provenance does not prevent physical deduplication. Use checked `size_t` to device-index conversions.

- [ ] **Step 4: Verify compiler tests GREEN**

Build and run `test_mie_phase` and `test_material_graph` serially.

### Task 5: GPU Resource Upload, Ownership, and Lookup

**Files:**
- Modify: `libs/ure_core/include/ure/gpu_structs.hpp`
- Modify: `libs/ure_core/include/ure/gpu_context.hpp`
- Modify: `libs/ure_core/include/ure/gpu_driver.hpp`
- Modify: `libs/ure_core/src/gpu_engine_impl.cpp`
- Modify: `libs/ure_core/src/path_tracer_host_api.cu`
- Modify: `libs/ure_core/src/gpu_multi_driver.cpp`
- Modify: `libs/ure_core/include/ure/gpu_multi_driver.hpp`
- Modify: `libs/ure_core/src/path_tracer_volume.cuh`
- Modify: `tests/gpu/test_gpu_volume.cu`

**Interfaces:**
- Produces: flattened GPU phase arrays, `GpuMiePhaseResource` offset descriptors, and device lookup helpers for phase and cross sections.

- [ ] **Step 1: Add failing device lookup tests**

Create a synthetic two-wavelength, non-uniform-cosine resource. Assert exact endpoints, bilinear interior interpolation, cross-section interpolation, out-of-domain failure, and malformed descriptor failure without invalid memory access.

- [ ] **Step 2: Observe RED and add checked host flattening/upload**

Allocate each flattened array once per device, upload descriptors with offsets/counts, store allocations in each `GpuContext`, expose them through `GpuScene`, and free them through the existing context lifecycle. Pass the immutable host carriers through both single- and multi-GPU scene-load APIs. Add two-resource offset isolation and multi-device ownership contract tests.

- [ ] **Step 3: Add bounded binary-search lookup helpers**

Lookup must return `false` for uncovered wavelength or invalid resource index. It must not clamp to endpoint rows.

- [ ] **Step 4: Build `gpu_test_volume` serially and verify GREEN**

Run the direct executable and require CUDA synchronization plus zero test failures.

### Task 6: Mie Eval/PDF/Sampling Closure

**Files:**
- Modify: `libs/ure_core/src/path_tracer_volume.cuh`
- Modify: `tests/gpu/test_gpu_volume.cu`

**Interfaces:**
- Produces: resource-aware `eval_volume_phase`, `pdf_volume_phase`, and `sample_volume_phase_lds_pdf` dispatch taking `GpuScene`, resource index, wavelength packet metadata, and returning per-lane phase plus scalar proposal PDF.

- [ ] **Step 1: Replace the unsupported-selector test with failing production expectations**

Assert Mie is supported only with a valid descriptor, sampled direction is unit length, sample PDF equals independently evaluated proposal PDF, and missing resource still returns unsupported/zero.

- [ ] **Step 2: Add failing histogram and normalization tests**

Use a non-uniform sharply sloped synthetic table. Compare sampled `mu` cell frequencies and moments to exact integrated piecewise-linear mass. Cover zero-mass plateaus and random inputs at zero and immediately below one.

- [ ] **Step 3: Implement wavelength-row mixture sampling**

For table interpolation, use the wavelength fraction to select a neighbouring row and remap the same random number into the selected row CDF. Locate a positive-mass angular cell and analytically invert its piecewise-linear density using stable linear/quadratic branches; never interpolate CDF nodes and then return a different PDF. For packet sampling, choose only among active lanes with equal probability and remap before wavelength-row selection. Cover repeated wavelengths, a single active lane, opposite lobes, and require positive proposal wherever any lane phase is positive. Evaluate the final proposal as the exact active-lane mixture.

- [ ] **Step 4: Verify GPU closure tests GREEN**

Run `gpu_test_volume.exe` twice to catch state-dependent descriptor ownership errors.

### Task 7: Wavefront Volume NEE and Continuation Weighting

**Files:**
- Modify: `libs/ure_core/src/path_tracer_wavefront.cuh`
- Modify: `tests/gpu/test_render_basic.cu`
- Modify: `tests/gpu/test_gpu_volume.cu`

**Interfaces:**
- Consumes: resource-aware phase dispatch from Task 6.
- Produces: wavelength-dependent Mie extinction sampling, per-lane NEE phase, mixture-PDF continuation weights, and correct `last_pdf`.

- [ ] **Step 1: Add failing sampled-wavelength and packet tests**

Use two wavelengths with intentionally different forward/backward tables. Assert sampled mode follows the active wavelength, packet mode reports the lane-mixture proposal, and each lane receives `phase_lambda / proposal_pdf`.

- [ ] **Step 2: Add failing cross-section transport test**

Assert Mie `sigma_s`, `sigma_a`, and `sigma_t` equal particle number density times interpolated cross sections and do not consume empirical medium coefficient arrays.

- [ ] **Step 3: Implement Mie distance, NEE, and continuation dispatch**

Keep HG/Rayleigh coefficient and phase behaviour unchanged. Mie NEE writes per-active-lane phase products while shadow metadata and `last_pdf` record the same scalar continuation mixture. Apply the scalar-radiometric depolarizing boundary by preserving `I` and zeroing `Q/U/V` after Mie scattering.

- [ ] **Step 4: Add failing NEE/continuation consistency and variance tests**

Compare direct phase evaluation with the value used by shadow contribution; verify emissive-hit MIS sees the stored proposal. Estimate a known forward-lobe integral and require table importance sampling variance below uniform-sphere sampling.

- [ ] **Step 5: Verify `gpu_test_volume` and `gpu_test_render` GREEN**

Build targets one at a time and run both direct executables.

### Task 8: Session Mutation and Resource Rebuild Semantics

**Files:**
- Modify: `libs/ure_core/src/session.cpp`
- Modify: `tests/host/test_session.cpp`

**Interfaces:**
- Produces: Mie resource topology/content changes classified as full reload; scalar-only compatible medium updates retain existing paths.

- [ ] **Step 1: Add failing mutation classification tests**

Cover unchanged shared resource, replacement pointer with equal content, changed phase kind, forged/stale stored hash, changed wavelength grid, changed table content, and density-only updates. Compare against the compiler-saved canonical fingerprint. Assert resource changes reload and reset accumulation.

- [ ] **Step 2: Implement collision-safe resource comparison and verify GREEN**

Use hash plus canonical equality. Never compare only `shared_ptr` identity.

### Task 9: Static Audit, Documentation, and Full Release Gate

**Files:**
- Modify: `scripts/check_phase_r_static.ps1`
- Modify: `PLAN.md`
- Modify: `README.md`
- Create: `docs/Phase_R_P6_Mie_Volume_Resources.md`
- Modify: `AGENTS.md`

**Interfaces:**
- Produces: static gates preventing Mie fallback or hard-coded HG production dispatch and records the verified R-P6 boundary.

- [ ] **Step 1: Add static audit checks before changing production markers**

Require a Mie resource descriptor, generated/imported validation path, wavelength-dependent wavefront dispatch, and fail-loud missing-resource path. Reject production volume calls hard-coded to HG.

- [ ] **Step 2: Run audit to observe RED, then update only current-state documentation**

Do not mark R-P6 complete until every gate below passes.

- [ ] **Step 3: Run formatting and resource determinism checks**

Run `git diff --check`, import/export repeatability, solver repeatability, and JSON fixture parsing.

- [ ] **Step 4: Run the targeted Release gate**

Run host Mie tests, `test_material_graph`, `test_session`, `gpu_test_volume`, `gpu_test_render`, Phase L/R static audits, and physics-optics gate.

- [ ] **Step 4a: Run true end-to-end resource lifecycle gates**

Render both a global Mie medium and a bounded material Mie medium from one generated and one imported resource through SceneIR, compiler deduplication, single-GPU upload, multi-GPU upload contract, rendering, retained-scene reload, destruction, and reload. Include two simultaneous resources to detect offset aliasing.

- [ ] **Step 5: Run full serial Release build and complete CTest**

```powershell
cmake --build build_modular_x64 --config Release --parallel 1
ctest --test-dir build_modular_x64 -C Release --output-on-failure
```

Expected: all registered tests pass with zero warnings or failures.

- [ ] **Step 6: Self-review and report without committing**

Review estimator correctness, units, checked allocation/free paths, no Mie fallback, HG/Rayleigh parity, mutation resets, and scope exclusions. Present the changes and evidence to the user and wait for explicit commit approval.
