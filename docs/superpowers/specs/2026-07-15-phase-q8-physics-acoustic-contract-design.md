# Phase Q.8 Physics and Acoustic Open Contract Design

> Archive status: historical design record. Phase Q is complete; use `PLAN.md`, `STATUS.md`, and current source/tests for present state.

Q.8 adds `URPC`, schema identity `ure.simulation-contract/1.0`, and core chunk kind 21. The contract describes solver identities and versions, supported physics/acoustic domains, time sampling, owned resources, coupling channels, and versioned extensions. It does not freeze the current demo solver as the permanent model.

Physics domains are rigid body, soft body, fluid, and extension. Acoustic domains are modal, geometric ray, wave, and extension. A domain request declares requirement level, solver identity/version, resource ownership, and migration policy. Unknown required solvers, domains, extensions, or coupling semantics fail capability negotiation; optional/advisory records are retained with diagnostics.

Time sampling uses rational ticks and a positive step, with an explicit interval and synchronization epoch. Coupling channels are directed and typed; they declare source/target domains, semantic, rate, latency, resource ownership, and whether feedback is permitted. Cycles require every participating channel to opt into feedback.

Native SceneIR `PhysicsConfig` remains a supported compiled subset. `compile_simulation_contract()` maps compatible rigid/fluid timing to it and returns retained acoustic/open-domain requests separately. Unsupported required semantics fail before simulation/session creation rather than degrading.

Binary and canonical text projections have the same semantic hash. A scene carrying the chunk declares required `ure.scene.simulation`. Tests cover subset compilation, future solver preservation, required/optional behavior, coupling cycles, resource ownership, migration/version gates, and archive roundtrip.
