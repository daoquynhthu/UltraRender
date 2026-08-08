# UltraRender Public Boundary Implementation Plan

Document status: authoritative subordinate execution plan for Phase PB

Last reviewed: 2026-08-08

> **For agentic workers:** Execute this plan under [`AGENTS.md`](../AGENTS.md) using the repository's single-agent `PLAN -> IMPLEMENT -> VERIFY -> REVIEW -> REPORT -> COMMIT` workflow. The root [`PLAN.md`](../PLAN.md) owns the global cursor; this document owns PB subcursor details. Do not dispatch subagents. The user granted standing authorization on 2026-08-08 for autonomous plan-scoped PB commits without intermediate progress prompts; push remains prohibited without separate authorization.

**Goal:** Build and graduate a minimal, version-negotiated public interaction boundary that lets an external frontend and UltraRender evolve independently without freezing renderer algorithms or internal layouts.

**Architecture:** One generated contract registry drives a Windows x64 C loader ABI and a local Named Pipe/shared-memory worker protocol. A private adapter lowers stable semantic envelopes to current native-scene, session, transport, reconstruction, world, and GPU implementations. Candidate 0.x remains breakable until mixed-version, security, lifetime, and external-client evidence permits PB.8 to declare Core ABI 1.0 and Worker Protocol 1.0.

**Tech stack:** C++23 host code, C11-compatible installed headers, CMake/Ninja/MSVC, FlatBuffers 25.12.19, nlohmann/json, Windows Named Pipes/file mappings/Job Objects, PowerShell validation, CTest.

---

## 0. Authority, current cursor, and global constraints

The approved architecture is [`Public_API_ABI_Architecture.md`](Public_API_ABI_Architecture.md). If this plan conflicts with that architecture, the architecture controls semantic design and this plan must be corrected before implementation. If either conflicts with the root `PLAN.md` cursor, the root cursor controls execution order.

Current PB cursor:

```text
PB.1 — Contract registry, generation, mock worker, and frontend kit
```

The HR route is suspended after completed HR.2. `HR.3` resumes only after PB.8 or an explicit root-PLAN revision. PB establishes the carrier; it does not promote learned proposals, reconstruction, world state, material graph, wave optics, or solver functionality to stable extensions.

Global constraints:

- PB.0-PB.7 artifacts are named and documented as Candidate 0.x; they carry no stable compatibility promise.
- Core growth requires proof that a schema, capability, or separate extension cannot express the requirement.
- Core ABI 1.0 initially targets only Windows 11 x64, little-endian x86-64, and the platform-standard Windows x64 C convention.
- Worker Protocol 1.0 initially uses only local Named Pipes and shared memory; it opens no network listener and requests no firewall exception.
- The abandoned repository `gui/` directory is excluded from inspection, build, migration, fixtures, and acceptance evidence.
- No public header includes C++ STL, CUDA, Vulkan, D3D12, OptiX, OpenUSD, Qt, or compiler-private types.
- Native `.ure`/`.urescene`/`.urepkg` semantics remain authoritative; `SceneIR` and renderer/session layouts remain internal.
- Existing `ure_c_api.h`, `pyure_native.dll`, and pyure ctypes remain legacy experimental during migration and are not deleted by PB.
- No code change is complete without the scoped host/ABI/protocol test and the maintained Release gate required by its PB subphase.
- Plan-scoped PB commits use the standing 2026-08-08 authorization after local VERIFY/REVIEW; no push is authorized.

## 1. Dependency graph and completion sequence

```text
PB.0 inventory/freeze
  |
  v
PB.1 registry/codegen/mock worker -------------------- external client can begin
  |
  v
PB.2 loader ABI and candidate runtime DLL
  |
  v
PB.3 instance/handle/error/capability/operation/event core
  |
  v
PB.4 immutable frames + Windows worker transport
  |
  v
PB.5 native full-scene validation/load/replacement
  |
  v
PB.6 UUID transactions + canonical camera extension
  |
  v
PB.7 mixed-version/security/crash/external-client closure
  |
  v
PB.8 freeze and declare Core ABI 1.0 / Worker Protocol 1.0
  |
  v
resume HR.3 through versioned extensions
```

