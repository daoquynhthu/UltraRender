# UltraRender Core 1.0 Integration

Document status: integration guide for the declared Windows x64 Core ABI 1.0 and local Worker Protocol 1.0. This is not an UltraRender 1.0 product release; package publication and public distribution are separate release actions.

## Boundary and packages

Core 1.0 defines a small interaction grammar: dynamic discovery, instance/error/operation/event lifetimes, immutable frame leases, bounded native-scene replacement, and generic render sessions. It does not stabilize renderer algorithms, SceneIR, RenderConfig, MaterialGraph, MeasurementBundle, WorldState, GPU scheduling, solver/provider APIs, model formats, or research behavior.

The locally staged SDK and runtime packages are independent:

- the SDK package contains C11 loader/value headers, Worker Protocol 1 schemas, the canonical registry, mock conformance worker, FlatBuffers headers, fixtures, goldens, reports, and this guide;
- the runtime package contains `ultrarender_runtime_1.dll`, `ultrarender_worker_1.exe`, ABI/runtime/registry manifests, protocol inspection schemas, reports, licenses, and the support policy.

The runtime package has no import library or compiler headers. The SDK has no product runtime. Neither package exposes renderer-private C++/CUDA/Vulkan/D3D12/OpenUSD types or enables ambient plugin, script, solver, model, or executable discovery.

## In-process C ABI

Load `ultrarender_runtime_1.dll` by an explicit path and resolve exactly `ureGetRuntimeManifest` and `ureQueryInterface`. Do not link an import library or depend on any other export.

Request runtime and interface major 1. Interface tables are immutable for the loaded module lifetime. Check both returned `table_size` and `table->header.struct_size` before reading a function pointer. A client that only needs an existing prefix uses the end of the last function it calls, not `sizeof(table)`:

```c
#define URE_PREFIX_SIZE(type, field) \
    (offsetof(type, field) + sizeof(((type *)0)->field))
```

Caller-owned input/output structures set `header.type`, `header.size`, a null `header.next` unless a documented chain is used, and zero reserved fields. Runtime 1 accepts the frozen 1.0 prefix of a structure and ignores a future caller's unknown tail. It writes only fields present in its implemented prefix. Clients must likewise read only the intersection of their compiled size and the size/version returned by the runtime.

The direct lifecycle is:

```text
manifest -> tables -> instance -> scene validate/create/replace
         -> session -> operation/events -> immutable frame map or copy
         -> release children -> close/release instance -> unload DLL
```

Full native-scene replacement is the permanent Core fallback. `ure_scene_interface_t` deliberately contains no incremental-edit function.

## Extensions

The initial StableExtension list is empty.

The UUID transaction facility is an `UnstableExtension`. Query `URE_INTERFACE_SCENE_TRANSACTION`; if it is unavailable, use full-scene replacement. Its table, request/result structures, payload schema, edit IDs, strategy diagnostics, operation ID, and event ID may change between builds and require an exact registry digest. A client must never infer its availability from Core ABI 1.0 alone.

Spectral, Stokes/polarimetric, MeasurementBundle, reconstruction, integrator, material, physical-world, differentiation, telemetry, and solver/provider capabilities follow the same rule: they are absent from Core unless separately identified and versioned.

## Local worker

`ultrarender_worker_1.exe` is the preferred isolation boundary. It loads the product runtime only through the two bootstrap exports and exposes the same scene/session/frame authority over a same-user Windows Named Pipe and read-only shared-memory leases.

Negotiate Worker Protocol, Core ABI, and Frame Schema major 1 plus the exact registry digest. Validate every response sequence, worker identity, message size, FlatBuffer, mapping range, access mode, byte digest, and lease generation. Release each lease explicitly, close the duplicated mapping handle, and treat process loss as `WorkerLost`. A restarted worker has a new identity and requires a new handshake and complete client-side state reconstruction.

The worker opens no TCP/UDP listener and requests no firewall exception. It uses a kill-on-close Job Object and rejects remote pipe clients. Worker Protocol 1 does not authorize remote/farm transport or arbitrary code execution.

## Frames and images

Frames are immutable retained snapshots. Query plane metadata, then either map for read or copy into caller storage. The Core color plane is float32 RGBA with explicit dimensions, strides, extent, normalization, identities, completion, sample range, and provenance. Limits are negotiated per instance/runtime and are not fixed ABI constants.

The packaged external E2E builds independent C11, C++23 extension, and worker clients. They render real scenes and write six PFM images covering direct map/copy, transaction replay/replacement, and worker first-run/restart paths. Each image gate rejects non-finite, all-zero, or spatially constant RGB data; paired paths also compare exact evidence or content identity.

## Errors, cancellation, and cleanup

Errors are retained handles with stable result/domain/detail and optional versioned structured detail. Retain before extending a lifetime; release every retained handle. A successful cancellation request means the request was recorded, not that terminal state is already `Canceled`. A race may finish successfully. `wait` timeout never cancels work.

Release map leases and frames before exhausting instance budgets. Close sessions/scenes/operations before the instance. After a worker crash, all worker-owned handles and leases are invalid regardless of their previous state.

Exact promises, support window, platform scope, scene-schema ranges, and non-promises are in [Public API Support Policy](Public_API_Support_Policy.md). The architecture and execution authority remain [Public API/ABI Architecture](Public_API_ABI_Architecture.md) and [PB Public Boundary Plan](PB_Public_Boundary_PLAN.md).

UltraRender project code is licensed under Apache License 2.0. Both staged packages include the project license; bundled third-party components retain their own license files.
