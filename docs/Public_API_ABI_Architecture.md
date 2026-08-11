# UltraRender Public API/ABI and Worker Boundary Architecture

Document status: approved normative architecture; PB.8 declaration complete

Last reviewed: 2026-08-11

Compatibility status: Core ABI 1.0 and Worker Protocol 1.0 are declared for the documented Windows x64 profile. This is not an UltraRender 1.0 product release; packages are not publicly distributed and the support clock has not started.

This document defines the public interaction boundary completed before resuming `HR.3`. It replaces the external `UltraRender_Stable_Public_API_ABI_Architecture_v1.md` draft as the repository-owned design authority. The draft's central decision—one semantic contract exposed through an in-process C ABI and an isolated worker protocol—is retained, while the stability surface is narrowed and several ABI, identity, transport, lifetime, and release details are made explicit.

The executable phase order and graduation evidence are defined by [`PB_Public_Boundary_PLAN.md`](PB_Public_Boundary_PLAN.md). The root [`PLAN.md`](../PLAN.md) remains authoritative for the global cursor.

---

## 1. Purpose and normative language

The boundary has four goals:

1. let an external frontend and UltraRender develop, test, release, pin, and roll back independently;
2. provide a version-negotiated local worker path with crash and GPU-driver isolation;
3. offer a small in-process C ABI for bindings, tests, embedding, and the worker itself;
4. preserve freedom to replace internal scene, transport, reconstruction, world, scheduling, and backend implementations.

The terms **MUST**, **MUST NOT**, **SHOULD**, **SHOULD NOT**, and **MAY** are normative. A conforming implementation must satisfy every applicable MUST/MUST NOT rule and the gates assigned to its declared maturity.

This architecture governs both the retained Candidate history and the declared 1.0 contracts. The repository declaration and its scoped evidence tag do not constitute package publication or public distribution.

## 2. Baseline and reason for a new boundary

The current C++ renderer, `RenderSession`, native scene formats, C API, pyure wrapper, and backend contracts are useful implementation foundations. They are not a suitable long-lived public binary boundary.

The baseline audit at the end of HR.2 found:

| Existing surface | Current property | Public-boundary consequence |
|---|---|---|
| `ure/ure_c_api.h` | Large configuration structures and `_v2`/`_v3`/`_v4` entry points | Remains a legacy experimental compatibility API; it is not renamed as stable |
| `pyure_native.dll` | Windows auto-export exposes thousands of symbols, including C++ implementation symbols | A new DLL with an explicit two-symbol export list is required |
| Framebuffer/AOV access | Returns transient internal storage | Stable clients receive immutable frame leases with explicit map/copy lifetime |
| Scene mutation | Several operations address array indices | Stable transactions address persistent semantic object IDs |
| Failure reporting | Many paths collapse to null or small integer results | Stable result domains and retained structured errors are required |
| Worker execution | No formal worker protocol | A bounded local protocol, mock worker, golden messages, and crash semantics are required |
| Native binary scenes | FlatBuffers schemas already use explicit IDs and conformance checks | Existing native semantics remain authoritative and are transported as versioned blobs |

The stable boundary MUST be introduced as a separate adapter and product surface. Internal C++ headers, layouts, exceptions, STL containers, GPU handles, and implementation-specific configuration structures MUST NOT leak through it.

### 2.1 Complete public interaction-surface convergence

PB unifies semantics, not encodings. Every externally reachable or historically public-looking surface MUST appear in a machine-validated Public Interaction Surface Ledger before Core ABI 1.0 can be declared. The ledger covers at least:

- native `.ure`, `.urescene`, `.urepkg`, and `.urecache` formats and tooling;
- glTF/GLB, MaterialX, USD schema, Hydra RenderDelegate, and USDA export adapters;
- C++ renderer/session headers, legacy `ure_c_api.h`, `pyure_native.dll`, pyure, and CLI render entry points;
- distributed/farm files, worker/resource/cache manifests, and coherent measurement exchange;
- local full-wave solver providers, native procedural/script hooks, optional runtime/provider factories, and installed public headers;
- the abandoned repository GUI and the frozen Phase X plugin proposal, recorded only to enforce their excluded/frozen disposition.

Each ledger entry records caller, owner, semantic authority, stability, evidence maturity, runtime state, input/output roles, identity/revision/error/operation/frame semantics, translation path, bypass risk, final disposition, migration phase, and conformance evidence.

The allowed dispositions are:

- `CanonicalAuthority`: the native semantic source of truth;
- `PublicTransport`: C ABI or worker exposure of registry semantics;
- `Adapter`: an ecosystem projection into or out of the canonical authority;
- `VersionedExtension`: optional stable/experimental/research capability using PB identities;
- `InternalContract`: maintained implementation/farm contract not presented as a frontend API;
- `LegacyMigration`: retained temporarily behind an explicit migration path;
- `FrozenExcluded`: prohibited from influencing the new boundary.

No surface may remain unclassified. No two entries may claim canonical authority for the same semantic domain. An Adapter MUST identify its canonical target and loss policy; a PublicTransport MUST use registry identities; a LegacyMigration entry MUST name a terminal migration gate; and an InternalContract MUST state why it is not a supported client API.

