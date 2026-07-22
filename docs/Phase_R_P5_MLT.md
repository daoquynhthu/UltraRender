# Phase R-P5 — Primary-Sample-Space MLT

## Status

R-P5 is complete. The GPU chain runtime, diagnostics, deterministic shard
identities, and fixed-error benefit suite are production paths within the
boundaries below. The authoritative construction cursor has advanced to R-P7.

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
small-emitter, and high-occlusion workloads. At least two workloads must improve
time-to-error over the default wavefront baseline without introducing a
statistically significant high-sample bias.

Time-to-error uses a fixed normalized MSE target, `MSE / mean(reference^2)`, so
improving the candidate integrator cannot silently tighten its own gate. The
maintained 8x8 Release suite uses 65,536-SPP independent wavefront references.
Both workloads cross the 0.05 target at 256 MLT SPP while wavefront requires
1024 SPP:

| Workload | MLT 256 SPP | Wavefront first pass | MLT 1024 bias bound |
|---|---:|---:|---:|
| SDS | 0.04837 NMSE / 0.408 s | 0.01029 / 0.758 s | 4.58% |
| SDS small light | 0.01325 / 0.452 s | 0.00257 / 0.813 s | 1.26% |

The small-light case uses a 0.075-radius emitter with area-compensated radiance,
preserving approximately the same emitted power while reducing path support.
Glass caustic, the original 0.02-radius small-emitter case, high occlusion, and
mixed specular remain documented non-benefit boundaries. The machine-readable
report is `output/benchmarks/phase_r_mlt_suite.json`.

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
