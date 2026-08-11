# Phase R-P4 Specular Manifold

Phase R-P4 closes the CUDA specular-manifold, BDPT, and VCM production path. The implementation shares typed camera/light path vertices, spectral and Stokes transport, path-density reconstruction, visibility, and bounded GPU-owned storage. `SpecularManifold` is a standalone integrator mode rather than an additive preview layered over an unweighted wavefront result.

## Estimator boundary

The manifold estimator owns finite-emitter paths with a non-delta area anchor followed by one to four smooth-delta events. Wavefront flags retain both the last-event delta bit and the existence of the preceding area anchor. Only this exact support is removed from the standalone wavefront contribution; camera-to-delta paths, environment paths, rough events, volume interruptions, and other unsupported paths remain on the wavefront estimator.

The contribution evaluator follows the camera-subpath transport direction from anchor to light. This matters for dielectric radiance transport because its eta scale is directional. The response combines anchor throughput and BSDF, endpoint emission and selection/area PDF, generalized manifold geometry, wavelength-resolved Mueller/Stokes boundary response, the reciprocal root-basin estimator, and the exclusive-support MIS weight. Each resolved contribution is committed once.

## Correctness reference

The renderer maintains a technique AOV for the same anchored delta-to-finite-emitter support before standalone partitioning. The R-P4 benchmark compares the SMS AOV directly against this independent wavefront AOV. It does not use total-image subtraction as a manifold reference and does not use a high-SPP SMS image as its own truth.

`tools/benchmarks/run_phase_r_manifold_suite.ps1` covers glass caustic, SDS, small emitter, and mixed rough/specular workloads. The default gate uses 8192 SPP references, with stronger deterministic budgets for rare-event references: small-emitter wavefront uses 262144 SPP and SMS uses 32768 SPP; mixed-specular wavefront uses 32768 SPP and SMS uses 131072 SPP. It verifies:

- a nonzero independent manifold reference;
- high-SPP relative mean bias and a 95% bound no greater than 35%;
- convergence of the SMS MSE curve;
- exact base-energy partition against the matching wavefront support;
- complete reciprocal-root drain and lifecycle accounting;
- rough-material rejection in the mixed scene;
- at least one workload with positive time-to-error evidence.

The 2026-07-18 Release run passed all four scenes. High-SPP relative mean bias was 14.3% for glass caustic, 7.6% for SDS, 7.1% for small emitter, and 1.3% for mixed specular; corresponding 95% bounds were 26.5%, 32.2%, 27.0%, and 14.1%. Two workloads reached the selected error target before the wavefront curve. The generated report is `output/benchmarks/phase_r_manifold_suite.json` and remains a reproducible build artifact rather than a tracked source fixture.

## Resource contract

High-memory CUDA targets use the Ninja `ur_cuda_heavy_compile` pool; host compilation and unrelated targets remain globally parallel. The technique AOV replaces the previously unused `d_accum_sq_buffer`, so R-P4 adds no net framebuffer-sized VRAM allocation.

R-P4 verification additionally requires the complete registered Release CTest gate and the Phase R static audit. At closure, the phase-local cursor advanced to R-P5; the root `PLAN.md` now owns the current PRV cursor.
