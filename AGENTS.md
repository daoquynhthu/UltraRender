# AGENTS.md — UltraRender Project Governance

This file defines the rules, conventions, and workflow that any AI agent must follow when working on this project.

**Critical rule**: After every conversation compaction, context reset, session resume, tool merge, or dream cycle (memory consolidation), the agent **must read this file (AGENTS.md) AND PLAN.md in full** before taking any actions. This ensures all governance rules remain in effect across sessions.

---

## 1. Project Identity

**UltraRender** is a physically-based spectral polarization path tracing renderer targeting CUDA GPU, refactored from a monolithic EXE into a modular industrial pipeline (Phase F complete).

### Architecture (Modular — Phase F Target State)

```
ure_types     — Header-only type library (INTERFACE). Vec3, Mat4, Quat, Ray, SceneIR, RenderConfig, World.
ure_core      — GPU rendering core (STATIC, CUDA 13+). Path tracer kernel, BVH, GPU driver, scene compiler.
ure_sceneio   — Scene I/O (STATIC, pure C++). glTF 2.0 parser, OBJ/legacy loader, stb_image, SPD loader.
ure_diag      — Unified logging/diagnostics (INTERFACE, planned Phase Dx).
ure_config    — Config system (STATIC, pure C++, planned Phase I).
ure_physics   — Physics/acoustic (STATIC, pure C++, built optionally).
ure_cli       — Thin orchestrator EXE; links ure_core + ure_sceneio + ure_config.
```

### Phase Completion Status (see PLAN.md for details)

| Phase | Status | Key Deliverables |
|-------|--------|-----------------|
| 0 (Hardware) | Done | `GpuHardwareInfo`, `query_hardware()`, `auto_configure()`, `RenderConfig` |
| F (Modular) | Done | 6 module libs + thin EXE, CMake export/install, C API |
| P (Data Pipeline) | Done | Instance desc/transform split, RingBuffer, World/ECS, ISpatialQuery, public API |
| H (Asset Pipeline) | Done | stb_image (BMP→stbi_loadf), SPD loader (`SPDData`/`load_spd_file`/`resample_uniform`), 6 tests |
| Dx (Diagnostics) | Done | `ure_diag` unified logging, CUDA error abstraction, RAII timer, ~73 站点迁移 |
| G (glTF) | Done | normalTexture + tangent generation, camera parsing, URE_spectral_material extension, non-glTF fallback |
| I (Config) | Not started | JSON + CLI11 + --verbose/--quiet |
| A (SoA Queue) | Not started | Dynamic N wavelengths from RenderConfig |
| **Cleanup** | **Done** | **GPU tests include paths migrated; old `include/` + `src/` + `tests/{unit,integration}` + legacy CMake block removed** |

### Core Commitments
- Spectral rendering with multi-channel wavelength packets (currently 4, target N >= 32)
- Polarization tracking via Stokes vectors and Mueller matrices
- Wavefront path tracing on CUDA, SIMT-optimized
- Physical correctness over performance tricks
- Textures are spectral data carriers (`HostTexture` → `GpuSpectrum[]` → `rgb_to_spectrum()`), not display RGB

### Non-Goals (out of scope)
- CPU integrator improvements (`src/integrators/` is OBSOLETE — do not modify)
- Adding features not in PLAN.md
- Random refactoring without a plan step
- OpenGL/Vulkan interactive viewport (CLI offline + Python future)
- OSL compiler (custom node graph + MaterialX in future Phase M)

---

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
| Enum class | `PascalCase` | `Level::Info`, `Tag::GPU` |
| Enum values | `PascalCase` | `CudaPolicy::Abort` |

### Code Organization Rules
- **NO comments** in code unless documenting a non-obvious physical formula or a known limitation
- `libs/ure_core/` = GPU rendering; `libs/ure_sceneio/` = scene/asset I/O (pure C++); `libs/ure_diag/` = diagnostics
- Each `.cu` file in `libs/ure_core/src/` must have a single responsibility
- API layer (`render.hpp`, `scene_io.hpp`, `ure_c_api.h`) bridges scene description to GPU data
- `ure_types` is pure header-only: no `.cpp`, no CUDA

