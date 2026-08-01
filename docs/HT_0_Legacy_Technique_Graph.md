# HT.0 Legacy Technique Graph

Document status: Current HT.0 architecture

Last verified: 2026-08-01

HT.0 describes the existing integrators as an executable, SDK-free graph without changing their CUDA sampling or accumulation order. `IntegratorMode` remains accepted by configuration and APIs, but it is now a legacy preset input rather than the semantic description of an estimator.

## Technique descriptor

[`TechniqueDescriptor`](../libs/ure_transport/include/ure/transport/technique_graph.hpp) binds each node to:

- stable technique, sample-space, parameter, persistent-state and replay-layout identities;
- one of eight existing technique families;
- estimator, proposal-service or replay-kernel role;
- the HO.1 observable, measure, support, density, normalization, correlation and bias contract for contributing nodes;
- explicit ownership of estimate normalization, adaptive state and replay;
- bounded pixel, scene, chain or solver resource scaling, persistent budgets, maximum samples per pass, current CUDA complete-scene capability identity and shared-spectral-primary-sample requirements.

Graph identity is a canonical SHA-256 over every field and edge. Parameter identities include the actual legacy controls relevant to each family, so changing a guiding rate, reservoir policy, manifold bound, VCM radius or MLT chain layout changes the graph identity even when the topology is unchanged.

Per-sample timing and scratch bounds are explicit unknowns rather than invented constants. HT.2 will replace those unknowns with pilot evidence tied to the same graph identity; HT.0 only records the persistent budgets and bounded launch restrictions that the legacy implementation can already prove.

## Existing preset mapping

| Existing configuration | Graph interpretation |
|---|---|
| Wavefront | Independent spectral-Stokes camera-path estimator and defensive baseline |
| PathGuided | Path-guiding proposal service feeding the wavefront estimator |
| ReSTIR DI | Wavefront plus reservoir-normalized direct-light estimator family |
| ReSTIR PT | Wavefront plus replayable reservoir-normalized suffix estimator family |
| SpecularManifold | Wavefront plus bounded singular-support manifold estimator |
| BDPT | Wavefront plus bidirectional connection family under the existing support policy |
| VCM | Wavefront, bidirectional connection and progressive vertex-merge family |
| MLT | Non-contributing wavefront replay kernel feeding an independent Markov-chain estimator |

Independent enable flags are retained because they affect the current implementation. `LegacyExecutionRoute` records their resolved execution meaning in one place. ReSTIR enable flags preserve the existing metadata precedence over the nominal mode. MLT remains an independently normalized route and cannot be flattened into the ordinary estimator family.

## Graph validation

Validation rejects:

- missing, duplicate or reordered node identities;
- invalid contributing estimator descriptors;
- proposal/replay nodes without a consumer;
- malformed or duplicate edges;
- invalid proposal, replay, disjoint-support or coupled-family edge roles;
- mixed observable or integral identity inside one legacy accumulation graph;
- cycles;
- missing estimators;
- non-canonical graph identity.

The generic graph contract contains no `IntegratorMode`; only the legacy adapter includes that enum.

## Structured rejection boundary

`LegacyTechniqueRejection` replaces an undifferentiated collection of mode-specific reasons with three classes:

- `Mathematical`: incompatible measure, normalization, probability, support or correlation policy;
- `Resource`: bounded history, vertex, suffix, queue, launch, grid or memory domain;
- `Unimplemented`: a mathematically possible route whose executable transform, sensor measure or shared spectral sample is absent.

[`legacy_rejection_matrix.json`](research/ht0/legacy_rejection_matrix.json) maps all 16 codes to the pre-HT.0 boundaries. Important examples include selected-mode biased reuse requiring opt-in, ReSTIR DI/PT mutual exclusion, primary-sample measure ownership, MLT/adaptive-scheduler incompatibility, missing shared spectral samples for MLT+BDPT and pinhole light tracing without a sensor-measure estimator. The independent biased-ReSTIR enable path remains described exactly as the legacy executor accepts it; HT.0 does not silently tighten that boundary.

HT.0 does not remove the existing CUDA-side detailed parameter messages. It provides the structured classifier before future graph compilation moves those checks out of legacy dispatch.

## Legacy execution equivalence

`gpu_engine_impl.cpp` now derives estimator metadata mode and ReSTIR policy from `LegacyTechniquePreset::route`. It asserts graph/route equivalence for executable presets. The CUDA queue order, kernels, sample dimensions, PDFs and accumulation paths are unchanged, so this phase cannot alter numerical samples by selecting a new executor.

The executable tests compile all eight presets twice, require stable graph identity, inspect each family’s estimator semantics, mutate parameters to prove identity sensitivity, corrupt graph invariants, exercise every rejection class and verify independent flags resolve to the same legacy route. Existing integrator/session/distributed tests continue to exercise the unchanged production paths.

## Mode-switch freeze

[`legacy_mode_switch_ledger.json`](research/ht0/legacy_mode_switch_ledger.json) records every handwritten production file that still names `IntegratorMode` and its exact occurrence count. The static audit fails if a new file or occurrence appears. Existing entries are compatibility adapters, schema/API projections, runtime lowering or the frozen CUDA legacy dispatcher; new estimator semantics must be added to `TechniqueGraph` first.

## Current limitations

- The graph describes the current estimator families; HT.1 will compile formal support/measure overlap and transformations.
- `CoupledEstimatorFamily` records a current internally coordinated family. It is not evidence that arbitrary external MIS is valid.
- Legacy CUDA dispatch still consumes resolved booleans and mode-compatible routes. HT.0 freezes and describes that path rather than rewriting kernels.
- Existing output remains a spectral/Stokes transport result resolved into the current film boundary. HR.0 must stabilize typed MeasurementBundle storage before large-scale graph execution changes.
- Structured rejection codes classify current causes but do not yet replace every detailed user-facing message.

## Verification

```powershell
.\scripts\check_phase_ht0_technique_graph.ps1
ctest --test-dir build_modular_x64 -C Release -R "^test_technique_graph$|^test_integrator$|^test_session$|^test_distributed_file_io$" --output-on-failure
```

The technique graph sources and tests also compile in the independent `tests/sdk_free` build with warnings as errors, and the installed-package consumer compiles a default legacy preset.
