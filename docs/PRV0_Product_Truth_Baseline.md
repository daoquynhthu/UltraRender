# PRV.0 Product Truth Baseline

Last reviewed: 2026-08-11

## Scope

PRV.0 establishes the machine-readable starting point for the `UltraRender_preview` integration route. It does not add product execution features and does not declare a Preview release. The preserved PB interaction ledger remains the declaration record for Core ABI 1.0 and Worker Protocol 1.0; the PRV.0 ledger separately classifies product execution ownership and convergence debt.

The baseline has four authorities:

- [`../contracts/product_closure_ledger.json`](../contracts/product_closure_ledger.json) classifies maintained entry points and capabilities by owner, current call chain, closure level, bypass, semantic risk, Preview disposition and migration phase.
- [`../contracts/product_semantic_audit.json`](../contracts/product_semantic_audit.json) records maintained Objective, configuration, native-block, backend, output and reconstruction semantics. Source digests make parser or consumer changes require an audit update.
- [`../contracts/product_e2e_scenarios.json`](../contracts/product_e2e_scenarios.json) retains the twelve risk-based product scenarios and binds their source identities, capability requirements, artifact schemas, metrics and permitted failure classes.
- [`reports/ure_preview_baseline_v1.json`](reports/ure_preview_baseline_v1.json) aggregates the live test inventory, current image evidence, backend inventory, Hydra state and the three contracts above.

## Findings

The initial ledger contains 44 entries: 2 Contract, 18 ComponentExecutable, 15 RendererIntegrated, 7 ClientReachable and 2 ProductE2E. The two ProductE2E entries are only the bounded Core/Worker PB fixture paths backed by six retained external-client PFM artifacts. They do not promote any of the twelve Preview product scenarios to ProductE2E.

At the PRV.0 snapshot, CLI render, the installed C++ session, Hydra, legacy C and legacy Python were product-service bypasses. PRV.1 subsequently converged CLI render through `ure_client`; the other legacy paths remain tracked by the live ledger. The retained CLI baseline image is historical evidence of the former RendererIntegrated direct path, not current canonical execution evidence.

The semantic audit records 25 maintained semantics. At the initial baseline, 12 are accepted without execution and 4 execute with semantic debt. The highest-priority findings are:

- Core Objective output, determinism, usage and latency fields are accepted but not executed;
- Core sample budget and Frame sample accounting currently describe render-pass iterations rather than a canonical sample-range identity;
- CLI `device_ids`, Russian roulette probability, tone map, SPD search paths and four advanced MLT fields are parsed without a complete consumer path;
- native procedural, resource, solver and simulation blocks are preserved and validated, while current renderer paths commonly extract only `archive.scene`;
- portable backend/provider availability, current output and reconstruction components do not yet imply a complete product scene path.

These are baseline facts, not permission to delete semantics. Each item has a target disposition and PRV migration phase. Preview completion permits only Executed, Rejected, PreservedForTooling or FrozenResearch behavior.

## Retained product scenarios

The manifest fixes diffuse, textured PBR, glass/SDS, small light, high occlusion, volume/Mie, large spectral resources, diffractive materials/camera, fluorescence, procedural packages, dynamic mutation and bounded simulation. Coverage is pairwise and risk-based across scene, material, transport, backend, session, scale, output and client dimensions. It intentionally avoids an unmaintainable Cartesian matrix.

Several current sources are retained component fixtures or deterministic generators rather than product-ready scene packages. Their `current_closure` records that limitation. Later PRV phases must replace or promote evidence only after the canonical product service produces the declared artifact and passes the scenario metrics.

## Historical validation

The static gate validates required coverage, unique execution-authority claims, source anchors and hashes, valid owners/dispositions, honest bypasses, non-expired migration gates, external evidence for ProductE2E claims and the GUI exclusion. Negative fixtures prove that each of those boundaries fails closed.

The committed baseline report and its legacy-CLI image are read-only historical evidence. Current validation checks that the report remains internally valid while the live ledger advances; it does not rerun the retired direct-render CLI path.

```powershell
pwsh -NoProfile -File scripts/check_phase_prv0_static.ps1 -RepoRoot .
ctest --test-dir build_modular_x64 -C Release -R "^test_phase_prv0_baseline$" --output-on-failure
```

The original baseline generator excluded its output file from the source-tree digest, normalized the CTest inventory, omitted dynamic available-memory values from backend identity and verified live image bytes against the retained PB report. `preview_release_declared` remains structurally fixed to `false`.

The generated report's `source.dirty` field honestly records whether it was produced before a commit. Its source commit and content digests remain sufficient to identify the exact audited inputs; a clean-tree reproduction may regenerate the report after checkout without changing the semantic contracts.

## Closure evidence

The PRV.0 closure run completed a Windows Release build and 103/103 registered tests, including the live CLI image, byte-deterministic baseline regeneration and negative ledger fixtures. A separate CUDA-off Windows build passed 33/33 host/contract tests, matching the hosted non-GPU root-test scope. The most recent hosted `main` CI run at the time of closure was green at commit `a87bfbc71b9c08c3d98977e1c02e837876b05ed1`; the PRV.0 commit itself is not described as hosted-validated until it is separately pushed and executed by GitHub Actions.
