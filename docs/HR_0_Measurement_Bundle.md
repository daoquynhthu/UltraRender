# HR.0 MeasurementBundle / Feature Film

Document status: Current HR.0 architecture

Last verified: 2026-08-01

HR.0 establishes the first stable, SDK-free measurement and feature-film data contract. It preserves physical observable, statistical, attribution, geometry and provenance semantics instead of treating every renderer or solver result as RGB plus incidental AOVs.

## Module boundary

[`ure_reconstruction`](../libs/ure_reconstruction/) owns `MeasurementSchema`, `MeasurementBundle`, budget selection, canonical shard merge, derived sufficient statistics and checkpoint adaptation. Its public headers expose no CUDA, Vulkan, D3D12, Windows or OpenUSD type. The module reuses the authenticated, front-indexed `UREM` container from `ure_research`; it does not duplicate a second generic artifact envelope.

The module is a data and reduction boundary. Existing CUDA kernels still populate the legacy framebuffer and AOV storage; HR.0 does not claim that every declared plane already has a production GPU producer. Later transport, reconstruction and inverse phases can add producers without changing the meaning of stored measurements.

## Typed plane model

Every plane declares a stable semantic identity, scalar representation, element/component shape, SI unit, retention class, merge rule and optional required validity mask. Observable planes additionally carry the HO.1 `ObservableDescriptor`.

The plane vocabulary covers:

- spectral/Stokes observable contributions, detector and transport wavelength, and joint PDF;
- technique, support, estimator weight, sample/range and path-event attribution;
- depth, time of flight, optical path length, material, medium and resource identity;
- normal, albedo, depth, UV and motion with a required validity plane;
- count, first/second/cross moments, effective sample count, variance and covariance;
- bounded sample records;
- distinct complex-field, Jones-field and mutual-intensity planes.

Jones and complex planes require a coherent `JonesField` observable and complex scalar storage. Mutual intensity requires its own HO.1 observable. These planes cannot masquerade as radiance or linear RGB.

Payload scalar words are canonical little-endian values. Complex scalar types are adjacent real/imaginary words. The schema identity hashes every field, including observable, unit, validity dependency and derivation inputs.

## Budget and loss semantics

Retention is ordered as `Required`, `Statistics`, `Attribution`, `Geometry` and `SampleRecords`. Required planes must fit or selection fails before allocation. Optional planes are considered deterministically by retention and schema order. A plane omitted because of the byte budget or caller retention ceiling produces a `MeasurementSelectionLoss` containing its identity, class, byte cost and reason.

The selected schema receives a new identity. A consumer therefore cannot mistake a degraded feature film for the requested full schema.

## Accumulation and distributed merge

Four explicit merge rules prevent accidental arithmetic on unlike data:

- `Sum` combines additive counts, observable sums and sufficient statistics with integer-overflow and finite-float checks;
- `RequireEqual` protects geometry, masks and invariant identities;
- `Append` concatenates bounded sample records and rejects capacity overflow;
- `Derived` ignores shard-local derived values and recomputes effective sample count, sample variance or sample covariance from merged additive sources.

Bundle provenance binds world definition/state, time sample, observation snapshot, Technique Graph, measurement schema, parameter/solver context, exposure interval, sample namespace, producer and sorted disjoint sample ranges. Merge rejects overlap or any physical/provenance mismatch. Inputs are sorted by sample range and producer identity before arithmetic, so caller or device order cannot change the reduction order. Aggregate producer/evidence identities are content-derived.

## Checkpoint and partial read

A checkpoint stores one self-contained metadata chunk followed by one authenticated chunk per selected plane in the HO.2 `UREM` container. Metadata round-trips the complete schema, provenance, exposure and sample-range set. Plane chunks preserve semantic identity, version, shape and component count; validity masks may use bounded deterministic RLE.

The partial-read path reads only the fixed header/front directory, verifies the container identity, locates a plane by ordinal and semantic identity, then authenticates and decodes only that stored byte range. Full reads additionally bind the metadata digest to the artifact source identity and reject missing, duplicate, reordered, malformed or corrupted chunks.

## Current boundary

- HR.0 defines merge-safe storage, not a denoising algorithm; HR.1 owns the first reconstruction baseline.
- Derived variance/covariance uses additive raw sums and sample count. Correlated Markov/reservoir estimators must provide suitable effective-count or covariance sufficient statistics rather than pretending samples are IID.
- Geometry equality is exact inside one snapshot. Dynamic histories require a new snapshot/time identity and HR.1 validity policy.
- Checkpoint publication and filesystem transactions remain caller responsibilities; the byte container supports local, remote or object-store ranges.
- The current UREM compression set is `None` and bounded RLE. A future scientific codec requires deterministic identity and decompression budgets.

## Verification

```powershell
.\scripts\check_phase_hr0_measurement_bundle.ps1
ctest --test-dir build_modular_x64 -C Release -R "^test_measurement_bundle$" --output-on-failure
```

The same implementation and tests compile independently in `tests/sdk_free` with warnings as errors. The installed-package consumer requires `UltraRender::ure_reconstruction` and includes its public schema header.
