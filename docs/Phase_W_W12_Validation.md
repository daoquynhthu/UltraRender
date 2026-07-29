# Phase W.12 Validation Closure

Status: completed on 2026-07-29.

Phase W.12 closes the bounded wave-optics program defined by `PLAN.md`. It validates the implemented diffraction-camera and radiometric material paths together with the coherent, partial-coherence, anisotropic, local-solver and distributed reference contracts. It does not reclassify those reference contracts as a scene-integrated coherent renderer.

## Validation contract

`scripts/run_phase_w_validation_suite.ps1` builds the maintained Release configuration, verifies the required CTest inventory, runs every Phase W static gate, runs the physics/optics gate, executes the complete registered CTest suite and writes `ure.phase_w.validation.v1`.

The report binds its result to:

- the source commit and clean/dirty tree state;
- SHA-256 identities for the host wave test, GPU wave test and `ure_core` library;
- the complete CTest count and the required test names;
- eleven named evidence categories and all applicable static gates.

The output defaults to `output/validation/phase_w_validation.json`. It is generated evidence rather than an authoritative source file.
`tools/benchmarks/validate_phase_w_validation_report.ps1` validates the schema and can require clean-tree provenance. A separate negative-contract script mutates schema, digest, physical evidence, CTest and static-gate fields and requires every forged report to be rejected.

## Evidence matrix

| Requirement | Executable evidence | Contract |
|---|---|---|
| Airy first zero | `test_wave_optics` | First zero follows `1.2196698912665045 λ/D`; sensor radius, symmetry and encircled energy are checked |
| Slit and grating angles | `test_wave_optics` | Slit first zero and propagating/evanescent grating orders follow their analytic equations |
| Two-beam interference | `test_wave_optics` | Equal in-phase fields produce power four; a half-wavelength path difference cancels |
| Thin-film phase | `gpu_polarization` | CUDA complex reflection amplitudes match an independent host complex Airy oracle for both polarization axes |
| Rough dielectric spectral/UV PDF | `gpu_spectral_soa` | Wavelength and UV-dependent film thickness enter the same visible-normal lobe and PDF; reflection/transmission normalization and furnace bounds remain checked |
| Stokes/Jones conversion | `gpu_wave_optics` | Identity, linear polarizer and quarter-wave Jones operators transform the Stokes coherency state with the project convention |
| Fluorescence Stokes shift | `test_wave_optics`, `gpu_wave_optics` | Forward/adjoint excitation-emission sampling, radiant weighting, detector-wavelength retention and feature gates are checked |
| Energy and PDF conservation | `gpu_polarization`, `gpu_spectral_soa`, `gpu_wave_optics` | Boundary power, BSDF PDFs, white-furnace bounds and diffractive passivity remain bounded |
| Coherent merge order | `test_distributed_wave_io` | Complex fields merge before realization power; weighted realization averages precede incoherent group addition |
| Unsupported fail-loud | `test_wave_optics`, `test_native_solver_contract`, `test_material_graph` | Unsupported modes, solver combinations and graph nodes reject rather than flattening to radiometric output |
| Config/API parity | `test_config`, `test_session`, `test_pyure_smoke` | JSON, CLI, C ABI, native session and Python surfaces preserve the supported switches and rejection behavior |

`scripts/check_phase_w12_static.ps1` freezes the presence of these gates and prevents coherent sufficient statistics from entering the RGB output path.

## Closure boundary

Phase W is complete only within its declared split between production radiometric features and bounded reference contracts.

Implemented production CUDA boundaries are:

- explicitly enabled diffraction-camera film resolve;
- explicitly enabled radiometric diffractive thin-sheet scattering;
- explicitly enabled radiometric fluorescence surface transitions.

Validated but not scene-integrated production paths are:

- general coherent and partially coherent path transport;
- complex/Jones production queues and coherent film output;
- anisotropic interface splitting, walk-off and scene attachment;
- scalable FFT/tiling propagation backends;
- engine-owned local full-wave solvers;
- production coherent farm-worker emission.

These unavailable paths continue to reject before rendering or remain explicit host/CUDA reference APIs. Phase W closure does not permit silent preview degradation or RGB flattening.
