# UltraRender Core 1.0 Integration

Document status: integration guide for the declared Windows x64 Core ABI 1.0 and local Worker Protocol 1.0. This is not an UltraRender 1.0 product release; package publication and public distribution are separate release actions.

## Boundary and packages

Core 1.0 defines a small interaction grammar: dynamic discovery, instance/error/operation/event lifetimes, immutable frame leases, bounded native-scene replacement, and generic render sessions. It does not stabilize renderer algorithms, SceneIR, RenderConfig, MaterialGraph, MeasurementBundle, WorldState, GPU scheduling, solver/provider APIs, model formats, or research behavior.

The locally staged SDK and runtime packages are independent:

- the SDK package contains C11 loader/value headers, Worker Protocol 1 schemas, pre-generated C++ protocol headers, the exact-build Preview client library and examples, the canonical registry, mock conformance worker, FlatBuffers headers, fixtures, goldens, reports, and this guide;
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

ProductJob 0.3 is also an `UnstableExtension`. The maintained `ure_client` library negotiates it over explicit Direct or Worker transport and is the current path used by CLI render. It exposes a bounded native-scene/color job, product build/snapshot/objective/plan identities, requested/accepted/completed work, monotonic progress, latest immutable frame generations and an artifact manifest. Unsupported objective semantics reject; Worker failure or registry mismatch never changes transport implicitly. This Preview surface is exact-build product integration, not an additional stable Core promise.

Spectral, Stokes/polarimetric, MeasurementBundle, reconstruction, integrator, material, physical-world, differentiation, telemetry, and solver/provider capabilities follow the same rule: they are absent from Core unless separately identified and versioned.

## Local worker

`ultrarender_worker_1.exe` is the preferred isolation boundary. It loads the product runtime only through the two bootstrap exports and exposes the same scene/session/frame authority over a same-user Windows Named Pipe and read-only shared-memory leases.

Negotiate Worker Protocol, Core ABI, and Frame Schema major 1 plus the exact registry digest. Validate every response sequence, worker identity, message size, FlatBuffer, mapping range, access mode, byte digest, and lease generation. Release each lease explicitly, close the duplicated mapping handle, and treat process loss as `WorkerLost`. A restarted worker has a new identity and requires a new handshake and complete client-side state reconstruction.

The worker opens no TCP/UDP listener and requests no firewall exception. It uses a kill-on-close Job Object and rejects remote pipe clients. Worker Protocol 1 does not authorize remote/farm transport or arbitrary code execution.

## Exact-build C++ Preview client

The staged SDK exports `UltraRender::Client` for renderer-free C++23 consumers:

```cmake
find_package(UltraRender 0.3.0 EXACT CONFIG REQUIRED COMPONENTS Client)
target_link_libraries(my_client PRIVATE UltraRender::Client)
```

`ure_client` is the same maintained Direct/Worker implementation used by the CLI; the examples do not duplicate scene, renderer, transport or artifact semantics. The target requires the matching SDK/runtime registry identity and has no compatibility promise across builds. `CoreHeaders` remains the independently stable C11 boundary.

Ordinary C++ integration does not run `flatc`. The SDK carries generated Worker/Frame/Scene/Product headers under `include/ultrarender/protocol`, the required FlatBuffers runtime headers, and `protocol_codegen_manifest.json` with the generator version, canonical command, registry digest, header hashes and generator identity. The `.fbs` files remain in `share/ultrarender/schemas` for inspection and independently managed language generation. No `ure_sceneio`, SceneIR or MaterialGraph C++ header/library is part of this SDK boundary.

## Frames and images

Frames are immutable retained snapshots. Query plane metadata, then either map for read or copy into caller storage. The Core color plane is float32 RGBA with explicit dimensions, strides, extent, normalization, identities, completion, sample range, and provenance. Limits are negotiated per instance/runtime and are not fixed ABI constants.

The packaged external E2E builds independent C11, C++23 extension, raw Worker Protocol, and maintained Preview client examples without invoking `flatc`. They render real scenes and write eight PFM images covering direct map/copy, transaction replay/replacement, worker first-run/restart, and `UltraRender::Client` Direct/Worker paths. Each image gate rejects non-finite, all-zero, or spatially constant RGB data; paired paths also compare exact evidence or content identity.

The maintained PRV.1R product matrix adds a shared renderer-free scenario runner. It exercises `ure_client` Direct/Worker and CLI Direct/Worker at 854×480 and retains milestone 1280×720 Direct plus 1920×1080 Worker evidence at 128 spp. The runtime float PFM is authoritative; a deterministic `ure.preview.view.linear-srgb-reinhard-srgb8/1.0` conversion produces review PNGs. Finite values, energy, spatial structure and nested-sample convergence are checked independently of byte parity. The machine records are `phase_prv1r_functional_validation_v1.json`, `phase_prv1r_quality_validation_v1.json` and `phase_prv1r_visual_review_v1.json` under `docs/reports`; they certify only the bounded exact-build color workflow and create no stable extension or product-release promise.

## Errors, cancellation, and cleanup

Errors are retained handles with stable result/domain/detail and optional versioned structured detail. Retain before extending a lifetime; release every retained handle. A successful cancellation request means the request was recorded, not that terminal state is already `Canceled`. A race may finish successfully. `wait` timeout never cancels work.

Release map leases and frames before exhausting instance budgets. Close sessions/scenes/operations before the instance. After a worker crash, all worker-owned handles and leases are invalid regardless of their previous state.

Exact promises, support window, platform scope, scene-schema ranges, and non-promises are in [Public API Support Policy](Public_API_Support_Policy.md). [Public API/ABI Architecture](Public_API_ABI_Architecture.md) remains authoritative for Core/Worker compatibility. The active product execution architecture and queue are [UltraRender Preview Product Architecture](UltraRender_Preview_Architecture.md) and the root [PLAN](../PLAN.md); the PB plan is a read-only closure record.

UltraRender project code is licensed under Apache License 2.0. Both staged packages include the project license; bundled third-party components retain their own license files.