Hydra requires special treatment because its external ABI is governed by OpenUSD. UltraRender does not replace that ABI. The maintained delegate must lower Hydra state into the same native scene/revision/transaction semantics and converge on the same execution service as PB, or remain explicitly legacy and unsupported as a stable client path. It MUST NOT preserve an independent camera, material, object-ID, error, operation, or framebuffer authority.

CLI native build/validate/pack tools may remain direct format tooling. CLI rendering, pyure, and any separately released integration must migrate to PB or prove semantic parity with the same contract adapter. Distributed/farm and solver-provider contracts remain internal or become explicitly versioned extensions; they never silently become alternative frontend APIs.

## 3. What stability means

### 3.1 The promise is deliberately small

Core stability covers the interaction grammar and lifecycle:

- runtime manifest and interface discovery;
- opaque handles and lifetime;
- capability and extension negotiation;
- result and structured error semantics;
- asynchronous operation, event, wait, and cancellation semantics;
- native scene blob, revision, and full-replacement lifecycle;
- objective-oriented session requests;
- immutable frame and plane discovery plus CPU map/copy;
- schema, semantic, registry, runtime, and build identities.

Core stability does **not** freeze the capability set. New material, integrator, reconstruction, physical-world, solver, measurement, differentiation, GPU-interoperability, and research functionality enters through independently versioned schemas and extensions.

### 3.2 Internal freedom is mandatory

The following are never public ABI layouts:

- `SceneIR`, `SceneDiff`, `RenderConfig`, `RenderSession`, `WorldState`, `MeasurementBundle`, and Technique Graph objects;
- MaterialGraph C++ nodes or an eternally fixed material-node universe;
- CUDA, Vulkan, D3D12, DXR, OptiX, OpenUSD, driver, queue, resource, or native synchronization handles;
- GPU scheduling, estimator composition, memory ownership, model format, training pipeline, or research algorithm implementation;
- STL containers, C++ classes, virtual tables, exceptions, RTTI, compiler-specific enums, or compiler allocation ownership.

A proposal to add a stable Core field or function MUST demonstrate that the requirement cannot be represented by a versioned payload schema, a capability, or a separately versioned extension. Without that evidence, the Core change is rejected.

### 3.3 Stability, evidence, and runtime state are independent

Every advertised capability carries three independent classifications:

| Axis | Values | Meaning |
|---|---|---|
| Contract stability | `Core`, `StableExtension`, `UnstableExtension` | Compatibility obligation of the interface or schema |
| Evidence maturity | `Research`, `Experimental`, `Production` | Strength and scope of evidence for the implementation |
| Runtime state | `Compiled`, `Available`, `Enabled`, `Applicable` | Whether the current build/device/session can use it |

These axes MUST NOT be collapsed into one enum or inferred from product version. A stable transport for a Research capability does not make the algorithm Production. A Production algorithm behind an unstable research callback does not create an ABI promise.

### 3.4 Candidate history and promise point

PB.0 through PB.7 produced only `Core ABI Candidate 0.x` and `Worker Protocol Candidate 0.x`. Candidate artifacts could break between candidate minors when the registry recorded the break and fixtures migrated.

PB.8 staged release-named binaries, manifests, schemas, packages, and exact compatibility evidence for verification. After the required post-REPORT approval on 2026-08-11, Core ABI 1.0 and Worker Protocol 1.0 were declared. Candidate artifacts remain historical evidence and never inherit the 1.0 promise retroactively. Declaration, tagging, package publication, public distribution, and the UltraRender product version are separate states.

### 3.5 Escape hatches for future research

The architecture preserves five non-breaking evolution paths:

1. add a schema-defined payload without changing Core layout;
2. advertise a new capability or a new extension minor/major;
3. expose an exact-build `Research` extension bound to registry, runtime, provider, and artifact identities, with no compatibility promise;
4. ship a new side-by-side runtime major (`ultrarender_runtime_2`) when a real Core break is unavoidable;
5. retain an old-major compatibility adapter for the documented support window without forcing new internals to retain old layouts.

Stable does not mean that every runtime contains every old optional extension, output is numerically identical, performance is unchanged, an old client understands new capability, a research interface persists, or every platform shares one machine ABI.

### 3.6 Mixed-version behavior after 1.0

Within the declared Windows x64 Core 1.x support range:

- a client binary compiled against Core 1.M MUST load and execute its Core calls on runtime 1.N where `N >= M`, without recompilation;
- a newer client connecting to an older runtime MUST query manifest/table sizes/capabilities and use only the negotiated common set;
- a missing required capability fails before object/session work; a missing optional capability remains explicitly unavailable;
- stable extension compatibility is governed by that extension's major and published availability policy, not inferred from Core or product version;
- source compatibility is secondary to binary compatibility and may require source changes when recompiling against stricter helper headers.

## 4. Architectural decision

One registry defines one semantic contract. Two transports expose it.

