# HO.1 Unified Semantic Contracts

Document status: Current HO.1 architecture

Last verified: 2026-08-01

HO.1 establishes the first executable vocabulary shared by future transport portfolios, measurement reconstruction, dynamic physical state and differentiation. It is an SDK-free contract layer, not an automatic integrator scheduler and not a claim that the existing eight integrators have already migrated.

## Module boundary

Two layers carry the contract:

- [`semantic_types.hpp`](../libs/ure_types/include/ure/semantic_types.hpp) contains stable value types needed outside light transport: 256-bit identities, provenance identities, SI dimension vectors, unit mappings, time bases and time intervals.
- [`ure_transport`](../libs/ure_transport/) owns observable, measure, support, estimator, uncertainty, validation and compatibility semantics.

The public structures are trivially copyable and contain no backend SDK type, dynamic string or vector. This allows them to enter native schema, runtime execution graphs, GPU ABI lowering and distributed metadata through explicit versioned adapters rather than compiler-specific object layouts.

`ure::runtime::IdentityDigest` is now an alias of `ure::semantic::IdentityDigest`. Existing runtime ABI continues to use the same `std::array<uint8_t, 32>` representation while world, transport and future reconstruction code gain one common identity type.

## Observable contract

`ObservableDescriptor` separates the mathematical quantity from its storage representation:

| Observable | Required value domain | Important boundary |
|---|---|---|
| Spectral radiance | Spectrum | Cannot be silently treated as display RGB |
| Stokes radiance | Four-component Stokes | Incoherent polarization intensity domain |
| Jones field | Four real components representing two complex amplitudes | Requires coherent phase-reference identity |
| Mutual intensity | Even-sized Hermitian cross-spectral representation | Requires partial-coherence phase-reference identity |
| Transient radiance | Spectrum or Stokes plus time-resolved flag | Not interchangeable with steady-state film |
| Sensor response | Scalar, linear RGB or spectrum | Requires explicit sensor-response identity |
| Loss functional | Scalar | A target for inverse workflows, not a radiance buffer |

Each observable also carries a physical unit. `DimensionVector` stores the seven SI base exponents; `UnitDescriptor` stores scale, optional affine offset and whether the mapping is affine. Incompatible dimensions are undefined. Equal dimensions with different mappings require an explicit value transform.

## Measure and support

`MeasureDescriptor` defines an ordered canonical product of up to eight domains, including path, sensor area, solid angle, surface area, volume, wavelength, time, primary-sample and Markov-chain measures.

Three identities have different roles:

- `integral_identity` states which mathematical integral is being estimated;
- `coordinate_identity` states the current measure parameterization;
- `conversion_identity` identifies a known Jacobian/shift family that may convert compatible parameterizations.

Measure terms must be strictly ordered and nonzero. This removes ambiguous duplicates and makes descriptor equality deterministic. A shared `conversion_identity` permits `RequiresTransform`; it does not perform or validate the Jacobian itself. HT.1 owns those executable transforms.

`SupportDescriptor` records path-event coverage, bounded depth, singular support and whether overlap is known. Optional partition identity/index/count allows techniques to prove disjoint coverage without applying MIS.

## Estimator semantics

`EstimatorDescriptor` composes observable, measure and support with:

- density kind: explicit PDF, unbiased contribution weight, normalized reservoir weight, Markov transition or deterministic solver;
- normalization: sample mean, MIS, reservoir, progressive kernel, chain bootstrap or deterministic;
- correlation: independent, shared-random, adaptive-history, reservoir, Markov-chain or deterministic;
- bias class: unbiased, asymptotically unbiased, consistent, biased preview or unknown research;
- replay, tangent and adjoint capability bits.

Validation enforces important coupled rules. Markov chains require Markov-transition density and chain-bootstrap normalization. Reservoir reuse requires normalized reservoir weights and reservoir normalization. Research descriptors may retain unknown density/normalization for inventory purposes, but the compatibility classifier never combines them.

## Time and provenance

`SemanticContext` binds an estimator execution to:

- world definition;
- world state;
- time sample;
- immutable observation snapshot;
- technique graph;
- measurement schema;
- one validated time basis and observation interval.

Parameter-set, solver-semantics and evidence identities are already present as optional provenance for HD/HW/Research Capsule integration.

Time comparison is deliberately conservative. Equal clock identity with a different rational rate or synchronization epoch returns `RequiresTransform`. Different clock identities or different intervals are not combinable. HO.1 does not approximate rational time by floating point.

## Uncertainty

`UncertaintyDescriptor` records channel count, first/second/cross moments, effective sample size, confidence level, correlation model, calibrated model-confidence status and OOD status.

It rejects second moments without first moments, cross moments for fewer than two channels, invalid confidence intervals, non-finite/negative effective sample size and OOD claims without a calibrated confidence source. It carries statistical semantics only; HR.0 will define the typed storage and accumulation layout.

## Compatibility algebra

The classifier returns one of the five PLAN outcomes and a concrete combination rule:

| Outcome | Representative reason | Rule |
|---|---|---|
| `Compatible` | Same observable/measure with known overlapping support | Multiple importance sampling |
| `Compatible` | Proven disjoint support partition | Direct disjoint-support sum |
| `Compatible` | Reservoir correlation with valid reservoir semantics | Generalized resampling |
| `RequiresTransform` | Same unit dimension, different mapping | Value transform then combine |
| `RequiresTransform` | Same clock, different rational time basis | Temporal transform then combine |
| `RequiresTransform` | Same integral and registered measure conversion | Measure transform then MIS |
| `IndependentAggregate` | Markov-chain family | Independent replicate aggregation, never ordinary MIS |
| `PreviewOnly` | Either estimator is biased preview | Separate preview accumulator |
| `Undefined` | Observable, unit dimension, integral, provenance, clock, interval or support is incompatible/unknown | No combination |

The ordering is safety-relevant: descriptor and provenance validity are checked before any apparent measure compatibility; time and unit transforms are surfaced before combination; preview and correlated families cannot accidentally reach ordinary MIS.

## Current limitations

- Existing `IntegratorMode` and `IntegratorEstimatorMetadata` have not yet been replaced. HT.0 will map each legacy preset to this contract while preserving current output.
- Measure and time transforms are identified but not executed.
- Support descriptors use event masks and explicit partitions; HT.1 still needs compiled path-event grammar and overlap proofs.
- Identity digests are caller-provided values. Canonical hashing of technique graphs, world snapshots and measurement schemas belongs to their owning phases.
- Uncertainty storage, covariance accumulation and effective-sample-size estimators belong to HR.0/HT.2.
- Compatibility decisions do not allocate budget or choose techniques; HT.2/HT.3 own pilot statistics and scheduling.

## Verification

The maintained gates are:

```powershell
.\scripts\check_phase_ho1_semantics.ps1
ctest --test-dir build_modular_x64 -C Release -R "^test_high_order_semantics$" --output-on-failure
```

The same source test is compiled independently through `tests/sdk_free` with MSVC `/W4 /WX`, and the installed package consumer requires and links `UltraRender::ure_transport`. The contract tests cover invalid observable/domain pairs, phase and sensor identities, canonical measures, partition identity, Markov/reservoir coupling, uncertainty, exact MIS, disjoint support, measure/unit/time transforms, preview separation, MCMC aggregation and provenance/time rejection.