Each PB phase produces one independently reviewable vertical capability. Substeps below are the execution checklist; the phase status and evidence block are updated only after verification.

## 2. Target file and module map

The following paths are created progressively. Generated build-tree files are not substituted for registry source authority.

```text
contracts/
  registry/
    public_contract_registry.json       canonical authoring registry
    registry_compatibility.json         version/change classification
  schemas/
    ure_worker_candidate.fbs            control handshake/envelope/messages
    ure_payload_candidate.fbs           common schema-tagged payload envelopes
    ure_frame_candidate.fbs             frame/plane descriptors
    ure_scene_transaction_candidate.fbs transaction and camera extension
  baselines/
    candidate/                          layouts, exports, manifests, golden wire data
  public_interaction_surface_ledger.json complete historical/external surface classification

tools/ure_contract_codegen/
  CMakeLists.txt
  src/main.cpp                          deterministic generation/lint entry point
  src/registry.cpp                      parse, normalize, validate, digest
  src/generator.cpp                     C/FlatBuffers/docs/manifest generation

libs/ure_public/
  CMakeLists.txt
  include/ultrarender/ure_loader.h       C11-compatible loader/types
  include/ultrarender/ure_registry.h     generated stable numeric identities

libs/ure_contract/
  CMakeLists.txt
  src/loader.cpp                         the only two exported loader functions
  src/runtime_adapter.cpp                runtime/instance bridge
  src/handle_table.cpp                   typed owner/generation/state validation
  src/error_adapter.cpp                  retained structured errors
  src/capability_adapter.cpp             three-axis capability descriptors
  src/operation_adapter.cpp              operation state/wait/cancel
  src/event_queue.cpp                    bounded events and gap records
  src/frame_adapter.cpp                  immutable frame leases/map/copy
  src/scene_adapter.cpp                  native blob/revision/replacement
  src/transaction_adapter.cpp            UUID transaction lowering/rollback
  src/session_adapter.cpp                objective/session bridge

apps/ure_worker/
  CMakeLists.txt
  src/main.cpp                            process/bootstrap/job lifecycle
  src/runtime_client.cpp                  dynamic DLL loader; public ABI only
  src/named_pipe_transport.cpp            same-user local control channel
  src/shared_blob_transport.cpp           duplicated mapping handles and leases
  src/protocol_dispatch.cpp               verified message-to-ABI translation

tests/contract/
  CMakeLists.txt
  test_registry.cpp
  test_abi_layout.cpp
  test_loader_exports.ps1
  test_runtime_lifecycle.cpp
  test_capabilities.cpp
  test_operations_events.cpp
  test_frame_leases.cpp
  test_worker_protocol.cpp
  test_worker_crash.cpp
  test_scene_boundary.cpp
  test_scene_transactions.cpp
  test_mixed_versions.ps1
  test_untrusted_inputs.cpp
  external_client/                        standalone SDK/package consumer

tests/fixtures/contracts/
  mock_worker/
  golden_messages/
  registry/
  old_clients/

scripts/
  run_phase_pb_validation_suite.ps1       unified machine-readable PB gate
```

`libs/ure_contract` depends inward on renderer modules. Existing internal libraries do not depend on adapter implementation files. `apps/ure_worker` discovers `ultrarender_runtime_candidate.dll`/`ultrarender_runtime_1.dll` dynamically and links no private renderer library.

## 3. PB.0 — Boundary freeze, inventory, and compatibility baseline

**Dependencies:** HR.2 complete; approved public-boundary architecture.

**Status:** Complete (2026-08-08).

**Outcome:** The repository can prove exactly what is legacy, what will be public, what must never leak, and which baseline artifacts later phases must preserve.

**Files:**

