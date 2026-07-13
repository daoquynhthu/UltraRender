# Phase R-P6 Mie Volume Resources

## Status

R-P6 is complete. UltraRender now treats Mie scattering as a validated spectral resource instead of an unsupported phase selector. The default radiometric renderer remains unchanged for HG and Rayleigh media.

## Architecture

`ure_types` owns the backend-neutral `MiePhaseResource` and canonical validation/hash contract. `ure_sceneio` imports and exports the versioned `ure-mie-phase-table` JSON exchange format. `ure_core` owns deterministic Lorenz-Mie generation, SceneIR compilation, GPU flattening, lookup, sampling, and wavefront transport.

Generated and imported resources follow the same path:

```text
physical parameters -> Lorenz-Mie generator --+
                                               +-> immutable MiePhaseResource -> SceneIR -> GPU tables
external JSON table -> strict adapter --------+
```

The CUDA path never solves the Mie series during rendering.

## Physical Contract

- Wavelength is vacuum wavelength in nanometres.
- Radius is in metres; cross sections are in square metres.
- `medium_density` is particle number density in inverse cubic metres.
- The size parameter is `2 pi n_host r / lambda_vacuum`.
- Particle refractive index uses `n + i kappa`; the coefficient recurrence uses the relative particle/host index.
- Radius mixtures are weighted by scattering cross section before angular normalization.
- Runtime scattering and extinction coefficients are number density multiplied by their independently interpolated tables. Absorption is retained as the validated `Cext - Csca` derived resource. Empirical medium coefficient fields must remain zero for Mie media.

The host solver uses double-precision complex Lorenz-Mie coefficients, guarded downward logarithmic-derivative recurrence, forward Riccati-Bessel recurrence, symmetric representable endpoint-refined angular grids, and explicit output, working-set, series-term, and angular-operation budgets. Angular convergence jointly checks coefficient-derived scattering cross section, successive asymmetry, and successive cumulative angular distribution before the final table is strictly normalized on its actual grid. The 16385-sample maximum grid is regression-tested for strict float monotonicity.

The fixed `x=1`, `m=1.5+i1` regression values are cross-checked against the Bohren-Huffman BHMIE convention as published with Bruce Draine's reference implementation and use the Wiscombe series truncation/downward-recurrence regime. The project convention is vacuum wavelength, relative particle/host index, and `n+i kappa` absorption.

References:

- W. J. Wiscombe, *Improved Mie scattering algorithms*, Applied Optics 19(9), 1980: <https://pubmed.ncbi.nlm.nih.gov/20221065/>
- B. T. Draine, Bohren-Huffman Mie reference implementation: <https://www.astro.princeton.edu/~draine/code/bhmie.f>

## Resource and Runtime Contract

The resource stores wavelength and nonuniform cosine grids, normalized phase rows, exact piecewise-linear CDF rows, scattering/extinction/absorption cross sections, asymmetry, polarization metadata, provenance, and a canonical physical-content hash. Validation rejects non-positive wavelengths, invalid enums, non-finite values, invalid ordering or dimensions, incomplete angular coverage, and energy violations. Rows within the declared input tolerance are canonicalized together so the stored phase and CDF describe exactly the same proposal. `source_hash` is opaque provenance metadata; only the recomputed physical `content_hash` participates in identity and deduplication.

The compiler always revalidates and recomputes the fingerprint. Deduplication uses hash plus canonical physical equality, so pointer identity, provenance, and stored hashes are not trusted. Mie requires a table covering the renderer wavelength domain; HG and Rayleigh reject attached Mie resources.

GPU resources are flattened once per device with checked offsets and independent phase/CDF/cross-section total-length bounds. Explicit resident spectral budgets include Mie tables before allocation. Lookup fails for uncovered wavelengths, invalid indices, or malformed descriptors instead of reading out of bounds; only direction-dot-product drift within four float ULP is clamped to `[-1,1]`. Sampling analytically inverts each positive-mass piecewise-linear CDF cell. Packet continuation samples the equal active-lane mixture and weights every lane by `phase_lambda / proposal_pdf`; NEE uses the same wavelength-dependent phase values and records the matching scalar proposal metadata. Any strictly positive extinction remains active, without an absolute density threshold that silently turns thin media into vacuum.

R-P6 uses a scalar-radiometric depolarizing Mie approximation: outgoing intensity is preserved while `Q`, `U`, and `V` are zeroed for both direct-light shadow state and continuation. Polarized Mie Mueller-matrix scattering remains future work.

## Mutation and Lifecycle

Mie resource kind, grid, table, cross-section, or polarization changes require a retained-SceneIR full reload. The session deep-freezes retained global and material Mie resources, so a caller retaining a mutable alias cannot alter the session snapshot behind the mutation API. Equal-content replacement pointers and density-only changes retain material hot-update semantics. Comparisons canonicalize both resources and use collision-safe physical equality; stale or forged stored hashes do not control the decision.

The end-to-end GPU test loads one imported table and creates one generated table, compiles two simultaneous resources, renders lit global and bounded Mie media, requires nonzero finite radiance and a measurable difference from a zero-density reference, reloads the retained scene, destroys the renderer, and repeats creation/rendering. Single-device and multi-GPU ownership tests separately cover offset isolation and destruction.

## Validation Boundary

Targeted gates cover:

- fixed external `Qext`, `Qsca`, and asymmetry references;
- Rayleigh limit, successful absorbing `x` near 100 convergence, nonabsorbing closure, absorbing energy bounds, radius-mixture identity, determinism, maximum-grid monotonicity, and output/working-set/operation budget failure;
- JSON version/unit/dimension/unknown-field/import-budget rejection and byte-repeatable export;
- compiler fail-loud rules, canonical deduplication, and forged-hash rejection;
- GPU lookup bounds, independent CDF bounds, direction ULP handling, direct extinction interpolation, exact CDF inversion, packet proposal parity, proposal-vs-uniform variance, polarization, resident-budget rejection, and lifecycle;
- Session equal-resource hot update, changed-resource reload, and retained mutable-alias isolation;
- `gpu_volume`, `gpu_render`, Phase L/R static audits, and the full project test gate.

R-P6 does not add heterogeneous density fields, nonspherical particle solvers, dependent scattering, coherent volume propagation, or polarized Mie matrix tables. Those require later resource/schema or wave-optics phases.
