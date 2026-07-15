# R-P6 Mie and Volume Phase Resources Design

> Archive status: historical design record. R-P6 is complete; use `PLAN.md`, `STATUS.md`, and current source/tests for present state.

## Status and Scope

R-P6 closes the production gap between the existing Henyey-Greenstein and Rayleigh phase implementations and the currently unsupported `Mie` selector. The production GPU path consumes validated tabulated resources. Tables can be generated deterministically by an in-engine host Lorenz-Mie solver or imported from an external authority through the same validation boundary.

This phase covers homogeneous non-magnetic spheres, monodisperse particles, discrete radius distributions, and log-normal radius distributions. It does not claim support for non-spherical particles, dependent scattering, near-field coupling, coherent volume transport, or full Mueller-matrix volume scattering. Those requests must remain unavailable or fail loudly.

## Architectural Decision

The runtime table is the sole production representation:

```text
Physical sphere parameters -> Host Lorenz-Mie solver --+
                                                       +-> validated MiePhaseResource -> SceneIR -> GPU table
External table -----------> Mie table importer --------+
```

The pure structural validator and canonical physical hash are header-only resource-contract logic in `ure_types`, so both `ure_core` and `ure_sceneio` consume one implementation without creating a core-to-I/O dependency. `ure_sceneio` exposes compatibility wrappers and owns only table serialization.

The CUDA renderer never evaluates spherical Bessel functions or the Lorenz-Mie series per scattering event. This keeps numerical generation independent from the wavefront runtime and gives generated and measured tables identical GPU semantics.

## Public Host Types

The following host concepts belong in `ure_types` so SceneIR can own them without depending on CUDA or scene I/O:

- `VolumePhaseFunction`: `HenyeyGreenstein`, `Rayleigh`, or `Mie`.
- `MieRadiusSample`: radius in metres and a non-negative number-density weight.
- `MieRadiusDistribution`: monodisperse, explicit discrete samples, or log-normal parameters compiled to deterministic quadrature samples.
- `MieOpticalSample`: wavelength in nanometres, particle complex refractive index, and host refractive index.
- `MieGenerationConfig`: optical samples, radius distribution, angular grid controls, and explicit resource budgets.
- `MiePhaseResource`: wavelength grid, monotone cosine grid, wavelength-major normalized phase values, wavelength-major CDF values, scattering/extinction/absorption cross sections, asymmetry values, provenance, and a deterministic content hash.

`MaterialNode` and `SceneIR` gain a phase selector and an optional `shared_ptr<const MiePhaseResource>` for bounded material media and the global medium respectively. Builders remain mutable only until validation freezes the resource. HG keeps using `medium_anisotropy`; Rayleigh requires no extra resource; Mie requires a valid resource and requires `medium_anisotropy == 0`. In Mie mode, `medium_density` is finite non-negative particle number density in inverse cubic metres and the runtime derives `sigma_s` and `sigma_a` from the table cross sections. The legacy empirical `medium_scattering` and `medium_absorption` coefficients must remain zero for Mie media, otherwise compilation rejects the ambiguous double specification. HG and Rayleigh reject attached Mie resources.

All physical units are explicit. Radius uses metres, wavelength is vacuum wavelength in nanometres, cross sections use square metres, phase values use inverse steradians, and refractive indices are dimensionless. The size parameter is `x = 2*pi*n_host*r/lambda_vacuum`, the relative refractive index is `m = n_particle/n_host`, the complex-index convention is `n + i*kappa` with `kappa >= 0`, and the host index is real and strictly positive.

## Host Lorenz-Mie Solver

The host solver uses double-precision complex arithmetic and a Wiscombe-style stable series evaluation:

- Size parameter is computed in the host medium.
- The truncation order is derived from size parameter and guarded by a configured maximum term count. The logarithmic-derivative recurrence starts above both the scattering truncation order and `ceil(abs(m) * x)`, with an explicit guard margin.
- Logarithmic derivatives use stable downward recurrence.
- Riccati-Bessel functions use forward recurrence after stable initialization.
- Complex Mie coefficients produce amplitude functions and efficiency factors.
- Unpolarized intensity uses the two amplitude components and is normalized over solid angle.
- Radius distributions accumulate differential scattering cross section and total cross sections using normalized non-negative number-density weights. The resulting mixture is `sum(w_i * dCsca_i/dOmega) / sum(w_i * Csca_i)`, never a number-weighted average of already normalized phase functions.
- Log-normal distributions are converted to a deterministic finite quadrature before solving.

Every generated wavelength slice is independently normalized. Absorption equals extinction minus scattering within numerical tolerance and may not be negative outside that tolerance. The generated asymmetry value is independently integrated from the normalized phase table.

