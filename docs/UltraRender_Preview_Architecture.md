# UltraRender Preview Product Architecture

Document status: normative architecture for the `UltraRender_preview` product-integration route.

Last reviewed: 2026-08-13

The root [`PLAN.md`](../PLAN.md) owns execution order. This document owns the architectural boundaries that the Preview route must preserve. The completed Core ABI 1.0 and Worker Protocol 1.0 specifications remain independently normative for their declared Windows x64 contracts. Historical construction and high-order plans are read-only references and cannot reopen frozen research work.

## 1. Product objective

`UltraRender_preview` is the first repository milestone defined by complete user workflows rather than by the existence of component contracts or isolated tests. It is not UltraRender 1.0, does not expand the Core ABI 1.0 promise, and does not imply that every backend executes every renderer feature.

The Preview route must connect the non-research capability islands already built by the legacy construction plan, then productionize the existing training-free reconstruction boundary. A capability is product-closed only when a supported client can submit it through the canonical service, obtain a real artifact, observe its limitations, and reproduce its failure or recovery behavior.

The product route freezes:

- learned proposal, neural control-variate and neural denoising work;
- new estimator research and new integrator families;
- the unified high-order physical-world route beyond the bounded implementations already present;
- differentiable and inverse workflows;
- general coherent/partial-coherent scene sessions beyond existing reference contracts;
- real-time enhancement and frame generation;
- the generic Phase X plugin ecosystem;
- the abandoned repository GUI.

Existing production radiometric diffraction, fluorescence, MaterialGraph, spectral/polarimetric transport, bounded simulation code and adapter work may be integrated. Their research boundaries must not be enlarged merely to satisfy Preview.

## 2. Product-closure model

Every maintained capability is classified at one highest attained level:

| Level | Meaning |
|---|---|
| `Contract` | Types, schemas, validation or a declared rejection boundary exist |
| `ComponentExecutable` | The implementation executes in an isolated component or fixture |
| `RendererIntegrated` | A complete-scene renderer consumes it without test-only bypasses |
| `ClientReachable` | The canonical runtime exposes it to at least one maintained external client path |
| `ProductE2E` | Retained external workflows produce and validate real artifacts, lifecycle, diagnostics and recovery |

`Done` in an archived phase does not imply `ProductE2E`. Preview graduation requires a machine-readable product-closure ledger with no unclassified maintained surface and no unsupported `ProductE2E` claim. PRV.1 established client reachability and a single execution spine, but its smoke-scale transport evidence has been superseded as ProductE2E proof by the PRV.1R runtime audit.

For every recognized input, configuration field, native feature and requested output, the runtime must choose exactly one disposition:

- `Executed`: consumed by the selected product plan;
- `Rejected`: rejected before work with a structured reason;
- `PreservedForTooling`: retained losslessly but explicitly unavailable to rendering;
- `FrozenResearch`: outside Preview and never selected by default.

Accepted-but-ignored semantics are forbidden. Silent fallback between transports, backends, acceleration providers, estimators, reconstruction modes or output products is forbidden.

### 2.1 Product evidence authority

ProductE2E begins outside the renderer implementation: a maintained client uses `ure_client` or a generated SDK, selects Direct or Worker explicitly, reaches the runtime Product extension and `ure_product`, and obtains a real frame or artifact. CLI is a human adapter over that route. Direct `ure_core`/`RenderSession` calls, mocks, test-only renderers and temporary diagnostic harnesses remain useful isolation evidence but cannot establish ProductE2E.

The retained validation model has four separate tiers:

| Tier | Typical scale | Purpose |
|---|---|---|
| Contract smoke | 32-128 px, 1-4 spp | Fast lifecycle, ABI/protocol, error and transport regression |
| Product functional | 854×480, 16-64 spp | Routine real-path scene, resource, control and nontrivial-artifact coverage |
| Product quality | 1280×720 / 1920×1080, 128-512+ spp | Convergence, material, spectral, AOV and reconstruction evidence |
| Stress/scale | 2560×1440 / 3840×2160, 500+ spp | Long-run throughput, VRAM, cancellation and stability evidence |

The exact sample count remains scenario-specific. Smoke cannot substitute for quality; stress is scheduled or manual hardware evidence rather than a routine per-commit requirement. Positive product evidence uses the production profile. Capability-reduced runs are labeled diagnostic controls, never graduation evidence.

### 2.2 Diagnostic maturity model

