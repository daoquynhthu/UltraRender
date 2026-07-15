# R-P3 Production ReSTIR Implementation Plan

> Document status: Active
>
> **For agentic workers:** Execute inline in the primary agent. Project governance forbids routine subagent-driven development. Track each task through a real red/green test cycle and keep CUDA target builds serialized.

**Goal:** Deliver unbiased temporal/spatial ReSTIR DI, an independent ReSTIR PT mode, explicit estimator metadata, and reproducible R-P3 correctness/benefit evidence.

**Architecture:** Replace contribution replay in production mode with ping-pong reservoirs containing reconstructable samples and generalized RIS normalization. Keep the legacy biased preview isolated. Add a separate path-suffix reservoir and versioned sample-space contract for ReSTIR PT.

**Tech Stack:** C++23 host, CUDA C++20 device, CMake/Ninja, existing UltraRender test framework and PowerShell validation scripts.

## Global Constraints

- Preserve Phase E/L explicit wavelength PDF and spectral domain/packet semantics.
- Preserve Stokes/Mueller transport for every reused candidate.
- Reject stale, incompatible, non-finite, or non-reconnectable candidates; never silently degrade.
- Use checked allocation sizes and context-owned cleanup for every reservoir buffer.
- Keep the biased preview explicitly opt-in and explicitly identified in output metadata.
- Do not begin R-P4 work.

---

### Task 1: Estimator oracle and public contracts

**Files:**
- Create: `libs/ure_core/include/ure/integrator/restir_reservoir.hpp`
- Create: `libs/ure_core/src/restir_reservoir.cpp`
- Modify: `libs/ure_core/CMakeLists.txt`
- Modify: `libs/ure_types/include/ure/render_config.hpp`
- Modify: `libs/ure_config/include/ure/config.hpp`
- Modify: `libs/ure_config/src/config_impl.cpp`
- Test: `tests/host/test_integrator.cpp`

**Interfaces:**
- Produce `RestirReservoirState`, `RestirCandidate`, `stream_restir_candidate`, `finalize_restir_reservoir`, `restir_pairwise_mis_weight`, and `restir_neighbor_offset` as host/device-neutral oracle contracts.
- Add `IntegratorMode::RestirPT`, bounded DI spatial controls, and `RestirPathConfig`.

- [x] Add host tests that enumerate two discrete proposals and assert reservoir expectation equals the exact integral, history clamping preserves normalization, invalid densities are rejected, and neighbor offsets are deterministic.
- [x] Build and run `test_integrator`; verify failures are behavioral missing-contract failures.
- [x] Implement the reservoir oracle with double-precision host accumulation, finite checks, checked candidate multiplicity, deterministic replacement variates, and defensive generalized pairwise MIS.
- [x] Add config JSON/CLI/native/C ABI parity tests for `RestirPT`, DI neighbor/reconnection controls, and PT configuration; verify their failure before mapping fields.
- [x] Implement all configuration mappings and validation, preserving the old biased-consent gate only for preview policy.
- [x] Build and run `test_integrator`, `test_config`, `test_native_solver_contract`, and `test_pyure_smoke`.

### Task 2: Reconstructable DI sample and ping-pong ownership

**Files:**
- Modify: `libs/ure_core/include/ure/gpu_structs.hpp`
- Modify: `libs/ure_core/include/ure/gpu_context.hpp`
- Create: `libs/ure_core/include/ure/integrator/restir_di.cuh`
- Modify: `libs/ure_core/src/path_tracer_host_api.cu`
- Test: `tests/gpu/test_render_basic.cu`

**Interfaces:**
- Produce `GpuRestirDISample`, `GpuRestirDIReservoir`, `RestirDISurfaceKey`, and context-owned input/output arrays.
- Produce `clear_restir_di_history`, `swap_restir_di_history`, and checked allocation planning.

