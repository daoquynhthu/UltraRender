# HT.4 Transport Integrator and Proposal Research Platform

Document status: Current HT.4 architecture

Last verified: 2026-08-01

HT.4 provides an executable route for new transport ideas without assigning them a production enum, default path or premature backend ABI. A new estimator, proposal, control variate, shift map or hybrid technique enters as a capsule-bound `ResearchExtension` node. It must state its sample, normalization, reuse, observable and evidence semantics before it can be materialized into a Technique Graph, and graph materialization always requires explicit research opt-in.

The phase establishes the platform and two replayable research outcomes. It does not claim completion, usefulness or eventual promotion of every research direction listed in `PLAN.md`.

## ResearchExtension boundary

`TechniqueFamily::ResearchExtension` is a generic Technique Graph family rather than a growing list of paper names. A research node still uses the HT.0/HT.1 estimator contracts for observable, target integral, measure, support, density, normalization, correlation, bias, resources and backend capability. Its Technique Graph representation additionally carries the Research Capsule identity. Existing legacy graph identities remain unchanged because the capsule field is encoded only for a research extension.

`TransportResearchDescriptor` binds:

- capsule, source, falsifiable hypothesis and algorithm identities;
- applicability and known failure-domain identities;
- baseline and candidate experiment variants;
- the complete research Technique Descriptor;
- joint sample and reuse contracts;
- one falsifiable support, time-to-error or observable-unlock claim;
- Research or evidence-backed Experimental maturity.

Production maturity is deliberately rejected by this platform. HT.4 can identify evidence suitable for promotion review, but it cannot enable a default route or bypass the HO.2 promotion checklist.

## Open mechanism model

The first contract version supports semantic mechanism classes rather than predetermined algorithms:

- independent and Markov estimators;
- proposal services;
- control variates;
- replay/shift maps;
- multifidelity estimators;
- hybrid-observable estimators.

These classes are validation rules, not implementations. A future manifold-guiding or neural proposal can remain a proposal node; a multiplexed path-space MCMC experiment can remain a Markov estimator; a gradient-domain method can expose a shift map; a wave/radiance coupling can expose a hybrid observable. The contract does not need a new user-facing integrator mode for each paper.

## Joint sample contract

`ResearchJointSampleContract` content-binds the sample space, random layout and all identities needed by the declared mechanism:

- exact proposal density and estimator normalization;
- Markov transition, chain normalization and independent replicate namespace;
- forward/inverse shift and Jacobian;
- known expectation for control-variate or multifidelity terms.

A Markov estimator cannot pass by declaring ordinary samples: it must use `MarkovTransition`, `ChainBootstrap`, `MarkovChain`, an explicit transition and normalization, and independent replicate namespaces. This preserves the HT.3 decision that ordinary paired sample covariance cannot be inserted between a sample-mean estimator and an MCMC chain. The negative capsule keeps that shortcut closed while leaving replicated chain-level covariance as a future research path.

Proposal nodes require an exact density identity. Control variates require the expectation used to preserve unbiasedness. Shift maps require forward, inverse and Jacobian identities and a replay layout. Missing mathematical terms reject at descriptor finalization, before an experiment graph exists.

## Reuse validity under a changing world

Every descriptor declares dependencies on geometry, material, emission, media, sensor, time or solver state. Reuse is one of:

- `NoReuse`;
- `ExactSnapshot`;
- `ReweightedTransportMap` with forward map, inverse, Jacobian and validity evidence.

This is intentionally stricter than a frame-count heuristic. A proposal trained under another world state cannot claim validity merely because its storage is still available. More local dependency and transport-map research can extend the evidence, but unknown changes remain invalid rather than silently biased.

## Capsule registry and graph materialization

`TransportResearchRegistry` accepts a descriptor only when its capsule/source identities match an independently valid HO.2 `ExperimentDefinition`, the named baseline/candidate variants exist, and the candidate parameter identity matches the research technique. Duplicate descriptor and technique identities reject.

Materialization starts from a valid baseline Technique Graph and is explicit opt-in. The registry appends the research node and a typed `ProposalFor`, `ReplayFor` or `CoupledEstimatorFamily` edge to the named baseline technique, then runs the normal graph validator. The resulting graph therefore preserves observable/integral compatibility, edge semantics, acyclicity and a capsule-bound graph identity. Registration alone never changes the production graph.

## Replicated assessment