```text
                         Public Contract Registry
              IDs / schemas / results / events / capabilities
                manifests / generated bindings / test vectors
                          /                         \
                         /                           \
        In-process C ABI (optional client path)   Worker Protocol
                         \                           /
                          \                         /
                       Contract Adapter Layer
                                |
       native scene / RenderSession / transport / reconstruction
           world / resources / GPU runtimes / CUDA reference path
```

The worker executable MUST load the product runtime through the public loader ABI. It MUST NOT statically link to private renderer classes. This makes the worker a real client of the boundary and prevents semantic drift between transports.

The abandoned repository `gui/` tree is not an architecture input, implementation target, or compatibility consumer. A future Studio/editor is an external client developed against generated fixtures, the mock worker, and released runtime packages.

## 5. Public contract registry

### 5.1 Authority and outputs

The registry is the only authority for:

- interface, structure, result, error-domain, event, capability, extension, payload-schema, frame-plane, and semantic IDs;
- numeric enum values, names, versions, dependencies, stability, maturity ceiling, and tombstones;
- Core ABI, Worker Protocol, native-scene, frame-schema, and extension compatibility ranges;
- generated C declarations, FlatBuffers schemas/bindings, Markdown reference tables, runtime manifests, mock fixtures, golden messages, and conformance data.

Registry source MUST use a deterministic repository-owned representation. PB.1 uses canonical JSON authoring to avoid introducing a YAML parser dependency. The published registry digest MUST be computed from a generated canonical representation with domain separation; it MUST NOT depend on whitespace, object-member order, comments, source paths, or mutable display names.

Registry numeric values are integers or decimal strings; binary floating-point values are forbidden. Candidate v1 computes `SHA-256("UltraRender.PublicRegistry.v1\0" || canonical_registry_bytes)`. Changing the digest algorithm or domain label creates a new registry-digest scheme identity rather than silently changing existing digests.

### 5.2 Identity rules

- Numeric IDs are assigned explicitly; they are never hashes of mutable names.
- Published IDs and enum values are never reused.
- Removed values become permanent tombstones.
- Stable and unstable namespaces use distinct reserved ranges.
- Canonical names are unique and immutable after stable publication; display text is not identity.
- A registry change records whether it is additive, candidate-breaking, stable-compatible, or major-breaking.
- The generator rejects duplicates, reuse, missing dependencies, invalid version ranges, unstable defaults, and undocumented Core growth.

Persistent scene identities use RFC 9562 UUIDs represented as exactly 16 canonical network-order bytes at the public boundary. Text form is lower-case hyphenated UUID. The mapping between the two forms is frozen and tested. UUIDs identify scene semantics; opaque handles identify live runtime objects and MUST NOT be interchanged.

Content and build identities use 256-bit digests with an explicit algorithm and domain label in the containing schema. A bare byte array without algorithm/domain metadata is not a portable identity.

### 5.3 Schema evolution

Wire schemas follow append-only FlatBuffers evolution:

- every field has an explicit numeric ID;
- field and enum IDs are never reused;
- removed semantics are deprecated, not reassigned;
- unknown optional fields are ignored;
- required behavior is negotiated as a capability, never inferred from an unknown field;
- `flatc --conform` and historical golden messages are mandatory gates;
- every received buffer is verified before dereference with bounded nesting, table count, vector length, string length, and total bytes.

## 6. Core ABI profile

### 6.1 First supported machine profile

Core ABI 1.0 initially promises only:

```text
OS: Windows 11 x64
architecture: little-endian 64-bit x86
calling convention: platform-standard Windows x64 C convention
compiler boundary: C ABI only; no C++ ABI promise
integer representation: fixed-width stdint types
alignment/layout: recorded by generated ABI manifest and golden layout test
```

Other OS/architecture profiles require their own declared ABI manifest and gates. Worker payload semantics remain platform-neutral even when only the Windows local transport profile is implemented.

### 6.2 Loader and exports

Candidate runtime name:

```text
ultrarender_runtime_candidate.dll
```

Stable PB.8 name:

```text
ultrarender_runtime_1.dll
```

The stable DLL exports exactly two unmangled C symbols:

```c
ure_result_t URE_CALL ureGetRuntimeManifest(
    const ure_runtime_manifest_request_t* request,
    ure_runtime_manifest_t* manifest,
    ure_bootstrap_diagnostic_t* diagnostic);

ure_result_t URE_CALL ureQueryInterface(
    const ure_interface_query_t* query,
    ure_interface_response_t* response,
    ure_bootstrap_diagnostic_t* diagnostic);
```

No opaque error object can be returned before the error interface has been discovered. Loader failures therefore use a caller-owned, fixed-layout `ure_bootstrap_diagnostic_t` containing structure size, stable result, domain/detail codes, required message length, and a bounded UTF-8 message buffer. Truncation is reported explicitly. All post-bootstrap failures use the retained Error interface.

The build MUST use an explicit `.def`/visibility list. Automatic export of Windows symbols is forbidden. The export test rejects any third symbol and any decorated or C++ symbol.

### 6.3 Fundamental types