### Header Order (in each file)
1. Project-specific config/defines
2. Standard library headers
3. CUDA runtime headers (only if needed, i.e. in `ure_core`)
4. Project headers (`ure/core/vector.hpp`, etc.)
5. Local declarations

### CUDA Specifics
- Use `__device__` functions, never host-device dual paths unless necessary
- Prefer `static __device__` for file-local device functions
- Use `__global__` kernels with clear naming: `verb_noun_kernel`
- Always check CUDA errors after kernel launches (use `checkCudaErrors` macro currently; Phase Dx will migrate to `UR_CUDA_CHECK`)
- No recursive device functions (GPU stack is limited)

---

## 3. File Layout (Modular — Current Target)

```
E:\Render Engine\
├── CMakeLists.txt                   # add_subdirectory(libs/ ure_types ure_core ...)
├── AGENTS.md                        # ← THIS FILE
├── PLAN.md                          # ← MUST READ on every session resume
│
├── libs/
│   ├── ure_types/                   # Header-only type library (INTERFACE)
│   │   └── include/ure/core/        # vector, matrix, quat, ray, aabb
│   ├── ure_core/                    # GPU rendering (STATIC, CUDA)
│   │   ├── include/ure/             # render.hpp, gpu_hardware.hpp, gpu_driver.hpp, spectral/...
│   │   └── src/                     # path_tracer_kernel.cu, gpu_driver.cu, gpu_engine_impl.cpp, ...
│   ├── ure_sceneio/                 # Scene I/O (STATIC, pure C++)
│   │   ├── include/ure/             # scene_io.hpp, spd_loader.hpp, image_loader.hpp
│   │   └── src/                     # gltf_scene_frontend.cpp, spd_loader.cpp, image_loader.cpp
│   ├── ure_diag/                    # Diagnostics (STATIC, Phase Dx completed)
│   │   ├── include/ure/             # log.hpp, log_sink.hpp, check_cuda.hpp, timer.hpp
│   │   └── src/log.cpp              # Global state + sink routing
│   ├── ure_config/                  # Config (STATIC, pure C++, planned Phase I)
│   │   └── include/ure/             # config.hpp
│   └── ure_physics/                 # Physics/Acoustic (STATIC, pure C++, optional)
│       └── include/ure/physics/     # physics_world.hpp, acoustic/...
│
├── apps/
│   └── ure_cli/src/main.cpp         # Thin orchestrator
│
├── tests/
│   ├── host/                        # Host tests (pure C++)
│   │   ├── test_world.cpp           #   39 tests (world/ECS)
│   │   └── test_asset_pipeline.cpp  #   42 tests (BMP, SPD, missing texture)
│   └── gpu/                         # GPU tests (CUDA, 183 total, 6 files)
│       ├── test_device.cu           # Device query & properties
│       ├── test_hardware.cu         # Hardware config + auto_configure
│       ├── test_math_functions.cu   # GPU math correctness
│       ├── test_spectral_pipeline.cu# Spectral sampling + RGB conversion
│       ├── test_render_basic.cu     # Basic rendering pipeline
│       └── test_instance_hotupdate.cu# Transform hot-update
│
├── third_party/
│   ├── stb/stb_image.h              # v2.30, header-only (no stb_image_write)
│   ├── CLI11/CLI11.hpp              # (not yet downloaded)
│   └── nlohmann/json.hpp            # (not yet downloaded)
│
├── scenes/                          # Scene files (.gltf, .glb, .scene legacy)
├── cmake/                           # CMake modules (UltraRenderConfig.cmake.in)
├── docs/                            # Documentation
├── scripts/                         # Utility scripts
├── tools/                           # Dev tools
└── gui/                             # GUI layer
```

**All development happens in `libs/`, `apps/`, and `tests/`.** The old monolithic `include/` and `src/` directories have been removed.

---

## 4. Testing

