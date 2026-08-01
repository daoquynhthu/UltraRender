# HT.5 Automatic Integration Closure

Document status: Completed HT.5 architecture and evidence record

Last verified: 2026-08-01

HT.5 changes the normal front-end decision from an integrator name to a quality, time and memory objective. The SDK-free transport layer can close a qualified Technique Graph and portfolio schedule into a provenance-bound automatic plan. The current CUDA production bridge independently pilots executable legacy estimator endpoints, retains wavefront path tracing as defensive coverage, allocates disjoint production sample ranges and combines the resulting unbiased films without asking the user to select a technique.

This is a bounded production closure. It does not claim that every Technique Graph node already executes inside one joint CUDA wavefront graph.

## Two execution levels

The SDK-free `AutomaticIntegratorPlan` is the fine-grained authority. It binds the objective, Technique Graph, support/measure composition, qualification report, portfolio schedule, world and observation snapshot. Each partition program records scheduled and defensive technique masks, its weight rule, estimate layer and sample allocation. An `AutomaticOutputTrace` closes only when observations preserve the plan, partition, measurement, normalization, coverage, uncertainty, time and memory identities.

The current complete-scene CUDA bridge operates one level higher. It treats each supported legacy mode as a complete endpoint estimator, runs an independent pilot range, rejects unsupported or biased endpoints, and executes selected endpoints sequentially so their persistent GPU allocations do not coexist. Endpoint films are combined with pilot-variance precision weights. Because pilot and production sample ranges are disjoint, adaptive selection does not reuse the observations that form the final estimate. The reported uncertainty is the conservative weighted sum of endpoint standard errors; it does not assume an unmeasured cross-estimator covariance.

The current eligible endpoint set is wavefront, path-guided wavefront, unbiased ReSTIR DI, bounded ReSTIR PT and BDPT. Standalone SMS lacks complete-integral support, finite-radius VCM is not finite-sample unbiased, and PSSMLT needs independent chain-level normalization and aggregation rather than ordinary endpoint precision weighting. Those three are reported as excluded before pilot execution. They can enter a future fine-grained automatic plan only through their HT.1 support and normalization contracts; `allow_experimental` does not bypass this boundary.

## Default and compatibility policy

CLI configuration and pyure now default to `automatic`. Users may specify target relative standard error, a time budget, a memory budget, pilot SPP, maximum selected techniques and the minimum wavefront fraction. The system reports every candidate as qualified, selected or rejected with a reason.

Manual modes remain supported as `CompatibilityAndReproducibilityOnly` presets. They are retained for regression reproduction, historical scene/config compatibility and controlled expert experiments. They are not scheduled for removal, but they no longer define the normal CLI or Python workflow. A default-constructed C++ `RenderConfig` remains wavefront for source compatibility; selecting automatic execution is explicit at that low-level API boundary.

## Coverage and output semantics

Wavefront is always selected first and receives a nonzero allocation floor. Unknown wave-optics combinations reduce the candidate set to this defensive route. Every Beauty report exposes the selected technique mask, per-technique allocation and aggregation weight, endpoint-ensemble and normalization policy, conservative uncertainty, achieved relative standard error, elapsed time and budget status.

Geometric AOVs currently come from the wavefront endpoint only and are explicitly reported as such. They are not precision-combined with estimator-specific AOVs. The fine-grained SDK-free plan remains the path for future per-partition MeasurementBundle production; HT.5 does not pretend the current RGB/AOV bridge already emits every HR.0 plane.

## Time and memory budgets

The time budget affects allocation using measured pilot cost and the report records whether actual elapsed time met the requested deadline. It is not a hard real-time preemption guarantee.

Candidates are created and destroyed sequentially. The runtime samples live adapter memory while a candidate context exists and records measured peak resident delta. It adds acceleration-build temporary telemetry to form an estimated construction peak, then checks it against the selected device or explicit automatic memory budget. These values are evidence for the tested process; they are not a system-wide allocator trace in the presence of unrelated GPU processes.

## Validation

The SDK-free host gate covers objective identity, qualification/schedule binding, defensive wavefront allocation, budget failures, partition coverage, normalization identity, confidence intervals and tamper rejection. The installed-package consumer includes the public automatic contract.

The CUDA gate renders three maintained scenes (`test_plane_sphere.gltf`, `cornell_box.gltf`, and `textured_quad_validation.gltf`). Each scene uses three independent automatic replicates and three disjoint 32-SPP wavefront references. It checks unresolved bias against pooled replicate uncertainty, finite replicate variance and tail evidence, elapsed time, achieved uncertainty, measured/estimated VRAM, normalized weights and order-independent framebuffer shard merging. The suite is deliberately small and statistical; it proves the automatic boundary and guards regressions rather than asserting universal superiority over every manual integrator.

```powershell
.\scripts\check_phase_ht5_automatic_integrator.ps1
ctest --test-dir build_modular_x64 -C Release -R "^(test_automatic_integrator|gpu_automatic_integrator|test_config|test_pyure_smoke)$" --output-on-failure
```

## Honest boundaries

- The CUDA bridge combines complete unbiased endpoint films; it does not yet lower the full HT.1 partition graph into one jointly sampled kernel graph.
- Experimental `ResearchExtension` nodes never enter the default CUDA candidate set. `allow_experimental` only preserves an explicit future policy field and currently grants no production execution capability.
- Pilot precision weighting is independent-sample valid but can be statistically conservative and suboptimal when endpoint covariance is useful but unavailable.
- Time budgets are allocation objectives, not deadline guarantees.
- Beauty is automatically combined; current non-Beauty AOVs retain wavefront-only provenance.
- Manual modes remain available for compatibility and research, not as the recommended default front-end choice.
