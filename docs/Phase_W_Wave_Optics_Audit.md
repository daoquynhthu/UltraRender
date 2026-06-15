# Phase W Wave Optics Audit

Date: 2026-06-15

This audit records the current boundary between UltraRender's spectral/polarimetric path tracer and a true wave-optics transport solver. The key finding is architectural: current transport state is radiometric plus Stokes polarization, not a phase-preserving complex field. Local boundary code uses complex amplitudes for Fresnel/thin-film/Mueller derivation, but the path queues, film, camera, volume model, and distributed merge contract do not preserve coherent phase relationships between different paths.

## Current Implemented Physics

| Area | Current evidence | Assessment |
|------|------------------|------------|
| Spectral path state | `RayQueue::throughput_vals`, `throughput_wavelengths`, `spectral_modes`, `active_channels`, `wavelength_pdfs` in `libs/ure_core/include/ure/gpu_structs.hpp`; sampled wavelength raygen in `libs/ure_core/src/path_tracer_raygen.cu` | Spectral transport and explicit wavelength PDF are implemented. This is spectral radiometry, not coherent field transport. |
| Stokes/Mueller polarization | `StokesVector`, `RayQueue::stokes_i/q/u/v`, `apply_mueller_*` in `path_tracer_polarization.cuh` | Polarization state and boundary Mueller transforms are implemented for intensity-domain polarization. Stokes does not encode absolute phase between independent paths. |
| Complex Fresnel amplitudes at boundaries | `ComplexF`, `eval_dielectric_reflection_amplitude`, `eval_conductor_boundary`, `eval_dielectric_surface_boundary` in `path_tracer_boundary.cuh` | Complex amplitude is used locally to derive power and Mueller response. It is not transported as a global field state. |
| Thin-film local interference | `eval_thin_film_boundary`, `eval_thin_film_conductor_boundary` in `path_tracer_boundary.cuh`; tests in `tests/gpu/test_gpu_polarization.cu` | Single local film boundary interference is represented through Airy-style complex reflection/transmission. This does not generalize to arbitrary multipath scene interference. |
| Rough dielectric and rough conductor transport | `eval_rough_dielectric_*`, `eval_bsdf`, `scatter` paths | Uses microfacet power BSDF/BTDF. It is an ensemble radiometric model, not coherent rough-surface scattering from a deterministic height field. |
| Participating media | `sample_henyey_greenstein`, `eval_henyey_greenstein` in `path_tracer_volume.cuh` and volume branch in `path_tracer_wavefront.cuh` | Radiative transfer approximation using HG phase function. No Mie/Rayleigh matrix scattering, coherence, or wave volume effects. |

## Architectural Gaps