- Create `contracts/registry/public_contract_registry.json` with registry metadata and empty, reserved Core namespaces.
- Create `contracts/registry/registry_compatibility.json` with candidate change classes and tombstone policy.
- Create `tests/fixtures/contracts/registry/legacy_surface.json`.
- Create `contracts/public_interaction_surface_ledger.json`.
- Create `tests/fixtures/contracts/old_clients/README.md` describing provenance and execution policy for retained binaries.
- Create `scripts/audit_public_boundary.ps1`.
- Modify `docs/reference/Backend_API.md` to label the current C/pyure surface legacy experimental.

**Interfaces produced:**

- `ure.registry.source.v1`: deterministic registry-source envelope and namespace reservations.
- `ure.pb.legacy_surface.v1`: machine-readable header/export/layout/ownership/mutation/error audit.
- `ure.pb.public_interaction_surface_ledger.v1`: complete authority, role, translation, bypass, disposition, migration, and evidence classification.
- `ure.pb.boundary_audit.v1`: static gate output with source commit, toolchain, DLL digest, intended C symbols, accidental exports, public-header dependencies, and forbidden type findings.

**Execution checklist:**

- [x] Record current `ure_c_api.h` functions, structure sizes/offsets, enum values, `_vN` families, pointer lifetimes, index-addressed mutations, and error behaviors in `legacy_surface.json`.
- [x] Inventory native formats/tooling; glTF/MaterialX/USD/Hydra adapters; C++/C/pyure/CLI client surfaces; distributed/farm/cache files; solver/procedural/script/provider hooks; installed headers; the abandoned GUI; and frozen Phase X in `public_interaction_surface_ledger.json`.
- [x] Assign each ledger entry exactly one disposition (`CanonicalAuthority`, `PublicTransport`, `Adapter`, `VersionedExtension`, `InternalContract`, `LegacyMigration`, or `FrozenExcluded`) plus semantic authority, caller/owner, identities, translation path, bypass risk, migration phase, and conformance evidence.
- [x] Reject missing classifications, duplicate canonical authorities, adapters without canonical target/loss policy, public transports without registry identity, internal contracts presented as client APIs, legacy paths without terminal migration, and any maintained path that bypasses the future contract adapter without an explicit convergence decision.
- [x] Build the current legacy DLL, enumerate every export, classify intended `ure_*` symbols versus accidental C++/auto-export symbols, and bind the evidence to the DLL SHA-256.
- [x] Compile a representative C client and pyure smoke client against the legacy surface; retain source plus binaries as migration-only fixtures, without describing them as stable clients.
- [x] Add a static forbidden-leak scan for STL/C++ ABI, exceptions, backend SDK types, native handles, internal layouts, and automatic Windows exports in future `ure_public` headers/runtime targets.
- [x] Reserve numeric ranges for Core, StableExtension, UnstableExtension/Experimental, UnstableExtension/Research, vendor/private tests, and tombstones.
- [x] Run `scripts/audit_public_boundary.ps1`; verify it reproduces identical JSON twice and fails on an injected duplicate ID, forbidden public type, and extra export fixture.
- [x] Run the maintained Release CTest gate to prove inventory work did not change renderer behavior.

**Completion evidence:** legacy surface and DLL identities are reproducible; every current or historical interaction surface has one owner/authority/disposition, while every known bypass has an explicit convergence phase and terminal gate; registry ranges are frozen for Candidate 0.1; no current API is mislabeled stable.

**Recorded evidence:** 25 interaction surfaces, 14 unique authority domains, zero unclassified/duplicate/forbidden-inspection entries; 2,030 legacy DLL exports classified as 55 intended C, 1,970 C++, and 5 other accidental symbols; deterministic C11 client fixture and audit report; malformed ledger/registry/header/export fixtures rejected; Release build and 72/72 CTest pass.

## 4. PB.1 — Contract registry, generation, mock worker, and frontend kit

**Dependencies:** PB.0.

**Outcome:** An external client can implement integration against generated declarations, deterministic fixtures, and a mock worker before the real runtime/worker exists.

