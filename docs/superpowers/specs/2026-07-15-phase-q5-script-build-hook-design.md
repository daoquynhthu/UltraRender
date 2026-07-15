# Phase Q.5 Script Build Hook Design

> Archive status: historical design record. Phase Q is complete; use `PLAN.md`, `STATUS.md`, and current source/tests for present state.

## Decision

URE owns a host-neutral, deny-by-default build coordinator. It never embeds or launches Python, Lua, WASM, or a native interpreter. An explicitly supplied `IScriptSandboxRunner` is the isolation boundary and must attest its runtime, policy, and dependency identities. Runtime scene loading, sessions, and GPU code never execute scripts.

## Manifest and execution contract

`ScriptBuildManifest` is a typed, versioned source object. It declares the script content hash, exact runtime identity and executable/module hash, dependency-lock hash, runner and sandbox-policy hashes, content-addressed read-only inputs, named outputs, and hard duration/memory/log/output limits. Inputs and outputs use virtual names; ambient paths, environment inheritance, network access, subprocesses, wall-clock time, and nondeterministic entropy are forbidden by policy.

Execution is disabled unless `ScriptBuildOptions::enabled` is true. The coordinator validates the manifest and runner capabilities before dispatch. The runner receives owned input bytes, never host paths. Its result is untrusted: URE rejects missing, extra, duplicate, oversized, wrong-kind, or hash-mismatched outputs and recomputes every output hash. Generated scene artifacts remain build outputs; Q.5 does not automatically merge or load them.

## Identity and invalidation

The source hash covers the canonical manifest excluding produced data. The cache key covers source hash, script/runtime/dependency-lock/runner/policy identities, and ordered input hashes. The output hash covers the cache key, attestation, exit status, and ordered verified output descriptors and hashes. Provenance contains only semantic identities and hashes; timestamps and host-local paths are excluded.

Any changed identity or input invalidates the cache. A successful result is cacheable only when runner attestation exactly matches the requested identities and every declared output verifies.

## Serialization and features

The schema identity is `ure.script-build/1.0`, FlatBuffers file identifier `URSB`, and native chunk kind 18. A scene carrying a required script manifest declares required feature `build.script`. Unknown required script chunks fail through the normal native container gate; optional unknown payloads remain preservable.

## Diagnostics and tests

Failures use stable `URE-Q5-*` diagnostics. Host tests cover default denial, opt-in, policy/attestation mismatch, limits, undeclared outputs, deterministic cache/provenance hashes, and successful execution through a deterministic recording runner. The test runner is not represented as a security sandbox.

## Rejected alternatives

Direct interpreter spawning provides no credible filesystem/network/process isolation. A Windows-only restricted-token implementation would make a platform mechanism look like a complete sandbox. A WASM-only design prematurely fixes the language and runtime dependency. Concrete isolated runners remain separately deployable implementations of the stable interface.
