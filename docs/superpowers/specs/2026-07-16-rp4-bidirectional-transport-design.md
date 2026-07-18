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

Connection scheduling enumerates bounded actual light/camera vertex pairs rather than connecting only the light endpoint to the deepest camera vertex. Each pair reevaluates both endpoint scattering factors, geometry, visibility, and its full-path strategy density. When the pair lies inside an enabled merge kernel, connection and merge weights include the same single merge strategy in the same denominator. The legacy wavefront radiance estimator is isolated while these modes run; only the weighted connection and merge buffers are committed once to film.

Material evaluation uses the Phase M runtime rather than a bidirectional-only approximation. Composite materials resolve and sample the authoritative pair of lobes and reconstruct the mixture PDF; layered materials reuse the finite-thickness dielectric coating, absorption, substrate scatter, evaluation, and PDF contracts. Their delta coating events remain excluded from ordinary connection and merging densities.

## Specular-manifold solver

The manifold solver consumes stable primitive identity and differentiable local coordinates. Analytic spheres use angular coordinates; triangles use barycentric coordinates with transform-aware tangent derivatives. A bounded chain stores each specular vertex and solves the half-vector/Snell constraint with damped Newton iterations, pivoted small-matrix elimination, line search, domain projection, residual and determinant gates.

The solver evaluates reflection or transmission per vertex from the actual spectral IOR and boundary orientation. A dispersive constraint is wavelength-specific: lane and sampled paths use their selected wavelength, while a packet whose IOR differs across channels is rejected with `SpectralSplitRequired`. Material expressions and textures are evaluated at the actual solved UV. Only effectively smooth metal or dielectric vertices may enter the manifold; rough events are ordinary non-delta BDPT vertices and are rejected with `NonDeltaMaterial` rather than approximated as ideal constraints.

The immutable solution separates the Newton constraint determinant from radiometric geometry. It reconstructs the endpoint area Jacobian and generalized geometry `|P2 A^-1 B_L| G(anchor, first)`, validates every reconstructed segment with production traversal, and reports TIR, singular, stale, occluded, invalid-differential, non-delta, spectral-split, or out-of-domain failures through typed telemetry.

The bounded numerical core supports at most four two-parameter specular events. Host and GPU implementations use partial-pivot elimination, expose the signed determinant, reject non-finite or singular systems, and solve the Newton linearization `J delta = -F`. Runtime validation caps the chain at eight variables and 64 iterations before any GPU allocation.

GPU primitive evaluation uses analytic sphere angular coordinates and triangle barycentric coordinates with explicit position and tangent derivatives. The single-event solver enforces reflection or generalized Snell tangent constraints, physical hemisphere topology, parameter-domain projection, central-difference Jacobians, damped line search, residual and determinant thresholds, and an explicit total-internal-reflection gate.

Multi-event chains assemble one coupled `2N` residual and Jacobian because every constraint depends on its neighboring manifold points. Each Newton iteration solves all variables together, projects every primitive domain, and accepts a line-search step only when the complete chain residual decreases. The two-interface refraction oracle converges both perturbed barycentric points to the analytic slab path.

Production chain initialization extracts analytic spheres, direct mesh triangles, and transformed instance triangles from stable scene geometry and primitive identities. Instance vertices are transformed into world space before solving. Initial sphere angles or triangle barycentrics are reconstructed from the captured hit position and projected into their valid parameter domains.

The production solve pass scans each camera path for a bounded trailing delta chain, validates material and scene epoch, solves against the sampled light endpoint, and writes a context-owned immutable solution artifact. The artifact contains anchor/light identities, world-space surface points, parameters, determinant, residual, iterations, epoch, and a typed rejection reason. Telemetry distinguishes missing chains, unsupported materials, invalid primitives, stale data, singular systems, total internal reflection, and residual failure.

The SDS response evaluator is a separate artifact-producing kernel. It reevaluates light emission expressions and textures at the sampled light UV, transports a spectral Stokes packet through the solved chain with the shared Mueller boundary implementation in radiance mode, evaluates the non-delta anchor BSDF, and composes those factors with the generalized geometry, light endpoint PDF, explicit reciprocal-root weight, and MIS weight. The artifact retains emission, anchor scattering, specular response, and final Stokes components for replay and audit. It is not committed to film until root selection and scheduling are expectation-unbiased.

Root selection follows reciprocal-probability SMS rather than a fixed trial cap. Independent seed walks continue across bounded GPU passes until the target root is reproduced; the geometric trial count is the unbiased estimator of reciprocal root probability. A render sample becomes complete only after that state resolves, and final output drains pending states. A fixed maximum trial count, treating a topology/root probability as one, or committing censored pending trials is forbidden because each introduces bias.

## Configuration and modes

`IntegratorMode` gains `BDPT` and `VCM`. A bidirectional configuration owns maximum camera/light vertices, connections per pixel, memory budget, and light-tracing policy. A VCM configuration owns initial radius, alpha, grid capacity, and surface/volume merge policy. Existing `SpecularManifoldConfig` remains the bounded Newton policy.

Native schema, JSON, CLI, C ABI, Session, distributed metadata, and pyure expose the same mode and policy values. Unsupported combinations fail before GPU allocation. Shards may merge only when integrator mode, spectral domain, radius iteration, and estimator policy match.

## Verification and closure

Host oracles cover measure conversion, technique enumeration, forward/reverse PDF ratios, power-heuristic partition, progressive radius, merge normalization, small-matrix Newton steps, determinant/Jacobian reciprocity, TIR, and singular rejection.

GPU tests cover light endpoint sampling/PDF parity, camera/light vertex capture, visibility connection, dielectric spectral/Stokes transport, BDPT technique normalization, VCM hash/merge/radius reset, manifold convergence and rejection, memory lifecycle, and deterministic sample-space replay.

Fixed glass-caustic, SDS, small-emitter, and rough/specular mixed workloads report bias bounds, MSE, variance, time-to-error, samples/s, memory, connection/merge counts, Newton iterations, residuals, and rejection reasons. Each mode must leave fail-loud state only after its correctness and benefit gate passes. The phase closes only after Release full build, complete CTest, Phase R static audit, the R-P4 benchmark suite, documentation consistency, and source self-review.
