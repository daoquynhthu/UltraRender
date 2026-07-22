# Phase R-P7 Industrial Validation

R-P7 is the final Phase R evidence gate. It does not add another estimator. It combines the correctness, convergence, performance, resource, applicability, and rejection-boundary evidence produced by R-P1 through R-P6.

## Profiles

`LocalQuick` proves that the current local build can reproduce the registered correctness subset and all seven evidence categories. It may explicitly report farm and profiler evidence as `not_collected` or `unavailable`. It cannot close R-P7.

`Closure` is the production gate. It requires a clean worktree, newly executed benchmark suites, complete advanced-integrator positive-benefit and boundary coverage, non-overlapping farm sample ranges with exact coverage, and measured Nsight kernel occupancy, launch count, and peak VRAM evidence. Farm and Nsight reports must identify the same executable SHA-256.

The aggregate report schema is `ure.phase_r.industrial_validation.v1`. Child artifacts are retained separately and identified by SHA-256. The validator rejects duplicate or missing suites, invalid hashes, missing metrics, overlapping farm ranges, incomplete sample coverage, invalid occupancy, and profiler or farm placeholders in `Closure`.

## Evidence categories

- `integrator_smoke`
- `light_sampling`
- `path_guiding`
- `restir_pt`
- `specular_manifold`
- `bidirectional`
- `mlt`
- `volume_mie`

Image evidence records MSE, variance, time-to-error, samples per second, and mean CIE 1976 Delta E reconstructed from the linear spectral-render RGB result. Farm evidence uses `ure.phase_r.farm_evidence.v1`; Nsight CSV conversion uses `ure.phase_r.nsight_evidence.v1` and requires the achieved-occupancy metric rather than a theoretical occupancy estimate.

The bidirectional suite uses four disjoint sample-range replicates. Its time metric is the renderer's measured GPU render loop, not process startup or context construction. The fixed preview target is normalized MSE 0.33, while every compared mode must also reach normalized MSE 0.25 at the final curve point. `rough_indirect` is the positive workload: a near-light rough reflector and direct-light blocker make importance subpaths materially easier than camera subpaths. `glass_caustic` remains the explicit camera-delta boundary workload.

## Specular-manifold support boundary

The fixed `glass_caustic` workload is a camera-delta path and is outside the standalone SMS support partition, which is `non-delta area anchor -> one to four smooth-delta events -> finite emitter`. The R-P7 boundary gate requires a nonzero ordinary image, zero anchored-delta reference energy, zero standalone SMS deposition, and exercised manifold solve telemetry. It is not used as a positive SMS reference.

SDS, small-emitter, and mixed-specular workloads remain the positive statistical set. SDS uses a 65,536-SPP independent wavefront and SMS reference budget for the closure profile. The 35% high-SPP bias bound is not relaxed for quick execution.

## Commands

```powershell
.\tools\benchmarks\run_phase_r_validation_suite.ps1 -BuildDir build_modular_x64 -Config Release -Profile LocalQuick -SkipBuild

.\tools\benchmarks\build_phase_r_farm_evidence.ps1 -ManifestPath <farm-manifest.json>
.\tools\benchmarks\run_phase_r_farm_longrun.ps1 -SkipBuild
.\tools\benchmarks\test_phase_r_bounded_connection_contract.ps1 -SkipBuild
.\tools\benchmarks\collect_phase_r_vram_evidence.ps1 -Executable <benchmark.exe> -ArgumentList <arguments>
.\tools\benchmarks\convert_phase_r_nsight_csv.ps1 -CsvPath <ncu.csv> -VramEvidencePath <vram-evidence.json> -ToolVersion <version>

.\tools\benchmarks\run_phase_r_validation_suite.ps1 -BuildDir build_modular_x64 -Config Release -Profile Closure -FarmReportPath <farm-evidence.json> -NsightReportPath <nsight-evidence.json>
```

Current status: the versioned evidence contract, `LocalQuick`, bounded-connection expectation gate, bidirectional benefit/boundary matrix, disjoint 4,096-SPP farm merge, and measured Nsight/VRAM evidence are implemented and passing. R-P7 remains open until the clean-tree `Closure` invocation and complete repository verification pass.

The bidirectional report evaluates BDPT and VCM independently. On `rough_indirect`, the fixed preview target is reached by wavefront, BDPT, and VCM in approximately 0.335, 0.255, and 0.270 seconds respectively; all three also pass the independent high-quality convergence bound. Cornell and SDS remain neutral controls rather than being relabeled as benefit scenes. The camera-delta glass workload supplies a valid rejection-boundary scene for both advanced modes.

Bounded connection scheduling does not truncate the first path strategies. Every light-endpoint strategy is evaluated, deeper strategies rotate across global SPP identity, and omitted deeper strategies receive inverse selection-probability compensation. At 4,096 SPP the bounded and full 64-strategy Cornell estimates differ by about 6.4e-6 in total energy and less than 8.6e-6 in each RGB channel.

The farm long-run renders `[0,2048)` and `[2048,4096)` as distinct artifacts, merges them by sample count, and compares the result with a direct `[0,4096)` render. The validator rehashes every artifact, requires contiguous coverage and unique workers/artifacts, and rejects merge normalized MSE above 1e-6.

Path guiding, ReSTIR PT, and MLT reports also execute their production configuration boundaries against retained workloads. A selected path-guided or ReSTIR PT mode without its required state is rejected before allocation; MLT rejects a scene configured with an adaptive guiding scheduler because the Markov chain exclusively owns transitions. These are runtime fail-loud evidence, not host-only configuration assertions.

The Nsight importer accepts both legacy long-form metric/value CSV and the wide raw CSV emitted by Nsight Compute 2025.3. It discards profiler preamble lines only until a recognized header, then requires achieved occupancy for every imported kernel. The current local capture contains 17 measured launches and is paired by executable SHA-256 with a 1,490,026,496-byte device-used VRAM delta. These artifacts are generated evidence under `output/benchmarks`; a fresh Closure run must supply them explicitly and cannot reuse prior reports.