Diagnostics are a product data plane, not log decoration or a one-time runtime repair. PRV.1R establishes the common envelope and cross-process behavior; every later Preview phase owns its domain-specific detail schemas, negative workflows, recovery guidance and client presentation. Preview release aggregates already-qualified diagnostics instead of inventing them at the end.

Every applicable failure carries a stable result/domain plus versioned detail, correlation, operation/session/job and product identities, cause, retryability and recovery hint. This enriches the existing Core Error and Operation model rather than creating a parallel error authority. Backend/vendor information is nested typed detail and never replaces the stable classification. Core results `Timeout`, `Incomplete`, `BudgetExhausted`, `Canceled`, `DeviceLost`, `CapabilityUnavailable` and `Internal` remain distinct; `NotApplicable` remains a product state/detail.

Ordinary post-bootstrap failures expose a retained Error or a queryable operation terminal error. If allocating the Error object itself fails, the original stable result remains authoritative and a bounded allocation-free emergency record preserves minimum domain/detail/correlation. This exceptional resource boundary does not permit routine null-error degradation.

Diagnostic messages and payloads are bounded, redacted and versioned. They must not leak private absolute paths, addresses, credentials or raw untrusted vendor text. Cause depth, event rate, structured payload and retention have explicit budgets. The generated SDK distributes a machine-readable diagnostic catalog and language adapters preserve classification and correlation.

## 3. One product execution authority

The Preview architecture has one semantic execution service:

```text
Human client / SDK client / Hydra adapter / Python adapter
                           |
                           v
                    ure_client API
                    /             \
             direct transport    worker transport
                    |                  |
                    |             ure_worker
                    |                  |
                    +--------+---------+
                             v
                 ultrarender_runtime_1
                             |
                         ure_product
             +---------------+----------------+
             |               |                |
        scene realizer  transport plan  output/reconstruction
             |               |                |
             +---------------+----------------+
                             v
                    backend executors
```

PRV.1 implemented this spine for the bounded native-scene color workflow. `ure_client` supplies explicit Direct and Worker transports, both negotiate ProductJob 0.1, and CLI render uses only this client and defaults to Worker. The retained 64×64 frame-byte and manifest parity proves routing and transport structure, not trustworthy ProductE2E: PRV.1R must repair actual work accounting, persistent execution, long-operation control, resource roots and tiered image validation before complete archive realization begins. Complete archive realization, scene tooling through the runtime, typed measurements, reconstruction and official artifact publication remain subsequent PRV phases.

`ure_product` is an internal C++ product-orchestration module. It owns product jobs, scene realization, immutable snapshots, execution planning, measurement production, reconstruction, checkpointing and artifact publication. It may depend on renderer modules; renderer modules must not depend on clients, CLI or Worker transport.

`ure_client` is the maintained client library used by CLI, generated language adapters and external integration fixtures. It presents one operation model over two explicit transports:

- `Direct`: load an explicitly named runtime through `ureGetRuntimeManifest` and `ureQueryInterface`;
- `Worker`: negotiate Worker Protocol and submit the same versioned product requests to a runtime hosted by `ure_worker`.

The client must not silently change transports. Direct and Worker requests, capability results, errors, progress, measurements and output manifests must have semantic parity.

### 3.1 CLI

`ure_cli` is a human-facing adapter. It parses arguments and configuration, constructs public product requests, selects an explicit transport, displays events and returns process exit status. Rendering commands must not link or call `ure_core`, `ure_sceneio`, `RenderEngineFactory` or `RenderSession` directly.

The default render transport is Worker. Direct transport remains an explicit profiling, diagnostic and embedded option. A failed Worker launch is reported; it never triggers an implicit direct fallback.

Scene tooling commands also converge on the runtime's versioned scene-tool extension so that `validate`, `build`, `pack`, `migrate`, `inspect`, `realize` and `render` use the same capability registry and scene realizer. CLI filesystem argument handling and terminal presentation may remain local, but semantic validation, migration, realization and package publication have one implementation.

Official image and checkpoint publication belongs to the product runtime. CLI must not own a second tone-mapping, EXR, denoising or artifact-identity implementation.

### 3.2 Worker

`ure_worker` is an isolation host and protocol bridge, not a renderer implementation. It loads the product runtime only through the two Core bootstrap exports, queries required Preview extensions, and forwards bounded requests, events, immutable leases and artifact results.

Long-running product work must not monopolize the Worker control plane. Cancellation, event polling, heartbeat and shutdown remain serviceable within a declared work quantum; queues and concurrent sessions are bounded. A client-side wait timeout is only an observation window, not the job budget or an implicit terminal failure. Worker and Direct expose the same requested, accepted, completed, partial, canceled and failed semantics.

