# Phase W.7 Partial-Coherence Reference Transport

Document status: completed reference and statistical contract

Last reviewed: 2026-07-29

W.7 establishes a bounded second-order field model for partially coherent light. It is a host/reference and CUDA reduction layer, not a scene-integrated coherent production renderer. The ordinary CUDA renderer continues to reject `partial_coherence` session requests before GPU scene allocation.

## Statistical representation

`CrossSpectralDensity` stores a Hermitian cross-spectral-density matrix over at most 256 spatial samples at one wavelength. Validation requires finite coordinates and entries, real nonnegative diagonal power, conjugate symmetry and positive semidefiniteness. A Cholesky-style factorization is therefore both the validation boundary and the source for coherent realizations.

`make_gaussian_schell_csd` provides an extended Gaussian-Schell source model with independent beam-radius and coherence-width parameters. `sample_coherent_realization` maps a stable realization identifier to deterministic circular complex-normal samples and applies the covariance factor. Weighted outer products recover the source CSD:

`W(i,j) = sum_r weight_r E_r(i) conjugate(E_r(j)) / sum_r weight_r`.

The host and CUDA implementations use this same estimator. CUDA owns its buffers, queue, fence and timeline through the private runtime backend. Inputs are bounded to 65,536 realizations and invalid shape, weight or covariance results fail closed.

## Generalized rays and ranging references

`GeneralizedRay` carries position, normalized direction, wavelength, complex Jones field, source/group/realization metadata, optical path length and statistical weight. Free-space propagation advances position and optical path length and applies the corresponding Jones phase.

`gaussian_temporal_coherence` supplies the low-coherence envelope used by OCT-style path-delay comparisons. `interferometric_power` evaluates two-beam mutual-coherence power with explicit optical-path difference and wavelength phase. Together these form bounded OCT, coherent lidar and interferometry oracles; they do not claim transient detector bins, arbitrary scene visibility or a production complex path queue.

The single-point Gaussian-Schell realization gate also checks fully developed speckle statistics: unit mean power and unit contrast within finite-sample tolerances.

## Accumulation and merge order

`PartialCoherenceFilm` retains at most 1,048,576 field contributions. Resolution uses the following fixed order:

1. add complex amplitudes sharing pixel, wavelength lane, source, coherence group and realization;
2. square the coherent sum to power;
3. average realization power using statistical weights within each source/group;
4. add distinct source/groups incoherently.

`merge_partial_coherence_film` combines raw contributions transactionally before resolution. This preserves cross-shard interference for the same realization; summing already-resolved radiance would not. Layout, wavelength order, contribution budget and realization-weight consistency are validated before a merged result is accepted.

This in-memory merge is the W.7 sufficient-statistics boundary. It deliberately does not change the distributed framebuffer file format. Versioned complex-field and mutual-intensity shard metadata, phase-reference provenance and portable serialization remain W.11.

## Runtime boundary

The implemented W.7 APIs are correctness references and statistical building blocks:

- Gaussian-Schell extended-source CSD construction;
- PSD and Hermitian validation;
- deterministic coherent realization sampling;
- host/CUDA weighted ensemble-to-CSD reduction;
- Jones/OPL generalized-ray propagation;
- Gaussian temporal coherence and two-beam ranging/interference power;
- partial-coherence film resolution and transactional raw-field merge.

The following remain unavailable in the production renderer:

- scene-integrated complex/Jones wavefront queues;
- arbitrary visibility and material transport of coherent realizations;
- production partial-coherence film output;
- serialized distributed coherent or mutual-intensity shards;
- scalable FFT/tiling/out-of-core propagation;
- transient detector output.

C++, native solver, C ABI and pyure production-session requests continue to fail loudly. W.7 therefore does not alter the default radiometric path or enlarge the supported advanced-integrator combinations.

## Verification

```powershell
ctest --test-dir build_modular_x64 -C Release -R "^(test_wave_optics|gpu_wave_optics|test_native_solver_contract|test_session|test_pyure_smoke)$" --output-on-failure
.\scripts\check_phase_w7_static.ps1
```

The gates cover invalid and non-PSD CSDs, source covariance, deterministic realization convergence, speckle statistics, host/CUDA parity, generalized-ray phase/OPL, temporal coherence, constructive/destructive interference, coherent-before-incoherent averaging, transaction-safe raw-field merge, budgets and production-session rejection.