- [x] Add GPU tests for allocation/reset/swap, overflow-safe memory planning, independent preview/production layouts, and scene/session invalidation.
- [x] Build `gpu_test_render` and run `gpu_render`; verify the new ownership tests fail for missing buffers or policies.
- [x] Implement RAII-style release paths and checked ping-pong allocation; wire resolution, scene, spectral, light, material, instance, volume, and integrator invalidation.
- [x] Re-run `gpu_render` and `test_session`.

### Task 3: Current-point DI target reconstruction

**Files:**
- Modify: `libs/ure_core/include/ure/integrator/restir_di.cuh`
- Create: `libs/ure_core/src/restir_di_runtime.cu`
- Create: `libs/ure_core/src/restir_di_runtime.cuh`
- Modify: `libs/ure_core/src/path_tracer_wavefront.cuh` only to share focused light/target helpers
- Test: `tests/gpu/test_render_basic.cu`

**Interfaces:**
- Produce `reconstruct_restir_di_sample`, `evaluate_restir_di_target`, and `compatible_restir_di_domain`.
- Consume R-P1 light identity/selection/PDF functions and existing BSDF/phase spectral evaluation.

- [ ] Add GPU tests for sphere, triangle, environment, and volume candidates; assert current-point light PDF, BSDF/phase PDF, wavelength PDF, geometry term, and Stokes state are reevaluated.
- [ ] Add rejection tests for primitive/material/medium mismatch, stale epochs, changed occlusion connection, delta/non-reconnectable paths, and surface-volume crossing.
- [ ] Run `gpu_render`; verify expected target/rejection failures.
- [ ] Implement canonical light parameters and current-point reconstruction without using stored final RGB as target authority.
- [ ] Re-run `gpu_render`.

### Task 4: Unbiased temporal and spatial ReSTIR DI

**Files:**
- Modify: `libs/ure_core/include/ure/integrator/restir_di.cuh`
- Modify: `libs/ure_core/src/path_tracer_wavefront.cuh`
- Modify: `libs/ure_core/src/path_tracer_host_api.cu`
- Test: `tests/gpu/test_render_basic.cu`
- Test: `tests/host/test_integrator.cpp`

**Interfaces:**
- Produce kernels `generate_restir_di_candidates_kernel`, `reuse_restir_di_temporal_kernel`, `reuse_restir_di_spatial_kernel`, and `resolve_restir_di_visibility_kernel`.
- Read only immutable input reservoirs; write only output reservoirs.

- [ ] Add an enumerated host expectation test for temporal plus spatial proposal combination using pairwise MIS.
- [ ] Add GPU tests for motion-reprojected temporal reuse, deterministic bounded spatial reuse, disocclusion rejection, history clamping, and a visibility change between passes.
- [ ] Run host/GPU tests and verify the existing fail-loud unbiased/spatial gates are exposed by the tests.
- [ ] Implement candidate generation, bidirectional visibility/target evaluation, temporal merge, spatial merge, and final spectral/Stokes accumulation in separate GPU passes.
- [ ] Remove only the production fail-loud gates; keep legacy preview consent and metadata.
- [ ] Run `test_integrator`, `gpu_render`, `test_session`, Phase E/L audits, and Phase R static audit.

### Task 5: Explicit estimator and distributed metadata

**Files:**
- Modify: `libs/ure_core/include/ure/render.hpp`
- Modify: `libs/ure_core/include/ure/ure_c_api.h`
- Modify: `libs/ure_core/src/ure_c_api.cpp`
- Modify: `libs/ure_core/src/gpu_engine_impl.cpp`
- Modify: `libs/ure_core/include/ure/distributed_render.hpp`
- Modify: `libs/ure_core/src/distributed_file_io.cpp`
- Modify: `pyure/__init__.py`
- Test: `tests/host/test_session.cpp`
- Test: `tests/host/test_distributed_file_io.cpp`
- Test: `tests/python/test_pyure_smoke.py`

**Interfaces:**
- Produce `IntegratorEstimatorMetadata` containing mode, policy, bias flag, reuse flags, sample-space version, and scene epoch.
- Require exact compatibility on distributed framebuffer merge.

