# Phase R-P5 — Primary-Sample-Space MLT

## Status

R-P5 is complete. The GPU chain runtime, diagnostics, deterministic shard
identities, and fixed-error benefit suite are production paths within the
boundaries below. The phase-local cursor advanced to R-P7 at this historical
closure point; the root `PLAN.md` owns the current PRV cursor.

## Estimator boundary

MLT replays the production wavefront contribution evaluator. It does not own a
second intersection, material, volume, spectral, polarization, NEE, or Russian
roulette implementation. Every stochastic decision is addressed by the existing
dimension layout and may be supplied by an explicit per-context primary-sample
view carried by the ray queue. A null view retains the default low-discrepancy
sampler.

The view is immutable during one proposal evaluation and is indexed by global
chain identity plus dimension. It is passed through queue values rather than CUDA
device globals so concurrent renderer contexts and devices cannot alias sampler
state.

## Chain runtime

The production scheduler evaluates independent chains in batches:

1. Uniform bootstrap paths estimate the normalization constant and provide a
   luminance-weighted seed distribution.
2. Each chain receives its own current vector, proposed vector, contribution,
   image location, last-large-step epoch, and counter-based random stream.
3. Burn-in runs the same transition kernel without film deposition.
4. A large step replaces the complete active vector. A small step mutates every
   dimension with the same wrapped symmetric Laplace proposal used by the host
   oracle. Camera, wavelength, surface,
   volume, light, lobe, and roulette dimensions therefore mutate together.
5. Each GPU shard uses deterministic stratified bootstrap CDF resampling,
   reducing redundant seeds within that shard without changing the target
   distribution. Global chain identities keep shard RNG streams disjoint.
6. Proposed and current contributions are deposited with the standard PSSMLT
   acceptance weights and bootstrap normalization. Proposal pixels are never
   written into the film before the accept/reject decision.

The active dimension count is derived from the configured maximum depth and the
authoritative sampling stride. Allocation is checked against an explicit or
device-derived memory budget; the runtime fails before partial allocation when
the requested chain population cannot fit.

## Determinism and distribution

Random values are counter based and keyed by the user seed, global chain ID,
mutation index, dimension, and proposal kind. A distributed shard owns a disjoint
contiguous global-chain interval. Results must therefore be invariant to device
enumeration, launch geometry, restart boundaries, and shard execution order.

## Required diagnostics and gates

The public context telemetry reports bootstrap mean and positive count, proposed
and accepted mutations, large and small steps, zero-target transitions, deposited
samples, and acceptance rate. Invalid/non-finite contributions are rejected and
counted, never silently deposited.

Closure requires deterministic CPU/GPU replay tests, shard seed disjointness and
reassembly tests, full CTest, and Release curves for low-probability caustic,
small-emitter, and high-occlusion workloads. The R-P7 contract requires at least
one reproducible positive workload and one explicit boundary for each advanced
mode, without a statistically significant high-sample bias.

Time-to-error uses a fixed normalized MSE target, `MSE / mean(reference^2)`, so
improving the candidate integrator cannot silently tighten its own gate. Schema
`ure.phase_r.mlt_suite.v2` uses four disjoint wavefront reference shards, four
independent wavefront sample ranges, and four independent MLT chain identities.
It reports replicate mean/variance, median GPU time, and a full-image replicate
bias interval that also includes reference-shard uncertainty.

This audit invalidated the earlier 8x8 two-workload claim: its wavefront curve
shared a sample prefix with the reference image, correlating the error estimate.
The claim is removed rather than preserved as historical evidence. Under the
hardened 16x16 gate, `sds_small_light` reaches 5% NMSE at 64 MLT SPP in
approximately 0.157 seconds, while wavefront first reaches it at 256 SPP in
approximately 0.294 seconds; its final 95% relative-bias bound is approximately
1.7%. SDS, the original small-emitter case, glass caustic, and high occlusion
are retained as non-benefit statistical boundaries. The more extreme
area-compensated high-occlusion small-light
variant has a deterministic path-distribution contract, but is not included in
the default statistical matrix because its current-budget high-sample
confidence bound is unstable. The machine-readable report is
`output/benchmarks/phase_r_mlt_suite.json`.

The BDPT audit corrected two independent defects: spectral accumulators now
retain the camera path wavelength grid, and reverse strategy reconstruction uses
the camera vertex's actual outgoing-light direction. The standalone diffuse
energy regression covers this boundary.

The complete bidirectional evaluator remains available to standalone BDPT, but
is not an MLT target. Camera and light subpaths do not yet share one authoritative
spectral wavelength sample in sampled-lane mode. MLT+BDPT therefore fails loudly
instead of exposing a configuration that is only correct for uniform packets.
VCM, manifold, and adaptive reuse schedulers remain rejected in MLT mode until
their mutable state and spectral contracts are Markov-owned.
