# Phase W Wave-Optics Boundary Audit

Document status: current capability boundary

Last reviewed: 2026-07-29

Phase W is complete within its declared bounded scope. W.2 integrates an explicitly enabled incoherent diffraction camera, W.5 adds radiometric diffractive thin-sheet materials, W.6 adds a bounded radiometric fluorescence surface transition, W.7 establishes a partial-coherence reference/statistical layer, W.9 establishes homogeneous anisotropic modal transport references, W.10 establishes a bounded local full-wave provider/cache contract, W.11 establishes coherent distributed sufficient-statistics files and merges, and W.12 unifies physical, API, fail-loud and static validation. The ordinary renderer remains a spectral/polarimetric radiometric path tracer. These capabilities do not make UltraRender a general coherent wave-optics renderer.

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
| Diffractive materials | grating, sinusoidal phase mask, ideal zone plate, blazed DOE and bounded passive RCWA/FMM Jones tables | Production CUDA wavefront thin-sheet scattering; per-lane order sampling without cross-path coherence |
| Fluorescence | bounded excitation-emission matrices, forward/adjoint oracles, lifetime state and detector-wavelength preservation | Production CUDA wavefront inelastic surface transition; steady-state film only |
| Partial coherence | bounded PSD cross-spectral density, Gaussian-Schell extended source, deterministic coherent realizations, generalized Jones rays, temporal-coherence/interferometry oracle and raw-field film merge | Host/statistical reference plus CUDA ensemble-to-CSD reduction; production sessions still reject |
| Anisotropic/modal media | bounded spectral dielectric-impermeability/extinction tensors, ordinary/extraordinary eigenmodes, optical activity, liquid-crystal directors and stress birefringence | Homogeneous host/CUDA Jones-segment reference; no SceneIR attachment, interface ray splitting or production path queue |
| Local full-wave coupling | versioned binary RCWA/FDTD/FEM/BEM/FMM/DDA/S-matrix request/result envelopes, provider negotiation, solver evidence and content-addressed cache | Imports strictly verified W.5 Jones tables; no bundled solver, ambient process launch, global Maxwell discretization or Phase X dynamic ABI |
| Coherent distributed contract | v6 frame semantics plus content-digested complex-field and mutual-intensity files, phase/layout/source/group/realization provenance and transactional merge | Coherent-realization fields merge before power and mutual intensity merges over disjoint realization ranges; production coherent sessions remain rejected |

## Production renderer boundary

The production wavefront queues transport spectral radiometric throughput and Stokes components. With camera diffraction enabled, terminal radiometric contributions are converted at their sampled wavelengths and accumulated into a bounded wavelength-binned XYZ film before wavelength-specific PSF convolution. The bin interpolation applies only to the PSF, so coarse wavelength banks do not approximate the CIE response. Beauty is filtered, and geometric AOVs remain unfiltered.

With diffractive materials enabled, a surface operator splits packet transport into wavelength lanes, samples a propagating diffraction order and applies its complex Jones response to Stokes state. RCWA/FDTD/FEM/BEM/FMM/DDA or S-matrix providers may produce the table through the W.10 byte contract before scene loading; there is still no in-render local full-wave solve. Results require exact request/provider provenance, convergence and budget evidence, a complete shared wavelength/incidence/order/side grid and joint `ΣJ†J` passivity. Thin-sheet transmission preserves the current medium.

With fluorescence enabled, a camera path observed at an emission wavelength samples a shorter excitation predecessor from the adjoint transition kernel. The transport wavelength changes while a separate film wavelength remains fixed at the detector sample. Quantum yield is converted to radiant energy with the excitation/emission wavelength ratio, and positive emission support is required above every excitation sample. The selected lane is depolarized, the current medium is preserved, and an exponential lifetime delay is accumulated. The present film is steady-state and does not expose that delay as a time-resolved output. With all three production feature gates disabled, the original radiometric material and RGB film paths are retained.

The diffraction film is currently limited to the CUDA `Wavefront` integrator. Path guiding, ReSTIR, BDPT, VCM, specular-manifold and MLT combinations reject rather than applying a post-hoc RGB blur. PSF banks cover 360–830 nm, interpolate each exact-wavelength XYZ contribution between adjacent kernels, normalize every wavelength kernel and renormalize each source footprint against valid sensor support at image edges. C ABI optical parameters use `ure_wave_optics_config_v2_t`; the original structure remains layout-compatible and selects documented defaults.

Consequently, the following are not implemented production capabilities:

- coherent or partially coherent scene transport;
- scene-integrated coherent grouping and mutual-intensity/cross-spectral-density transport;
- a complex-field GPU path queue and coherent film;
- production worker output wired to the coherent distributed files;
- scalable FFT, tiling, out-of-core or multi-GPU propagation backends;
- coherent holographic reconstruction or cross-path interference through diffractive materials;
- scene-integrated anisotropic interfaces, walk-off/ray splitting and tensor optical materials;
- transient/time-domain wave propagation;
- bundled local full-wave solver implementations or a stable Phase X dynamic plugin ABI;
- engine-owned local-solver process discovery and execution;
- scene-scale global Maxwell discretization.

Configuration fields, schemas and host references for these areas are capability requests or correctness scaffolding. They must not be presented as completed runtime support.

## Radiometric work that remains independent of Phase W

Ordinary spectral estimator correctness, BSDF/PDF consistency, polarization convention, energy conservation and volume transport remain requirements of the radiometric renderer. A future wave solver is not a justification for leaving those paths inconsistent.

The current code includes rough dielectric eval/PDF/sample and direct-light tests, local thin-film boundary tests, CIE/spectral estimator tests and Mie resource tests. Their coverage is finite and should be extended when the active PLAN item requires it.

## Boundaries beyond Phase W closure

Before claiming a scene-integrated coherent production renderer, the project must still provide evidence for:

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
.\scripts\check_phase_w5_static.ps1
.\scripts\check_phase_w6_static.ps1
.\scripts\check_phase_w7_static.ps1
.\scripts\check_phase_w9_static.ps1
.\scripts\check_phase_w10_static.ps1
.\scripts\check_phase_w11_static.ps1
.\scripts\check_phase_w12_static.ps1
.\scripts\check_physics_optics.ps1
.\scripts\run_phase_w_validation_suite.ps1 -BuildDir build_modular_x64 -Config Release
```

Passing the W.12 suite proves the covered diffraction-camera, radiometric diffractive-material, fluorescence, partial-coherence statistical, anisotropic modal, local-solver exchange, coherent distributed sufficient-statistics and reference boundaries only. It does not establish a complete coherent wave-optics production renderer. The evidence schema and exact closure matrix are recorded in [`Phase_W_W12_Validation.md`](Phase_W_W12_Validation.md).