Product progress is asynchronous and monotonic. At bounded work quanta, the runtime may coalesce progress events and publish a new immutable frame generation. Clients can poll/wait independently and acquire the latest available frame; publishing every sample is neither required nor permitted to impose a synchronous renderer barrier. Slow consumers observe explicit gaps/backpressure without blocking execution.

Worker Protocol 1.0 remains same-user local IPC. Preview additions use additive, versioned extension messages and explicit registry negotiation. They do not mutate frozen v1 field meanings or authorize network/farm transport. Remote farm execution uses internal authenticated job/shard contracts, not the local Worker Protocol.

### 3.3 Stable Core and Preview extensions

Core ABI 1.0 remains unchanged. Preview capabilities are exposed through independently queried 0.x extensions, initially `UnstableExtension` unless a separate stability review proves a smaller stable contract is justified.

The first Preview extension is ProductJob 0.1. It reuses Core handles, operations and immutable Frame leases while exposing product build/snapshot/objective/plan identities, accepted-work progress and an artifact manifest. It does not add fields or functions to any Core 1.0 prefix. PRV.1 implemented its bounded native-scene/color request path through Direct and Worker transports, but PRV.1R must correct the mapping from public sample requests to actual work before that progress can be called exact. Until later Preview phases define richer execution, unsupported determinism, usage, output, latency and precision-losing budget requests reject explicitly.

The registry digest identifies one exact generated registry snapshot, including unstable extensions. Adding or changing an `UnstableExtension` therefore changes the digest without changing Core major 1. Clients that pin the digest intentionally request that exact snapshot and may reject a newer runtime; clients seeking Core-prefix compatibility negotiate the Core/interface version and leave the exact registry digest unconstrained.

Expected extension domains are:

- product job and objective;
- scene tooling and realization diagnostics;
- capability and automatic-technique reports;
- measurement frame and output products;
- reconstruction and applicability;
- checkpoint and resume;
- farm/shard control only where a supported public client use case exists.

Internal `SceneIR`, `RenderConfig`, MaterialGraph objects, Technique Graph objects, MeasurementBundle C++ layouts and backend handles never become public ABI layouts.

The external SDK may provide generated serialization builders, pre-generated Worker/Frame/Scene/Product protocol headers, the renderer-free `ure_client` reference library/source and CMake targets. It records the exact FlatBuffers generator version and command but does not require ordinary consumers to run `flatc`. It does not publish `ure_sceneio`, SceneIR or MaterialGraph C++ ABI as a supported integration surface; scene authoring converges on versioned schemas/builders and the scene-tool extension.

## 4. Canonical product data flow

### 4.1 Product job

A product job binds:

- exact scene source and content identity;
- requested output semantics;
- quality, time, latency, memory and sample constraints;
- determinism and intended-usage policy;
- allowed platform/backend/device constraints;
- reconstruction policy;
- checkpoint and artifact-publication policy;
- client, registry, runtime and build identities.

The runtime either lowers every requested field into the product plan or rejects it. Identity hashing is not execution.

Requested, accepted and completed work are distinct domains. Pilot and production samples, technique allocations, tiles, wavelength packets, time points, devices and Markov chains keep explicit non-overlapping identities. Progress is advanced by completed work, not by API loop count. Partial success is legal only when the objective requested it and the artifact records the exact missing domain.

The product objective is the execution authority for requested production work. Native `scene.spp` and simulation `spp_per_frame` are authoring/default inputs whose precedence, override and reset behavior is compiled explicitly into the plan; they never multiply the objective budget implicitly. Progressive accumulation, pilot work and per-frame work keep separate domains and conflicting requests reject before execution.

### 4.2 Scene realization

The scene realizer consumes a complete `NativeSceneArchive`, not only its embedded `SceneIR`. It validates and realizes:

- scene graph and canonical camera;
- procedural graph;
- resource catalog and content-addressed payloads;
- MaterialGraph, MaterialX-derived graphs and preset provenance;
- solver contract;
- the bounded supported simulation subset;
- package dependencies and compiled-cache identity;
- required/optional feature declarations.

Its result is an immutable `ProductSnapshot` containing a realized scene, frozen `ResourceSet`, material programs, execution constraints, bounded time state and identities. Required unsupported semantics reject before renderer allocation. Tooling-only optional data is retained in diagnostics and provenance, never silently discarded.

