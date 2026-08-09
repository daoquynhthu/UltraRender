# PB.7 Candidate Compatibility and Validation Record

Document status: Candidate 0.1 closure record. No stable compatibility promise.

## Declared range

PB.7 retains compiled Windows x64 C11 clients from PB.2 through PB.6. Each fixture binds its source and binary digest, historical SDK-header digest, source commit, compiler/SDK identity, expected phase-known interfaces and expected negotiation behavior. Those clients exercise prefix-bounded interface discovery against the current runtime.

The reverse supported-runtime range contains only the current PB.7 runtime. Earlier Candidate runtime binaries are not supported releases and are not silently treated as such. Worker protocol support is the exact Candidate 0.1 protocol/core/frame combination with the exact registry digest.

## Evidence map

| Evidence | Contract |
|---|---|
| `test_candidate_compatibility` | Frozen PB.2-PB.6 binaries, digests, table prefixes, required/optional discovery, current-client/current-runtime reverse row |
| generated mock golden messages | protocol minor behavior, unknown optional field, missing optional/required capability, registry mismatch, malformed/truncated/oversized payload, lifecycle/backpressure/gap/device-loss/crash outcomes |
| `test_worker_protocol`, `test_worker_crash` | product/conformance parity, immutable lease identity/digest, misuse rejection, budgets, crash phases and restart identity |
| `test_pb7_fuzz` | fixed-seed bounded loader-chain, scene-blob, transaction-payload and invalid-handle corpus |
| `test_worker_runtime_security` and `test_worker_security` | no TCP/UDP endpoint or network import, no ambient discovery, package exclusion and clean worker exit |
| `test_external_client_package` | independently staged SDK/runtime packages and clean out-of-tree direct, transaction and worker consumers |
| `test_public_boundary_audit` | complete interaction-surface ledger with zero unresolved classification, duplicate authority, bypass or forbidden inspection |
| `scripts/run_phase_pb_validation_suite.ps1` | deterministic `ure.phase_pb.validation.v1` aggregation and complete CTest gate |

## Interpretation

Passing PB.7 means the currently declared Candidate behavior is reproducible and its intentionally narrow support range is implementable. It does not mean that an earlier Candidate runtime is supported, that Candidate schemas cannot change, or that the project has declared Core ABI 1.0 / Worker Protocol 1.0. Those decisions remain exclusively in PB.8 and require explicit approval.