Public ABI uses fixed-width integers, 32-bit booleans, explicit byte spans, explicit string views, and opaque pointer handles. It does not use C/C++ `bool`, `long`, bitfields, compiler-default enum storage, references, variadic functions, ownership-bearing unqualified pointers, or callbacks invoked on undocumented threads.

All public enums have a fixed `int32_t` or `uint32_t` storage typedef. Unknown enum values received from a newer runtime are preserved or rejected according to the defining contract; they are never treated as a known default.

### 6.4 Input and output structure evolution

Input and output extension headers are deliberately different:

```c
typedef struct ure_input_header_t {
    uint32_t type;
    uint32_t size;
    const void* next;
} ure_input_header_t;

typedef struct ure_output_header_t {
    uint32_t type;
    uint32_t size;
    void* next;
} ure_output_header_t;
```

Rules:

- fields are append-only inside a stable major;
- callers set `type`, `size`, `next`, and all reserved fields;
- runtimes read/write only bytes covered by the supplied size;
- output tails beyond the runtime's known layout remain zero;
- chains contain at most 32 structures in Core 1.0;
- cycles and duplicate types are rejected unless the registry explicitly allows repetition;
- unknown optional input structures are ignored; an unknown required behavior must already have failed capability negotiation;
- output chains are caller-provided writable storage and are never allocated implicitly by the runtime.

### 6.5 Interface discovery and tables

Each interface has a stable 128-bit ID, independent major/minor version, dependency set, and immutable function-table layout. Every table begins with `struct_size` and selected interface version. Functions are appended only at the tail inside a major. Clients check `struct_size` before reading appended members.

Returned tables are immutable runtime-owned static data valid until the DLL is unloaded. They contain no per-instance mutable state. Query selects the highest compatible version in the requested closed range or returns a stable interface-version error.

### 6.6 Runtime manifest

The manifest is available before instance creation and contains:

- product version plus Core ABI and Worker Protocol ranges;
- machine ABI profile and calling-convention identity;
- registry digest scheme/value and exact runtime build digest;
- native-scene and frame-schema read/write ranges;
- interface and capability indexes with stability/maturity/runtime state;
- maximum structure-chain, message, blob, scene, event, frame, retained-byte, and retained-frame limits;
- local transport/security features and whether any external execution policy is enabled;
- compiler, toolchain, backend, driver, and reproducibility identities needed to interpret failures and Research artifacts.

Product version is descriptive and MUST NOT be used as a capability proxy.

## 7. Minimal Core object model

Core defines only these semantic roles:

| Interface | Stable responsibility |
|---|---|
| Runtime | Manifest, registry identity, interface discovery, instance creation |
| Instance | Capability negotiation, object ownership domain, diagnostics/event queue |
| Scene | Native blob validation/load, immutable revision identity, transaction envelope |
| Session | Bind scene revision and objective request, start/pause/resume/reset/close |
| Operation | State, progress, wait, timeout, cancel request, terminal error |
| Frame | Immutable descriptor, plane enumeration, lease, CPU map/copy |
| Error | Domain/code/message, cause chain, structured schema payload, retention/release |

Opaque handles are owned by one runtime and one parent instance. Cross-instance handle use fails. Destroying a parent with live children either returns `BUSY` or closes children according to the interface's declared rule; it never silently leaks or invalidates memory still mapped by the client.

Handles embedded in descriptors, error records, or event records are borrowed identities whose validity is bounded by the owning record or query. A client that needs an embedded Error, operation, or other retainable object after releasing that owner MUST retain the handle first; inspecting a borrowed handle never transfers ownership implicitly.

Core contains no integrator enum, material-node enum, backend-native handle, learned-model format, solver family, or internal world-state representation.

## 8. Results, errors, threading, and lifetime

### 8.1 Results and errors

Stable results distinguish success, incomplete/progress states, invalid argument/structure/handle, unavailable capability, incompatible version/schema, malformed/untrusted data, revision conflict, budget exhaustion, timeout, cancellation, device loss, worker loss, and internal failure.

An Error object contains:

- stable result and error-domain IDs;
- domain-specific detail code;
- bounded UTF-8 message;
- optional schema-tagged details;
- operation, session, scene revision, and build identities when applicable;
- an optional retained cause.

C++ exceptions MUST be caught inside the adapter. Exception type names and private stack/storage addresses MUST NOT enter stable messages. Failure to allocate an Error object still returns the original stable result.

### 8.2 Threading

- Interface tables and runtime manifest queries are concurrently readable.
- Handles declare `ExternallySynchronized`, `ConcurrentRead`, or `Concurrent` in the registry.
- Mutation and render submission on one session are externally serialized unless a later extension says otherwise.
- Callbacks from render/GPU threads into client UI code are forbidden in Core 1.0.
- Events are pulled from an instance-owned queue or waited on through an operation/event wait primitive.

### 8.3 Operations and cancellation

Long work returns an operation handle. States are `Queued`, `Running`, `Paused`, `CancelPending`, `Succeeded`, `Canceled`, `Failed`, and `DeviceLost`. State transitions are monotonic except the documented running/paused pair.