**Files:** all `tools/ure_contract_codegen`, `libs/ure_public`, initial `contracts/schemas`, `tests/contract/test_registry.cpp`, `tests/fixtures/contracts/mock_worker`, and `tests/fixtures/contracts/golden_messages` paths from the module map.

**Interfaces produced:**

- `ure_contract_codegen lint|generate|compare` command modes.
- C loader declarations for Candidate 0.1, with `ure_input_header_t`, `ure_output_header_t`, UUID/digest/span/string/result/bootstrap types, manifest query, and interface query.
- Worker Candidate 0.1 handshake, envelope, result/error/capability, operation/event, frame-ready, and shared-blob descriptors.
- Deterministic mock-worker scenarios: normal lifecycle, missing optional capability, missing required capability, old minor, unknown optional field, malformed message, event gap, backpressure, device loss, and worker crash.

**Execution checklist:**

- [ ] Implement strict JSON parsing, numeric-range validation, duplicate/tombstone/dependency/version/default checks, and path-independent canonical registry serialization.
- [ ] Compute a domain-separated SHA-256 over canonical generated registry bytes and emit the digest into C constants, FlatBuffers fixtures, Markdown reference, and manifests.
- [ ] Generate C11-compatible declarations and compile them as C11, C++23, and a standalone external C consumer without repository-private include paths.
- [ ] Generate FlatBuffers schemas with explicit field IDs and run `flatc --conform` against the retained previous candidate baseline.
- [ ] Implement a deterministic standalone mock worker that never loads renderer code and replays registry-defined request/response/event scripts over the candidate Named Pipe profile or an in-memory protocol harness.
- [ ] Publish golden byte messages for every mock scenario plus intentionally malformed/truncated/oversized inputs.
- [ ] Verify a clean second generation produces byte-identical headers, schemas, docs, manifests, and fixtures; fail the build on generated drift.
- [ ] Build and run the standalone `external_client` against only the generated SDK staging directory and mock worker.

**Completion evidence:** external-client work is unblocked by a self-contained candidate SDK/mock package; registry/generator output is reproducible; schema evolution and malformed-message gates pass.

## 5. PB.2 — Windows x64 loader ABI and candidate runtime product

**Dependencies:** PB.1.

**Outcome:** A separately packaged candidate DLL exposes exactly two bootstrap functions and dynamically returns immutable interface tables without leaking C++ ABI.

**Files:** `libs/ure_contract/CMakeLists.txt`, `src/loader.cpp`, initial `src/runtime_adapter.cpp`, Windows export definition, `tests/contract/test_abi_layout.cpp`, and `tests/contract/test_loader_exports.ps1`.

**Interfaces produced:**

- `ultrarender_runtime_candidate.dll`.
- `ureGetRuntimeManifest` and `ureQueryInterface` exports.
- Caller-owned `ure_bootstrap_diagnostic_t` for pre-interface errors.
- ABI manifest containing platform profile, type/field/table sizes, offsets, alignment, enum values, interface IDs, registry digest, runtime digest, and compiler/toolchain identity.

**Execution checklist:**

- [ ] Add the candidate SHARED target with hidden/default-off visibility and an explicit Windows `.def` containing only the two loader names.
- [ ] Implement loader argument, structure-size/type/reserved/chain validation without allocating runtime handles.
- [ ] Implement bounded bootstrap diagnostics with explicit required/written UTF-8 byte counts and deterministic truncation behavior.
- [ ] Return immutable runtime/interface tables and reject unsupported version ranges without accessing caller bytes beyond declared sizes.
- [ ] Generate and compare the Windows x64 ABI layout manifest using C and C++ compilation units.
- [ ] Inspect the built DLL export directory and fail unless it contains exactly two undecorated C loader symbols.
- [ ] Load the DLL through `LoadLibraryW`/`GetProcAddress` in a consumer that links no import library or private header.
- [ ] Exercise undersized/oversized structures, unknown optional chains, duplicate/cyclic/overlength chains, null output, invalid reserved fields, and message truncation.

