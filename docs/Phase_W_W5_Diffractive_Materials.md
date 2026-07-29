# Phase W.5 — Diffractive Material Operators

## Scope

W.5 adds explicitly enabled diffractive thin-sheet scattering to the ordinary radiometric CUDA wavefront integrator. It does not add coherent path state, optical-path phase accumulation between vertices, holographic reconstruction, or a scene-scale Maxwell solver.

The supported MaterialGraph surface nodes are:

- rectangular amplitude grating;
- sinusoidal phase mask;
- ideal continuous-phase zone plate;
- blazed diffractive optical element;
- imported RCWA/FMM scattering table.

Each node is a terminal surface operator connected directly to `OutputSurface`. Mixing or layering these delta-order operators with the Phase M opaque and dielectric BSDF nodes remains unsupported.

## Scene and adapter contract

`DiffractiveOperator` is SDK-free SceneIR data. Analytic operators carry period, UV-frame orientation, order bound and operator-specific parameters. Scattering-table operators additionally carry a stable table identity and wavelength/incidence/order/side samples of a full complex 2×2 Jones matrix.

The native FlatBuffers schema appends the new enum values and optional operator field without renumbering the frozen W.2-era values. Canonical `.ure` text and binary `.urescene` preserve the same data. The MaterialX adapter uses URE custom nodes and a deterministic bounded table projection; MaterialX remains an adapter rather than the authoritative schema.

Each table is limited to 4,096 entries. Every wavelength/incidence sample point must expose the same complete set of order/side channels. Duplicate tuples, invalid enum values, non-finite coefficients and out-of-range orders are rejected.

## Physical and numerical contract

For every wavelength/incidence sample point, validation forms the joint polarization power operator

`A = Σ J(order, side)† J(order, side)`.

The largest eigenvalue of `A` must not exceed one within the numerical tolerance. This is stronger than checking unpolarized average power and prevents a table that is passive for one polarization mixture from amplifying another.

Host and CUDA table evaluation select the same deterministic four nearest wavelength/incidence sample points and interpolate complex amplitudes before converting to power. Because all channels share the same sampling grid, the common convex interpolation preserves the joint contraction boundary.

Analytic orders use the vector grating momentum condition. Non-propagating orders retain an evanescent decay classification in the host oracle and are excluded from radiometric continuation on the GPU. The surface frame comes from the actual UV tangent for triangle and instanced geometry and from the analytic spherical UV tangent for spheres. Zone-plate phase gradient is radial in that frame.

## CUDA estimator

Packet transport splits into wavelength lanes at a diffractive event. Each lane samples one propagating order with probability proportional to its polarization-dependent efficiency and applies the corresponding efficiency/PDF weight. The full Jones matrix transforms the lane Stokes state, including cross-polarization terms.

The operator is a zero-thickness sheet. Reflection and transmission change ray direction but do not implicitly enter or leave a dielectric medium. A later material contract would need an explicit interface medium before such a transition could be valid.

Diffractive operators require `wave_optics.diffractive_materials.enabled`. The implemented boundary is radiometric `Wavefront` without camera diffraction, coherent/partial-coherent features, fluorescence, local full-wave coupling, path guiding, ReSTIR, manifold, BDPT/VCM or MLT. Unsupported combinations reject before rendering.

The GPU stores operators and scattering entries in separate immutable scene allocations. Any material hot update that would add, remove or alter a diffractive material therefore requires retained-scene recompilation and a full GPU reload.

## Validation

The W.5 gate covers:

- analytic grating angles, energy and propagating/evanescent classification;
- phase-mask and zone-plate bounded energy;
- complex table interpolation, shared-grid completeness and joint Jones passivity;
- native binary/text and MaterialX round trips;
- compiler and C/pyure/native-solver feature gates;
- actual CUDA Jones-to-Stokes response;
- actual CUDA order transport, table upload/interpolation, UV orientation and disabled-gate rejection;
- fail-loud rejection of diffractive material hot updates that would leave immutable operator resources stale;
- static checks for the thin-sheet medium boundary and host/device interpolation contract.

Run:

```powershell
.\scripts\check_phase_w5_static.ps1
ctest --test-dir build_modular_x64 -C Release -R "test_wave_optics|test_materialx_io|test_native_scene_ir|test_native_solver_contract|test_session|test_pyure_smoke|gpu_wave_optics" --output-on-failure
```
