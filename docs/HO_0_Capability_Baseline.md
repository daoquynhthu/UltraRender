# HO.0 Capability and Research Baseline

Document status: Current HO.0 baseline

Last verified: 2026-08-01

This document records the evidence produced by the first high-order planning audit. It does not claim that the listed gaps are implemented. The machine-readable files under [`research/ho0/`](research/ho0/) are the authoritative HO.0 inventory; this document explains their engineering meaning.

## Baseline result

The executable audit currently proves the following snapshot:

| Inventory | Count | Meaning |
|---|---:|---|
| Explicit quoted capability-boundary diagnostics | 92 | Every scanned line is classified exactly once; the lexical scan is deliberately narrower than every typed runtime status |
| Active boundary groups | 9 | Physical/model, estimator, backend, resource, projection and schema/identity boundaries with a forward owner |
| Resolved accidental boundary groups | 1 | Three stale pre-U.5 Hydra lifecycle diagnostics were replaced with current build-capability messages |
| Existing integrator techniques | 8 | Every current `IntegratorMode` has observable, sample-space, support, density, normalization, correlation and combination notes |
| Missing measurement information classes | 10 | Information destroyed or hidden by the current RGB framebuffer plus six public AOVs |
| State ownership nodes | 11 | Authoring, retained scene, ECS, solver-private, mutation, backend and distributed state holders |
| High-order benchmark families | 7 | Ordinary, caustic, volume, polarization, wave/fluorescence, dynamic coupling and inverse-problem coverage |

Run the baseline gate with:

```powershell
.\scripts\check_phase_ho0_baseline.ps1 -ReportPath build_modular_x64\ho0_baseline_report.json
```

The generated report is reproducible evidence and is not committed. It records the Git identity, aggregate metrics, per-boundary counts and SHA-256 digests of the committed inventory files.

## Capability Boundary Ledger

[`capability_boundary_ledger.json`](research/ho0/capability_boundary_ledger.json) scans explicit diagnostic strings containing unsupported, unavailable, not-implemented, precondition and mutual-exclusion language. Ordered, non-overlapping coverage rules bind every discovered line to one owner and expected count. Any new line fails the audit until it is reviewed and classified.

The scan exposed a missing category in the initial roadmap: schema and identity incompatibility is neither accidental debt nor a backend omission. Unknown scene, solver, execution-graph, distributed-frame or byte-order semantics must remain rejected until an explicit migration exists. `PLAN.md` now includes `Schema/Identity Boundary` as a durable class.

The ledger does not use the diagnostic count as a quality metric. Several groups should remain fail-loud indefinitely:

- unknown schema and observable identities;
- unsupported physical polarization models;
- adapters that cannot preserve native semantics;
- unavailable backend capabilities with no valid fallback;
- resource requests that cannot be honored safely.

The principal accidental transport debt is the single `IntegratorMode`/single estimator metadata decision. The first concrete accidental cleanup was also performed: the Hydra delegate no longer reports that U.5 is pending after U.5/U.6 closure.

## Integrator inventory

[`integrator_inventory.json`](research/ho0/integrator_inventory.json) gives the current eight modes a target role:

| Technique | Target role | Main open combination issue |
|---|---|---|
| Wavefront PT | Defensive full-support baseline | No graph-level portfolio attribution |
| Path guiding | Shared proposal service | Adaptive history and selection derivative are not represented |
| ReSTIR DI | Direct-light reuse layer | DI/PT share no graph-level eligibility and covariance model |
| ReSTIR PT | Bounded suffix-reuse layer | Specular suffixes lack a manifold shift contract |
| Specular manifold | Delta-chain solver/proposal | Scheduled as a standalone mode despite an explicit support partition |
| BDPT | Bidirectional connection family | Technique enumeration is hidden behind a mode selector |
| VCM | Connection and vertex-merge family | Progressive correlation/cost is absent from shared metadata |
| PSSMLT | Independent Markov-chain family | Cannot enter ordinary iid MIS; shared spectral primary samples are incomplete |