**Completion evidence:** the candidate DLL loads through the two-symbol ABI, layout/export manifests are reproducible, all bootstrap misuse fails deterministically, and no C++ symbol/exception crosses the boundary.

## 6. PB.3 — Core lifecycle, errors, capabilities, operations, and events

**Dependencies:** PB.2 and existing HO.1/HO.2 capability/maturity semantics.

**Outcome:** The Core object grammar is complete independently of scene rendering and has explicit ownership, concurrency, cancellation, diagnostic, and overflow behavior.

**Files:** `runtime_adapter.cpp`, `handle_table.cpp`, `error_adapter.cpp`, `capability_adapter.cpp`, `operation_adapter.cpp`, `event_queue.cpp`, and the matching `test_runtime_lifecycle.cpp`, `test_capabilities.cpp`, `test_operations_events.cpp`.

**Interfaces produced:** Runtime, Instance, Error, Operation, and Event Candidate 0.x tables; typed handle registry; three-axis capability descriptors.

**Execution checklist:**

- [ ] Implement typed handles containing owner instance, object type, generation, parent, state, and thread policy; reject stale, cross-instance, wrong-type, and parent-closed use.
- [ ] Implement retained Error objects with stable result/domain/detail, UTF-8 message, schema-tagged details, context identities, cause retention, and allocation-failure fallback.
- [ ] Catch every C++ exception at every function-table entry and map it without exposing exception type, private address, or uncontrolled path.
- [ ] Map HO.1/HO.2 stability/maturity/runtime semantics to structured capability descriptors; verify required/optional negotiation and dependency closure.
- [ ] Implement the monotonic operation state machine, wait timeout, pause/resume where supported, cancellation request/terminal result, and device-loss terminal mapping.
- [ ] Implement bounded per-instance event queues with monotonic sequence, diagnostic coalescing policy, explicit gap records, and queryable terminal object state.
- [ ] Stress concurrent manifest/table reads and declared handle-thread policies; use deterministic races for cancel/complete, close/wait, and event overflow.
- [ ] Verify capability absence never silently selects a weaker semantic and Research capabilities cannot be enabled by production defaults.

**Completion evidence:** lifecycle/leak/race sanitization available on the Windows toolchain is clean; all structured failure and negotiation fixtures match between direct interface calls and registry expectations.

## 7. PB.4 — Immutable frames and Windows local worker

**Dependencies:** PB.3, existing framebuffer/AOV and HR.0/HO.1 measurement semantics.

**Outcome:** A real worker uses only the loader ABI; a conformance frame source proves immutable budgeted frame transfer through verified local IPC; worker failure/restart leaves no dangling client memory. PB.5 replaces the conformance source with a public native-scene/session workflow.

**Files:** `frame_adapter.cpp`; all `apps/ure_worker` files; `ure_frame_candidate.fbs`; `test_frame_leases.cpp`, `test_worker_protocol.cpp`, and `test_worker_crash.cpp`.

**Interfaces produced:** Frame Candidate 0.x table; frame/plane schemas; worker handshake/control protocol; shared-blob lease; worker process manifest.

**Execution checklist:**

- [ ] Implement frame snapshots that copy or retain a bounded source image so later production cannot mutate a live frame; use a conformance-only deterministic producer excluded from runtime packages until PB.5 connects the real session path.
- [ ] Implement retained-frame and retained-byte budgets, explicit `Backpressure`, checked map/unmap/copy state, stride/extent validation, and release accounting.
- [ ] Map plane semantics to HO.1 observable/unit/measure/time and HR.0 provenance identities; keep complex/Jones/Stokes/spectral/statistical data out of legacy AOV enums.
- [ ] Implement same-user Named Pipe creation with `PIPE_REJECT_REMOTE_CLIENTS`, bounded messages, handshake version/capability/registry checks, and no socket/network initialization.
- [ ] Launch the worker in a kill-on-close Job Object and load the runtime DLL only through the two loader symbols.
- [ ] Transfer large frames/blobs through inherited or `DuplicateHandle` file mappings with checked offset/length/digest/access/lease identities.
- [ ] Verify every FlatBuffer before allocation or dispatch and enforce nesting/table/vector/string/message/blob/frame limits.
- [ ] Kill the worker during queueing, rendering, frame mapping, and shutdown; verify terminal `WorkerLost`, invalid mappings, no orphan process, no ID reuse, and clean restart identity.
- [ ] Compare direct ABI and worker results/errors/events/frame metadata for the same deterministic mock and renderer fixtures.