### Mandatory Rules
- Every code change must be verifiable
- Test gate must be **green** before reporting completion of any phase
- GPU tests must be written for any kernel modification
- Host tests must continue to pass after any change

### Test Commands
```powershell
# Build all (Release)
cmake --build build_modular --config Release

# Run host tests
& "E:\Render Engine\build_modular\tests\host\Release\test_world.exe"
& "E:\Render Engine\build_modular\tests\host\Release\test_asset_pipeline.exe"

# Run GPU tests
& "E:\Render Engine\build_modular\tests\gpu\Release\gpu_test_device.exe"
& "E:\Render Engine\build_modular\tests\gpu\Release\gpu_test_hardware.exe"
& "E:\Render Engine\build_modular\tests\gpu\Release\gpu_test_math.exe"
& "E:\Render Engine\build_modular\tests\gpu\Release\gpu_test_spectral.exe"
& "E:\Render Engine\build_modular\tests\gpu\Release\gpu_test_render.exe"
& "E:\Render Engine\build_modular\tests\gpu\Release\gpu_test_instance.exe"
```

### Current Test Inventory
| Type | Count | Location | Runner |
|------|-------|----------|--------|
| Host (World/ECS) | 39 | `tests/host/test_world.cpp` | Direct EXE |
| Host (Asset Pipeline) | 6 | `tests/host/test_asset_pipeline.cpp` | Direct EXE |
| GPU (Device) | ~30 | `tests/gpu/test_device.cu` | Direct EXE |
| GPU (Hardware) | ~30 | `tests/gpu/test_hardware.cu` | Direct EXE |
| GPU (Math) | ~30 | `tests/gpu/test_math_functions.cu` | Direct EXE |
| GPU (Spectral) | ~30 | `tests/gpu/test_spectral_pipeline.cu` | Direct EXE |
| GPU (Render) | ~30 | `tests/gpu/test_render_basic.cu` | Direct EXE |
| GPU (Hot-Update) | ~33 | `tests/gpu/test_instance_hotupdate.cu` | Direct EXE |
| **Total** | **~228** | | |

### Test Writing Rules
- GPU kernel tests: render a minimal scene (1 sphere + environment), produce 4x4 pixel block, compare against known-correct values
- Error tolerance: 1e-4f for unit tests, 0.5% perceptual difference for reference renders

---

## 5. Conversation Compaction & Session Management

### 5.1 Mandatory Re-read on Resume

After **EVERY** conversation compaction, context reset, session resume, tool merge, or dream cycle (memory consolidation), the agent **must**:

1. **Read this file (AGENTS.md) in full** — re-establish governance rules
2. **Read PLAN.md in full** — re-establish phase context and current status
3. **Check `build_modular/` last build output** — verify project still compiles before making changes

### 5.2 Session Summary / Dream Cycle Constraint

When summarizing or compressing the conversation (e.g., via a dream cycle, tool merge, or context compression handler):

1. The summary **must be written to AGENTS.md §10 (Knowledge Summary — latest session)** in the following format:
   ```
   ## 10. Knowledge Summary — latest session
   
   ### Session Log
   | # | Overview | Key Decisions & Findings |
   |---|----------|--------------------------|
   | 1 | <brief session goal> | <decisions made, bugs found, design rationale> |
   
   ### Consolidated Truth
   <!-- key facts extracted, cross-referenced with PLAN.md -->
   ```

2. The first action after the summary is injected into the new context **must** be to re-read AGENTS.md and PLAN.md in full.

3. **Never** rely on the summary alone — it is a fallback for continuity, not a substitute for the full governance documents.

---

## 6. Workflow

Every work session must follow this sequence:

```
PLAN → IMPLEMENT → VERIFY → REVIEW → REPORT → COMMIT
```

### Step 1: PLAN
- Read PLAN.md to identify which phase you are in
- Read AGENTS.md to re-establish governance
- Break the phase into sub-steps (no step larger than ~50 lines changed)
- Write each sub-step into a TODO list (use todowrite tool)

