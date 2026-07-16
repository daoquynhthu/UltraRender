# R-P4 Bidirectional Transport Design

> Document status: Active
>
> This design is subordinate to `PLAN.md` and defines the shared production runtime for specular-manifold, BDPT, and VCM execution.

## Scope

R-P4 replaces the dielectric direct-light blocker boundary with explicit path-space algorithms. It delivers three independent integrator modes on one shared bidirectional transport runtime:

- specular-manifold connection for bounded SDS chains;
- bidirectional path tracing with camera/light subpath connection and path-space MIS;
- vertex connection and merging with a deterministic progressive radius schedule.

The implementation remains radiometric and preserves sampled wavelength PDFs and Stokes/Mueller transport. Coherent Jones-field propagation remains Phase W work. MLT scheduling remains R-P5 work.

## Shared path vertex contract

Camera and light subpaths use the same bounded GPU vertex layout. Each vertex carries position, geometric and shading normal, incoming/outgoing direction, geometry and primitive identity, material and medium identity, spectral mode, active wavelength, wavelength PDF, Stokes-compatible throughput, forward and reverse directional PDFs, converted area/volume PDFs, delta flag, transport mode, path length, sample identity, and scene epoch.

PDF conversion is centralized. Surface edges convert solid angle to area with `abs(n dot -wi) / distance^2`; volume edges retain the volume measure. Delta events have discrete probability and are excluded from ordinary density division. No scheduler stores display RGB as reusable transport authority.

## GPU ownership and scheduling

`GpuContext` owns checked, bounded camera/light vertex buffers, per-path lengths, connection work, merge grid storage, output accumulation, and telemetry. Allocation uses explicit overflow checks and a VRAM-derived budget. Reset, scene reload, camera discontinuity, material/light/resource mutation, instance topology change, and spectral-layout change invalidate all reusable state.

The wavefront camera tracer captures vertices through focused hooks. A separate light-subpath pass samples the R-P1 typed light distribution, emits a position/direction pair with matching joint PDF, and traces through the same material/medium scattering helpers. Connection, merging, and manifold solving are separate kernels; none of their loops are embedded in `shade_kernel`.

## BDPT estimator

For each complete path length, BDPT evaluates all valid `(s,t)` techniques whose endpoints and delta constraints permit connection. Endpoint selection, emission, BSDF/phase factors, geometry terms, visibility, wavelength density, and Stokes transport are reevaluated for the current connection. MIS uses the power heuristic over technique probabilities reconstructed from forward/reverse area-measure PDF ratios. Impossible techniques contribute zero and are excluded from normalization.

Light tracing to the camera is included only when the camera importance and raster mapping are evaluated with a matching PDF. The first production slice may reject unsupported lens models rather than use an implicit pinhole approximation.

## VCM estimator

VCM reuses the same subpaths and connection estimator. Non-delta light vertices enter a deterministic bounded spatial hash grid. Camera vertices query neighboring cells and merge compatible surface or volume vertices. The kernel applies the progressive radius and kernel normalization in the matching measure, and MIS includes both connection and merging technique probabilities.

The radius schedule is explicit in configuration and metadata. Each iteration applies `r_(n+1) = r_n * ((n + alpha) / (n + 1))^(1/d)`, with `d=2` for surfaces and `d=3` for volumes. Resolution, scene epoch, or estimator-policy changes reset the iteration count. Grid overflow and budget exhaustion fail loudly.

The surface-grid implementation stores exact signed cell coordinates beside each chained hash entry, so hash collisions affect traversal cost but never merge eligibility. Surface compatibility additionally requires bounded Euclidean distance, aligned geometric normals, and bounded normal-plane separation to prevent thin-wall light leaks. Light endpoints and delta vertices never enter the merge grid. The grid heads, entries, counter, and merge accumulation are context-owned and included in the same checked bidirectional VRAM budget.

Surface and volume grids are independently allocated according to merge policy because their progressive radii and measures differ. Camera and importance light paths both capture homogeneous-medium scattering vertices, including resource-backed Mie phase transport. Mixed surface-volume edges convert the directional PDF into the target vertex measure in each direction. Volume merging requires matching medium identity and uses the normalized sphere kernel `3 / (4 pi r^3)`.

The immutable light-endpoint position density is stored separately from the first path-edge density. For each accepted merge pair, MIS reconstructs the complete light prefix, evaluates the actual spectral BSDF or phase density in both directions across the virtual connection bridge, appends the reversed camera suffix, and normalizes the merge technique together with every valid BDPT split of that same full path. Local pairwise merge-versus-connection weighting is not used.

## Specular-manifold solver

The manifold solver consumes stable primitive identity and differentiable local coordinates. Analytic spheres use angular coordinates; triangles use barycentric coordinates with transform-aware tangent derivatives. A bounded chain stores each specular vertex and solves the half-vector/Snell constraint with damped Newton iterations, pivoted small-matrix elimination, line search, domain projection, residual and determinant gates.

The solver evaluates reflection or transmission per vertex from the actual spectral IOR and boundary orientation. It carries the determinant/Jacobian needed by the path-space contribution, validates visibility for every reconstructed segment, and rejects TIR, singular, stale, occluded, or out-of-domain solutions. Rough dielectric events are ordinary non-delta BDPT vertices, not manifold constraints.

## Configuration and modes

`IntegratorMode` gains `BDPT` and `VCM`. A bidirectional configuration owns maximum camera/light vertices, connections per pixel, memory budget, and light-tracing policy. A VCM configuration owns initial radius, alpha, grid capacity, and surface/volume merge policy. Existing `SpecularManifoldConfig` remains the bounded Newton policy.

Native schema, JSON, CLI, C ABI, Session, distributed metadata, and pyure expose the same mode and policy values. Unsupported combinations fail before GPU allocation. Shards may merge only when integrator mode, spectral domain, radius iteration, and estimator policy match.

## Verification and closure

Host oracles cover measure conversion, technique enumeration, forward/reverse PDF ratios, power-heuristic partition, progressive radius, merge normalization, small-matrix Newton steps, determinant/Jacobian reciprocity, TIR, and singular rejection.

GPU tests cover light endpoint sampling/PDF parity, camera/light vertex capture, visibility connection, dielectric spectral/Stokes transport, BDPT technique normalization, VCM hash/merge/radius reset, manifold convergence and rejection, memory lifecycle, and deterministic sample-space replay.

Fixed glass-caustic, SDS, small-emitter, and rough/specular mixed workloads report bias bounds, MSE, variance, time-to-error, samples/s, memory, connection/merge counts, Newton iterations, residuals, and rejection reasons. Each mode must leave fail-loud state only after its correctness and benefit gate passes. The phase closes only after Release full build, complete CTest, Phase R static audit, the R-P4 benchmark suite, documentation consistency, and source self-review.
