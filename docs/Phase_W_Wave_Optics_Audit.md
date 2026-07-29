# Phase W Wave-Optics Boundary Audit

Document status: current capability boundary

Last reviewed: 2026-07-29

Phase W is active. W.2 integrates an explicitly enabled incoherent diffraction camera into the CUDA wavefront film, while the ordinary renderer remains a spectral/polarimetric radiometric path tracer. This does not make UltraRender a general coherent wave-optics renderer.

## Implemented reference and contract work

| Area | Current evidence | Scope |
|---|---|---|
| Configuration gates | `WaveOpticsConfig` reaches C++, JSON/CLI, versioned native schema, C ABI v2 and pyure | Unsupported modes, optics, budgets and integrator combinations reject before GPU allocation |
| Local complex boundaries | complex Fresnel and thin-film helpers feed power/Mueller evaluation | Local boundary interference only; no global phase transport |
| Diffraction references | circular Airy PSF, encircled energy, diffraction MTF, pupil/defocus, slit/aperture/grating and knife-edge references | Host correctness oracles, not scene-integrated production effects |
| Complex field carriers | `ComplexSpectrum`, `JonesSpectrum`, `CoherenceMetadata`, `ComplexFieldFilm`, `WaveFieldGrid` | Host-side mathematical contracts |
| Propagation references | direct Fraunhofer, Fresnel, angular-spectrum, Huygens–Fresnel and Rayleigh–Sommerfeld operators | Direct/reference implementations; not a scalable general backend |
| CUDA reference | direct Fraunhofer GPU DFT path | Reference parity path, not optimized FFT/tiling infrastructure |
| Diffraction camera | normalized wavelength PSF bank, circular or regular-blade pupil, defocus phase, 2x2 sensor-pixel integration and CUDA spectral film resolve | Production CUDA wavefront boundary; explicitly enabled and incoherent |

## Production renderer boundary

The production wavefront queues transport spectral radiometric throughput and Stokes components. With camera diffraction enabled, terminal radiometric contributions are converted at their sampled wavelengths and accumulated into a bounded wavelength-binned XYZ film before wavelength-specific PSF convolution. The bin interpolation applies only to the PSF, so coarse wavelength banks do not approximate the CIE response. The material path is unchanged, Beauty is filtered, and geometric AOVs remain unfiltered. With the feature disabled, the original RGB accumulation path is retained.

The diffraction film is currently limited to the CUDA `Wavefront` integrator. Path guiding, ReSTIR, BDPT, VCM, specular-manifold and MLT combinations reject rather than applying a post-hoc RGB blur. PSF banks cover 360–830 nm, interpolate each exact-wavelength XYZ contribution between adjacent kernels, normalize every wavelength kernel and renormalize each source footprint against valid sensor support at image edges. C ABI optical parameters use `ure_wave_optics_config_v2_t`; the original structure remains layout-compatible and selects documented defaults.

Consequently, the following are not implemented production capabilities:

- coherent or partially coherent scene transport;
- coherent grouping and mutual-intensity/cross-spectral-density models;
- a complex-field GPU path queue and coherent film;
- distributed coherent merge with phase-reference guarantees;
- scalable FFT, tiling, out-of-core or multi-GPU propagation backends;
- diffractive materials, gratings and holograms as first-class scene scattering operators;
- birefringent/modal propagation and tensor optical materials;
- transient/time-domain wave propagation;
- production coupling to RCWA, FDTD, FEM, BEM, DDA or other local full-wave solvers.

Configuration fields, schemas and host references for these areas are capability requests or correctness scaffolding. They must not be presented as completed runtime support.

## Radiometric work that remains independent of Phase W

Ordinary spectral estimator correctness, BSDF/PDF consistency, polarization convention, energy conservation and volume transport remain requirements of the radiometric renderer. A future wave solver is not a justification for leaving those paths inconsistent.

The current code includes rough dielectric eval/PDF/sample and direct-light tests, local thin-film boundary tests, CIE/spectral estimator tests and Mie resource tests. Their coverage is finite and should be extended when the active PLAN item requires it.

## Integration requirements when Phase W becomes active

Before claiming production wave-optics support, the project must provide evidence for:

1. explicit complex/Jones path state and phase-reference conventions;
2. source coherence and realization grouping;
3. propagation/operator normalization against independent oracles;
4. complex-field path and film behavior beyond the completed incoherent diffraction camera;
5. coherent/incoherent accumulation and distributed merge order;
6. wavelength, polarization and energy normalization across adapters and resources;
7. memory budgets, performance measurements and failure behavior on supported hardware;
8. validation scenes that distinguish radiometric approximations from wave results.

## Verification entry points

```powershell
ctest --test-dir build_modular_x64 -C Release -R "test_wave_optics|gpu_wave_optics|gpu_polarization" --output-on-failure
.\scripts\check_phase_w2_static.ps1
.\scripts\check_physics_optics.ps1
```

Passing these tests proves the covered diffraction-camera, contract and reference boundaries only. It does not establish a complete coherent wave-optics production renderer.
