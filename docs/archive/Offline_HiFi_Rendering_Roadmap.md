# Archived Offline Rendering Roadmap Summary

> Archive status: retained for historical context. This file is not a live roadmap. `PLAN.md` is the only authoritative construction queue.

Document status: non-authoritative summary

Last reviewed: 2026-07-15

The authoritative construction order, dependencies and completion criteria are maintained in `PLAN.md`. This file provides a short orientation only and must not be used to start work out of sequence.

## Current baseline

UltraRender currently uses a CUDA spectral/polarimetric radiometric wavefront path tracer. Completed foundations include modular libraries, runtime spectral packets, multi-GPU sample partitioning, session APIs, MaterialGraph, large spectral resource domains, Mie volume resources, and the Phase Q native scene system.

The project does not currently have a production Vulkan/DXR/OptiX backend, interactive viewport, general coherent wave solver, or general-purpose production physics/acoustic solver.

## Authoritative near-term sequence

```text
R-P3  unbiased/spatial ReSTIR DI and ReSTIR PT contracts
  -> R-P4  specular manifold and BDPT/VCM
  -> R-P5  MLT chain integrator
  -> R-P7  Phase R validation/performance closure
  -> Phase T  backend-neutral runtime and additional GPU backends
  -> Phase V  acceleration-provider architecture
  -> Phase W  wave-optics production integration
  -> Phase U  USD/Hydra adapter
  -> Phase X  plugin ABI
```

Previously completed reference/oracle work in a later phase does not move the construction cursor and must not be described as production integration.

## Directional goals and constraints

### Rendering algorithms

- Preserve unbiased estimator and explicit PDF contracts.
- Keep wavelength PDF, material/phase lobe PDF and Stokes-compatible throughput consistent across reuse algorithms.
- Add advanced integrators only with reference comparison, bias tests and scene-specific evidence.

### Execution backends

- CUDA remains authoritative until Phase T establishes a backend-neutral kernel/runtime contract.
- Vulkan and D3D12/DXR are planned backends, not current capabilities.
- Avoid maintaining independent renderers whose physical semantics can diverge.

### Acceleration

- Current custom BVH/traversal remains the working baseline.
- OptiX, Vulkan RT and DXR integration belongs behind the future Phase V provider contract.

### Scene and asset pipeline

- `.ure`, `.urescene` and `.urepkg` are authoritative source/package formats.
- glTF, MaterialX and future USD support are adapters with explicit loss reporting.
- Compiled caches are rebuildable and identity-checked; they never replace source manifests.

### Wave optics

- Host/CUDA reference components may be used as correctness oracles.
- Main-path coherent state, diffraction film, propagation scaling and distributed merge must be completed before claiming a wave-optics production solver.

### Physics and acoustics

- Existing modules are experiments and interface groundwork.
- Numerical validation, solver ownership, coupling contracts and benchmark evidence are required before production claims.

## Deferred product work

Interactive editors, real-time viewports, neural caches, differentiable rendering and broad DCC integration may be explored only when their dependencies reach the authoritative cursor. They are not current deliverables and carry no schedule or performance promise in this document.

## Verification policy

Every roadmap completion claim must point to current source, registered tests and phase-specific gates. Historical plans and unchecked boxes under `docs/superpowers/` are implementation records rather than live roadmap state.