HT.4 strengthens the HO.2 comparison boundary with `validate_comparison_result`. It rechecks finite observations, equal nonzero sample counts, distinct artifact identities, means, difference, standard error and confidence interval. A modified summary cannot be assessed merely because its individual artifacts look valid.

`assess_transport_research` orients the independent-replicate confidence interval as an improvement interval according to the metric direction. A result is:

- positive only when the entire interval exceeds the declared minimum effect;
- negative when the entire interval is non-improving;
- inconclusive otherwise.

A positive assessment is only `promotion_review_eligible`. Applicability, failure domain, bias class, resource evidence and explicit opt-in still pass through the full HO.2 Research-to-Experimental checklist.

## Representative positive experiment

[`analytic_control_variate.json`](research/ht4/capsules/analytic_control_variate.json) executes a real bounded Monte Carlo experiment. The baseline samples `x²` over `[0,1)`. The candidate subtracts `x - 0.5`, whose expectation is exactly zero. Eight independently namespaced replicates use 4,096 deterministic counter-based samples each and compare sample variance at equal declared cost.

The current deterministic evidence is:

```text
baseline variance mean  = 0.089368742599
candidate variance mean = 0.005522631813
95% improvement interval = [0.082597484992, 0.085094736580]
```

The interval clears the declared `0.01` effect threshold. This proves that the registry, joint-sample contract, independent runner and assessment can carry a positive estimator experiment. It does not prove a SceneIR rendering benefit, GPU efficiency or broad control-variate applicability, so the descriptor and capsule remain Research.

## Preserved negative result

[`mcmc_sample_covariance_rejected.json`](research/ht4/capsules/mcmc_sample_covariance_rejected.json) records that ordinary sample-level covariance is not a valid shortcut for a Markov endpoint. HT.3 rejects the edge, while HT.4 requires transition, chain normalization and independent replicate namespaces. This is a closed negative branch, not an unsupported claim that correlation-aware MCMC aggregation is impossible.

## Evidence

The host gate covers descriptor tampering, capsule/experiment binding, explicit opt-in, research graph validation, duplicate rejection, control-variate expectation, Markov replicate normalization, shift inverse/Jacobian, reweighted reuse evidence, maturity boundaries, actual replicated Monte Carlo comparison, positive/negative/inconclusive assessment rules and summary-tamper rejection.

The same source builds under the independent SDK-free tree with warnings as errors. The installed-package consumer includes the public transport-research contract.

```powershell
.\scripts\check_phase_ht4_transport_research.ps1
ctest --test-dir build_modular_x64 -C Release -R "^(test_transport_research|test_research_substrate|test_technique_graph)$" --output-on-failure
```

## Research basis

- [Hachisuka et al., A Path Space Extension for Robust Light Transport Simulation](https://research.nvidia.com/publication/2012-04_path-space-extension-robust-light-transport-simulation) motivates adding explicit sampling-space and measure structure before combining estimator families.
- [Bitterli and Jarosz, Selectively Metropolised Monte Carlo Light Transport Simulation](https://cs.dartmouth.edu/~wjarosz/publications/bitterli19selectively.html) motivates keeping difficult-path MCMC selective and defensively combined with a baseline rather than globally replacing it.
- [Guo et al., Primary Sample Space Path Guiding](https://publications.graphics.tudelft.nl/papers/205) demonstrates that a proposal can operate in primary sample space outside the rendering kernel, supporting the separation between proposal service and estimator.
- [Hart et al., Practical Product Sampling by Fitting and Composing Warps](https://research.nvidia.com/publication/2020-07_practical-product-sampling-fitting-and-composing-warps) motivates explicit composable warp, density and Jacobian contracts.

These papers demonstrate that materially different research mechanisms need different mathematical contracts. They do not predetermine which future HT.4 capsules will be successful.

## Current boundary

- No new research technique is enabled in RenderConfig, CLI, C ABI, pyure or a production CUDA dispatch graph.
- The representative control variate is a bounded host oracle, not a CPU production integrator and not a SceneIR renderer.
- Neural training/runtime, manifold guiding, multiplexed MCMC, gradient-domain reconstruction, transient/hybrid transport and nonstationary maps remain research directions rather than claimed implementations.
- `ResearchExtension` is not a stable plugin ABI. Phase X remains frozen until the world, transport, measurement and solver boundaries mature.
- A positive local capsule does not imply Experimental maturity; a negative capsule may legitimately close a branch.