- [ ] Add session/C/Python tests proving biased preview and unbiased production metadata differ.
- [ ] Add distributed round-trip and incompatible-merge rejection tests.
- [ ] Run tests and confirm failures precede implementation.
- [ ] Implement metadata propagation and versioned file serialization with backward-incompatible fail-loud handling where required.
- [ ] Re-run session, distributed, and Python tests.

### Task 6: Independent ReSTIR PT sample-space and suffix reuse

**Files:**
- Create: `libs/ure_core/include/ure/integrator/restir_pt.cuh`
- Modify: `libs/ure_core/include/ure/gpu_structs.hpp`
- Modify: `libs/ure_core/include/ure/gpu_context.hpp`
- Modify: `libs/ure_core/src/path_tracer_wavefront.cuh`
- Modify: `libs/ure_core/src/path_tracer_host_api.cu`
- Test: `tests/host/test_integrator.cpp`
- Test: `tests/gpu/test_render_basic.cu`

**Interfaces:**
- Produce `GpuRestirPathVertex`, `GpuRestirPathSuffix`, `GpuRestirPTReservoir`, sample-space version `kRestirPTSampleSpaceVersion`, and suffix reconnection kernels.

- [ ] Add host tests for versioned dimension intervals, forward/reverse PDF measure conversion, and deterministic replay.
- [ ] Add GPU tests for diffuse surface suffix, supported volume suffix, stale/moving geometry rejection, wavelength/Stokes preservation, and fail-loud specular-manifold suffix.
- [ ] Run tests and verify `RestirPT` currently fails because the independent scheduler is absent.
- [ ] Implement bounded suffix storage, temporal/spatial proposal, reconnection visibility, pairwise MIS reservoir selection, and selected suffix accumulation.
- [ ] Wire scheduler dispatch without routing through `RestirDirectConfig`.
- [ ] Re-run integrator/GPU/session tests and static audits.

### Task 7: R-P3 benchmark and bias gate

**Files:**
- Create: `scripts/run_phase_r_restir_suite.ps1`
- Create: `scenes/benchmarks/restir/README.md`
- Create deterministic scene/config assets under `scenes/benchmarks/restir/`
- Modify: `scripts/run_phase_r_validation_suite.ps1`
- Modify: `scripts/check_phase_r_static.ps1`

**Interfaces:**
- Produce stable JSON with scene, policy, seed, samples, mean, confidence interval, bias, MSE, variance, time-to-error, samples/second, rejection counters, and pass/fail reason.

- [ ] Add a script self-test mode that rejects missing metrics, biased production metadata, non-finite values, and statistically unresolved bias.
- [ ] Run self-test and verify failures for absent R-P3 result schema.
- [ ] Implement deterministic multi-light, occlusion, and volume fixtures and reference comparisons.
- [ ] Run quick bias gates for all three scenes and benefit measurements; retain evidence JSON only if repository policy treats it as a deterministic fixture.
- [ ] Integrate the suite into Phase R validation.

### Task 8: Documentation, full verification, and closure

**Files:**
- Modify: `PLAN.md`
- Modify: `README.md`
- Modify: `STATUS.md`
- Create: `docs/Phase_R_P3_ReSTIR.md`
- Modify: `AGENTS.md`

- [ ] Update capability language, exact supported boundaries, estimator metadata, invalidation, benchmark reproduction, and authoritative cursor only after every R-P3 gate is satisfied.
- [ ] Run serialized Release build: `.\scripts\build_x64.ps1 -BuildDir build_modular_x64 -Config Release`.
- [ ] Run full CTest: `ctest --test-dir build_modular_x64 -C Release --output-on-failure`.
- [ ] Run Phase E/L/R audits, R-P3 benchmark suite, native validation, physics-optics gate, documentation consistency audit, and `git diff --check`.
- [ ] Self-review estimator equations, PDF measures, CUDA errors, allocation/free symmetry, mutation invalidation, preview isolation, and absence of R-P4 scope.
- [ ] Commit only the verified coherent R-P3 closure with a `phaseR-P3:` commit message.
