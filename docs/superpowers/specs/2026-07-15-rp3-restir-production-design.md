# R-P3 Production ReSTIR Design

> Document status: Active
>
> Current design for the authoritative R-P3 cursor. This document is subordinate to `PLAN.md` and records the estimator and runtime contracts chosen for implementation.

## Scope

R-P3 separates the existing explicitly biased temporal direct-light preview from two production modes:

- unbiased temporal and spatial ReSTIR DI;
- ReSTIR PT path reuse with an independent mode and sample-space contract.

The phase also supplies reproducible bias and benefit evidence for multi-light, occlusion, and volume scenes. It does not implement the specular-manifold, BDPT/VCM, or MLT work assigned to R-P4 and R-P5.

## Rejected approaches

1. Replaying a previous visible shadow contribution with a history divisor is retained only as the biased preview. It cannot become unbiased because it does not reevaluate the sample at the current shading point and has no reservoir normalization.
2. Storing final RGB contribution as the reusable sample is rejected. RGB loses the spectral proposal, light-domain identity, and reconnection information needed to evaluate another pixel's target density.
3. Treating ReSTIR PT as a boolean extension of ReSTIR DI is rejected. Direct-light reservoirs and path suffix reuse have different state, validity, and replay contracts.

## Runtime modes

`IntegratorMode` gains `RestirPT`. `RestirDirectConfig` remains the DI configuration surface and gains bounded spatial-neighbor controls. A separate `RestirPathConfig` owns PT reuse depth, candidate count, history bound, and reconnection thresholds.

ReSTIR DI has two explicit policies:

- preview: `unbiased=false`, requires `allow_biased_reuse=true`, and emits biased metadata;
- production: `unbiased=true`, permits temporal and/or spatial reuse, and never consumes the preview reservoir layout.

Every render result/session diagnostic exposes the selected integrator, estimator policy, temporal/spatial reuse flags, and whether the output is biased. Distributed shards must carry compatible policy metadata before merge.

## Direct-light sample and reservoir

A reusable DI sample stores light-list identity, light kind, primitive identity, canonical light parameters, sampled direction/distance, spectral emission state, wavelength proposal metadata, and the visibility/reconnection epoch. It does not store a final pixel contribution as authoritative data.

Each reservoir stores:

- the selected reusable sample;
- `weight_sum`, candidate count `M`, selected target density, and final normalization weight;
- source pixel/pass, scene epoch, geometry/material/light epochs, surface or volume domain, and reconnection data;
- Stokes-compatible incident state and spectral lane metadata.

Candidate streaming uses deterministic path dimensions. Invalid, non-finite, zero-density, stale-epoch, incompatible-domain, or non-reconnectable candidates are rejected before their weight reaches the reservoir.

## Unbiased temporal and spatial reuse

At the current shading point, every reused candidate is reconstructed and its target density is evaluated using the current BSDF or phase function, cosine/geometric term, light selection/conditional PDF, wavelength PDF, Stokes throughput, and visibility. Production mode includes visibility in both directions of the shift. The cheaper selected-sample-only visibility policy remains preview-only because incomplete proposal support is insufficient for the convergence claim required here.

Temporal candidates use previous-frame geometry identity, position, normal, depth, material/medium identity, motion-vector reprojection, and scene epochs. Spatial candidates come from a deterministic bounded neighbor pattern and pass the same compatibility checks. Surface and volume candidates never cross domains.

Combination uses the defensive generalized pairwise MIS from GRIS over the proposals that could have produced the candidate. Each pair evaluates both the current target of the reused sample and the source-domain target of the canonical sample; substituting the stored selected target for either cross-evaluation is forbidden. The reservoir carries reciprocal marginal contribution weight and bounded confidence. No visibility result is reused across a changed connection.

Ping-pong reservoirs make reads immutable during a pass and writes race-free. Spatial reuse reads only the completed input reservoir set. Single- and multi-GPU execution partition sample space deterministically; no device reads another device's mutable reservoir memory.

## GPU scheduling boundary

Production reuse is a separate GPU pass, not additional control flow inside the monolithic wavefront shade kernel. The pass consumes immutable fresh-candidate and shading-domain buffers, performs bidirectional target reconstruction, writes the opposite reservoir set, and emits selected shadow work. Shared light reconstruction and BSDF/phase target evaluation live in focused device modules used by both the ordinary NEE path and the reuse pass.

This boundary is also a build and occupancy invariant. An attempted inline implementation made the CUDA translation unit fail to finish compilation within ten minutes and materially enlarged the shade kernel. R-P3 therefore forbids embedding the spatial/temporal reuse loop in `shade_kernel`; static audit and target-level build evidence must preserve the separate-pass structure.

## ReSTIR PT

ReSTIR PT stores reconnectable path suffix vertices rather than DI light samples. A suffix records geometry/medium identity, directions, forward and reverse PDFs, wavelength proposal state, Stokes throughput, random-dimension interval, scene epochs, and terminal emission/environment identity.

The independent `RestirPT` scheduler generates ordinary paths, proposes temporal/spatial suffixes at non-delta vertices, reconnects them under current geometry and medium visibility, and evaluates proposal ratios in a shared measure before reservoir selection. Delta chains that cannot be reconstructed are rejected. Deterministic replay owns a versioned sample-space layout so adding a dimension cannot silently reinterpret cached suffixes.

R-P3 PT covers radiometric surface and supported volume suffixes. Specular manifold connections remain fail-loud until R-P4.

## Ownership and invalidation

Reservoir buffers are context-owned RAII allocations with checked size arithmetic and a configurable memory budget. Resolution, camera discontinuity, integrator policy, spectral layout, light distribution, material/resource, instance topology, or volume-resource changes invalidate affected history. Transform changes may retain history only when previous transforms and stable primitive identity permit reprojection.

Session reset, scene reload, and device reconfiguration clear both ping-pong sets. Allocation failure is reported before rendering; the implementation does not silently disable reuse.

## Verification

Host oracle tests cover reservoir streaming, normalization, pairwise MIS, history clamping, reconnection rejection, deterministic neighbor selection, and unbiased expectation against enumerated discrete proposals.

GPU tests cover surface and volume target reevaluation, temporal reprojection, spatial reuse, occlusion changes, wavelength PDF propagation, Stokes state, stale epochs, buffer reset, and deterministic replay. A regression test proves the previous history-divisor replay cannot satisfy the production policy.

Reference comparisons use fixed multi-light, occlusion, and volume scenes. For each scene the suite records mean bias with confidence bounds, MSE, variance, time-to-error, samples/second, and reservoir rejection counters. Production unbiased mode must agree with the wavefront reference within the predeclared statistical bound. A claimed benefit requires a lower time-to-error on at least one fixed workload without regression beyond tolerance on the others.

The phase closes only after Release build, the complete CTest gate, Phase R static audit, the R-P3 benchmark suite, documentation consistency audit, and source self-review pass.
