# Archived UltraRender Development Handover

> Archive status: retained for historical context. This file is not maintained as a current handover source. Use the repository `README.md`, `STATUS.md`, `AGENTS.md`, and the authoritative cursor in `PLAN.md`.

Last reviewed: 2026-07-15

This is a practical onboarding note for continuing development. It is not a substitute for `AGENTS.md` governance or the authoritative queue in `PLAN.md`.

## Read order

1. Read `AGENTS.md` in full.
2. Search `PLAN.md` for `当前游标` and read only the active phase, its dependencies, and directly relevant status sections.
3. Inspect `git status --short --branch` before editing.
4. Confirm the `build_modular_x64` build tree exists and run a relevant test before relying on it.
5. Use `docs/README.md` to distinguish current technical references from archived plans.

Do not load `PLAN.md` wholesale. Do not treat historical plan checkboxes or old CTest counts as current work.

## Current authoritative state

- Phase Q is complete.
- The construction cursor is `R-P3`.
- The remaining Phase R order is `R-P3 -> R-P4 -> R-P5 -> R-P7`.
- Phase T/V/W/U/X implementation must not be started out of order, although previously completed reference/oracle work remains in the tree.
- `main` may be ahead of its remote; inspect the actual branch state before deciding whether publication is authorized.

## Working tree and build baseline

```text
apps/ure_cli/       CLI orchestration
libs/ure_types/     backend-neutral types and contracts
libs/ure_core/      CUDA renderer and session/C ABI
libs/ure_sceneio/   native and adapter I/O
libs/ure_config/    CLI/JSON configuration
libs/ure_diag/      diagnostics
libs/ure_physics/   optional experimental physics/acoustics
pyure/              Python wrapper
tests/              GPU, host and Python tests
```

The old root `include/` and `src/` trees were removed. Do not recreate or reference them.

Maintained build command:

```powershell
.\scripts\build_x64.ps1 -BuildDir build_modular_x64 -Config Release
```

The local environment used for the current verification baseline is Windows 11, VS 2022 Build Tools, Ninja, CUDA 13.0 and an RTX 5060 Laptop GPU (CC 12.0). Heavy CUDA targets should be built serially in this environment to avoid `ptxas` host-memory pressure.

## Verification baseline

```powershell
ctest --test-dir build_modular_x64 -C Release --output-on-failure
```

At this snapshot CTest registers 37 entries. Always use `ctest -N` as the live inventory.

Long-lived gates relevant to the current architecture include:

```powershell
.\scripts\check_phase_l_static.ps1
.\scripts\check_phase_r_static.ps1
.\scripts\check_physics_optics.ps1
.\scripts\run_phase_q_validation_suite.ps1 -BuildDir build_modular_x64 -Config Release
git diff --check
```

Phase Q schema regeneration requires the pinned FlatBuffers 25.12.19 compiler. The validation suite accepts a `-Flatc` path when it is not available on `PATH`.

## Architectural invariants

- The current production renderer is CUDA spectral/polarimetric radiometric wavefront path tracing.
- `spectral_domain_bins` describes source/resource domain resolution; `spectral_packet_lanes` describes per-ray packet width. Never couple the two.
- The current GPU packet cap is 32; `packet_lanes=1` is the sampled-wavelength mode.
- Stokes/Mueller state does not provide absolute coherent phase between independent paths.
- URE native schema is authoritative. glTF, USD and MaterialX are adapters and cannot define native feature limits.
- `.urecache` is disposable and must be rejected or rebuilt when source/compiler identity changes.
- Unknown required features/extensions fail; unknown optional content may be retained with diagnostics.
- Script builds are explicit, disabled by default and externally attested. Runtime GPU code does not interpret authoring scripts.
- Unsupported advanced integrators and wave modes must fail loudly instead of falling back silently.

## Current public surfaces

- C++: `IRenderEngine`, `RenderSession`, SceneIR and native scene APIs.
- C: `ure_c_api.h`, including session creation, scene loading, rendering, mutation and AOV access.
- Python: `pyure`, including session configuration, native/package loading, progressive passes, mutation and AOV access.
- CLI: `render`, `info`, `list-devices`, `validate`, `build`, `pack`, `unpack`, `inspect`, `migrate`.

These APIs are implemented and tested but are not yet declared long-term ABI-stable.

## Important limitations

- There is no CPU production integrator.
- There is no supported interactive viewport.
- USD/Hydra and native plugin ABI are not implemented.
- Unbiased/spatial ReSTIR DI, ReSTIR PT, GPU specular manifold, BDPT, VCM and MLT chains remain Phase R work.
- Main-path coherent transport, production diffraction film and distributed coherent merge remain Phase W work.
- Physics/acoustic modules are experimental and should not be presented as general validated solvers.

## Change workflow

Follow `PLAN -> IMPLEMENT -> VERIFY -> REVIEW -> REPORT -> COMMIT` from `AGENTS.md`.

- Preserve unrelated user changes in a dirty worktree.
- Use `apply_patch` for file edits.
- Do not add features outside the active PLAN item.
- Run targeted tests during development and the proportionate full gate before completion.
- Review diagnostics, resource ownership, CUDA errors and memory lifetimes for code changes.
- Do not push unless the user explicitly authorizes it.

## Documentation maintenance

When behavior or stage state changes:

- update `PLAN.md` only where the authoritative queue or phase evidence changes;
- update `STATUS.md` for user-visible capability boundaries;
- update `README.md` only for stable project-entry information;
- update the relevant current technical document;
- leave archived specs/plans intact except for their standard archive banner;
- avoid embedding test counts as timeless claims—include the verification date and point to `ctest -N`.
