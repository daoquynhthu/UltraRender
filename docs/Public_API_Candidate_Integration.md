# UltraRender Candidate Public Boundary Integration

Document status: Candidate 0.1 integration guide for PB.7. This is not a stable ABI, protocol, SDK, or support promise.

## Package boundary

PB.7 produces two content-digested packages:

- the SDK package contains the C11 loader/value headers, FlatBuffers schemas, canonical registry, FlatBuffers headers and bounded scene fixtures;
- the runtime package contains `ultrarender_runtime_candidate.dll`, `ure_worker.exe`, the ABI manifest, runtime manifest and canonical registry.

The packages are intentionally independent. A client compiles without the runtime package. The runtime package contains no compiler headers, import library, private conformance DLL, test producer, renderer SDK header, CUDA header, plugin, script, solver, or model discovery payload.

## Loading and negotiation

Clients load the runtime DLL by explicit path and resolve only `ureGetRuntimeManifest` and `ureQueryInterface`. They first request Candidate Core 0.1 without assuming a registry match, inspect the returned manifest, and then decide whether to bind the exact registry digest. Interface tables are immutable process-lifetime objects. A client reads no field beyond both its compiled structure size and the returned `table_size`/table-header `struct_size` bounds.

Required capabilities fail instance or worker negotiation. Unknown optional capabilities and optional FlatBuffers fields are ignored only where the enclosing schema permits them. Internal renderer types, SceneIR layouts, RenderConfig, MaterialGraph, estimators, solvers, devices and provider models never cross this boundary.

## Current Candidate support matrix

| Client or protocol input | Candidate PB.7 runtime/worker | Policy |
|---|---:|---|
| Compiled PB.2 bootstrap client | Bootstrap prefix passes | Retained migration evidence |
| Compiled PB.3 lifecycle client | Known interface prefixes pass | Retained migration evidence |
| Compiled PB.4 frame client | Known interface prefixes pass | Retained migration evidence |
| Compiled PB.5 scene/session client | Known interface prefixes pass | Retained migration evidence |
| Compiled PB.6 transaction client | Known interface prefixes pass | Retained migration evidence |
| Current PB.7 client | Passes against current PB.7 runtime | Only retained supported runtime combination |
| Worker protocol 0.1 + exact registry | Negotiates protocol/core/frame 0.1 | Current supported worker combination |
| Older, newer, or registry-mismatched worker input | Explicit incompatibility | No compatibility promise |

Candidate runtime binaries before PB.7 are not retained as supported runtimes. They may have been published as development evidence, but Candidate 0.x explicitly permits replacement. This keeps the reverse matrix narrow and truthful: the current client is tested against every retained supported runtime, which is the current content-digested PB.7 runtime only. No row implies Core ABI 1.0 or Worker Protocol 1.0.

## Direct and worker workflows

The direct workflow is manifest → table negotiation → instance → native scene → objective session → operation/events → immutable frame map/copy. Scene changes use a UUID/base-revision transaction. A revision conflict returns a retry revision and never merges implicitly. An unsupported incremental edit either supplies an explicit full-scene fallback or fails without changing the accepted scene.

The worker implements the same semantics through a same-user Named Pipe and read-only shared-memory leases. The client validates the worker identity and registry on every response, validates the shared-memory descriptor and digest, releases the lease explicitly, and treats process loss as `WorkerLost`. Restart creates a new worker identity and requires full handshake plus scene/session reconstruction.

## Failure and security boundary

Control messages, scene blobs, transaction payloads, frame bytes, retained handles and nesting all have declared budgets. Corrupt, truncated, oversized, wrong-registry and missing-required-capability inputs fail explicitly. The product worker has no network import or endpoint, is assigned to a kill-on-close Job Object, performs no ambient plugin/script/solver/model discovery, and is absent after clean shutdown or client-owned job termination.

The authoritative implementation and evidence remain [Public API/ABI Architecture](Public_API_ABI_Architecture.md), [PB plan](PB_Public_Boundary_PLAN.md), the generated registry, and `ure.phase_pb.validation.v1`. PB.8 requires separate approval before any stable promise can be created.
