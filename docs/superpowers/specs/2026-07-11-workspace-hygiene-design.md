# UltraRender Workspace Hygiene Design

Date: 2026-07-11

## Objective

Restore a clean, reproducible workspace without changing renderer behavior. Preserve the active build baseline, promote the existing generated glTF scenes and their generators into maintained end-to-end assets, and align governance documents with the repository's actual state.

## Scope

### Remove

- Legacy or inactive generated directories: `build/`, `build_modular/`, `cmake-build-debug/`, and `output/`.
- Local IDE state: `.idea/` and `.vscode/`.
- The empty obsolete root `src/` directory.
- Temporary state produced by interrupted verification inside the active build tree, if it is demonstrably generated and safe to recreate.

### Preserve

- `build_modular_x64/` as the active build baseline.
- All tracked source, tests, documentation, and third-party dependencies.
- Historical project documents, with explicit archival positioning where they are no longer authoritative.
- All unrelated user work.

### Promote to maintained assets

- `scenes/cornell_box.gltf`
- `scenes/showcase.gltf`
- `scenes/test_plane.gltf`
- `scenes/test_plane_sphere.gltf`
- `tools/gen_cornell_box.py`
- `tools/gen_showcase.py`
- `tools/gen_test_scenes.py`

The generator scripts are the reproducible source path. The checked-in glTF files are stable, directly runnable fixtures for future end-to-end image rendering and regression gates.

## Documentation Changes

- Update `AGENTS.md` to reflect Phase M completion, the active `build_modular_x64/` build tree, the current 25-test CTest inventory, and the absence of a root source tree.
- Update current-state sections in `PLAN.md` without rewriting or deleting historical implementation records.
- Align `README.md` with the current construction cursor: R-P6 Mie and volume phase resources, followed by Phase Q.
- Classify `HANDOVER.md`, `STATUS.md`, and `progress.md` as historical references when their contents are stale; keep useful history instead of deleting it.
- Add scene documentation describing purpose, generator command, expected use, and ownership of each promoted fixture.

## Ignore Policy

- Ignore `.idea/` and `.vscode/` completely.
- Keep generated build and output directories ignored.
- Do not ignore the promoted scene fixtures or generator scripts.

## Historical Codex Data

Only project-specific facts from `J:\.codex` may inform cleanup decisions. Stale ambient suggestions must not be copied into project planning. The Phase M rollout summary may be used as corroborating evidence, but the repository and current `PLAN.md` remain authoritative.

## Verification

1. Confirm all intended tracked and promoted files remain present.
2. Confirm removed directories are absent and `git status` contains only intentional project changes.
3. Run every scene generator in an isolated temporary directory and compare generated glTF semantics or bytes with the checked-in fixtures, according to generator determinism.
4. Configure or refresh `build_modular_x64/` as needed and run the complete build.
5. Run all 25 CTest entries with failure output enabled.
6. Run static audit scripts relevant to current Phase L/R architecture.
7. Run `git diff --check` and review the final diff for accidental scope expansion.

An environment-only Python temporary-directory failure must be reported separately and must not be misrepresented as a renderer failure or a green gate.

## Non-Goals

- No renderer, physics, material, integrator, or wave-optics behavior changes.
- No duplicate accelerator-header cleanup or backend abstraction work.
- No new Phase R-P6 implementation.
- No commit or push.

## Success Criteria

- The working tree has no unexplained untracked files.
- Only `build_modular_x64/` remains as the active generated build tree.
- The seven scene assets are tracked-ready, documented, and reproducible.
- Governance and user-facing documentation agree on the active phase and verification commands.
- Build and test results are reported from fresh verification evidence.
