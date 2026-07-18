# Phase R-P5 — Primary-Sample-Space MLT

## Status

R-P5 is the active construction phase. The GPU chain runtime is enabled for
validation and now has diagnostics plus deterministic shard identities. The phase
is not closed until the difficult-scene benefit suite passes; current short and
full-scale diagnostic runs show a real gain on the maintained small-emitter case,
but not yet across the complete caustic/small-light/high-occlusion set.

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
   dimension with a wrapped symmetric proposal. Camera, wavelength, surface,
   volume, light, lobe, and roulette dimensions therefore mutate together.
5. Proposed and current contributions are deposited with the standard PSSMLT
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
