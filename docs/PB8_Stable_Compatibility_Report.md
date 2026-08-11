# PB.8 Core 1.0 Freeze and Compatibility Report

Document status: PB.8 complete; Core ABI 1.0 and Worker Protocol 1.0 declared on 2026-08-11.

## Freeze review outcome

The pre-release audit retained 140 Core registry identities. Each is covered exactly once by `contracts/stability/core_1_0_freeze_review.json` with an extension-impossibility rationale and evolution rule.

Eleven pre-release Core IDs were tombstoned before the 1.0 freeze. Telemetry, spectral and Stokes planes, four renderer update strategies, and the entire UUID transaction request/result/event/operation surface moved to `UnstableExtension` identities. `ure_scene_interface_t` now ends at `get_revision`; `apply_transaction` is discoverable only through `URE_INTERFACE_SCENE_TRANSACTION`. The initial StableExtension list is empty.

The runtime validates caller structures against frozen Core 1.0 prefix sizes rather than the implementation's current `sizeof(T)`. This permits future tail extension without rejecting a 1.0 client or writing beyond the caller's declared prefix. The C and C++ Windows x64 layout gate freezes only Core structures and tables; unstable transaction layouts remain runtime-inspectable but are excluded from the Core ABI baseline.

## Compatibility policy

The first stable major has no prior stable runtime. The current-client/prior-stable-runtime matrix row is therefore `NotApplicable`; no Candidate runtime is relabeled or accepted through a compatibility shortcut. Five PB.2-PB.6 binaries remain immutable Candidate evidence only.

A compiled Core 1.0 seed uses the final PB.7 value/table prefixes, excludes the removed Candidate-only Scene tail, negotiates major 1, and queries every Core table. It is the oldest client seed that every future `runtime_1` build must retain. Starting with the first post-1.0 runtime release, the matrix must also retain the preceding stable runtime and run the then-current client against it.

## Calling modes and image E2E

The release package gate builds clients outside the repository product targets using only staged package contents:

| Calling mode | Boundary exercised | Real image artifacts |
|---|---|---|
| C11 in-process dynamic loading | Two exports and all 39 Core table functions | `direct_map.pfm`, `direct_copy.pfm` |
| C++23 in-process unstable extension | Separate UUID transaction table, conflict/fallback/replay | `transaction_replay.pfm`, `transaction_replace.pfm` |
| Out-of-process local worker | Handshake, scene/render, read-only shared memory, lease release, crash/restart/shutdown | `worker_first.pfm`, `worker_restart.pfm` |

Each image must contain finite, nonzero, spatially nonuniform RGB. Map/copy and worker restart pairs also require exact evidence/content identity. The mock worker remains conformance-only and is not accepted as rendering evidence.

## Exact scope

The stable platform, support window, cancellation behavior, frame-limit policy, native-scene read/write ranges, worker locality, empty StableExtension list, and research/internal non-promises are normative in `Public_API_Support_Policy.md`. The declaration is limited to Core ABI 1.0 and Worker Protocol 1.0; it is not an UltraRender 1.0 product release. The scoped annotated tag `public-boundary-v1.0.0` records this declaration commit. Package publication, push, and public distribution remain unauthorized.

## Verification record

The complete Windows x64 Release build succeeds and all 101 registered CTest entries pass. The `ure.phase_pb.validation.v2` report validates against its checked-in JSON schema and binds the source tree, release packages, registry, ABI, compatibility matrix, fuzz/security corpus, boundary ledger, six image files, and complete CTest inventory. The registry semantic digest is `c358276424a2cdc71cfefc6edac290ee78fa75a2bf918edecb8f37f4d991af42`.

The verified runtime SHA-256 is `4517f50335437f9c800ad5d3cf55a1ebd0582ea37d8c4b3be8c35b7999cd9817`; the worker SHA-256 is `7e5468fa462f769c22f8225e3b0a15bfbc32c139ba30bbf7f78522735b156a97`. Direct map/copy and worker first/restart images share the expected content hash `357fb5754224969bebb77a4bf237627b5ba7d2c5e9e153ca5cc8822a4c519394`; transaction replay/replacement images share `11dbc09b94b53128cc302c861f04f51de0facd84d155d43e4cd88d5d81a00ee9`. The report file carries package, semantic-digest, and environment-specific identities without making this document self-referential.

Self-review found no remaining duplicate public authority, Core transaction leak, current-size table check, network import/listener, ambient discovery path, Candidate compatibility shortcut, or unclassified maintained interaction surface. The user explicitly approved the stable declaration after this verification record; the root cursor advanced to HR.3. Public package distribution and push remain outside the declaration.