**Completion evidence:** local worker requires no firewall permission, opens no network endpoint, survives malformed clients, enforces frame backpressure/lifetimes, and has semantic parity with the in-process path.

## 8. PB.5 — Native full-scene boundary and revisioned replacement

**Dependencies:** PB.4 and Phase Q native-scene validation/serialization.

**Outcome:** External clients can validate, load, bind, render, acquire frames, and replace complete native scenes through memory/file/package blobs without seeing `SceneIR`; the PB.4 conformance-only frame source is no longer needed by product paths.

**Files:** `scene_adapter.cpp`, `session_adapter.cpp`, native scene payload schemas/registry entries, `test_scene_boundary.cpp`, and standalone external-client scene fixtures.

**Interfaces produced:** Scene and Session Candidate 0.x tables; native blob descriptor; scene validation result; immutable scene revision descriptor; objective request envelope.

**Execution checklist:**

- [ ] Accept `.ure`, `.urescene`, `.urepkg`, and equivalent memory blobs only through declared schema ranges and existing strict Phase Q validation.
- [ ] Bind blob digest, semantic digest, source schema, resource manifest, selected package scene, and accepted monotonic revision into the public descriptor.
- [ ] Implement atomic full-scene replacement with old scene retention on failure, structured loss/warning propagation, and explicit accumulation reset reason.
- [ ] Lower versioned objective envelopes and generic time/memory/sample/output/determinism policies to current session/automatic-plan semantics without exposing integrator enums.
- [ ] Connect real session progress and framebuffer snapshots to the PB.4 operation/event/frame interfaces and prove the product worker contains no conformance-only frame command.
- [ ] Enforce content, resource, decompression, nesting, object-count, and memory budgets before renderer allocation.
- [ ] Test malformed/corrupt packages, unsupported schemas, missing resources, duplicate package scene ambiguity, budget exhaustion, device loss, and replacement during active work.
- [ ] Compare direct ABI and worker scene revision/digest/error/session/frame behavior on the same maintained native fixtures.

**Completion evidence:** full-scene submission is a complete permanent fallback; validation and replacement are atomic; no internal scene layout or transient index crosses the boundary.

## 9. PB.6 — Persistent UUID transactions and canonical camera extension

**Dependencies:** PB.5 and the Phase Q schema migration/tooling boundary.

**Outcome:** Common editor changes use stable semantic identities and atomic base-revision transactions, while unsupported changes report full reload or rejection explicitly.

**Files:** new versioned native-scene UUID fields/schemas and migration tool updates; `transaction_adapter.cpp`; `ure_scene_transaction_candidate.fbs`; `test_scene_transactions.cpp`; Phase Q schema baselines and fixtures.

**Interfaces produced:** Core transaction envelope; `ure.scene.camera.v1`, `ure.scene.transform.v1`, and bounded baseline edit schemas; commit result with strategy/reset/rebuilt-resource details.

**Execution checklist:**

