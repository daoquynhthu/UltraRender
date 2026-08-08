# HR.2 Sample-level Spectral and Polarimetric Reconstruction

Document status: Current HR.2 Research architecture

Last verified: 2026-08-08

HR.2 establishes an executable Research boundary for reconstruction from typed Monte Carlo sample records. It does not install a trained model or promote a neural denoiser. The purpose is to preserve the sample, path, spectral, polarization and phase semantics that future kernel-prediction, point-set and hybrid experiments need, while keeping raw estimates and applicability evidence available for honest comparison.

## Sample record and batch

[`sample_reconstruction.hpp`](../libs/ure_reconstruction/include/ure/reconstruction/sample_reconstruction.hpp) defines content-identified records carrying:

- sample, technique, path-event, material, medium and spectral-resource identities;
- raster position, time, detector/transport wavelength and joint PDF;
- estimator weight, kernel radius, depth, normal and typed albedo feature;
- the observable value and, for coherent data, an exact phase-reference identity.

A batch binds those records to the HR.0 measurement schema and the HO.1 world, state, time, snapshot and Technique Graph identities. Records are reduced in canonical sample-identity order, so file, device or caller ordering cannot alter the result. Duplicate sample identities reject.

The raw layer is the nominal-pixel average of `value * estimator_weight / joint_pdf`. Sample-space reconstruction is separate and cannot overwrite or become evidence for raw-estimator bias.

## Executable sample-space baseline

The analytic Research baseline splats individual samples over a bounded Gaussian kernel. Target-pixel affinity uses raster distance, time, detector wavelength, depth, normal and the typed albedo feature. It reports effective support, uncertainty, confidence, projection distance and rejection state.

The choice to retain and splat individual samples is informed by Gharbi et al., [Sample-based Monte Carlo Denoising using a Kernel-Splatting Network](https://groups.csail.mit.edu/graphics/rendernet/data/mc_denoising.pdf), which shows why aggregate pixel buffers can discard useful multimodal sample information. UltraRender's implementation is an analytic oracle and contract gate; it does not reproduce or claim the trained network from that work.

The fixed positive Research Capsule uses a five-pixel, three-component spectral fixture. It requires lower reconstructed MSE than the raw layer, exact permutation invariance, zero physical violations and sensor-observation residual below `1e-8`.

## External Research candidates

Three external method families share the sample contract:

- `ExternalKernelPrediction` for learned or experimental per-sample kernels;
- `ExternalSampleTransformer` for permutation-invariant point-set/attention outputs;
- `ExternalHybrid` for combined kernel and point-set decisions.

Kernel prediction is motivated by Bako et al., [Kernel-Predicting Convolutional Networks for Denoising Monte Carlo Renderings](https://studios.disneyresearch.com/wp-content/uploads/2019/03/Kernel-Predicting-Convolutional-Networks-for-Denoising-Monte-Carlo-Renderings-Paper33.pdf). The permutation contract follows the general point-set requirement exemplified by Qi et al., [PointNet](https://openaccess.thecvf.com/content_cvpr_2017/papers/Qi_PointNet_Deep_Learning_CVPR_2017_paper.pdf). These are research directions, not imported model architectures.

UltraRender intentionally does not freeze a model file or inference ABI in HR.2. An external provider returns a sorted mapping from sample identity to multiplier, kernel radius and confidence. That mapping is content-bound to the exact input batch, candidate, provider and artifact. Execution requires explicit Research opt-in. Missing, duplicated, reordered, stale-batch or provenance-mismatched outputs reject before reconstruction. Production maturity is rejected by this contract because no promotion evidence exists yet.

The negative Research Capsule records this decision: an unbound kernel-prediction, transformer or hybrid result cannot execute merely because its numbers look plausible.

## Physical projections

Every supported domain has an explicit policy:

- Spectrum uses an Euclidean nonnegative projection. When a sensor response and per-pixel observation are present, a deterministic one-dimensional dual solve enforces `dot(response, spectrum) = observation` within tolerance.
- Stokes uses the exact second-order-cone projection for `I >= sqrt(Q²+U²+V²)`. All four components remain one physical state.
- Complex Jones values are filtered with one shared scalar weight over adjacent real/imaginary pairs. No componentwise magnitude clamp is applied. Batch and sample phase-reference identities must match, and a global phase rotation of every input rotates the output by the same phase while preserving energy.

Projection distance is reported per pixel and added conservatively in quadrature to uncertainty. The projected layer remains a reconstruction; it never retroactively makes a nonphysical raw sample valid.

## OOD and confidence evidence

Each external candidate declares an applicability envelope over observable domain, component layout, world definition, measurement schema, wavelength, technique, material, sample count and polarization degree. Default execution rejects any mismatch. Research-only OOD override retains a bit mask, marks pixel rejection state and reduces reported confidence.

Evaluation reports raw/reconstructed MSE, empirical one- and two-sigma coverage, deviation from Gaussian nominal coverage, maximum sensor-observation residual, permutation error and physical violation count. The current gates cover:

- in-domain deterministic spectral reconstruction;
- unseen world and material identities;
- out-of-range wavelength;
- overpolarized Stokes input;
- negative spectrum with sensor-observation consistency;
- coherent Jones gauge covariance and phase-reference mismatch;
- content tampering and unbound external providers.

Coverage metrics are diagnostics, not a claim that the small deterministic fixture proves general calibration. Scene-scale independent repeats and actual trained candidates remain required for any Experimental promotion.

## Current boundary

- All HR.2 outputs are explicitly `Research` maturity.
- No trained weights, ONNX/TensorRT dependency, permanent model format or production inference provider is shipped.
- The SDK-free executor is a research/oracle path. Complete-scene GPU sample-record production and high-throughput reconstruction are not claimed.
- The analytic positive capsule does not establish edge preservation, temporal quality, arbitrary material generalization or time-to-error benefit on rendered scenes.
- Complex Jones support is gauge-covariant averaging under one phase reference. Partial-coherence/CSD reconstruction and gauge changes remain unsupported.
- HR.3 owns proposal/control-variate use and correction rules. HR.2 reconstructed values cannot enter an unbiased estimator without those contracts.

## Verification

```powershell
.\scripts\check_phase_hr2_sample_reconstruction.ps1
ctest --test-dir build_modular_x64 -C Release -R "^test_sample_reconstruction$" --output-on-failure
```

The same source and test compile in the independent SDK-free build with warnings as errors. The installed-package consumer includes the public HR.2 header and checks its version.
