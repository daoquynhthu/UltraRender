# UltraRender Preview Product Architecture

Document status: normative architecture for the `UltraRender_preview` product-integration route.

Last reviewed: 2026-08-11

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

`Done` in an archived phase does not imply `ProductE2E`. Preview graduation requires a machine-readable product-closure ledger with no unclassified maintained surface and no unsupported `ProductE2E` claim.

For every recognized input, configuration field, native feature and requested output, the runtime must choose exactly one disposition:

- `Executed`: consumed by the selected product plan;
- `Rejected`: rejected before work with a structured reason;
- `PreservedForTooling`: retained losslessly but explicitly unavailable to rendering;
- `FrozenResearch`: outside Preview and never selected by default.

Accepted-but-ignored semantics are forbidden. Silent fallback between transports, backends, acceleration providers, estimators, reconstruction modes or output products is forbidden.

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

Worker Protocol 1.0 remains same-user local IPC. Preview additions use additive, versioned extension messages and explicit registry negotiation. They do not mutate frozen v1 field meanings or authorize network/farm transport. Remote farm execution uses internal authenticated job/shard contracts, not the local Worker Protocol.

### 3.3 Stable Core and Preview extensions

Core ABI 1.0 remains unchanged. Preview capabilities are exposed through independently queried 0.x extensions, initially `UnstableExtension` unless a separate stability review proves a smaller stable contract is justified.

Expected extension domains are:

- product job and objective;
- scene tooling and realization diagnostics;
- capability and automatic-technique reports;
- measurement frame and output products;
- reconstruction and applicability;
- checkpoint and resume;
- farm/shard control only where a supported public client use case exists.

Internal `SceneIR`, `RenderConfig`, MaterialGraph objects, Technique Graph objects, MeasurementBundle C++ layouts and backend handles never become public ABI layouts.

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

Pilot and production samples are disjoint. Executor state is retained across progressive passes. Automatic scheduling operates on declared tile, wavelength, time, device, sample and chain domains rather than repeatedly recreating complete renderers. A manual integrator override remains available for reproduction and expert diagnosis but is not required for normal product use.

## 6. Measurement, reconstruction and output

The renderer's canonical output is a typed MeasurementBundle, not an RGB framebuffer. The production producer supplies the subset required by the selected plan, including:

- raw estimate and sufficient statistics;
- sample count, variance, effective sample size and tail evidence;
- Beauty, normal, albedo, depth, UV and motion;
- technique, support, sample-range, world/snapshot and backend provenance;
- bounded Spectrum and Stokes planes where requested and supported;
- reconstruction applicability and history identity.

The Preview reconstruction baseline is training-free. It productionizes the existing statistical temporal and spatial/à-trous path with variance, tail, disocclusion, history validity and physical Spectrum/Stokes constraints. It produces raw, reconstructed, uncertainty, rejection/OOD and provenance outputs together.

The existing analytic sample-level reconstruction may be exposed as an explicitly Experimental Preview option after it consumes real complete-scene records. External learned kernels, point-set models, trained models and neural inference remain frozen.

Official artifacts include:

- multilayer OpenEXR for image, AOV, reconstruction and uncertainty products;
- a versioned MeasurementBundle/checkpoint container;
- a machine-readable output manifest with content, plan and provenance identities;
- lightweight display formats as derived products, never as physical authority.

Publication is atomic. Failed or canceled publication does not replace an existing artifact.

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

The existing distributed formats and `.urecache` become runtime consumers rather than test-only contracts. A cache hit must measurably avoid the corresponding compilation/upload work and must never replace authoring authority. Identity mismatch causes rebuild or explicit rejection according to job policy.

Checkpoints preserve sufficient statistics, automatic schedule state, estimator-specific state where supported, resource and snapshot identities, reconstruction history and completed work domains. Resume after process restart must neither repeat nor omit accepted work. A non-checkpointable estimator is either scheduled only within an atomic checkpoint epoch or rejected for a job that requires durable resume.

## 9. Bounded simulation

Preview does not implement the high-order unified physical world. It may expose only existing deterministic solver subsets that can complete a real workflow. At minimum, supported rigid transform evolution must realize a versioned time sequence, update the product session, invalidate histories correctly and render retained frames. Any fluid, acoustic or coupling declaration not actually executed is rejected or preserved for tooling according to its feature requirement.

This bounded bridge cannot establish new WorldState, coupling or differentiable public promises.

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

The Preview deliverable is a versioned product bundle containing runtime, Worker, CLI, client SDK, schemas, manifests, licenses, fixtures and validation evidence. `UltraRender_preview` is a product milestone label, not Core ABI 2.0 or UltraRender 1.0. Preview extensions remain 0.x until separately reviewed.

Preview may be declared only when the product-closure ledger contains no accepted-but-ignored semantics, duplicate execution authority, unresolved maintained-client bypass, or falsely classified `ProductE2E` entry.