Cancellation is a request, not an immediate guarantee. The runtime reports whether cancellation was accepted and eventually emits a terminal state. Timeout of `wait` does not cancel. Closing a client or worker maps every nonterminal operation to an explicit terminal loss state.

Progress consists of a normalized fraction only when meaningful, a stable stage ID, completed/total work units when known, and a monotonic sequence number. Clients MUST tolerate progress that is unavailable or non-linear.

### 8.4 Events and overflow

Events carry instance/session/operation/frame identities, event type, monotonic per-queue sequence, timestamp, and optional schema-tagged payload. Ordering is guaranteed only within one queue.

Queues are bounded. Overflow MUST emit or expose a gap record with the first and last lost sequence and affected classes. State-changing terminal events cannot be the only source of truth; clients can always query the owning object after a gap. Diagnostic coalescing and dropping policy is declared in the manifest.

### 8.5 Memory and string ownership

- Caller input storage remains caller-owned and need only remain valid for the synchronous call unless an accepted operation/blob lease explicitly retains it.
- Runtime output is returned through caller-sized two-call queries, caller-provided buffers, or retained opaque handles. The runtime never asks a client to free runtime storage with the client CRT.
- Strings are UTF-8 byte spans with explicit length; embedded NUL policy is defined per field and NUL termination is never inferred.
- Arrays carry element count, element size/stride, and checked total byte extent. Count/stride multiplication is overflow-checked before access.
- A future custom allocator is a separately negotiated extension. Core 1.0 performs no cross-module allocator ownership transfer.
- A runtime DLL MUST NOT be unloaded while it owns live handles, mapped frames, operations, event waiters, or worker calls. The client closes the instance and receives `Busy` until the unload preconditions hold.

## 9. Capabilities and extensions

Capability negotiation occurs before object/session creation and returns structured descriptors containing identity, semantic version, stability class, maturity, runtime state, dependencies, limits, and reason when unavailable/inapplicable.

Requests classify capabilities as required or optional. Missing required capability fails before work begins. Missing optional capability is returned explicitly; the runtime MUST NOT silently substitute a weaker semantic.

Extension classes:

- `StableExtension`: append-only within its major and carries its own compatibility suite and support policy;
- `UnstableExtension/Experimental`: versioned but no cross-release compatibility promise; explicit opt-in;
- `UnstableExtension/Research`: exact registry/runtime/provider/artifact binding; explicit opt-in; may be removed after negative evidence.

Promotion creates a new stable identity/version and an explicit adapter where possible. A Research identity is never silently reclassified in place.

Likely future extensions include material graph, spectral resources, wave/coherence, world state, animation, high-order measurement, reconstruction, differentiation, external GPU memory, manual integrator control, and local solver providers. None is pre-declared stable by this architecture.

## 10. Scene authority and editing

### 10.1 Authority

The native semantic formats `.ure`, `.urescene`, and `.urepkg`, plus equivalent in-memory versioned blobs, remain the public scene authority. `SceneIR` remains internal compiled state. The runtime advertises exact readable/writable schema ranges.

Immutable full-scene replacement is the permanent compatibility fallback:

```text
external authoring state
    -> native scene/package blob
    -> bounded validation
    -> accepted scene revision or structured rejection
```

Fallback is never silent. Stable Core reports success through the accepted revision identity or returns a structured rejection. Renderer update strategy, rebuilt-resource detail, and transaction-specific diagnostics are extension data, not Core semantics.

PB.5 implements the Candidate full-replacement subset on Windows x64. The public Scene table accepts bounded memory/file native blobs, returns validation diagnostics and immutable revision identities, and swaps revisions atomically. The Session table binds an accepted revision, lowers generic objective and resource budgets to the internal automatic path, exposes operation progress, and publishes immutable PB.4 frames. The local worker invokes the same tables through the two loader exports; it does not define a second scene, error, session, or framebuffer authority.

PB.6 historically added a generated transaction function to the Candidate Scene table and worker transport. SceneIR schema 2 persists RFC 9562 UUIDs while legacy source IDs remain aliases. PB.8 removed that function from the stable Scene prefix and exposes it only through the separately queried `URE_INTERFACE_SCENE_TRANSACTION` UnstableExtension. Exact edits are validated against isolated state and swapped only after the complete result fits caller storage; stale bases return a retry revision without merge, and unsupported edits either consume an explicit full-scene fallback or reject without changing scene, renderer binding, revision, or accumulation. Camera payloads, transaction results, renderer update strategies, and transaction events remain independently evolving extension semantics.

PB.7 validates, but does not stabilize, this boundary. Historical PB.2-PB.6 C11 clients are retained with source, SDK, compiler and binary identities and exercise only their compiled table prefixes against the current runtime. Earlier Candidate runtime DLLs are not retained as supported releases; the reverse supported matrix contains only the current client/current content-digested runtime. The SDK and runtime are independently content-digested: the SDK carries headers, schemas, registry, mock worker, goldens, fixtures, FlatBuffers headers/license and integration guide; the runtime carries the product DLL/worker, registry, ABI/runtime manifests, inspection schemas, license and compatibility record. A clean out-of-tree client builds direct, transaction and worker workflows solely from these packages. This remains Candidate evidence, not a cross-release promise.