`.urepkg` Preview packages are self-contained for every required local resource. Ambient search paths, current directories and author-machine absolute paths cannot be necessary for execution. Explicit external provider references require a versioned capability and are not treated as packaged resources.

### 4.3 Product execution plan

The plan combines:

- realized snapshot;
- automatic Technique Graph and support/measure composition;
- qualified estimators and persistent executor state;
- backend/acceleration capability selection;
- device and farm work domains;
- measurement schema;
- reconstruction and output graph;
- budgets, cancellation and checkpoint boundaries.

The plan identity is included in every shard, measurement, checkpoint, frame and artifact. A runtime change that alters an executable plan changes its identity.

## 5. Transport and automatic integration

The current simplified automatic renderer is a migration source, not the Preview authority. Preview automatic execution consumes the existing `ure_transport` Technique Graph, support/measure, pilot qualification, portfolio scheduling and automatic-plan contracts.

Every maintained estimator registers:

- exact support and measure;
- evidence maturity and bias class;
- required scene/material/backend features;
- lifecycle and persistent-state requirements;
- measurement planes produced;
- memory, scratch and checkpoint capability;
- known incompatibilities.

Wavefront remains the defensive radiometric baseline. Path guiding, ReSTIR DI/PT, specular manifold, BDPT, VCM and PSSMLT participate only where applicable and where their finite-sample normalization/evidence permits the requested output layer. An excluded technique reports a structured reason.

Pilot and production samples are disjoint. Scene realization and executor state are retained across progressive passes. Automatic scheduling operates on declared tile, wavelength, time, device, sample and chain domains rather than repeatedly recreating complete renderers or rerendering accumulated targets from zero. A manual integrator override remains available for reproduction and expert diagnosis but is not required for normal product use.

## 6. Measurement, reconstruction and output

The renderer's canonical output is a typed MeasurementBundle, not an RGB framebuffer. The production producer supplies the subset required by the selected plan, including:

- raw estimate and sufficient statistics;
- sample count, variance, effective sample size and tail evidence;
- Beauty, normal, albedo, depth, UV and motion;
- technique, support, sample-range, world/snapshot and backend provenance;
- bounded Spectrum and Stokes planes where requested and supported;
- reconstruction applicability and history identity.

Worker transport carries every plane published by the runtime Frame, preserving per-plane schema, extent, stride, identity, lease and digest. Single-plane snapshots or client assumptions are migration debt, not an architectural restriction.

The Preview reconstruction baseline is training-free. It productionizes the existing statistical temporal and spatial/à-trous path with variance, tail, disocclusion, history validity and physical Spectrum/Stokes constraints. It produces raw, reconstructed, uncertainty, rejection/OOD and provenance outputs together.

The existing analytic sample-level reconstruction may be exposed as an explicitly Experimental Preview option after it consumes real complete-scene records. External learned kernels, point-set models, trained models and neural inference remain frozen.

Official artifacts include:

- multilayer OpenEXR for image, AOV, reconstruction and uncertainty products;
- a versioned MeasurementBundle/checkpoint container;
- a machine-readable output manifest with content, plan and provenance identities;
- lightweight display formats as derived products, never as physical authority.

Publication is atomic. Failed or canceled publication does not replace an existing artifact.

The raw measurement/artifact remains the validation authority. Derived PNG inspection uses one deterministic conversion with recorded orientation, color space, exposure and tone-map identity. Automated validation covers shape, finite values, nontriviality, energy/statistical bounds and convergence or reference metrics; quality and release evidence additionally retain a visual review record. Artifact existence, readable headers, hashes and Direct/Worker byte equality are transport/integrity checks, not image-quality proof.

## 7. Backend and acceleration profiles

Product closure does not require the Cartesian product of every feature and backend. It requires honest profiles and real complete-scene execution.

### 7.1 CUDA Reference Profile

CUDA remains the complete-scene physical reference and must carry the currently production-qualified radiometric feature set. It is the fallback only when the objective permits CUDA and the selected device is applicable; it is never an implicit replacement for an explicitly requested backend.

### 7.2 Portable Common Profile

CUDA, Vulkan and D3D12 must each execute a bounded common complete-scene profile through the same ProductSnapshot and MeasurementBundle semantics:

- native/glTF realized static scenes;
- the declared common MaterialGraph subset and texture resources;
- runtime spectral packet transport within the advertised backend limits;
- wavefront transport;
- Beauty and required feature planes;
- training-free reconstruction inputs and output publication;
- lifecycle, budgets, cancellation and device-loss behavior.