- [ ] Add a canonical RFC 9562 UUID byte field to editable native objects, preserve legacy source IDs as non-authoritative aliases, migrate deterministically, and reject duplicates.
- [ ] Freeze UUID byte/text conversion, save/load/package/undo/replay preservation, and generation policy with cross-language golden fixtures.
- [ ] Implement transaction UUID, base scene revision, ordered schema-tagged operations, required capabilities, and client metadata.
- [ ] Validate all operations and resources before apply; guarantee rollback of scene state, renderer state, revision, and accumulation on any rejection.
- [ ] Return `HotUpdate`, `PartialRebuild`, `FullReload`, or `Rejected` plus exact reset reason, rebuilt UUIDs/resources, warnings, revision, and semantic digest.
- [ ] Reject stale base revisions without automatic merge and verify concurrent-client conflict/retry behavior.
- [ ] Implement the camera extension with one right-handed world transform and one projection truth per schema; convert look-at/FOV authoring conveniences before submission.
- [ ] Exercise transform, camera, material reference/payload, mesh replacement, add/remove, visibility, light, and environment schemas only where current semantics are exact; capability-gate or full-reload unsupported cases.
- [ ] Verify transaction replay and full-scene replacement produce the same semantic digest and frame identity for deterministic fixtures.

**Completion evidence:** no public edit uses vector indices; revision conflicts and rollback are deterministic; canonical camera fields cannot disagree; full reload remains explicit and correct.

## 10. PB.7 — Mixed-version, security, packaging, and external-client closure

**Dependencies:** PB.6.

**Outcome:** Candidate behavior is proven across versions, processes, malformed inputs, package boundaries, worker failures, and an independently built client.

**Files:** all remaining `tests/contract` and `tests/fixtures/contracts/old_clients` paths; SDK/runtime package CMake; `scripts/run_phase_pb_validation_suite.ps1`; integration guide and compatibility report templates.

**Interfaces produced:** `ure.phase_pb.validation.v1` report; candidate SDK/runtime packages; compatibility matrix; support-policy candidate.

**Execution checklist:**

- [ ] Retain compiled clients from every published candidate baseline with source, SDK digest, compiler identity, expected capability set, and content-digested manifest.
- [ ] Run old compiled clients against the current runtime and the current client against every retained supported runtime; verify table-size bounds and required/optional negotiation.
- [ ] Run worker golden messages across supported minor combinations, unknown optional fields, missing required capabilities, registry mismatch, corrupt/truncated/oversized payloads, and shared-memory misuse.
- [ ] Fuzz loader headers/chains, registry input, FlatBuffers messages, scene blobs, transaction payloads, handle lifecycle, frame mapping, and cancellation/crash races under fixed corpus/budgets.
- [ ] Package runtime and SDK independently; build `tests/contract/external_client` in a clean out-of-tree directory using only installed headers, schemas, libraries, worker, and fixtures.
- [ ] Execute external-client E2E: manifest, negotiation, scene load, objective session, progressive operation/events, immutable frame copy/map, camera transaction, revision conflict, cancellation, worker crash/restart, and full-reload fallback.
- [ ] Validate the complete Public Interaction Surface Ledger against maintained adapter/client conformance results: native, CLI render, pyure, USD/Hydra and any installed/farm/provider surface must be canonical, migrated, explicitly internal, or explicitly excluded with no unresolved bypass.
- [ ] Inspect process/network state during the full suite to prove no listener/firewall request, ambient plugin/script/solver/model discovery, or orphan worker.
- [ ] Generate `ure.phase_pb.validation.v1` with source/runtime/worker/registry/package digests, ABI/export/layout results, compatibility matrix, fuzz corpus identity, behavioral gates, and complete maintained CTest result.
- [ ] Run the suite from a clean tree and verify the report schema and digest are deterministic apart from declared timing/environment fields.

**Completion evidence:** all supported candidate combinations and standalone package consumers pass; security/lifecycle failure classes are explicit; the candidate support promise is narrow, auditable, and implementable.

## 11. PB.8 — Freeze and stable 1.0 declaration

**Dependencies:** PB.7 green; architecture §17.2 satisfied; explicit user approval to create the stable promise.

**Outcome:** The candidate becomes the first published stable major without expanding its feature surface.

**Files:** registry/manifest version records; stable schema baselines; DLL/worker/package names; public reference documentation; support policy; old-client fixtures; root README/STATUS/PLAN/AGENTS synchronization.