### 10.2 Revisions and unstable transactions

Every accepted state has a monotonic revision, semantic digest, source-schema version, and resource-manifest digest. The unstable transaction extension carries transaction UUID, base revision, ordered schema-tagged operations, required capabilities, and client metadata.

Operations address UUIDs, never container indices. A stale base revision returns `RevisionConflict`; the extension performs no automatic merge. Validation and apply are atomic: rejection leaves the old scene and accumulation state unchanged.

Core freezes no transaction envelope or edit vocabulary. Camera edits, transform edits, material payloads, mesh replacement, object creation/removal, lights, environments, animation, and world-state changes use versioned operation schemas/extensions. Clients that cannot or choose not to use the unstable transaction extension retain full-scene replacement as the stable fallback.

PB.6 migrates native objects that currently use arbitrary strings or indices to a canonical UUID field through a new schema version and migration tool. Existing source identifiers may be retained as aliases, but duplicate UUIDs fail validation.

### 10.3 Camera semantics

The first camera extension uses one canonical pose and one canonical projection parameterization:

- pose: right-handed world transform with frozen matrix element order and axis convention;
- perspective projection: physical sensor dimensions plus focal length, or a separately identified FOV projection schema, never both as independent truth;
- orthographic and specialized lens models: separate schemas/capabilities;
- shutter interval, aperture, focus distance, lens shift, exposure, and units are explicit where supported.

Look-at target, Euler angles, horizontal/vertical FOV, and derived aspect values are authoring conveniences. They are converted before submission and MUST NOT create conflicting public truth sources. Unsupported fields reject or negotiate as unavailable; they never reset silently.

## 11. Session and objective model

A session binds an instance, exact scene revision, enabled capabilities, device/backend constraints, requested outputs, and a versioned render-objective payload.

Session states are `Created`, `Ready`, `Running`, `Paused`, `Failed`, `DeviceLost`, and `Closed`. Cancellation state belongs to the operation handle rather than creating a second session-state machine. State-changing calls either complete atomically or return an operation whose terminal state determines the transition. Reset records the accumulation reason and never changes the bound scene revision implicitly.

Core knows only the objective envelope and generic budgets:

- quality target identity;
- wall-time, memory, sample/work, and latency budgets;
- output semantic IDs;
- determinism/reproducibility policy;
- preview/final/research policy;
- objective payload schema and digest.

Automatic technique selection remains internal. Manual integrator/technique selection, backend-specific tuning, learned proposal configuration, reconstruction policies, and world stepping are extensions. This prevents public UI controls from freezing current internal mode enums.

## 12. Immutable frames and measurement semantics

### 12.1 Lease model

Frame acquisition returns an immutable frame handle and consumes a retained-frame/byte lease. A frame remains valid until release, worker loss, or an explicitly documented fatal runtime teardown. Later render passes cannot mutate it.

The manifest declares maximum retained frames and bytes. When the client reaches the limit, acquisition returns `Backpressure` with current usage; the runtime does not overwrite an older live frame. Worker crash invalidates shared-memory mappings and transitions leases to `WorkerLost`; clients MUST NOT dereference them afterward.

Map/unmap has a checked state machine:

```text
Unmapped -> MappedRead -> Unmapped -> Released
```

Double map, double unmap, release while mapped, cross-frame plane use, and use after release fail deterministically. Core 1.0 always supports bounded copy to caller-owned CPU memory; CPU map is supported when advertised.

### 12.2 Frame and plane metadata

Frame metadata includes frame/schema IDs, scene/camera/operation revisions, sample/work coverage, dimensions, completion state, objective/estimator identity, timestamp, dirty region, plane count, and provenance digest.

Plane metadata includes semantic/schema IDs, observable, scalar/component layout, dimensions/strides, units, measure/normalization, wavelength/time/phase/coherence semantics, color/display metadata where applicable, uncertainty/provenance identities, and byte extent.

Beauty, normal, albedo, depth, UV, and motion may receive stable plane schemas after evidence. High-order planes reuse the HO.1 observable/unit/measure/time identities and HR.0 measurement/provenance semantics. Complex field, Jones, Stokes, mutual intensity, spectral samples, uncertainty, and sufficient statistics MUST NOT be encoded as ad-hoc AOV enum additions or flattened into RGB.

Native GPU-memory import/export is excluded from Core and belongs to platform extensions.

## 13. Worker Protocol local profile

### 13.1 Semantic parity

The worker protocol and C ABI share the same IDs and meanings for capabilities, extensions, scene revisions, object IDs, results, errors, operations, events, frames, maturity, and degradation policy. Transport-only handle descriptors may differ. The worker translates protocol objects to public ABI calls, not directly to private C++ methods.

### 13.2 Windows 1.0 transport

Worker Protocol 1.0 initially supports only a local Windows profile:

- one duplex Named Pipe control channel;
- same-user access control and `PIPE_REJECT_REMOTE_CLIENTS`;
- shared memory/file mappings for bounded bulk blobs and frames;
- inherited or explicitly duplicated handles rather than globally guessable names where practical;
- a Job Object with kill-on-close so client death cannot orphan the worker;
- no TCP, UDP, remote pipe, discovery broadcast, or implicit network access.

The initial worker therefore does not require a firewall exception. Remote execution is a later transport profile with separate authentication, authorization, confidentiality, quotas, and threat analysis.

### 13.3 Handshake and envelope

Handshake exchanges protocol range, Core semantic range, scene/frame schema ranges, registry digest, runtime/frontend build identities, required/optional capabilities, transport features, and maximum control/blob/frame sizes. Major incompatibility or a required registry/capability mismatch fails before instance creation.

Every message envelope carries protocol major/minor, message type, flags, correlation ID, instance/session/operation IDs, payload schema/version, payload length, and monotonic channel sequence. Supported patterns are request/response, asynchronous event, cancellation, frame-ready, and shared-blob reference.

Control messages are FlatBuffers and are verified before use. Ordinary control messages never contain unbounded geometry, texture, package, frame, measurement, or model data. Bulk descriptors bind mapping handle, offset, length, access, digest, producer, lease, and expiration/operation identity.

### 13.4 Failure model

Clients distinguish clean exit, protocol violation, version mismatch, worker crash, watchdog termination, device loss, runtime fatal error, and client-requested shutdown. Restart creates a new worker/runtime instance identity; old handles, mappings, revisions, and operation IDs are never reused or presented as live.

## 14. Contract adapter and packaging

The target module boundaries are:

```text
contracts/                         registry sources, wire schemas, baselines
tools/ure_contract_codegen/        deterministic generator and registry linter
libs/ure_public/                   installed C headers and generated value tables
libs/ure_contract/                 private adapter and product runtime DLL
apps/ure_worker/                   worker using only the loader ABI
tests/contract/                    ABI/protocol/behavior/security tests
tests/fixtures/contracts/          golden messages, manifests, old-client binaries
```

`libs/ure_contract` may depend inward on current modules. Existing renderer modules MUST NOT depend on adapter implementation code. Shared semantic value registries may be consumed through generated SDK-free headers when explicitly reviewed.

Runtime and SDK packages are separate. A runtime package includes candidate/stable DLL, worker, registry/manifests, schemas required for inspection, licenses, and compatibility report. An SDK includes the loader header, generated constants/bindings, schema descriptions, mock worker, fixtures, and integration guide. Reproducible package metadata binds source commit, toolchain, generator version, registry digest, runtime digest, and supported platform profile.

### 14.1 External client integration profile

The initial external Studio/client profile is:

```text
transport: local worker by default
scene: immutable native scene/package or equivalent memory blob
editing: revisioned UUID transactions with explicit full-reload fallback
rendering: objective-oriented asynchronous operation
frames: immutable CPU copy/map or shared-memory lease
capabilities: required Core plus explicitly optional extensions
deployment: exact runtime package pin with manifest/digest check
```

The client MUST NOT link private renderer libraries, retain legacy framebuffer pointers, address SceneIR indices, infer capability from product version, assume immediate cancellation, or hide worker/runtime mismatch. In-process C ABI use is permitted for specialized embedding, bindings, and conformance tests, but it does not weaken these semantic rules.

## 15. Legacy migration

The current `ure_c_api.h`, `pyure_native.dll`, and ctypes surface remain legacy experimental compatibility interfaces during PB. They receive compatibility and safety fixes needed for migration, but new high-order feature vocabulary SHOULD first receive registry/schema identities rather than expanding legacy mega-structures.

The Core adapter may lower to current `RenderSession`, native-scene tooling, `SceneDiff`, and framebuffer implementations. That is an implementation detail, not a promise to preserve those layouts.

Legacy removal requires all of:

- stable Core ABI has shipped through the documented support interval;
- maintained external client and pyure paths use the stable boundary;
- CLI has no legacy-only requirement;
- old-client and mixed-version matrices are green;
- migration guidance and a product-level breaking decision exist.

PB does not authorize deleting the legacy API.

## 16. Security and robustness

Both transports treat client-controlled data as untrusted. Conformance requires:

- bounded structure-chain, message, blob, scene, resource, frame, string, vector, nesting, and retained-memory sizes;
- FlatBuffers verification and semantic validation before allocation/use;
- checked offset/length arithmetic and mapping bounds;
- handle type, owner, generation, parent, state, and thread-policy validation;
- resource digest and schema identity verification;
- same-user local transport and no ambient network listener;
- no ambient native plugin, script, solver, model, or executable discovery;
- explicit capability and policy for any external execution;
- sanitized structured diagnostics without secrets, private pointers, or uncontrolled paths;
- worker crash containment, deterministic cleanup, and watchdog-compatible cancellation.

Fuzzing covers loader structures, extension chains, registry inputs, handshake/envelopes, FlatBuffers payloads, shared-memory descriptors, scene transactions, and lifecycle misuse.

## 17. Compatibility and release gates

