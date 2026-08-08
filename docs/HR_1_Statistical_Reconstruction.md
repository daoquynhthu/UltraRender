# HR.1 Statistical Reconstruction Baseline

Document status: Current HR.1 architecture

Last verified: 2026-08-01

HR.1 establishes a training-data-free reconstruction baseline over typed physical measurements. It is a statistical estimator companion, not a replacement for the Monte Carlo estimate: every result retains the untouched raw estimate, reconstruction uncertainty, accepted support and rejection provenance.

## Contract boundary

[`ure_reconstruction`](../libs/ure_reconstruction/) owns an SDK-free `StatisticalReconstructionFrame`, validated configuration, temporal history and content-identified output. A frame binds the observable, SI unit, world definition/state, time sample, observation snapshot, Technique Graph and measurement schema inherited from HR.0.

The input carries raw estimates, sample variance, effective sample count, tail frequency, maximum absolute contribution, normal, albedo, depth, motion, motion/time confidence and validity. Sample variance is converted to estimate variance using ESS before filtering. Invalid samples may retain non-finite raw payloads for diagnosis, but the inferred reconstruction remains finite and does not change their validity mask.

Complex Jones and cross-spectral-density observables reject at this boundary. They require phase-aware reconstruction rather than radiance filtering and remain HR.2 work.

## Spatial baseline

The spatial path uses iterative edge-avoiding à-trous filtering. Shared weights combine the wavelet kernel with variance-normalized signal distance, normal, relative depth, albedo and heavy-tail confidence. The output reports an effective spatial-support measure from the first and second weight sums. If no sufficient support exists, the previous estimate is retained and the rejection reason remains explicit.

The design follows the training-free structure of edge-avoiding à-trous filtering and variance-guided filtering, while extending their data boundary to typed spectral and Stokes observables:

- Dammertz et al., [Edge-Avoiding À-Trous Wavelet Transform for Fast Global Illumination Filtering](https://jo.dreggn.org/home/2010_atrous.pdf)
- Schied et al., [Spatiotemporal Variance-Guided Filtering](https://cwyman.org/papers/hpg17_svgf.pdf)

These references motivate the baseline; UltraRender's physical-domain, provenance and tail contracts are project-specific.

## Tail and error attribution

A local median/MAD signal test is combined with producer-supplied tail frequency and maximum-contribution evidence. Classification distinguishes:

- `Ordinary`: no statistically exceptional evidence;
- `HeavyTail`: repeated extreme-contribution evidence, downweighted during spatial reconstruction;
- `HighEnergyPreserved`: spatially exceptional energy without heavy-tail evidence, retained by the variance-aware signal edge;
- `InvalidSample`: producer-declared invalid data, preserved in the raw layer and excluded as support.

The classifier does not claim to prove that a path is physically correct. It prevents a bright sample from being called a firefly solely because it is bright and preserves enough attribution for later sample-level analysis.

## Temporal baseline

History reuse requires matching image shape, observable, world definition, Technique Graph and measurement schema. The time and snapshot identities may advance; motion reprojection and the current per-pixel motion/time confidence govern reuse. Depth, normal and albedo tests reject disocclusion before blending. Accepted history uses one variance-derived weight shared across all observable components and reports confidence and bounded history length.

The explicit validity checks are consistent with the role of temporal reuse validation in modern spatiotemporal resampling, including ReSTIR's need to reject invalid history rather than silently reuse it: Bitterli et al., [Spatiotemporal Reservoir Resampling for Real-Time Ray Tracing with Dynamic Direct Lighting](https://cs.dartmouth.edu/~wjarosz/publications/bitterli20spatiotemporal.html).

## Physical observable handling

Spectrum components are reconstructed before any display conversion and must remain non-negative. Stokes filtering uses one shared convex weight for `I,Q,U,V`, preserving `sqrt(Q²+U²+V²) <= I` when all accepted inputs are physically realizable. No RGB tonemapping or display transform exists in this subsystem.

The CUDA implementation provides bounded temporal and one-iteration à-trous kernels for up to 32 components. The GPU gate compares its Stokes result and propagated variance directly against the SDK-free double-precision oracle. Multiple iterations use ping-pong buffers and the same immutable raw layer.

## Outputs and identity

`StatisticalReconstructionOutput` contains:

- untouched `raw_estimate` and separate `reconstructed` values;
- propagated variance and standard uncertainty per component;
- effective spatial support, temporal confidence and history length per pixel;
- tail class, rejection reason and original validity;
- configuration, frame, optional history and content-derived output identities.

Validation checks physical realizability, uncertainty/variance consistency, enum ranges, support/confidence bounds and content identity. Tampering changes or invalidates the identity.

## Current boundary

- HR.1 is the authoritative statistical baseline; the older RGB-only dark-outlier and fixed-sigma kernels remain compatibility test surfaces and are not the production definition.
- Existing complete-scene sessions do not yet emit every required HR.1 statistic. The bounded CUDA kernels are available and verified, but automatic post-processing is not enabled when variance, ESS, tail or dynamic-confidence planes are absent.
- Tail evidence must be derived after canonical HR.0 accumulation from sufficient statistics or bounded sample records. It must not be merged as an unweighted per-shard percentage.
- Quality reports must always include the raw estimate and evaluate bias or convergence against that layer. A visually smoother reconstructed layer is not evidence of estimator correctness.
- HR.2 owns sample-level, learned and phase-aware reconstruction. HR.1 does not introduce model weights, training data or a learned ABI.

## Verification

```powershell
.\scripts\check_phase_hr1_statistical_reconstruction.ps1
ctest --test-dir build_modular_x64 -C Release -R "^test_statistical_reconstruction$|^gpu_denoise$" --output-on-failure
```

The same SDK-free source and host gate build independently under `tests/sdk_free` with warnings as errors. The installed-package consumer includes the HR.1 public header and checks its version.