### Step 2: IMPLEMENT
- Make changes, one sub-step at a time
- Update TODOs status to "in_progress"
- Commit is NOT allowed at this stage

### Step 3: VERIFY
- Run the test suite (see §4 for commands)
- Ensure ALL tests pass (zero failures)
- If tests fail, go back to IMPLEMENT and fix
- Update TODOs with verification result

### Step 4: REVIEW
- After all sub-steps done, do a self-review:
  - Does the change match what PLAN.md specified?
  - Are there any unintended side effects?
  - Does the code follow AGENTS.md style rules?
  - Are CUDA errors properly checked?
  - Is memory correctly allocated/freed?
  - Are there any duplicate code paths between `src/` (old) and `libs/` (new)?
- If issues found, go back to IMPLEMENT

### Step 5: REPORT
- Present the completed phase to the user
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

---

## 7. What NOT To Do

- ❌ Do NOT add features not listed in PLAN.md
- ❌ Do NOT refactor code without a corresponding PLAN step
- ❌ Do NOT skip PLAN.md or AGENTS.md on session resume
- ❌ Do NOT commit without user approval
- ❌ Do NOT ignore failing tests
- ❌ Do NOT change `kNumWavelengths` from 4 (that is Phase E — N-channel spectral upgrade)
- ❌ Do NOT modify acoustic/physics modules during Phase Dx (diagnostics) — they are out of scope
- ❌ Do NOT introduce pink checkerboard generation for missing textures (see PLAN.md Phase H.3)
- ❌ Do NOT add stb_image_write (engine has own tonemapping pipeline, see PLAN.md Phase H.1)
- ❌ Do NOT use `{fmt}` library or `snprintf` for logging — use `std::format` (C++20) / `std::print` (C++23)

---

## 8. Communication

- Use Chinese for status reports to the user (they prefer it)
- Use English for code, comments, and AGENTS.md
- Be concise: report what changed, verification result, any blockers
- If blocked, state the blocker clearly and ask for guidance
- When presenting plans or design decisions, offer structured options rather than open-ended questions

---

## 9. Build Environment

- **OS**: Windows 11
- **Compiler**: VS 2022 BuildTools (`vcvarsall.bat x64`)
- **CUDA**: 13.0
- **GPU**: RTX 5060 Laptop (CC 12.0, 8 GB VRAM, 26 SMs)
- **Generator**: `cmake -G "Visual Studio 17 2022"`
- **Build directory**: `build_modular/`
- **Build config**: `Release` (for tests), `Debug` (for development)

### Build Commands
```powershell
# Configure
cmake -G "Visual Studio 17 2022" -B build_modular -S .

# Build specific target
cmake --build build_modular --config Release --target <target_name>

# Build all
cmake --build build_modular --config Release

# Build and run a GPU test
cmake --build build_modular --config Release --target gpu_test_hardware
& "E:\Render Engine\build_modular\tests\gpu\Release\gpu_test_hardware.exe"
```

---

## 10. Knowledge Summary — latest session

*This section is updated by conversation compaction / dream cycle mechanisms. See §5.2.*

| # | Session | Overview | Key Decisions & Findings |
|---|---------|----------|--------------------------|
| 1 | 2026-06-09 Dx | Phase Dx 收尾：migrate scene_factory.cpp (6处) + obj_loader.cpp (2处) | 264 tests all pass; 所有 std::cout/cerr 已清除或确认为进度条(Dx.7)/声学物理(范围外)/注释代码 |
| 2 | 2026-06-09 Cleanup | 迁移 GPU test includes (3 files) + CMakeLists.txt 移除旧 include/ 路径; 删除旧目录 include/ src/ tests/{unit,integration} 和遗留 CMake 构建块 | 264 tests all pass; 项目完全脱离旧 monolithic 架构 |
| 3 | 2026-06-09 Phase G | Phase G audit: 修正 URE_spectral_material 匹配 PLAN 规范 (SpectralMaterialExtension), extensionsUsed/Required 校验, tangent GPU 上传管线 (4 处) | 8 files, 92 insertions; 185 tests all pass |
