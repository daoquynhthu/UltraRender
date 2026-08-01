# UltraRender Documentation Index

Last reviewed: 2026-08-01

This index separates current references from historical records. The root `PLAN.md` is the only authoritative high-order research and production queue. `README.md` and `STATUS.md` summarize the current user-visible state; source code, CMake/CTest registration and fresh command output remain the strongest implementation evidence.

## Current project references

| Document | Purpose |
|---|---|
| [`../README.md`](../README.md) | Project entry, supported baseline and build commands |
| [`../STATUS.md`](../STATUS.md) | Current capability and limitation matrix |
| [`../PLAN.md`](../PLAN.md) | Authoritative HO/HT/HR/HW/HD queue, research maturity, dependencies and completion criteria |
| [`../AGENTS.md`](../AGENTS.md) | Agent governance and workflow |
| [`reference/Backend_API.md`](reference/Backend_API.md) | Implemented engine/session/C/Python API boundary |
| [`Spectral_Semantics_Guide.md`](Spectral_Semantics_Guide.md) | Current semantic reference for spectral quantities |
| [`Phase_Q_Native_Scene_Format.md`](Phase_Q_Native_Scene_Format.md) | Completed native scene architecture and closure record |
| [`Phase_R_P6_Mie_Volume_Resources.md`](Phase_R_P6_Mie_Volume_Resources.md) | Completed Mie resource/transport contract |
| [`Phase_R_P5_MLT.md`](Phase_R_P5_MLT.md) | Completed production PSSMLT architecture and closure gates |
| [`Phase_R_P7_Industrial_Validation.md`](Phase_R_P7_Industrial_Validation.md) | Completed Phase R industrial evidence schemas and Closure record |
| [`Phase_T_Portable_GPU_Runtime.md`](Phase_T_Portable_GPU_Runtime.md) | Completed T.0-T.11 portable-runtime contracts and validation record |
| [`Phase_V_GPU_Acceleration.md`](Phase_V_GPU_Acceleration.md) | Current GPU acceleration audit, configuration contract, risks and migration boundary |
| [`Phase_W_Wave_Optics_Audit.md`](Phase_W_Wave_Optics_Audit.md) | Current boundary between references and production wave transport |
| [`Phase_W_W5_Diffractive_Materials.md`](Phase_W_W5_Diffractive_Materials.md) | W.5 radiometric diffractive operator, Jones-table and CUDA estimator contract |
| [`Phase_W_W6_Fluorescence.md`](Phase_W_W6_Fluorescence.md) | W.6 excitation-emission resource, adjoint CUDA wavelength transport and lifetime boundary |
| [`Phase_W_W7_Partial_Coherence.md`](Phase_W_W7_Partial_Coherence.md) | W.7 cross-spectral-density, coherent-realization, generalized-ray and averaging-order reference contract |
| [`Phase_W_W9_Anisotropic_Media.md`](Phase_W_W9_Anisotropic_Media.md) | W.9 spectral dielectric-tensor, eigenmode, birefringence, dichroism and optical-activity reference contract |
| [`Phase_W_W10_Local_Fullwave_Coupling.md`](Phase_W_W10_Local_Fullwave_Coupling.md) | W.10 bounded local solver provider, evidence, binary exchange and deterministic cache contract |
| [`Phase_W_W11_Coherent_Distributed_Contract.md`](Phase_W_W11_Coherent_Distributed_Contract.md) | W.11 radiance/complex-field/mutual-intensity/coherent-realization shard semantics and merge-order contract |
| [`Phase_W_W12_Validation.md`](Phase_W_W12_Validation.md) | W.12 unified physical, API, fail-loud, static and complete CTest closure |
| [`Phase_U_U1_USD_Schema_Adapter.md`](Phase_U_U1_USD_Schema_Adapter.md) | U.1 SDK-free USD semantic snapshot, native-schema mapping and fail-loud boundary |
| [`Phase_U_U2_Hydra_RenderDelegate.md`](Phase_U_U2_Hydra_RenderDelegate.md) | U.2 optional OpenUSD `HdURE` delegate/plugin foundation and non-ready boundary |
| [`Phase_U_U3_Hydra_Mesh_RPrim.md`](Phase_U_U3_Hydra_Mesh_RPrim.md) | U.3 actual `HdMesh` RPrim, SceneIR geometry mapping, updates and rejection boundary |
| [`Phase_U_U4_Hydra_Material_Conversion.md`](Phase_U_U4_Hydra_Material_Conversion.md) | U.4 actual `HdMaterial` SPrim, MaterialGraph conversion and structured loss report |
| [`Phase_U_U5_Hydra_Progressive_Render.md`](Phase_U_U5_Hydra_Progressive_Render.md) | U.5 actual camera/render-buffer/render-pass bridge to CUDA `RenderSession` progressive execution |
| [`Phase_U_U6_USDA_Export.md`](Phase_U_U6_USDA_Export.md) | U.6 deterministic native-scene to USDA projection, strict loss policy and actual OpenUSD validation |

## Completed-phase evidence records

These documents preserve design rationale and closure evidence. Dates, test counts and “next step” statements inside them are snapshot-specific.

| Document | Scope |
|---|---|
| [`Phase_E_Spectral_Architecture.md`](Phase_E_Spectral_Architecture.md) | Phase E spectral architecture history and final invariants |
| [`Phase_L_Completion_Audit.md`](Phase_L_Completion_Audit.md) | Phase L closure evidence |
| [`Phase_R_P4_Specular_Manifold.md`](Phase_R_P4_Specular_Manifold.md) | R-P4 bounded manifold estimator and statistical closure evidence |

## Historical and experimental archives

Files under [`archive/`](archive/) are retained for historical context and are not maintained as current specifications. This includes former root handover/roadmap/progress documents and early physics/acoustic assessments.

[`archive/Legacy_Construction_PLAN_2026-08-01.md`](archive/Legacy_Construction_PLAN_2026-08-01.md) is the frozen former root roadmap for completed Q/R/T/V/W/U construction and the deferred Phase X proposal. It is useful for design history but cannot override the current `HO.0 — 能力债务普查与研究基线` cursor.

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
