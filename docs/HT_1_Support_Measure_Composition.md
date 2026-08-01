# HT.1 Support/Measure Graph and Composition

Document status: Current HT.1 architecture

Last verified: 2026-08-01

HT.1 replaces informal event masks and pairwise compatibility guesses with an executable, bounded description of path support, measure conversion and estimator composition. It does not yet select techniques or allocate runtime budgets automatically; HT.2 and HT.3 own those decisions.

## Bounded path-event grammar

`PathEventGrammar` is a finite union of ordered alternatives. Each clause accepts one or more declared `PathEvent` symbols with a bounded occurrence interval. Every grammar declares a maximum event count, receives a content identity and compiles through an epsilon NFA into a deterministic automaton under explicit NFA/DFA state budgets.

The grammar is intentionally finite. It covers the renderer's current bounded camera/emitter, diffuse/glossy, smooth-delta, volume, wavelength-shift, diffractive and coherent event vocabulary without presenting an unbounded regular-expression language as a GPU allocation contract.

## Exact support partitions

`compile_support_partition_graph` binds one grammar to every contributing Technique Graph node and compiles the target integral grammar plus all technique grammars into one reachable product automaton. Each accepting target state produces a technique-membership bit mask. Paths with the same mask form one canonical support partition.

The compiler rejects before GPU resource allocation when:

- an estimator node has no unique grammar binding;
- an NFA, DFA, product state set or partition set exceeds its declared budget;
- a target-accepted path has no eligible technique, with a concrete witness sequence;
- a technique accepts a terminating path outside the target integral, also with a witness;
- the source Technique Graph or any grammar identity is invalid.

This product construction is exact for the declared finite grammars. It does not infer material feasibility, visibility or a nonzero numerical PDF from an event name; runtime densities and scene eligibility remain explicit inputs.

## Canonical measure and MIS

Every composition plan names one canonical `MeasureDescriptor`. Each contributing technique supplies an explicit identity, constant-Jacobian or sample-Jacobian transform from its native coordinates. For `y = T(x)`, the stored absolute Jacobian is `|dy/dx|` and the canonical density is `p_y = p_x / |dy/dx|`. Missing conversion identities, non-finite values, zero or out-of-bound Jacobians and incompatible integral identities reject.

Independent explicit-PDF techniques in the same support partition form one MIS family. Balance and bounded power heuristics use the standard score `(n_i p_i)^beta`; every active technique must provide its canonical density and allocated sample count, and the weights must normalize to one. A compact fixed-layout program carries the same validated transforms and heuristic to the CUDA composition gate.

## Estimator families and output layers

Composition is hierarchical rather than forcing every estimator into per-sample MIS:

- explicit-PDF independent techniques form one MIS group;
- reservoir/reuse techniques retain generalized-resampling semantics and require a validated GRIS record;
- unbiased contribution-weight and deterministic estimators remain independent contribution groups;
- Markov-chain estimators aggregate only normalized, independently identified chain replicates;
- groups that estimate the same partition use fixed, data-independent aggregation weights until HT.2 supplies evidence-backed allocation.

Bias maturity is also structural. Unbiased, asymptotically unbiased, consistent, preview and research results occupy distinct `EstimateLayer` values. A preview-only technique cannot satisfy an unbiased support requirement and never shares its accumulator with the unbiased output.

## GRIS provenance

`GrisProvenance` binds reservoir, source/target snapshot, proposal mixture, support partition, sample namespace, reuse mapping, selected sample, source technique, generation and candidate count. Its selected target/proposal density, candidate-weight sum, reuse Jacobian and normalization weight are finite and cross-checked against `W / (M p_hat(y))`.

The record does not claim that arbitrary temporal or spatial reuse is valid. The producer must supply the mapping identity and Jacobian whenever snapshots differ, and later execution must preserve these fields in HR.0 measurement provenance.

## Markov-chain aggregation

Each `MarkovChainReplicate` carries technique, chain, replicate, sample namespace, integral, support partition, observation snapshot and normalization identities plus retained count, bootstrap normalization, normalized estimate, ESS and a fixed aggregation weight. Aggregation rejects duplicate chains, replicates or sample namespaces and any target mismatch. It reports the fixed-weight estimate, between-replicate variance and summed ESS; it never inserts chain transitions into ordinary MIS.

## Evidence

The host gate covers grammar compilation, exact overlap partitions, finite path-space enumeration, support-hole/outside-target witnesses, state budgets, analytic balance-MIS expectation, sample Jacobians, preview separation, GRIS normalization and independent Markov replicates.

The CUDA gate compiles a two-technique support/composition graph on the host, packs its overlap MIS group, executes the same density/Jacobian policy on the GPU and recovers the exact finite integral `7.0` with weights that sum to one at every enumerated point.

```powershell
.\scripts\check_phase_ht1_support_measure_graph.ps1
ctest --test-dir build_modular_x64 -C Release -R "^(test_support_measure_graph|gpu_support_measure_composition)$" --output-on-failure
```

The same transport sources and host gate compile independently under `tests/sdk_free` with warnings as errors, and the installed-package consumer includes the public Support/Measure Graph header.

## Current boundary

- Existing legacy presets are still selected by current configuration routes. HT.1 supplies the composition contract; it does not claim that all legacy CUDA kernels now execute concurrently.
- Scene/world-dependent qualification, pilot cost/variance/covariance estimation and user-free technique selection begin in HT.2.
- Online budget allocation, drift detection and distributed portfolio scheduling begin in HT.3.
- A grammar proves declared event-language coverage, not visibility, numerical support or solver applicability. Those facts must arrive as validated runtime capability and density evidence.
- GRIS and MCMC records make normalization and correlation explicit; they do not erase the statistical assumptions of either estimator family.
