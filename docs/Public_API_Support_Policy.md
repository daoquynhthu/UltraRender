# UltraRender Public Boundary Support Policy

Document status: normative support scope for the declared Core ABI 1.0 and Worker Protocol 1.0. This is not an UltraRender 1.0 product release. The declaration tag records evidence but does not publish packages or authorize distribution.

The `UltraRender_preview` integration route does not expand this promise. Preview capabilities use independently versioned 0.x schemas or extensions unless they pass a separate stability review. No Preview product release has been declared.

## Stable promises

Within runtime major 1:

- `ultrarender_runtime_1.dll` exports exactly `ureGetRuntimeManifest` and `ureQueryInterface` using the Windows x64 C calling convention;
- published Core interface UUIDs, table prefixes, function signatures, structure prefixes, field order/types, numeric IDs, enum/result values, successful semantics, ownership, synchronization, and required lifetime behavior do not change;
- additions are optional tail fields/functions, new schema versions, or new extension UUIDs/IDs; a caller may continue using only its known prefix;
- Worker Protocol 1 preserves envelope/handshake field IDs and meanings, same-user locality, bounded framing, explicit registry negotiation, immutable shared-memory lease semantics, crash invalidation, and no network listener;
- breaking stable changes require a side-by-side runtime/worker major with a separate migration and support decision. Runtime major 1 is never mutated into an incompatible ABI.

The frozen profile is Windows 11 x64, little-endian x86-64, C11-compatible headers, and the platform-standard Windows x64 calling convention. No Linux, macOS, ARM64, 32-bit, COM, C++ ABI, static-link, or compiler-private binary profile is promised by 1.0. The Worker schemas are portable data definitions, but the only supported 1.0 transport implementation is the local Windows Named Pipe/shared-memory profile.

## Support window

Core 1 contract specifications, registry/tombstones, schema baselines, and compatibility fixtures are retained permanently in project history.

Maintained `runtime_1` binary updates continue while major 1 is current. If a successor stable runtime major is publicly distributed, correctness and security maintenance for the last supported `runtime_1` line continues for at least 12 months after that successor's public availability, with at least 6 months' deprecation notice. The clock begins only when packages are separately authorized and publicly distributed; a repository declaration, local build, `public-boundary-v1.0.0` evidence tag, or Candidate artifact does not start it.

Support covers reproducible defects inside the declared platform/threat/resource boundary. It does not guarantee support for end-of-life operating systems, drivers, toolchains, or hardware that cannot execute the documented runtime prerequisites.

## Version and compatibility matrix

This is the first stable major, so no prior stable Core 1 runtime exists for a current-client/prior-runtime row. That row is explicitly `NotApplicable`, not silently passed using a Candidate binary. Beginning with the first post-1.0 `runtime_1` release, both directions are mandatory: the oldest retained Core 1 client against the current runtime, and the current client against the preceding retained stable runtime.

PB.2-PB.7 Candidate 0.x artifacts remain historical evidence and carry no Stable compatibility promise. The retained Core 1.0 seed uses the final PB.7 table/value prefixes but negotiates major 1; every future `runtime_1` build must run that compiled seed without recompilation.

## Schema and extension availability

Runtime 1.0 reports native-scene read majors 1 through 2 and write major 2. Native scene schemas are independently versioned payload contracts, not C ABI layouts. A client must inspect the runtime manifest and may receive `IncompatibleVersion` for a schema outside the advertised range. Full-scene replacement remains the stable interaction fallback; it does not make every future native-scene feature available in every runtime.

The initial StableExtension list is empty. UUID transactions and all spectral/polarimetric, estimator, reconstruction, physical-world, differentiable, telemetry, material, solver/provider, and model interfaces are absent from the Stable Core promise unless a separate extension document says otherwise. `UnstableExtension` and Research/Experimental interfaces may require an exact registry/runtime/provider/artifact identity and may change or disappear without a Core-major change.

Stability, evidence maturity, and runtime state are independent. A stable capability identity does not promise that a provider is compiled, available, enabled, applicable, Production-mature, or successful for a particular scene.

## Operational semantics

- Frame-count and byte limits are queried/negotiated; their numeric values are not frozen.
- A successful cancel request records cancellation. Terminal state may be `Canceled` or `Succeeded` if completion won the race. Timeout does not cancel.
- Worker timestamps are monotonic process-local observations, not synchronized wall-clock time.
- Worker Protocol 1 is same-user local IPC only. It opens no network listener, requests no firewall exception, and does not promise remote/farm operation.
- Exact bitwise image equality is promised only where a specific test/manifest says so. Core 1 does not promise identical pixels, performance, convergence, estimator choice, memory use, or scheduling across hardware, drivers, runtime patches, or enabled extensions.

## Explicit non-promises

Core 1.0 does not stabilize or support as public ABI: renderer/internal C++ layouts; SceneIR or RenderConfig memory; integrator selection/composition; MaterialGraph; MeasurementBundle/reconstruction; WorldState/physics; differentiation; GPU/backend scheduling or native handles; shader/model/solver formats; Hydra; distributed/farm/provider internals; the legacy `ure_c_api.h`/`pyure_native.dll` surfaces; or any repository GUI. No plugin loading, ambient discovery, remote execution, package publication, public distribution, or UltraRender product release is implied.

Project code is available under Apache License 2.0. Contract compatibility and maintenance obligations are defined by this policy and are independent of the copyright license; third-party components retain their own licenses.