| Severity | Gap | Evidence | Why it matters |
|----------|-----|----------|----------------|
| P0 | Main path has no coherent field transport | `RayQueue` carries scalar throughput per wavelength plus Stokes components. W.3 added `ComplexSpectrum`, `JonesSpectrum`, `CoherenceMetadata`, OPL phase helpers, and `ComplexFieldFilm` host oracle contracts in `ure::wave`, but these are not yet wired into the GPU path state. | Host contracts can now prove two-beam interference and coherent/incoherent accumulation order. GPU path transport, partial coherence, and distributed coherent merge remain future W.7/W.11 work. |
| P0 | Production film/merge is radiance-only | Accumulation paths in `path_tracer_wavefront.cuh` atomic-add RGB/XYZ-derived values to `GpuVec3* accum_buffer`; distributed merge sums float framebuffer values in `distributed_contract.cpp`. W.3 added a host `ComplexFieldFilm` accumulator that computes `|sum E|^2` separately from `sum |E|^2`. | The mathematical film contract exists, but production GPU film and distributed merge still cannot accept coherent frames without W.11. |
| P0 | No wave propagation operator | Camera/raygen emit geometric rays; there is no `WaveField`, `PropagationOperator`, Fresnel/Fraunhofer/angular-spectrum/Rayleigh-Sommerfeld API, FFT backend, or scalar/vector diffraction solver. | Diffraction, wavefront reconstruction, holography, and aperture propagation cannot be expressed as local BSDF events alone. |
| P1 | Camera has no diffraction-limited imaging | `PinholeCamera` and `ThinLensCamera` in `libs/ure_sceneio/src/camera.cpp` generate geometric rays and random disk DOF only. GPU `GpuCamera` stores only origin and viewport vectors. | No Airy disk, wavelength-dependent PSF/OTF/MTF, aperture blade diffraction, defocus phase, sensor aperture integration, or polarization-dependent PSF. |
| P1 | Diffractive materials are not first-class | Material enum is Lambertian/Metal/Dielectric/Light/Cloth; material graph currently evaluates spectral resources and scalar BSDF parameters. | Gratings, phase masks, holographic foils, photonic structures, and RCWA/FMM scattering tables need direction/order-generating complex scattering operators, not only radiometric BSDF values. |
| P1 | No partial coherence model | Config and ray state have spectral sampling modes, but no coherence length, mutual intensity, cross-spectral density, Wigner/generalized ray state, or coherent realization grouping. | Natural light, LEDs, lasers, OCT, lidar, and extended coherent sources need coherence gating rather than all-coherent/all-incoherent extremes. |
| P1 | No anisotropic modal transport | Dielectric material uses scalar `ior` and scalar dispersion; media use scalar density/anisotropy. | Birefringence, optical activity, dichroism, liquid crystals, stress birefringence, and index ellipsoid propagation require modal/Jones transport and tensor material data. |
| P2 | No edge/aperture diffraction | Visibility and shadow paths are geometric blockers; no knife-edge, slit, aperture, UTD/GTD, or edge diffraction events. | Sharp geometric visibility is wrong near wavelength-scale apertures/edges and for long wavelengths. |
| P2 | No time-domain wave optics | Spectral domain stores intensity/resource samples, not complex frequency-domain amplitude or group-delay metadata. | Ultrafast pulse propagation, chirp, group velocity dispersion, transient interference, and phase ToF cannot be represented. |
| P2 | No local full-wave coupling interface | No solver/plugin boundary for RCWA, FDTD, FEM, BEM, DDA, S-matrix extraction, or cached scattering tables. | Nano-optical structures should be solved locally and coupled through scattering operators; global Maxwell discretization is not feasible for scene-scale rendering. |

## Current Radiometric Physics Debt Found During Audit

These are not solved by declaring a future wave solver. They affect the current spectral/polarimetric renderer and should be fixed before using Phase W results as reference images.

| Severity | Issue | Evidence | Minimal fix direction |
|----------|-------|----------|-----------------------|
| Fixed W.0 | Rough dielectric direct-light MIS used a different PDF than the evaluated BSDF | Previous `pdf_bsdf()` evaluated rough dielectric Fresnel/thin-film at fixed `550.0f` and used raw `mat.thin_film_thickness`; scatter/wavefront paths used wavelength-dependent IOR and UV-modulated `effective_thickness`. | Fixed on 2026-06-15: `pdf_bsdf_spectral()` now carries wavelength, UV effective film thickness, and dispersion clamp; direct-light MIS uses per-channel material PDF. Verified by `gpu_test_spectral_soa` 739/0 and `gpu_test_render` 338/0. |
| P1 | Packet-mode boundary event selection remains a packet-average/hero-event approximation | `load_packet_average_stokes()` in `path_tracer_wavefront.cuh` averages lane Stokes before `scatter()` samples one reflection/transmission event; lane split only handles selected dispersive/thin-film delta cases. | Treat strongly wavelength/polarization-selective boundary events as lane/sampled events or document and gate the approximation by mode/quality preset. |
| P1 | Thin-film is single-layer scalar-IOR only | `eval_thin_film_boundary()` and `eval_thin_film_conductor_boundary()` implement air/film/substrate with real film IOR and one thickness; no absorbing film, multilayer transfer matrix, coating stack resource, rough/partial coherence damping, or true thickness texture. | Add multilayer coating stack as a material/resource graph operator before claiming coating-grade physical coverage. |
| P1 | Dielectric dispersion is empirical, not material-data driven | `dispersed_dielectric_ior()` uses a Cauchy-like offset around 550nm with a scalar dispersion/clamp. | Add Sellmeier/Abbe/sampled IOR resources and route material IOR through the spectral resource graph. |
| P2 | Metal fallback is artistic when measured `n,k` is absent | `conductor_f0_eta_from_albedo()` and Schlick-style fallback infer scalar eta/F0 from albedo. | Keep fallback explicitly documented as non-measured preview; measured conductor mode should require spectral eta/k for physical validation scenes. |
| P2 | Rough-surface polarization is local microfacet Mueller, not coherent rough-surface scattering | Rough branches apply Fresnel/Mueller at sampled microfacet frames but do not represent height-field phase, correlation length, coherent specular peak, or depolarizing ensemble statistics. | Defer coherent rough-surface scattering to Phase W diffractive/material operator work; keep current model labeled as radiometric microfacet approximation. |