### 17.1 Candidate gates

Every candidate release records:

- exact export list, ABI sizes/offsets/alignment/enums/table sizes and registry digest;
- generated-output reproducibility from a clean tree;
- historical candidate golden messages and schema conformance;
- mock-worker behavior and deterministic fixtures;
- malformed input, limit, lifecycle, transaction, cancellation, frame, crash, and backpressure results;
- in-process/worker semantic parity;
- package content and build identities.

Candidate breaks are allowed only with an explicit registry compatibility record and fixture migration.

### 17.2 Stable 1.0 declaration gate

Core ABI 1.0 and Worker Protocol 1.0 are declared only when all are true:

1. loader exports, Windows x64 ABI profile, registry, and manifests are frozen;
2. no C++ symbol or exception crosses the DLL boundary;
3. structured errors, capabilities, operations, events, cancellation, and lifetime behavior are complete;
4. immutable frames, lease budgets, map/copy, backpressure, and crash invalidation pass;
5. native full-scene load/replacement and revision conflict behavior pass;
6. UUID transactions and canonical camera extension pass as an UnstableExtension with explicit full-reload fallback;
7. the retained final-Candidate layout seed runs unchanged against runtime 1.0, and current/current passes; because no prior stable runtime exists for the first stable major, current-client/prior-stable-runtime is explicitly `NotApplicable` rather than simulated. Both directions become mandatory after the first post-1.0 runtime is retained;
8. malformed protocol/ABI fuzz corpus and worker crash/restart gates pass;
9. independently built C11, C++, and worker clients use only the staged SDK/runtime packages, cover every Core call plus the unstable transaction call, and produce validated nontrivial image files through in-process map/copy and worker shared memory;
10. release documentation states exact platform, support window, extension availability, schema read/write ranges, and non-promises.
11. the Public Interaction Surface Ledger has zero unknown, duplicate-authority, bypass, or unresolved-migration entries, and cross-entry conformance evidence covers every maintained external adapter/client path.

PB.8 satisfied these gates and received explicit post-REPORT approval on 2026-08-11. The declaration is limited to Core ABI 1.0 and Worker Protocol 1.0; it does not assign version 1.0 to UltraRender as a product or imply public package distribution.

### 17.3 Stable-major policy

Inside a published stable major, symbols, signatures, IDs, enum values, field order/types, successful semantics, and required lifetime behavior are immutable. Additions are tail-only or new extension/schema versions. Breaking changes require a new side-by-side major and an explicit migration/support decision.

No schedule pressure, frontend convenience, or existing internal layout is sufficient justification to expand Core or waive a gate.

## 18. Decision record

1. One registry governs both C ABI and worker semantics.
2. The preferred external-client path is a local isolated worker; in-process ABI remains available.
3. The first stable machine ABI and worker transport profile are Windows x64 only.
4. Core freezes lifecycle grammar, not algorithms or feature inventory.
5. Stability, evidence maturity, and runtime state are independent.
6. Candidate 0.x carries no public stability promise; 1.0 is gated at PB.8.
7. Native scene semantics remain authoritative; full-scene replacement is permanent fallback.
8. Incremental changes use UUIDs, base revisions, schema-tagged operations, and atomic transactions.
9. Camera uses one canonical pose and projection truth per schema.
10. Frames are immutable, leased, budgeted, and invalidated explicitly on worker loss.
11. Worker 1.0 uses Named Pipes and shared memory with no network listener.
12. Research interfaces bind exact identities and can evolve or disappear without burdening stable Core.
13. Runtime majors coexist when a genuine breaking change is required.
14. The current C API remains legacy experimental and is not retroactively stabilized.
15. The initial StableExtension list is empty; telemetry, spectral/Stokes planes, transaction semantics, and renderer update strategy identities are UnstableExtension or tombstoned pre-release identities.
16. The annotated tag `public-boundary-v1.0.0` identifies the declaration commit only; it is not an UltraRender product-release tag and does not start the public support clock.

## 19. Standards references

- [FlatBuffers schema evolution](https://flatbuffers.dev/evolution/)
- [FlatBuffers C++ verification](https://flatbuffers.dev/languages/cpp/)
- [Vulkan extensible structure chains](https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html)
- [Microsoft x64 calling convention](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention?view=msvc-170)
- [Microsoft DLL exports](https://learn.microsoft.com/en-us/cpp/build/exporting-from-a-dll?view=msvc-170)
- [Microsoft `GetProcAddress`](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getprocaddress)
- [Windows Named Pipe creation and remote-client rejection](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-createnamedpipea)
- [Windows shared memory](https://learn.microsoft.com/en-us/windows/win32/memory/sharing-files-and-memory)
- [Windows handle duplication](https://learn.microsoft.com/en-us/windows/win32/api/handleapi/nf-handleapi-duplicatehandle)
- [Windows Job Objects](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects)
- [RFC 9562 UUIDs](https://www.rfc-editor.org/info/rfc9562/)
- [RFC 8785 JSON Canonicalization Scheme](https://www.rfc-editor.org/rfc/rfc8785.html)
