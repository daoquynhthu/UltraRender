# AGENTS.md — UltraRender Project Governance

This file defines the rules, conventions, and workflow that any AI agent must follow when working on this project.

## 1. Project Identity

**UltraRender** is a physically-based spectral polarization path tracing renderer targeting CUDA GPU. The CPU integrator path (`src/integrators/`) is **OBSOLETE** — do not modify it. All rendering development happens in the GPU path (`src/gpu/` + `include/gpu/`).

### Core Commitments
- Spectral rendering with multi-channel wavelength packets (currently 4, target N >= 32)
- Polarization tracking via Stokes vectors and Mueller matrices
- Wavefront path tracing on CUDA
- Physical correctness over performance tricks
- Future upgradeability to OptiX, BDPT, N-channel spectral

### Non-Goals (out of scope)
- CPU integrator improvements
- Adding features not in PLAN.md
- Random refactoring without a plan step

## 2. Code Style & Conventions

### Language Standards
- C++ host code: C++23 (`CMAKE_CXX_STANDARD 23`)
- CUDA device code: C++20 (`CMAKE_CUDA_STANDARD 20`)
- No compiler warnings allowed (MSVC `/W4`, GCC `-Wall -Wextra -pedantic`)

### Naming Conventions
| Category | Convention | Example |
|----------|-----------|---------|
| Namespaces | `snake_case` | `ure::gpu` |
| Classes/Structs | `PascalCase` | `GpuSpectrum`, `StokesVector` |
| Functions | `snake_case` | `rgb_to_spectrum()`, `ggx_D()` |
| Variables | `snake_case` | `ray_count`, `current_throughput` |
| Member variables | `snake_case` (no prefix) | `values`, `wavelengths` |
| Constants/Macros | `kCamelCase` or `UPPER_SNAKE` | `kNumWavelengths`, `checkCudaErrors` |
| File names | `snake_case` | `path_tracer_kernel.cu` |

### Code Organization Rules
- **NO comments** in code unless documenting a non-obvious physical formula or a known limitation
- Each `.cu` file in `src/gpu/` must have a single responsibility (kernel, material, post, denoise, raygen)
- `include/gpu/` headers are for data structures and device utility functions only
- API layer (`src/api/`, `include/api/`) bridges scene description to GPU data

### Header Order (in each file)
1. CUDA runtime headers
2. Standard library headers
3. Project headers (`gpu/gpu_structs.hpp`, etc.)
4. Local declarations

### CUDA Specifics
- Use `__device__` functions, never host-device dual paths unless necessary
- Prefer `static __device__` for file-local device functions
- Use `__global__` kernels with clear naming: `verb_noun_kernel`
- Always check CUDA errors after kernel launches: `checkCudaErrors(cudaGetLastError())`
- No recursive device functions (GPU stack is limited)

## 3. File Layout

```
E:\Render Engine\
├── include/gpu/          — GPU data structures and device utilities
│   ├── gpu_structs.hpp   — GpuVec3, GpuSpectrum, StokesVector, RayQueue, etc.
│   ├── gpu_spectrum_utils.cuh  — CIE matching, spectrum↔RGB conversions
│   ├── gpu_driver.hpp    — Host-side GPU API declarations
│   ├── gpu_scene_loader.hpp    — Device-side scene access
│   ├── bvh_builder.hpp   — BVH construction
│   ├── material_library.hpp   — Material parameter presets
│   └── path_tracer_sampling.cuh  — Sampling / RNG for device
├── src/gpu/              — GPU implementation
│   ├── gpu_driver.cu     — Host-side GPU orchestration (init, render_pass, etc.)
│   ├── path_tracer_kernel.cu   — Wavefront loop + shade + (TODO: remove old scatter)
│   ├── path_tracer_material.cu — BSDF scatter functions
│   ├── path_tracer_raygen.cu   — Ray generation (currently empty, to fill)
│   ├── path_tracer_denoise.cu  — A-Trous wavelet denoiser
│   ├── path_tracer_post.cu     — Resolve framebuffer + FXAA
│   └── gpu_driver_stub.cpp    — Stub for CPU-only fallback
├── src/api/              — API implementation
│   ├── gpu_engine_impl.cpp    — GpuRenderEngine (uses CompiledGpuScene)
│   ├── gpu_scene_compiler.cpp — Scene → GpuScene compiler
│   └── scene_parser.cpp       — Scene file parser
├── tests/                — Tests (unit: CPU only; TODO: GPU tests)
├── tools/calibration/    — Physical calibration scripts
└── docs/                 — Design docs and guides
```

