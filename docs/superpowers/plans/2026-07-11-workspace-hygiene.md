# UltraRender Workspace Hygiene Implementation Plan

> Archive status: completed execution record. Paths, counts, checkboxes and next steps below describe the dated snapshot only.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restore a clean, reproducible UltraRender workspace, promote the existing generated glTF fixtures, and align project documentation with the current repository state.

**Architecture:** Treat cleanup as a sequence of independently verifiable filesystem, asset, documentation, and build-gate tasks. Preserve `build_modular_x64/` as the sole active build tree and avoid all renderer behavior changes.

**Tech Stack:** PowerShell, Git, CMake/Ninja, CUDA 13, C++23/CUDA C++20, Python glTF generators, Markdown.

## Global Constraints

- Follow `AGENTS.md` and the current execution cursor in `PLAN.md`.
- Do not modify renderer, physics, material, integrator, or wave-optics behavior.
- Do not commit or push without the user's later explicit approval.
- Preserve unrelated user work and the seven approved scene/generator assets.
- Use `apply_patch` for tracked file edits.
- Verify every recursive deletion target resolves directly under `E:\Render Engine` before deletion.

---

### Task 1: Promote end-to-end scene fixtures

**Files:**
- Create: `scenes/README.md`
- Preserve: `scenes/cornell_box.gltf`
- Preserve: `scenes/showcase.gltf`
- Preserve: `scenes/test_plane.gltf`
- Preserve: `scenes/test_plane_sphere.gltf`
- Preserve: `tools/gen_cornell_box.py`
- Preserve: `tools/gen_showcase.py`
- Preserve: `tools/gen_test_scenes.py`

**Interfaces:**
- Consumes: the existing standalone Python generator scripts.
- Produces: documented, reproducible glTF fixtures for future CLI image-rendering gates.

- [ ] **Step 1: Inspect generator output contracts**

Run:

```powershell
rg -n "argparse|sys\.argv|write_text|open\(|\.gltf|def main" tools/gen_cornell_box.py tools/gen_showcase.py tools/gen_test_scenes.py
```

Expected: identify each output filename and whether the generator accepts an output path.

- [ ] **Step 2: Reproduce fixtures outside the source directories**

Run each generator from an isolated directory under `build_modular_x64/fixture_repro/`, using its actual command-line contract discovered in Step 1.

Expected: four `.gltf` outputs corresponding to the checked-in fixtures, without modifying `scenes/`.

- [ ] **Step 3: Compare generated and checked-in semantics**

Run:

```powershell
Get-FileHash scenes/cornell_box.gltf,scenes/showcase.gltf,scenes/test_plane.gltf,scenes/test_plane_sphere.gltf
Get-FileHash build_modular_x64/fixture_repro/*.gltf
```

If bytes differ, compare parsed JSON structure and record the deterministic difference before editing any fixture.

Expected: byte equality or an explained non-semantic difference.

- [ ] **Step 4: Document scene ownership and use**

Create `scenes/README.md` with one entry per fixture containing generator path, intended visual coverage, and future end-to-end role. State that scripts are the reproducible source and glTF files are stable runnable fixtures.

- [ ] **Step 5: Verify asset status**

Run:

```powershell
git status --short -- scenes tools
python -m json.tool scenes/cornell_box.gltf > $null
python -m json.tool scenes/showcase.gltf > $null
python -m json.tool scenes/test_plane.gltf > $null
python -m json.tool scenes/test_plane_sphere.gltf > $null
```

Expected: the seven approved assets plus `scenes/README.md` are visible and all glTF JSON parses successfully.

---

### Task 2: Remove obsolete generated and local-state directories

**Files:**
- Delete: `build/`
- Delete: `build_modular/`
- Delete: `cmake-build-debug/`
- Delete: `output/`
- Delete: `.idea/`
- Delete: `.vscode/`
- Delete: `src/`
- Modify: `.gitignore`

**Interfaces:**
- Consumes: the user-approved deletion list.
- Produces: one active build tree and no local IDE state in repository status.

- [ ] **Step 1: Validate deletion targets**

Resolve every target with `Resolve-Path` and confirm its parent is exactly `E:\Render Engine`; confirm `src/` is empty and all other targets are ignored or untracked.

Expected: no target resolves outside the workspace and no tracked file appears under any target.

- [ ] **Step 2: Remove targets using native PowerShell**

Use `Remove-Item -LiteralPath <verified-absolute-path> -Recurse -Force` independently for each approved directory.

Expected: all seven targets are absent; `build_modular_x64/` remains present.

- [ ] **Step 3: Update ignore policy**

Add `.idea/` and `.vscode/` to `.gitignore`. Keep the existing build and output ignores and do not ignore `scenes/*.gltf` or `tools/gen_*.py`.

- [ ] **Step 4: Verify filesystem hygiene**

Run:

