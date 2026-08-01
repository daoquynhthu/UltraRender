# HT.2 Pilot Statistics and Automatic Qualification

Document status: Current HT.2 architecture

Last verified: 2026-08-01

HT.2 turns a compiled Technique Graph and HT.1 composition plan into a scene-, snapshot-, observable- and backend-specific eligibility report. It records short-pass statistical and resource evidence without yet deciding online sample allocation. HT.3 owns cost-aware portfolio scheduling.

## Pilot evidence

`TechniquePilotObservation` stores sufficient statistics produced from ordered, uniquely identified samples of one technique in one exact support partition. The accumulator verifies every global sample identity against the provenance-declared pilot ranges:

- elapsed nanoseconds and derived cost per sample;
- first and second contribution moments;
- absolute-tail thresholds, exceedance counts, mean excess and observed maximum;
- importance-weight sums and squared sums for effective sample size;
- peak scratch and persistent memory;
- non-finite counts and content-bound observation identity.

`TechniquePilotCrossObservation` accepts only sample pairs with identical global sample identities and records cross moments. The derived covariance therefore describes an explicit common-random-number pairing rather than an accidental correlation between unrelated passes. Estimates retain the pilot-provenance identity, so evidence from another world snapshot or pilot pass cannot qualify the current plan.

The contribution vector is already in the estimator's canonical correction domain. `importance_weight` is retained for ESS diagnostics; it is not silently applied a second time. Tail metrics describe the declared finite pilot and are risk signals, not distribution-free guarantees about unseen tails.

## Adaptive-selection bias boundary

`PilotSamplingProvenance` supports three explicit policies:

- `IndependentHoldout` uses distinct namespaces or disjoint sample ranges for pilot and production;
- `CrossFitted` binds deterministic fold assignment and requires distinct selection and evaluation folds;
- `SelectionProbabilityCorrected` binds the selection-probability and correction identities and checks the reciprocal inverse-probability weight.

Every policy binds the Technique Graph, world state, observation snapshot, pilot/production namespaces and sample ranges. Ambiguous overlap, malformed folds or inconsistent selection probabilities reject before qualification. The correction contract makes reuse auditable; it does not claim that every adaptive policy is automatically unbiased.

## Automatic qualification

Qualification evaluates each composition binding against:

- exact HT.1 support-partition membership and required output layer;
- observable and material/medium/path-event support;
- scene facts attached to the current world snapshot;
- the technique's declared backend capability plus workload-specific requirements;
- descriptor and measured resident/scratch budgets;
- valid pilot evidence bound to the same partition and provenance.

The result is stable, content-identified and contains one structured decision per technique. Its identity binds the full qualification context, canonicalized requirements, override policy, composition plan, pilot provenance and decisions, so equivalent outcomes under different world/backend/budget facts do not alias. Separate `production_executable` and `experimental_executable` flags prevent an explicit experiment from being mistaken for default coverage. A production required layer remains executable when at least one eligible technique covers the current partition. Preview and research techniques remain separate and cannot satisfy an unbiased layer by default.

No user-facing integrator name or `IntegratorMode` participates in this contract. HT.2 decides whether a technique is eligible; HT.3 will decide how much work eligible techniques receive.

## Expert override

Expert override is disabled by default and requires experiment and rationale identities. Force-exclude is always explicit. Force-include may create only an `ExperimentalOverride` for missing/invalid pilot evidence or a non-production output layer; it cannot bypass observable, support, scene capability, backend capability or memory-budget failures. Experimental contributions remain in their declared layer and do not contaminate the required production accumulator.

## Evidence

The host gate covers independent, cross-fitted and probability-corrected provenance; exact moments, unbiased sample variance, paired covariance, tail risk and ESS; content tamper rejection; world/snapshot/provenance binding; automatic capability and budget decisions; disabled/enabled overrides; non-bypassable backend failures; and preview-layer separation.

The CUDA gate executes a real kernel that emits pilot contribution and importance-weight records, transfers those records to the SDK-free accumulator and verifies mean, variance, tail rate, ESS and measured positive cost. This proves the device-to-contract ingestion boundary without claiming that every legacy production kernel already emits a pilot stream.

```powershell
.\scripts\check_phase_ht2_pilot_qualification.ps1
ctest --test-dir build_modular_x64 -C Release -R "^(test_pilot_qualification|gpu_pilot_statistics)$" --output-on-failure
```

The same implementation and host gate build independently under `tests/sdk_free` with warnings as errors. The installed-package consumer includes the public pilot contract.

## Research basis

- [Veach, Robust Monte Carlo Methods for Light Transport Simulation](https://graphics.stanford.edu/papers/veach_thesis/) establishes the path-space and multiple-importance foundation used by HT.1 and the variance reasoning consumed here.
- [Grittmann et al., Efficiency-Aware Multiple Importance Sampling](https://graphics.cg.uni-saarland.de/publications/grittmann-sig2022.html) motivates treating sampling cost and variance jointly rather than selecting by variance alone.
- [Grittmann et al., Correlation-Aware Multiple Importance Sampling](https://graphics.cg.uni-saarland.de/publications/grittmann-2021-camis.html) motivates making covariance and correlation provenance explicit instead of assuming independent estimators.

These references guide the statistical contract; they do not predetermine the HT.3 scheduler or rule out project-specific allocation research.

## Current boundary

- HT.2 provides eligibility, evidence and bias-protection contracts. It does not implement online portfolio allocation, exploration floors, drift detection or distributed graph shards.
- The GPU gate validates actual sample ingestion, not simultaneous execution of all legacy estimators. Existing production routes remain unchanged until later transport slices lower pilot and portfolio passes into their execution graphs.
- Pilot estimates are local to a declared world state, observation snapshot, support partition and observable. Dynamic changes require fresh or explicitly reusable evidence.
- Tail and ESS summaries are diagnostics. Promotion claims still require independent replicated evidence under HO.2.
