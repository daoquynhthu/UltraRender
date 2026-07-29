# Phase W.9 Anisotropic and Modal Media

Document status: completed reference and modal-transport contract

Last reviewed: 2026-07-29

W.9 adds a bounded spectral material representation for homogeneous anisotropic optical segments. It solves polarization eigenmodes from a dielectric impermeability tensor and propagates the two complex components of transverse electric displacement with birefringence, dichroism and optical activity in one matrix exponential. It does not extend the radiometric dielectric material's scalar `ior`, and it does not claim scene-integrated coherent transport.

## Material tensor

`AnisotropicMedium` contains at most 256 strictly ordered spectral samples. Each `AnisotropicMediumSample` stores:

- a real symmetric positive-definite dielectric impermeability tensor;
- a real symmetric positive-semidefinite extinction tensor;
- optical activity in radians per metre;
- the wavelength at which the tensors are defined.

Interpolation is performed on the tensors, not on a scalar effective IOR. A single sample is treated as wavelength-independent; a multi-sample resource rejects wavelengths outside its declared support.

Factory functions cover:

- arbitrary principal-axis/biaxial data with three refractive indices and extinction coefficients;
- uniaxial crystals;
- homogeneous liquid-crystal segments through an explicit director;
- stress birefringence through the deviatoric stress-optic change to dielectric impermeability.

Principal frames must be orthonormal. Negative extinction, non-positive dielectric tensors, invalid stress-derived tensors, unordered spectral grids and non-finite values fail closed.

## Modal solution

For propagation direction `s`, W.9 constructs a deterministic transverse basis and solves the two-dimensional eigenproblem

`P B P D_m = (1 / n_m^2) D_m`,

where `B` is dielectric impermeability, `P` projects onto the plane normal to `s`, and `D_m` is a transverse displacement eigenmode. The result exposes two orthonormal modal polarizations, their effective refractive indices, projected extinction coefficients and the degeneracy state.

This formulation reproduces ordinary/extraordinary indices for a transverse uniaxial crystal and degenerates to the ordinary index along its optic axis. It also handles a general biaxial tensor without selecting a scalar IOR by direction.

## Complex modal propagation

The two modal indices reconstruct the transverse refractive-index matrix `N`. The full extinction tensor is independently projected to `K`, so dichroic and birefringent axes do not need to commute. Optical activity contributes the antisymmetric rotation generator. A homogeneous segment uses the exact two-by-two complex matrix exponential

`exp[(i k0 N - k0 K + omega J) distance]`.

This keeps simultaneous birefringence, dichroism and optical activity in one generator rather than applying order-dependent post-process rotations. Host tests cover quarter-wave retardance, pure optical rotation, analytic dichroic attenuation, liquid-crystal director equivalence, stress-induced mode splitting, passivity and invalid tensors.

The propagated two-component carrier is explicitly named `transverse_displacement`; it represents transverse electric displacement in the deterministic modal basis, not a silently approximated transverse electric field. Interface conversion is outside this homogeneous-segment contract.

`propagate_anisotropic_displacements_gpu` performs the modal solve and complex matrix exponential on CUDA for a bounded batch of 1,048,576 samples. Inputs are spectrally sampled and validated on the host, while the device independently reconstructs modes and propagation. Buffers, queue, fence and completion timeline remain owned by the private CUDA runtime. Host/device parity is tested for spectral interpolation, oblique directions, mixed polarization, dichroism and optical activity.

## Boundary

W.9 is a homogeneous-segment reference contract. The following remain outside this closure:

- anisotropic Fresnel boundary matching, walk-off and ray splitting;
- spatially varying liquid-crystal integration beyond explicit piecewise segments;
- SceneIR material attachment and production complex/Jones path queues;
- coherent visibility, film output and distributed modal-field serialization;
- local RCWA/FDTD/FEM/BEM solver execution.

The production path tracer therefore continues to use its existing scalar radiometric dielectric model and rejects coherent/partially coherent sessions. W.9 data is not silently flattened into `ior`, Stokes throughput or RGB.

## Verification

```powershell
ctest --test-dir build_modular_x64 -C Release -R "^(test_wave_optics|gpu_wave_optics|test_public_surface_sdk_free)$" --output-on-failure
.\scripts\check_phase_w9_static.ps1
```

These gates establish the bounded tensor, modal and Jones propagation reference. They do not establish production anisotropic scene rendering.