Backend capability differences remain visible to automatic qualification. Common-profile parity uses physical and statistical thresholds, not an unsupported promise of bitwise equality.

### 7.3 Acceleration and geometry

CUDA self-compute, OptiX, Vulkan RT and DXR become selectable complete-scene acceleration providers on their owning backend. Provider selection must preserve hit, material, spectral, visibility and AOV semantics. Clustered geometry joins product traversal only after residency, physical LoD, dynamic lifecycle and exact-path rejection are connected to the complete renderer.

## 8. Scale, cache and recovery

Single-device, multi-GPU and farm execution consume the same plan. The scheduler partitions sample, wavelength, tile, time and MLT chain identities without overlap. Shards carry snapshot, resource, executable, backend, compiler, schedule and measurement identities.

Before GPU allocation, the product plan estimates framebuffer, spectral/AOV planes, queues, acceleration, persistent executor state, scratch, reconstruction and output peaks against the applicable device/driver memory budget. An inapplicable plan rejects with structured budget/applicability diagnostics or selects only an objective-permitted semantically equivalent plan. It must not silently reduce lanes, techniques, output planes, reconstruction or precision, and must not treat uncontrolled residency paging as supported execution.

Performance evidence binds hardware, driver, available VRAM, build, production profile, resolution and work domain. Same-hardware regressions use relative comparisons; cross-hardware acceptance uses declared classes and applicability. Correctness, implementation complexity, performance regression and local hardware limitation are recorded as separate findings.

The existing distributed formats and `.urecache` become runtime consumers rather than test-only contracts. A cache hit must measurably avoid the corresponding compilation/upload work and must never replace authoring authority. Identity mismatch causes rebuild or explicit rejection according to job policy.

Checkpoints preserve sufficient statistics, automatic schedule state, estimator-specific state where supported, resource and snapshot identities, reconstruction history and completed work domains. Resume after process restart must neither repeat nor omit accepted work. A non-checkpointable estimator is either scheduled only within an atomic checkpoint epoch or rejected for a job that requires durable resume.

## 9. Bounded simulation

Preview does not implement the high-order unified physical world. It may expose only existing deterministic solver subsets that can complete a real workflow. At minimum, supported rigid transform evolution must realize a versioned time sequence, update the product session, invalidate histories correctly and render retained frames. Any fluid, acoustic or coupling declaration not actually executed is rejected or preserved for tooling according to its feature requirement.

This bounded bridge cannot establish new WorldState, coupling or differentiable public promises.

Ordinary multi-frame rendering is a separate ProductSession concern. Frame/time/camera revision identities determine whether estimator, measurement and reconstruction state is reused, accumulated or reset. Repeated frames, frame jumps, camera changes and scene transactions publish explicit invalidation/reset reasons and are validated independently of physics simulation.

## 10. Validation and release identity

Preview validation is workflow-first. Component tests remain necessary but cannot graduate a capability by themselves. Retained external scenarios cover:

- native, package, glTF and MaterialX-derived scene execution;
- automatic selection across diffuse, difficult direct-light, caustic, volume, spectral and wave-material scenes;
- raw/reconstructed/uncertainty/rejection outputs;
- progressive camera, transform, material and resource changes;
- direct and Worker semantic parity;
- CUDA Reference and Vulkan/D3D12 Common Profile rendering;
- selectable native acceleration providers and clustered-geometry boundaries;
- multi-device/farm merge and cross-process checkpoint resume;
- bounded simulation-to-render execution;
- CLI, generated C/C++ client, Python adapter and optional Hydra adapter convergence.

Coverage uses a risk-based pairwise matrix rather than an impractical full Cartesian product, but every maintained calling mode performs at least one real render and artifact acquisition, and representative scenarios are reused across transports and clients. Mocks never replace required real runtime/Worker evidence. Each cell binds artifact and plan identities, runtime/build, hardware/driver/VRAM, production profile, work domain, metric, repeat count and failure/applicability policy.

The Preview deliverable is a versioned product bundle containing runtime, Worker, CLI, client SDK, schemas, manifests, licenses, fixtures and validation evidence. `UltraRender_preview` is a product milestone label, not Core ABI 2.0 or UltraRender 1.0. Preview extensions remain 0.x until separately reviewed.

Preview may be declared only when the product-closure ledger contains no accepted-but-ignored semantics, duplicate execution authority, unresolved maintained-client bypass, or falsely classified `ProductE2E` entry.