## 4. Testing

### Mandatory Rules
- Every code change must be verifiable
- Test gate must be **green** before reporting completion of any phase
- GPU tests must be written for any kernel modification
- CPU unit tests must continue to pass after any change

### Test Categories
| Type | Location | Runner | Current Status |
|------|----------|--------|---------------|
| CPU Unit | `tests/unit/` | CMake `add_test` | 5 tests exist, all pass |
| GPU Kernel | `tests/gpu/` (TODO) | Custom runner | **None exist — must create** |
| Integration | `tests/integration/` | CMake `add_test` | 1 test exists |
| Reference Render | `tests/reference/` (TODO) | Manual compare | **None exist — must create** |

### Test Writing Rules
- GPU kernel tests: render a minimal scene (1 sphere + environment), produce 4x4 pixel block, compare against known-correct values
- Reference render tests: render a scene at fixed SPP, compare with stored reference image
- Error tolerance: 1e-4f for unit tests, 0.5% perceptual difference for reference renders

## 5. Conversation Compaction Rule

After every conversation compaction (tool merge, context reset, or session resume), the agent **must read this file (AGENTS.md) in full** before taking any actions. This ensures all governance rules remain in effect across sessions.

## 6. Workflow

Every work session must follow this sequence:

```
PLAN → IMPLEMENT → VERIFY → REVIEW → REPORT → COMMIT
```

### Step 1: PLAN
- Read PLAN.md to identify which phase you are in
- Break the phase into sub-steps (no step larger than ~50 lines changed)
- Write each sub-step into progress.md as "pending"

### Step 2: IMPLEMENT
- Make changes, one sub-step at a time
- Update progress.md status to "in_progress"
- Commit is NOT allowed at this stage

### Step 3: VERIFY
- Run the test suite: `ctest --output-on-failure` (or equivalent build command)
- Ensure ALL tests pass (zero failures)
- If tests fail, go back to IMPLEMENT and fix
- Update progress.md with verification result

### Step 4: REVIEW
- Launch a reviewer subagent (subagent_type: explore) to audit the changes
- The reviewer must check:
  - Does the change match what PLAN.md specified?
  - Are there any unintended side effects?
  - Does the code follow AGENTS.md style rules?
  - Are CUDA errors properly checked?
  - Is memory correctly allocated/freed?
- If reviewer finds issues, go back to IMPLEMENT
- Update progress.md with review result

### Step 5: REPORT
- Present the completed phase to the human user
- Include: what was changed, verification results, review findings
- Wait for explicit approval

### Step 6: COMMIT
- Only commit after user approval
- Commit message format:
  ```
  phase<N>: <brief description>
  
  - <change 1>
  - <change 2>
  ...
  ```
- Push is NOT allowed unless explicitly requested

### Progress Tracking
- `progress.md` at repo root tracks ALL work done
- Each entry has: timestamp, phase, sub-step, status, notes
- Timestamp format: `YYYY-MM-DD HH:MM`

## 7. What NOT To Do

- ❌ Do NOT modify files in `src/integrators/` (CPU path is OBSOLETE)
- ❌ Do NOT change `kNumWavelengths` from 4 (that is a future upgrade)
- ❌ Do NOT add features not listed in PLAN.md
- ❌ Do NOT refactor code without a corresponding PLAN step
- ❌ Do NOT commit without user approval
- ❌ Do NOT ignore failing tests
- ❌ Do NOT remove dead code without verifying it is actually dead

## 8. Communication

- Use Chinese for status reports to the user (they prefer it)
- Use English for code, comments, and AGENTS.md
- Be concise: report what changed, verification result, any blockers
- If blocked, state the blocker clearly and ask for guidance
