# Phase W.11 Coherent Distributed Contract

Document status: completed distributed field contract

Last reviewed: 2026-07-29

W.11 separates radiance, complex-field, mutual-intensity and coherent-realization payloads at the distributed file and merge boundary. It preserves phase-reference, field-layout, source/group and realization provenance, and makes the required averaging order executable. It does not enable the production renderer's currently rejected coherent scene path.

## Frame semantics

Distributed shard metadata v6 adds `DistributedFrameSemantics` with four non-interchangeable kinds:

| Kind | Payload and merge rule |
|---|---|
| `Radiance` | Existing RGB floating-point sums. Only incoherent schedules may use this kind. |
| `ComplexField` | Dense complex field-estimator sums plus per-element estimator weights. Compatible shards add complex amplitudes and weights before normalization. |
| `CoherentRealization` | The same dense complex estimator representation, bound to one source, coherence group and realization ID with a positive statistical realization weight. Shards for one realization merge before any power operation. |
| `MutualIntensity` | A weighted Hermitian positive-semidefinite cross-spectral-density sum plus disjoint realization ranges and total statistical weight. Compatible ranges add before final weight normalization. |

Every non-radiance frame requires non-empty SHA-256 identities for its phase reference and field layout. The layout identity is independently reconstructed from exact dimensions, ordered wavelengths, or the mutual-intensity wavelength and sample coordinates. A coherent worker schedule cannot use the default radiance semantics, and an RGB framebuffer cannot write or merge any coherent kind.

Mutual-intensity provenance records sorted, non-overlapping half-open realization ranges. Range overlap, integer overflow, duplicate realizations, mismatched source/group, phase reference, field layout, resource set, frame index, spectral domain, backend semantics or execution sample coverage fails before mutation.

## Complex field and realization reduction

`DistributedComplexFrameStorage` is bounded to 1,048,576 complex elements. Each element stores an unnormalized complex estimator sum and a non-negative estimator weight. Resolution performs complex division only after all compatible sample shards have merged.

`DistributedPartialCoherenceAccumulator` accepts a completed `CoherentRealization` once. It retains the semantics of every realization, requires one phase/layout identity within each source/group, and converts the normalized field into W.7 raw contributions. W.7 then applies:

1. complex amplitude sum within one realization;
2. magnitude square for that realization;
3. statistical-weight average across realizations in one coherence group;
4. incoherent sum across source/groups.

The validation fixture deliberately merges two quadrature field shards into `0.5 + 0.5i`, obtains power `0.5`, and only then averages it with another realization. Squaring the shards independently would produce a different result and is rejected by the separated contracts.

## Mutual intensity

`DistributedMutualIntensityFrameStorage` carries an unnormalized weighted CSD matrix. Each input matrix must already be finite, Hermitian and positive semidefinite under the W.7 validator. Merge requires identical wavelength/sample layout and disjoint realization ranges, adds the complex matrices and statistical weights transactionally, and normalizes only in `resolved_density()`.

The retained two-realization fixture uses fields `[1, 1]` and `[1, -1]`. Their merged density has unit diagonal and zero off-diagonal coherence, which distinguishes a correct second-moment reduction from either complex-field addition or RGB addition.

## File boundary

The existing distributed range/RGB format advances to version 6 to carry frame semantics; version 4 and version 5 radiance files remain readable. New coherent payloads use distinct complex-field and mutual-intensity magics, a versioned layout, an explicit byte-order marker and a SHA-256 footer over the entire serialized body. The supported x64 writers emit little-endian data; a differently ordered file fails before payload interpretation. The readers also reject truncation, trailing bytes, content changes, oversized arrays and cross-kind reinterpretation.

Complex and mutual files reuse the same resource, spectral, frame, backend/compiler/executable, cache and sample-range provenance as radiance shards. File-level merge calls the same transactional in-memory contracts. A coherent file is never accepted by `read_framebuffer_file()` or `merge_partial_framebuffer()`.

## Boundary

W.11 does not provide:

- production scene-integrated complex/Jones path queues;
- coherent visibility, diffraction or anisotropic interface integration;
- a production coherent film or public C/Python coherent-frame API;
- network transport, worker orchestration or cross-endian execution evidence;
- coherent combination across incompatible clocks, phase origins or field bases;
- automatic conversion between field, mutual-intensity and radiance estimators.

The new files and merges are sufficient-statistics contracts for later coherent execution. Ordinary RGB rendering and its merge behavior remain unchanged.

## Verification

```powershell
ctest --test-dir build_modular_x64 -C Release -R "^(test_distributed_file_io|test_distributed_wave_io|gpu_contract|test_public_surface_sdk_free)$" --output-on-failure
.\scripts\check_phase_w11_static.ps1
```

These gates establish payload separation, provenance, file integrity, merge ordering, transactional rejection and W.7 reduction parity. They do not establish a complete coherent production renderer.
