# UltraRender Documentation Index

Last reviewed: 2026-07-26

This index separates current references from historical records. `PLAN.md` is the only authoritative construction queue. `README.md` and `STATUS.md` summarize the current user-visible state; source code, CMake/CTest registration and fresh command output remain the strongest implementation evidence.

## Current project references

| Document | Purpose |
|---|---|
| [`../README.md`](../README.md) | Project entry, supported baseline and build commands |
| [`../STATUS.md`](../STATUS.md) | Current capability and limitation matrix |
| [`../PLAN.md`](../PLAN.md) | Authoritative phase queue, dependencies and completion criteria |
| [`../AGENTS.md`](../AGENTS.md) | Agent governance and workflow |
| [`reference/Backend_API.md`](reference/Backend_API.md) | Implemented engine/session/C/Python API boundary |
| [`Spectral_Semantics_Guide.md`](Spectral_Semantics_Guide.md) | Current semantic reference for spectral quantities |
| [`Phase_Q_Native_Scene_Format.md`](Phase_Q_Native_Scene_Format.md) | Completed native scene architecture and closure record |
| [`Phase_R_P6_Mie_Volume_Resources.md`](Phase_R_P6_Mie_Volume_Resources.md) | Completed Mie resource/transport contract |
| [`Phase_R_P5_MLT.md`](Phase_R_P5_MLT.md) | Completed production PSSMLT architecture and closure gates |
| [`Phase_R_P7_Industrial_Validation.md`](Phase_R_P7_Industrial_Validation.md) | Completed Phase R industrial evidence schemas and Closure record |
| [`Phase_T_Portable_GPU_Runtime.md`](Phase_T_Portable_GPU_Runtime.md) | Current portable-runtime ledger, completed T.0-T.6 contracts and T.7 Vulkan foundation boundary |
| [`Phase_W_Wave_Optics_Audit.md`](Phase_W_Wave_Optics_Audit.md) | Current boundary between references and production wave transport |

## Completed-phase evidence records

These documents preserve design rationale and closure evidence. Dates, test counts and “next step” statements inside them are snapshot-specific.

| Document | Scope |
|---|---|
| [`Phase_E_Spectral_Architecture.md`](Phase_E_Spectral_Architecture.md) | Phase E spectral architecture history and final invariants |
| [`Phase_L_Completion_Audit.md`](Phase_L_Completion_Audit.md) | Phase L closure evidence |
| [`Phase_R_P4_Specular_Manifold.md`](Phase_R_P4_Specular_Manifold.md) | R-P4 bounded manifold estimator and statistical closure evidence |

## Historical and experimental archives

Files under [`archive/`](archive/) are retained for historical context and are not maintained as current specifications. This includes former root handover/roadmap/progress documents and early physics/acoustic assessments.

Files under [`superpowers/specs/`](superpowers/specs/) and [`superpowers/plans/`](superpowers/plans/) are archived design and execution records. They intentionally preserve proposed paths, old test counts and unchecked implementation steps. They do not override the completed implementation or current PLAN cursor.

## Scene documentation

[`../scenes/README.md`](../scenes/README.md) lists maintained glTF fixtures. Native validation fixtures live under `tests/assets/native_scene/`, with Q.12 coverage indexed by `tests/assets/native_scene/q12_validation/fixture_manifest.json`.

## Maintenance rules

- Keep only `README.md`, `STATUS.md`, `PLAN.md` and `AGENTS.md` as Markdown entry/governance files at repository root.
- State whether a document is current, a completed-phase record or an archive.
- Avoid promotional superlatives, unqualified “production-ready” claims and unsupported performance statements.
- Qualify large-scale claims precisely; for example, a million-bin resource domain is not a million-lane ray payload.
- Describe enums, schemas, host oracles and rejection tests as contracts unless an executable runtime path is also implemented and verified.
- Use dated test counts as snapshots and point readers to `ctest -N` for the live inventory.
- Update links when files move; do not retain dead root-level aliases merely for convenience.