## Switch Policy

Advanced wave optics must be opt-in and fail-loud when unsupported. Existing radiometric spectral path tracing remains the default because it is the correct economical solver for most macroscopic incoherent scenes.

Phase W.1 centralized the first switch set under `WaveOpticsConfig` and exposed it through:

- `ure::RenderConfig`
- JSON config
- CLI
- C ABI / Session API
- `pyure`
- distributed shard metadata when coherent outputs are involved (planned for W.11)

Initial switch groups:

| Group | Default | Required behavior |
|-------|---------|-------------------|
| `wave_optics.mode = radiometric | camera_diffraction | coherent_field | partial_coherence` | `radiometric` | Non-radiometric modes must reject unsupported integrators, films, distributed merge modes, or material operators before rendering. |
| `wave_optics.camera_diffraction.enabled` | off | Enables pupil function, wavelength PSF, aperture diffraction, and sensor integration. |
| `wave_optics.coherent_field.enabled` | off | Enables Jones/complex spectral field transport, optical path length, coherent film, and coherent realization grouping. |
| `wave_optics.partial_coherence.enabled` | off | Enables coherence length/source coherence metadata and mutual-intensity/Wigner/generalized-ray paths. |
| `wave_optics.diffractive_materials.enabled` | off | Enables grating/DOE/phase-mask/RCWA scattering operators. |
| `wave_optics.fluorescence.enabled` | off | Enables excitation-to-emission wavelength conversion resources and PDF/energy accounting. |
| `wave_optics.specular_manifold.enabled` | off | Enables refractive/specular direct-light connection; disabled mode must keep current blocker policy. |
| `wave_optics.local_fullwave.enabled` | off | Enables local solver coupling and cached S-matrix/scattering table consumption. |
| `wave_optics.experimental_allow_preview_degradation` | off | Allows explicitly marked nonphysical preview degradation for unsupported wave nodes; it does not enable a solver by itself. |

Phase W.1 verification on 2026-06-15:

- `test_config` 29/0 covers JSON and CLI parse parity for the switch set.
- `test_session` 163/0 covers C ABI radiometric success, preview-only success, unsupported coherent mode rejection, and invalid enum rejection.
- `test_pyure_smoke.py` covers pyure radiometric/preview creation and unsupported coherent/unknown mode rejection.
- `ure_cli render <missing> --wave-optics-mode invalid_mode` rejects the invalid mode before scene file checks.

## Planning Conclusion

Phase W should be a complete wave-optics solver track, not a thin-film cleanup. The first deliverable should be a feature-gated solver boundary and a diffraction camera path because it gives visible, analytic, low-intrusion validation. W.2/W.4 now have a `WaveFieldGrid` complex field carrier, direct Fraunhofer/Fresnel/angular-spectrum/Rayleigh-Sommerfeld/Huygens-Fresnel CPU propagation oracles, a unified `PropagationOperatorKind`/`PropagationConfig`/`PropagationResult` dispatch interface, a first CUDA Fraunhofer direct DFT reference backend, circular-aperture Airy PSF, diffraction-limited MTF, defocused circular-pupil host oracle, and feature-gated `DiffractionCameraPlan` in `ure::wave`; this gives the GPU diffraction camera a hard reference for first-zero radius, sensor-space scaling, encircled energy, wavelength scaling, radial symmetry, normalized discrete PSF kernels, cutoff frequency, MTF monotonicity, pupil aperture masking, defocus phase, sampled complex-field power, propagation normalization, and half-enabled config rejection. W.3 now adds `ComplexSpectrum`, `JonesSpectrum`, OPL phase accumulation, coherence metadata, and `ComplexFieldFilm`, with tests proving same-phase constructive interference, opposite-phase cancellation, and the difference between coherent and incoherent accumulation order. Direct `GpuRenderEngine` scene load still rejects camera diffraction requests before GPU initialization, so direct C++ users cannot bypass CLI/Session gating and silently run the radiometric path. Production coherent path transport, partial coherence, diffractive materials, distributed coherent merge, and local full-wave coupling remain explicit later Phase W work.
