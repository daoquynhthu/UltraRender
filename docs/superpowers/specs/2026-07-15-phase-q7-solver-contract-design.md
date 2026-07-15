# Phase Q.7 Solver and Integrator Contract Design

> Archive status: historical design record. Phase Q is complete; use `PLAN.md`, `STATUS.md`, and current source/tests for present state.

## Decision

Q.7 adds `URSC`, schema identity `ure.solver-contract/1.0`, and core chunk kind 20. It is a backend-neutral source request compiled into `RenderConfig`; it never serializes CUDA launch state or silently substitutes an unsupported solver.

The contract separates requested semantics, required capabilities, and non-semantic execution hints. Integrator modes include the current wavefront, path-guided, ReSTIR DI, specular-manifold, and MLT modes plus reserved BDPT and VCM requests. Reserved modes are representable but fail capability negotiation until a provider explicitly supports them.

## Capability negotiation

`SolverCapabilityRegistry` declares supported integrator modes, samplers, wave modes, acceleration providers, execution backends, coherent merge modes, and named feature versions. Validation occurs before configuration compilation. Every requested unsupported mode, backend, acceleration provider, wave feature, or required validation metric produces `URE-Q7-*` diagnostics. There is no fallback from coherent to radiometric, hardware RT to software BVH, or unbiased to biased reuse.

## Contract domains

The request owns spectral domain/packet/sampling and resident budget; integrator mode/sampler/quality/bias declaration; path guiding, ReSTIR DI, specular manifold, and MLT parameters; wave-optics mode and feature switches; backend and acceleration requirements; coherent/distributed merge mode; and required validation metrics/tolerances.

Backend workgroup, rays-per-block, device count, and samples-per-pass are explicit hints excluded from semantic identity. Physical and estimator settings remain semantic.

## Compilation

`compile_solver_contract()` first validates schema and capabilities, then produces a `RenderConfig`. It rejects currently unrepresentable BDPT/VCM, coherent-field execution, unsupported acceleration providers, and unsupported reuse guarantees before session creation. Supported current modes map bit-for-bit to existing typed configuration fields.

## Serialization and archive ownership

Binary `URSC` and canonical JSON projections have equal semantic hashes. A scene carrying the chunk declares required `ure.render.solver`. Native archives retain the typed contract. Unknown enum values, invalid bounds, contradictory flags, and missing feature declarations fail loudly.

## Verification

Tests cover all currently supported mappings, BDPT/VCM representation with capability failure, coherent/radiometric non-degradation, biased ReSTIR declaration, backend/acceleration rejection, execution-hint hash independence, binary/text scene roundtrip, and retained Q.3 compilation fixtures.