```powershell
Test-Path build,build_modular,cmake-build-debug,output,.idea,.vscode,src
Test-Path build_modular_x64
git status --short
```

Expected: the deleted paths return false, the active build path returns true, and no IDE files remain in status.

---

### Task 3: Calibrate governance and current-state documentation

**Files:**
- Modify: `AGENTS.md`
- Modify: `PLAN.md`
- Modify: `README.md`
- Modify: `HANDOVER.md`
- Modify: `STATUS.md`
- Modify: `progress.md`

**Interfaces:**
- Consumes: current CMake/CTest inventory, recent Phase M commits, and the authoritative PLAN execution queue.
- Produces: consistent current-state documentation while preserving historical implementation records.

- [ ] **Step 1: Capture authoritative facts**

Run:

```powershell
git log -8 --date=short --pretty=format:'%h %ad %s'
ctest --test-dir build_modular_x64 -C Release -N
Select-String -Path build_modular_x64/CMakeCache.txt -Pattern 'CMAKE_GENERATOR:|CMAKE_BUILD_TYPE:|CMAKE_CUDA_ARCHITECTURES:'
```

Expected: Phase M completion commits, 25 registered tests, and the active build generator/configuration.

- [ ] **Step 2: Update `AGENTS.md`**

Change Phase M to complete; document `build_modular_x64/` as the active build directory; replace stale test inventory/counts with the current 25 CTest entries; state that root `src/` is absent; preserve the R-P6 cursor and no-commit rule.

- [ ] **Step 3: Correct current-state contradictions in `PLAN.md`**

Update only status summaries or historical calibration notes that incorrectly say Phase M is unfinished or use obsolete test/build facts. Preserve chronological implementation history and the authoritative queue `R-P6 -> Q -> R-P3 -> R-P4 -> R-P5 -> R-P7`.

- [ ] **Step 4: Align `README.md`**

State the current production/reference CUDA status, completed M/L/S foundations, and current R-P6 construction cursor. Avoid describing fail-loud advanced modes as production implementations.

- [ ] **Step 5: Mark historical status documents**

Add a prominent header to `HANDOVER.md`, `STATUS.md`, and `progress.md` stating that they are historical snapshots and that `AGENTS.md` plus `PLAN.md` are authoritative for current work. Do not delete their historical contents.

- [ ] **Step 6: Audit documentation consistency**

Run searches for stale current-state claims including `Phase M.*In progress`, `Phase M.*未开始`, `21/21`, and active `build_modular/` instructions outside explicitly historical sections.

Expected: no ambiguous current-state contradiction remains.

---

### Task 4: Refresh the active build and verification baseline

**Files:**
- Preserve/regenerate: `build_modular_x64/`

**Interfaces:**
- Consumes: the unchanged source tree and updated documentation/assets.
- Produces: fresh build and test evidence for the cleaned workspace.

- [ ] **Step 1: Remove only interrupted verification residue**

Inspect `build_modular_x64/fixture_repro/` and CTest temporary logs. Remove the isolated fixture reproduction directory after comparison; leave normal CMake/Ninja state intact.

- [ ] **Step 2: Refresh configuration**

Run the repository's supported x64 configure/build script if present; otherwise run the exact generator recorded in the active CMake cache.

Expected: configuration completes without warnings or errors.

- [ ] **Step 3: Build all Release targets**

Run:

```powershell
cmake --build build_modular_x64 --config Release
```

Expected: exit code 0 with no warnings.

- [ ] **Step 4: Run architecture static audits**

Run the Phase L and Phase R static audit scripts present under `scripts/` using their documented invocation.

Expected: both audits pass.

- [ ] **Step 5: Run the complete test suite**

Run:

```powershell
ctest --test-dir build_modular_x64 -C Release --output-on-failure
```

Expected: 25/25 pass. If Python temporary-directory permissions fail, rerun `test_pyure_smoke` directly with a verified writable workspace-local temp directory and report the environment distinction explicitly.

---

### Task 5: Final review and report

**Files:**
- Review all intentional changes; create no additional files.

**Interfaces:**
- Consumes: Tasks 1-4 results.
- Produces: a review-ready, uncommitted cleanup diff and evidence report.

- [ ] **Step 1: Validate the final worktree**

Run:

```powershell
git status --short
git diff --check
git diff --stat
git diff -- .gitignore AGENTS.md PLAN.md README.md HANDOVER.md STATUS.md progress.md scenes/README.md docs/superpowers
```

Expected: only intentional documentation, ignore-policy, design/plan, and promoted asset changes remain.

- [ ] **Step 2: Self-review against governance**

Confirm no renderer code changed, no user asset was deleted, no stale build directory remains, no CUDA error policy or module boundary changed, and the documentation matches the R-P6 cursor.

- [ ] **Step 3: Report without committing**

Report removed directories and reclaimed space, promoted assets, documentation corrections, build/test/static-audit results, and any blocker. Wait for explicit user approval before any commit.
