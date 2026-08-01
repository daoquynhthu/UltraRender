# HO.2 Executable Research Substrate

Document status: Current HO.2 architecture

Last verified: 2026-08-01

HO.2 provides a shared, SDK-free execution and evidence layer for high-order research. It makes experiments reproducible and comparable without claiming that provisional algorithms have production ABI, lifecycle or backend coverage.

## Module boundary

[`ure_research`](../libs/ure_research/) depends only on `ure_runtime` and `ure_transport`. Its installed headers expose no CUDA, Vulkan, D3D12, Windows or OpenUSD type. The module owns six contracts:

- research execution manifests and deterministic sample/counter allocation;
- measurement artifact indexing, storage and selective reads;
- experiment registration and replicated baseline/candidate comparison;
- maturity-aware feature capability negotiation;
- bounded host and small-GPU oracle hooks;
- machine-readable promotion evidence evaluation.

These contracts may use dynamic research-side values and callbacks. They are not a promise that their current C++ object layout is a stable public ABI.

## Execution identity and random ranges

`ResearchExecutionManifest` binds a run to capsule, source, parameter set, seed namespace, semantic provenance, rational observation time, global seed, replicate and execution mode. Its canonical SHA-256 identity changes whenever one of those inputs changes.

`allocate_execution_shards` sorts worker identities before allocation, then assigns contiguous sample and counter ranges by declared capacity weight. Input worker order therefore cannot change the partition. Every shard records:

- exact sample start/count;
- exact counter start/count;
- a replicate-specific random namespace;
- worker identity;
- a digest over manifest, worker and both ranges.

Replicates and experiment variants derive distinct random namespaces. Local, multi-device and farm executions use the same allocator and range validator. Re-running the same manifest reproduces the same identity by design; callers create a distinct run or replicate identity when they need independent evidence.

## Measurement artifact container

The `UREM` container is a little-endian, versioned research artifact envelope with a fixed header and fixed-size front directory. Each chunk declares:

- semantic role and schema version;
- codec;
- stored and uncompressed sizes;
- element/component shape;
- semantic identity;
- SHA-256 digest of uncompressed content.

The container identity hashes schema/source identities and every canonical chunk descriptor/content digest. It can therefore be checked from the header and directory without reading payload bytes. The partial-read flow is:

1. read the fixed header and obtain `measurement_artifact_index_size`;
2. read only that prefix and call `inspect_measurement_artifact_index` with total file size;
3. fetch one descriptor's byte range;
4. decode and authenticate only that range with `read_artifact_chunk_payload`.

The initial codecs are `None` and deterministic byte run-length compression. RLE is a bounded reference codec for highly repetitive masks/count grids, not a general scientific compression recommendation. Container, aggregate stored/uncompressed, per-chunk and expansion-ratio budgets are checked before allocation. Malformed offsets, overflow, truncation and digest mismatch reject.

`SampleCount`, `FirstMoment`, `SecondMoment` and `CrossMoment` are explicit chunk roles. `has_sufficient_statistics` recognizes the minimal count/first/second-moment set; it does not assert that those statistics are sufficient for every future observable or correlated estimator.

## Experiments and statistical comparison

`ExperimentRegistry` binds a capsule to source, semantics, seed namespace and at least two parameterized variants. Each variant declares executable feature requirements. Registration rejects incomplete identities and duplicates.

`run_comparison` negotiates every variant before execution, creates disjoint variant/replicate random namespaces, uses the common shard allocator and requires each callback result to carry finite data, exact sample count and artifact identity. It reports baseline/candidate replicate arrays, means, mean difference, standard error and a two-sided confidence interval.

The current scalar comparison uses independent between-replicate sample variance and a normal-quantile interval. It intentionally does not infer image-space covariance, paired common-random-number intervals, Markov-chain effective sample size or multiple-comparison correction. Tracks that need those statistics must store them in artifacts and extend the comparison method explicitly.

## Capability negotiation

`FeatureCapability` separates vocabulary from execution:

- a feature identity alone is never executable;
- `implemented` must be true;
- an execution-contract identity must be present and match when required;
- minimum maturity must be satisfied;
- declared dependencies must be available;
- Research and Experimental implementations require explicit opt-in;
- only a Production capability may be default-enabled.

Negotiation returns an individual status for every requirement, including not implemented, maturity insufficient, contract mismatch, missing dependency and opt-in required. This prevents a schema enum or configuration field from being mistaken for a runnable algorithm.

## Reference backend hooks

`ReferenceBackendRegistry` accepts deterministic `HostOracle` and `SmallGpuOracle` providers only. Descriptors bind provider/executable identities, observable coverage, precision and hard input/element budgets. The module-level element ceiling is 1,048,576.

Requests carry the HO.1 observable and semantic context. Results must match provider/executable identities, have the exact declared shape, contain finite values and include evidence identity. These hooks support analytic or small numerical oracles; they do not introduce a CPU production integrator.

## Promotion evidence

`evaluate_promotion` reports missing and failed evidence by typed category. Research-to-Experimental requires a reproducible capsule, deterministic inputs/seeds, baseline, metric, artifact digest, result, failure domain, independent replicates, uncertainty, applicability, bias classification and explicit opt-in. Experimental-to-Production additionally requires lifecycle, resource budgets, fail-loud boundaries, API contract, backend coverage, regression gates and documentation.

Only adjacent promotions are accepted. An evidence enum without a non-empty identity and passing result is invalid.

## Current limitations

- The module is an execution/evidence substrate, not a distributed worker service or database.
- Artifact file I/O and atomic publication remain caller responsibilities; the core exposes byte ranges so local and remote storage can share semantics.
- RLE is the only compressed codec in v1. Future codecs require deterministic identity, decompression budgets and independent validation.
- Comparison is scalar and independent-replicate based. HR.0 owns typed feature-film statistics and covariance layouts.
- Capability dependencies are availability contracts, not an automatic solver or plugin loader.
- Reference callbacks are deliberately bounded and cannot become the CPU production renderer.
- Promotion reports identify evidence gaps; they do not override the authoritative PLAN or automatically enable defaults.

## Verification

```powershell
.\scripts\check_phase_ho2_research_substrate.ps1
ctest --test-dir build_modular_x64 -C Release -R "^test_research_substrate$" --output-on-failure
```

The same sources and test compile independently through `tests/sdk_free` with warnings as errors. The installed-package consumer requires and links `UltraRender::ure_research`.
