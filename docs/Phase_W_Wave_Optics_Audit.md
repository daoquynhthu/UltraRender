# Phase W Wave-Optics Boundary Audit

Document status: current capability boundary

Last reviewed: 2026-07-15

Phase W is not the active construction phase. Existing reference/oracle work is retained, but new production integration is frozen until the authoritative queue reaches W. The current renderer must be described as a spectral/polarimetric radiometric path tracer, not as a general wave-optics renderer.

## Implemented reference and contract work

| Area | Current evidence | Scope |
|---|---|---|
| Configuration gates | `WaveOpticsConfig` reaches C++, JSON/CLI, C ABI and pyure | Unsupported production modes are rejected before rendering |
| Local complex boundaries | complex Fresnel and thin-film helpers feed power/Mueller evaluation | Local boundary interference only; no global phase transport |
| Diffraction references | circular Airy PSF, encircled energy, diffraction MTF, pupil/defocus, slit/aperture/grating and knife-edge references | Host correctness oracles, not scene-integrated production effects |
| Complex field carriers | `ComplexSpectrum`, `JonesSpectrum`, `CoherenceMetadata`, `ComplexFieldFilm`, `WaveFieldGrid` | Host-side mathematical contracts |
| Propagation references | direct Fraunhofer, Fresnel, angular-spectrum, Huygens–Fresnel and Rayleigh–Sommerfeld operators | Direct/reference implementations; not a scalable general backend |
| CUDA reference | direct Fraunhofer GPU DFT path | Reference parity path, not optimized FFT/tiling infrastructure |
| Diffraction camera plan | feature-gated `DiffractionCameraPlan` and PSF construction | Planning/reference boundary; main GPU film is still radiometric |

## Production renderer boundary

The production wavefront queues transport spectral radiometric throughput and Stokes components. They do not transport an absolute complex phase shared across independent paths. The production framebuffer and distributed merge accumulate radiometric values, not coherent field samples.

Consequently, the following are not implemented production capabilities:

- coherent or partially coherent scene transport;
- coherent grouping and mutual-intensity/cross-spectral-density models;
- a complex-field GPU path queue and coherent film;
- distributed coherent merge with phase-reference guarantees;
- diffraction-camera integration in the main rendering pipeline;
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
4. scene-integrated diffraction camera and complex-field film behavior;
5. coherent/incoherent accumulation and distributed merge order;
6. wavelength, polarization and energy normalization across adapters and resources;
7. memory budgets, performance measurements and failure behavior on supported hardware;
8. validation scenes that distinguish radiometric approximations from wave results.

## Verification entry points

```powershell
ctest --test-dir build_modular_x64 -C Release -R "test_wave_optics|gpu_wave_optics|gpu_polarization" --output-on-failure
.\scripts\check_physics_optics.ps1
```

Passing these tests proves the covered contracts and references only. It does not establish a complete wave-optics production renderer.