This inventory intentionally does not claim that all listed bias classes have one universal proof. It records the boundary of existing implementations and the exact missing contract that HT.0/HT.1 must resolve.

## Measurement gap

[`measurement_gap_matrix.json`](research/ho0/measurement_gap_matrix.json) confirms that the public output surface is six AOVs plus an RGB framebuffer. Private queues preserve more information, but private ReSTIR, MLT, fluorescence and wave records do not form a shared measurement contract.

The most consequential losses are:

- spectral/Stokes values are resolved before a reconstruction client can use them;
- detector and transport wavelengths, joint PDFs and technique weights are not exposed together;
- no general path-event signature, moments, covariance or effective sample count exists;
- history cannot bind to a canonical world-state/time/snapshot identity;
- coherent/Jones/mutual-intensity observables have distributed contracts but no typed session film;
- no output carries reconstruction uncertainty, confidence or OOD status.

This is why HR.0 precedes production neural denoising. A model trained against the current public AOV set could improve RGB images, but it would permanently cement a weak data boundary.

## State ownership

[`state_ownership_map.json`](research/ho0/state_ownership_map.json) distinguishes five intended authority layers plus derived backend state. The current repository contains useful seeds but no unified dynamic world:

- native scene data is the authoring source;
- `World` duplicates scene and runtime values in directly mutable vectors;
- `PhysicsWorld`, `FluidSystem` and `AcousticSystem` own in-place solver-private state;
- `NativeSimulationContract` already has rational ticks, domains and coupling semantics, but compilation reduces supported domains to a coarse `PhysicsConfig` and does not instantiate transfer operators;
- `SceneDiff` provides a transactional seed for selected mutations but has no general field dependency or observation invalidation graph;
- `RenderSession` retains SceneIR and a scene epoch, not a physical state/time/snapshot identity;
- distributed frames bind substantial execution/resource provenance without binding the future canonical dynamic world state.

HW.0 therefore starts with identity, publication and immutable observation snapshots. It does not start by adding another solver.

## Research Capsule v1

[`research_capsule.schema.json`](research/ho0/research_capsule.schema.json) requires a question, falsifiable hypothesis, input identity, replay policy, baseline, candidate, metrics, artifacts, conclusion and known failure domain. The baseline includes both outcomes:

- [`boundary_taxonomy_coverage.json`](research/ho0/capsules/boundary_taxonomy_coverage.json) is a positive result: all 92 scanned diagnostics are uniquely classified.
- [`measurement_contract_insufficient.json`](research/ho0/capsules/measurement_contract_insufficient.json) is a negative result: six current AOV planes omit ten required information classes and expose no typed spectral/Stokes plane.

These examples are deliberately small and fully replayable. Later statistical and physical experiments can add seeds, ranges, replicates and external artifacts without changing the minimum epistemic contract.

## Benchmark family

[`benchmark_family_manifest.json`](research/ho0/benchmark_family_manifest.json) reuses existing project-authored analytic and executable fixtures where they are strong. Dynamic coupling is currently only a contract seed, and the inverse small-problem family is explicitly a planned fixture built from existing forward oracles. The manifest records this maturity instead of describing either as implemented.

Every family carries provenance, a reference strategy and metrics. Imported assets cannot be promoted without a source URL, license and immutable digest.

## HO.0 completion mapping

| PLAN step | Evidence |
|---|---|
| HO.0.1 Capability Boundary Ledger | Complete lexical coverage, per-group counts, source anchors and one resolved accidental group |
| HO.0.2 Integrator audit | Eight exact legacy modes with required semantic fields and source assertions |
| HO.0.3 Measurement Gap Matrix | Six current planes and ten forward-owned gaps |
| HO.0.4 State Ownership Map | Eleven state holders mapped to target authority layers and owner phases |
| HO.0.5 Research Capsule v1 | JSON Schema, deterministic gate, positive and negative examples |
| HO.0.6 Benchmark family | Seven required categories with provenance, references, metrics and honest maturity |

The remaining work after this baseline is HO.1: turn the inventory vocabulary into backend-neutral observable, measure, time, identity, uncertainty and compatibility contracts.