**Interfaces produced:** Core ABI 1.0, Worker Protocol 1.0, Frame Schema 1.0 baseline, declared native-scene read/write ranges, initial stable extension list (which may be empty).

**Execution checklist:**

- [ ] Review every Core type/function and remove or demote anything expressible as a schema/capability/extension before freeze.
- [ ] Freeze loader exports, interface IDs/tables, enum values, layouts, semantics, registry tombstones, Windows x64 profile, worker handshake/envelope, frame lifecycle, and support window.
- [ ] Rerun PB.7 using release-named `ultrarender_runtime_1.dll` and worker packages without candidate compatibility shortcuts.
- [ ] Preserve the final Candidate client binaries as Core 1.0 old-client seeds and store content-digested ABI/protocol/registry/package manifests.
- [ ] Publish exact promises and non-promises, including optional extension availability, platform scope, scene-schema ranges, worker locality, cancellation semantics, frame limits, and research exclusions.
- [ ] Obtain explicit user approval for stable declaration after REPORT; do not infer approval from earlier implementation authorization.
- [ ] Tag/version only when separately authorized; this plan never authorizes push or public release by itself.
- [ ] Advance the root cursor to HR.3 and require future HR/HW/HD capabilities to enter through versioned stable/unstable extensions rather than Core growth.

**Completion evidence:** every architecture 1.0 gate is green; release artifacts and documentation contain one exact compatibility statement; no advanced capability was batch-promoted merely to finish PB.

## 12. Unified verification contract

The PB suite is invoked as:

```powershell
.\scripts\run_phase_pb_validation_suite.ps1 -BuildDir build_modular_x64 -Config Release
```

It MUST run, as applicable to the current PB cursor:

```text
registry lint and deterministic generation
C11/C++23/standalone installed-SDK compilation
FlatBuffers conformance and golden-message verification
Windows x64 ABI layout and exact export audit
direct C ABI lifecycle/behavior tests
worker protocol, shared-memory, crash, restart, and no-network tests
scene validation/replacement and transaction rollback/conflict tests
old-client/current-runtime and current-client/old-runtime matrix
bounded malformed-input/fuzz corpus
standalone external-client E2E
complete registered Release CTest gate
static forbidden-leak and abandoned-gui exclusion audit
```

The suite emits a versioned JSON report. Test counts are discovered from CTest and never frozen in prose. Negative fixtures must exercise real validation or lifecycle boundaries; the plan does not require ceremonial tests for deliberately absent files or symbols.

## 13. Review checklist for every PB phase

- Does the slice implement only the active PB phase and its direct prerequisites?
- Did any current internal type, enum, pointer lifetime, native handle, or array index leak into public semantics?
- Can any new Core field/function be replaced by a payload schema, capability, or extension?
- Are stability, maturity, and runtime state still independent?
- Are allocation, message, mapping, frame, queue, chain, and scene limits checked before use?
- Are exceptions contained and errors retained without losing the root cause?
- Do direct ABI and worker behavior share registry identities and semantics?
- Is full-scene replacement still complete and explicit?
- Are Research paths exact-build opt-in and absent from production defaults?
- Does the implementation ignore the abandoned repository GUI?
- Did scoped tests and the maintained Release gate pass from the recorded source state?

## 14. Documentation and commit policy

At each completed PB phase:

- update this document's phase status/evidence without rewriting future claims as implemented facts;
- update the root `PLAN.md` cursor;
- update `STATUS.md` with only observed capability and limitation changes;
- keep `README.md` concise and free of release claims before PB.8;
- update `docs/README.md` and generated public reference tables;
- record candidate breaks in `registry_compatibility.json` and retain fixtures;
- retain scope, verification, and review findings in the phase evidence;
- use the standing 2026-08-08 plan-scoped commit authorization after VERIFY/REVIEW, following the repository phase commit convention.

No PB phase authorizes push, tag, public package publication, deletion of legacy APIs, or declaration of stable 1.0 before PB.8.