The angular grid is monotone in `cos(theta)` and supports adaptive forward-peak refinement. Before normalization, each wavelength compares the angular integral of differential scattering cross section with the coefficient-derived scattering cross section. Refinement continues until the configured tolerance is met; exhausting the angular-sample or term budget is a hard failure. This prevents normalization from hiding an unresolved high-size-parameter forward peak. The runtime representation does not assume a uniform grid.

## External Table Adapter

`ure_sceneio` provides a versioned JSON table adapter as an exchange format until Phase Q supplies the native scene/package schema. The adapter maps directly to `MiePhaseResource` and does not define runtime semantics.

The document records units, wavelength and cosine grids, phase rows, mandatory scattering/extinction cross sections, optional asymmetry values, provenance, and source hash. Absorption, CDF rows, and omitted asymmetry values are derived during validation. Unknown required versions, inconsistent dimensions, invalid units, non-finite values, negative densities, and non-monotone grids are rejected.

## Validation Boundary

A single `validate_mie_phase_resource` path is used for solver output, imported data, and SceneIR compilation. It checks:

- At least two strictly increasing wavelengths.
- At least two strictly increasing cosine samples spanning `[-1, 1]` within tolerance.
- Exact table dimensions and finite values.
- Non-negative phase density and cross sections.
- Per-wavelength solid-angle normalization.
- Monotone CDF rows starting at zero and ending at one.
- Consistency of scattering, absorption, and extinction.
- Asymmetry in `[-1, 1]` and agreement with independent integration.
- Checked multiplication and byte budgets before allocation or GPU upload.
- Stable deterministic physical-content hashing over canonical values. Hashing excludes `content_hash` and provenance, canonicalizes signed zero, serializes fixed-width IEEE fields in a defined byte order, and remains collision-checked by canonical equality. Provenance and source hashes are retained separately.

Structural validation is independent of a renderer configuration. Scene compilation separately requires coverage of the configured wavelength domain; runtime lookup never clamps an uncovered wavelength to a table endpoint. The compiler always revalidates and recomputes the canonical fingerprint rather than trusting a stored hash. Invalid Mie requests fail before GPU initialization. A Mie selector without a resource never falls back to HG or Rayleigh.

## GPU Representation and Lookup

The GPU representation uses compact resource descriptors into flattened scene-owned arrays:

- wavelength grid;
- cosine grid;
- phase values;
- CDF values;
- scattering, extinction, absorption, and asymmetry arrays.

Descriptors contain offsets and counts, not owning pointers. The scene owns each flattened allocation and frees it with the existing context lifecycle. Scene compilation deduplicates shared resources by deterministic content hash and verifies equality on hash collisions.

Evaluation performs bounded interpolation over wavelength and cosine. Each CDF node stores the exact `2*pi*integral(p(mu), dmu)` mass of the same piecewise-linear phase representation used by evaluation. Sampling locates a positive-mass cell, skips zero-mass plateaus, and analytically inverts the cell's linear density with a stable quadratic/linear branch. A unit random input is mapped below one. The returned solid-angle PDF is the same piecewise-linear `p(mu)` evaluated by direct lookup.

For wavelengths between table rows, sampling treats the two neighbouring rows as a linear mixture: the wavelength interpolation fraction selects a row and the selected portion of the random value is remapped into that row's CDF. Evaluation uses the identical linear mixture, so sampling and PDF remain closed without constructing a temporary CDF on the GPU.

## Spectral Estimator Contract

Mie scattering is wavelength-dependent and cannot rely on the HG cancellation `phase / pdf = 1`.

- Sampled-wavelength paths sample the active wavelength distribution.
- Packet paths sample an equal-weight mixture of active packet-lane distributions.
- The existing two volume phase dimensions remain sufficient: the first random value selects a packet lane and is remapped into that lane's CDF sample; the second selects azimuth.
- The actual scalar proposal is the equal-weight mixture PDF evaluated for the sampled direction.
- Every lane applies `throughput[lambda] *= phase(lambda, direction) / proposal_pdf(direction)`.
- `last_pdf` records the actual scalar proposal PDF.
- Volume NEE evaluates the wavelength-dependent phase for every lane.
- Shadow, path-guiding, and ReSTIR metadata record the same scalar proposal convention used by continuation sampling.

For Mie media, extinction sampling uses `particle_number_density * extinction_cross_section(lambda)`, scattering throughput uses `particle_number_density * scattering_cross_section(lambda)`, and absorption is their validated difference. HG and Rayleigh retain the existing empirical coefficient resources multiplied by their existing density scale.

