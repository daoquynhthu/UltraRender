# Phase W.10 Local Full-Wave Coupling

Document status: completed provider and cache contract

Last reviewed: 2026-07-29

W.10 defines a bounded, SDK-free exchange boundary between UltraRender and local electromagnetic solvers. It accepts RCWA, FDTD, FEM, BEM, FMM, DDA and precomputed S-matrix providers, and imports their result as the W.5 complex Jones scattering-table resource. It does not discretize a complete scene or claim that these solvers are bundled with UltraRender.

## Request envelope

`LocalFullWaveRequest` identifies one local optical cell and contains:

- an exact solver kind and minimum provider version;
- content-addressed geometry and material payloads, each verified against a lowercase SHA-256 digest;
- strictly ordered wavelength and incident-cosine grids;
- a bounded diffraction-order interval and explicit reflection/transmission sides;
- period, orientation, tolerance, memory budget, iteration budget and deterministic-execution requirement.

The combined input payload is limited to 16 MiB. The requested Cartesian grid must fit the existing bounded W.5 scattering-table capacity. Invalid hashes, grids, polarization bases, order ranges, budgets and non-finite physical values fail before provider invocation.

Requests use a versioned little-endian binary envelope. Their semantic digest excludes the human-facing `request_id`, but includes every solver, payload, sampling and budget field that can affect the result.

## Provider negotiation

`LocalFullWaveProviderDescriptor` declares a stable provider identity and semantic version, executable and solver-semantic digests, supported solver kinds, sample/table limits, memory capacity and determinism. `LocalFullWaveRegistry` requires an exact provider identity and rejects unsupported solver kinds, insufficient versions or capacities, and a nondeterministic provider when determinism is required.

The actual invocation is an injected byte callback. The core library neither searches the ambient machine for solvers nor starts arbitrary processes. Phase X may map a stable dynamic ABI or isolated external runner onto this byte contract without changing the physical request or result identity.

## Verified result

A provider returns `ure.local-fullwave.scattering/1.0` with:

- the request semantic digest;
- exact provider version, executable digest and semantic digest;
- one W.5 `DiffractiveOperatorKind::ScatteringTable`;
- convergence state, iteration and peak-memory counts;
- residual, reciprocity-error and energy-error evidence;
- a digest of the provider's native solver artifact.

The envelope also carries a canonical SHA-256 content digest over provider provenance, the scattering table and all evidence fields. The table must exactly cover every requested wavelength, incidence, order and side once. Its period, orientation and order bound must equal the request. W.5 validation then independently enforces finite complex coefficients, shared channel coverage and joint Jones passivity through the maximum eigenvalue of the accumulated `ΣJ†J` power matrix. Missing channels, duplicate samples, modified cache content, active tables, mismatched provenance, unconverged results, excessive residuals or exceeded budgets fail closed. An S-matrix import may report zero solver iterations, but it must still provide convergence, error and provenance evidence.

Verified tables pass unchanged into the existing W.5 host interpolation and CUDA diffractive-material path. The GPU gate constructs a verified local-solver artifact before loading its table into the actual CUDA wavefront renderer.

## Deterministic cache

The cache key combines the semantic request digest with provider identity, semantic version, executable digest and semantic digest. Human request labels therefore do not fragment the cache, while any physical input or provider-binary/semantic change invalidates it.

`LocalFullWaveCache` is byte-budgeted and stores only artifacts that validate against the supplied request and descriptor and survive a binary round trip. Conflicting entries and insufficient cache capacity are errors at the registry boundary rather than silent cache misses. Binary read/write functions allow a caller to persist the same validated envelope without making filesystem policy part of the rendering core.

## Boundary

W.10 does not provide:

- a bundled RCWA/FDTD/FEM/BEM/FMM/DDA implementation;
- engine-owned subprocess discovery or execution;
- a stable dynamically loaded C ABI, which remains Phase X work;
- scene-scale Maxwell meshing or global full-wave propagation;
- coherent distributed complex-field merging, which remains W.11;
- scene-integrated anisotropic boundary matching from W.9.

The authoritative result is the verified W.5 scattering table plus solver evidence. Native solver meshes, checkpoints and proprietary binary formats remain provider-owned and are represented by content digest, not copied into SceneIR.

## Verification

```powershell
ctest --test-dir build_modular_x64 -C Release -R "^(test_local_fullwave|gpu_wave_optics|test_public_surface_sdk_free)$" --output-on-failure
.\scripts\check_phase_w10_static.ps1
```

These gates establish deterministic capability negotiation, serialization, cache invalidation, strict result import and CUDA consumption. They do not establish a general-purpose full-wave scene renderer.
