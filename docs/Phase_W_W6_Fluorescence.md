# Phase W.6 — Fluorescence and Phosphorescence

## Scope

W.6 adds explicitly enabled fluorescence to the ordinary radiometric CUDA wavefront integrator. The supported material is an inelastic diffuse surface transition described by an excitation-to-emission resource. A nonzero lifetime also carries a sampled phosphorescence delay through the path queue.

This implementation is steady-state. The delay state is preserved for future transient-film work, but the current Beauty film does not time-bin or otherwise display it. Anti-Stokes fluorescence, coherent emission, participating fluorescent media and advanced integrator combinations are outside this boundary.

## Resource contract

`FluorescenceResource` is SDK-free SceneIR data containing:

- strictly increasing excitation and emission wavelength axes;
- excitation efficiency and photon quantum yield per excitation sample;
- one normalized piecewise-linear emission density row per excitation sample;
- a stable resource identity and nonnegative lifetime.

The rectangular matrix is limited to 4,096 entries. All values must be finite. Efficiency and yield are bounded by one, every emission row integrates to one, and positive emission support must lie strictly above the largest excitation wavelength. This intentionally accepts only Stokes-shift resources and makes radiant-energy conservation locally checkable.

Native text and FlatBuffers serialization preserve the complete resource. MaterialX uses the custom `URE_bsdf_fluorescence` adapter node; MaterialX remains an interchange adapter rather than the authoritative resource model.

## Forward and adjoint transport

The host forward oracle samples an emission wavelength for a known excitation wavelength. Photon quantum yield is converted to radiant energy by

`efficiency × quantum_yield × lambda_excitation / lambda_emission`.

The strict Stokes shift therefore prevents this transition from increasing radiant energy.

Production camera paths run in the adjoint direction. At an observed emission wavelength the CUDA path samples a shorter predecessor excitation wavelength from the piecewise-linear adjoint kernel. It multiplies the existing wavelength density by the transition density and preserves the original film wavelength separately from the current transport wavelength. Environment, emitter and shadow contributions are evaluated at the transport wavelength but converted to display color at the detector wavelength.

The transition depolarizes the selected lane, preserves the current medium and samples an exponential delay from the configured lifetime. Packet paths split into individual wavelength lanes at the event. Detector wavelengths and lifetime consume optional queue storage only when fluorescence is enabled; its size is checked against the backend budget and available device memory before allocation. Fluorescence operator and matrix allocations are immutable; adding, removing or changing one requires a full retained-scene reload.

## Feature boundary

The feature requires `wave_optics.fluorescence.enabled`. A fluorescence node encountered while the gate is disabled fails before rendering.

The implemented combination is the ordinary radiometric CUDA `Wavefront` integrator without camera diffraction, coherent or partially coherent transport, diffractive materials, local full-wave coupling, path guiding, ReSTIR, manifold sampling, BDPT, VCM or MLT. Unsupported combinations reject rather than falling back to an elastic diffuse material.

## Validation

The W.6 gate covers:

- matrix shape, normalization, finite-value, lifetime and 4,096-entry limits;
- strict Stokes-shift and radiant-energy bounds;
- forward emission and adjoint excitation sampling with PDF conversion;
- native binary/text and MaterialX round trips;
- compiler, native solver, C ABI and pyure feature gates;
- actual CUDA wavelength conversion with detector-wavelength film preservation;
- depolarization, medium preservation, immutable-resource reload policy and disabled-gate rejection;
- static checks for the forward/adjoint distinction and queue state.

Run:

```powershell
.\scripts\check_phase_w6_static.ps1
ctest --test-dir build_modular_x64 -C Release -R "test_wave_optics|test_materialx_io|test_native_scene_ir|test_native_solver_contract|test_session|test_pyure_smoke|gpu_wave_optics" --output-on-failure
```