HG and Rayleigh pass through the same dispatcher while preserving their current numerical behaviour. This makes the estimator contract uniform without forcing those analytic models into table resources.

R-P6 is scalar radiometric Mie. At a Mie scattering event it applies a documented depolarizing approximation: the outgoing Stokes state preserves only radiometric intensity and sets `Q`, `U`, and `V` to zero. The resource metadata records this polarization model. No code path may imply polarized Mie or silently preserve incident polarization.

## Scene Compilation and Mutation

The scene compiler validates and deduplicates phase resources, assigns resource indices to global and material media, and emits GPU descriptors. It saves the compiled canonical fingerprint for later mutation classification. Global medium configuration is copied into the compiled scene alongside its phase selector and resource index. The single-GPU and multi-GPU drivers receive the same immutable host carriers; every device context owns and frees its own uploaded arrays.

Changes to density, anisotropy, scattering, or absorption that do not change phase-resource topology retain existing update semantics. Changes to phase kind, wavelength grid, angular grid, table contents, distribution parameters, or optical constants require a resource rebuild. A `SceneDiff` material mutation that changes a Mie resource triggers a safe retained-SceneIR full reload rather than attempting an in-place pointer replacement. Global medium resource changes already use scene replacement and full reload.

Accumulation is reset after every medium or phase mutation.

## Error Handling

Public host APIs throw `std::invalid_argument` for malformed physical input and `std::runtime_error` for resource generation, import, compilation, or budget failures. Error messages identify the wavelength/radius/table row and violated invariant where applicable.

GPU dispatch treats an invalid descriptor as unsupported and contributes no invalid memory access, but host validation is responsible for rejecting the scene before launch. CUDA allocations and uploads use the existing checked error policy. There is no silent clamping of invalid optical parameters beyond documented floating-point normalization tolerances.

## Test Strategy

Host tests cover:

- Rayleigh-limit behaviour for sufficiently small size parameter.
- Known Lorenz-Mie reference efficiencies and angular values.
- Fixed external-authority reference values with recorded source and tolerances, including a high-size-parameter forward-scattering case.
- Non-absorbing energy closure and absorbing extinction/scattering ordering.
- Monodisperse, discrete, and log-normal deterministic generation.
- Two-radius differential-cross-section mixture identity and adaptive angular convergence.
- Angular normalization, CDF monotonicity, asymmetry integration, and content hashing.
- External table round-trip and every fail-loud validation boundary.
- Scene compiler deduplication, resource budget rejection, and mutation/reload classification.

GPU tests cover:

- Wavelength/cosine interpolation.
- Mie `eval/pdf/sample` equality at sampled directions.
- Per-wavelength normalization and sampler histograms.
- Non-uniform grids with strong within-cell slopes, zero-mass plateaus, endpoint random values, and Monte Carlo moment checks for exact piecewise-linear inversion.
- Packet-mixture proposal and per-lane `phase / proposal` weighting.
- Sampled-wavelength active-lane behaviour.
- Volume NEE and continuation PDF consistency.
- HG and Rayleigh regression parity.
- A sharply forward-scattering variance comparison against uniform-sphere direction sampling.
- Missing, malformed, and out-of-range descriptor fail-closed behaviour.
- Single- and multi-GPU ownership, two-resource offset isolation, reload, and free/reload lifecycle.

End-to-end gates cover both generated and imported resources through SceneIR, collision-safe compiler deduplication, upload, global and bounded-material rendering, retained-scene reload, and destruction. Mutation tests cover replacement pointers, forged stale hashes, and compile-time immutability.

The full Release build, Phase L and R static audits, physics-optics gate, and complete CTest suite remain mandatory. GPU-heavy targets are built serially in this environment.

## Completion Criteria

R-P6 is complete only when:

- `VolumePhaseFunction::Mie` is a production selector backed by a validated resource.
- Host generation and external import produce the same resource contract.
- Mie evaluation, PDF, sampling, NEE, continuation, and spectral weighting are closed and tested.
- HG and Rayleigh remain green and unchanged in physical meaning.
- Missing resources and invalid parameters fail loudly before rendering.
- Material and global-medium resource mutation semantics are explicit and tested.
- Correctness, normalization, energy, interpolation, sampling, and variance gates pass in Release.

## Numerical References

- W. J. Wiscombe, “Improved Mie scattering algorithms,” *Applied Optics* 19(9), 1980, DOI 10.1364/AO.19.001505.
- B. T. Draine's maintained BHMIE implementation derived from Bohren and Huffman, including coefficient, amplitude, efficiency, and asymmetry conventions: `https://www.astro.princeton.edu/~draine/code/bhmie.f`.
