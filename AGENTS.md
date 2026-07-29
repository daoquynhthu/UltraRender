# AGENTS.md — UltraRender Project Governance

This file defines the rules, conventions, and workflow that any AI agent must follow when working on this project.

**Critical rule**: After every conversation compaction, context reset, session resume, tool merge, or dream cycle (memory consolidation), the agent **must read this file (AGENTS.md) in full, then use the PLAN.md index/search to read only the authoritative queue, current phase, dependencies, and directly relevant status sections** before taking project actions. Never load PLAN.md wholesale; it is intentionally long and must be accessed progressively.

---

## 1. Project Identity

**UltraRender** is a physically-based spectral polarization path tracing renderer targeting CUDA GPU, refactored from a monolithic executable into a modular library/application layout (Phase F complete).

### Architecture (Modular — Phase F Target State)

```
ure_types     — Header-only type library (INTERFACE). Vec3, Mat4, Quat, Ray, SceneIR, RenderConfig, World.
ure_runtime   — Backend-neutral GPU runtime contracts (STATIC, pure C++).
ure_vulkan    — Vulkan 1.3 compute/acceleration runtime (STATIC, SDK-neutral public surface).
ure_d3d12     — Windows D3D12/DXR compute/acceleration runtime (STATIC, SDK-neutral public surface).
ure_core      — GPU rendering core (STATIC, CUDA 13+). Path tracer kernel, BVH, GPU driver, scene compiler.
ure_sceneio   — Scene I/O (STATIC, pure C++). glTF 2.0 parser, OBJ/legacy loader, stb_image, SPD loader.
ure_diag      — Unified logging/diagnostics (INTERFACE, Phase Dx complete).
ure_config    — Config system (STATIC, pure C++, Phase I complete).
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
| G (glTF) | Done | normalTexture + tangent generation, camera parsing, URE_spectral_material extension, non-glTF fallback; audit fix (5 gaps), comprehensive tests (11 host + 3 GPU) |
| I (Config) | Done | JSON config, CLI11 render/info/device/native-tool subcommands, override chain |
| A (SoA Queue) | Done | Dynamic N wavelengths from RenderConfig, SoA spectral queues/materials |
| B (Multi-GPU) | Done | `MultiGpuContext`, sample-space partitioning, per-device render contexts, merged framebuffer copy |
| C (Distributed Contract) | Done | Sample range partitioning, deterministic framebuffer merge, Release-safe validation |
| D (Distributed Integration) | Done | File backend for sample-range/framebuffer exchange and merge workflow |
| E (N-Channel Spectral) | Done | Runtime-N spectral pipeline, SPD input, spectral lane split, Mueller/dispersion closure |
| S (Session API) | Done | `RenderSession`, `SceneDiff`, AOVs, C ABI, pyure progressive/mutation workflow |
| M (Material System) | Done | MaterialGraph, GPU expression graph, BSDF mix/layer, MaterialX adapter, presets |
| L (Large Spectral Domain) | Done | `domain_bins` / `packet_lanes` split, 1M oracle/sampled smoke, resource descriptors, distributed spectral shard metadata, runtime presets, static audit |
| R-P6 (Mie Volume Resources) | Done | Deterministic Lorenz-Mie generation, strict table adapter, immutable SceneIR resources, spectral GPU eval/pdf/sample, NEE/continuation, Session rebuild |
| R-P3 (Production ReSTIR) | Done | Unbiased temporal/spatial DI; bounded diffuse/volume PT suffix replay; bias/variance benchmark suite |
| R-P4 (Specular Manifold) | Done | GPU SMS + BDPT/VCM; exact support partition; independent technique AOV; four-scene statistical gate |
| R-P5 (PSSMLT) | Done | Independent GPU chains, production wavefront replay, normalization/diagnostics/shards, replicated disjoint-range fixed-NMSE gate |
| R-P7 (Industrial Validation) | Done | Clean-tree eight-category Closure, farm/Nsight same-binary evidence, 37/37 CTest |
| Q.0-Q.12 (Native Scene) | Done | Native schema/serialization, procedural/script, resources/solvers/simulation, tooling/adapters/cache/farm, validation suite |
| T (Portable GPU Runtime) | Done | T.0-T.11 complete; portable runtime, optional backends, multi-backend scheduling and unified validation closed |
| V (GPU Acceleration) | In progress | V.0-V.8 complete; V.9 physical error LoD is the authoritative cursor |
| W (Wave Optics Solver) | In progress | W.0 audit + rough dielectric spectral/UV PDF/MIS fix done; W.1 WaveOpticsConfig gates done; W.2 Airy PSF oracle started |
| **Cleanup** | **Done** | **GPU tests include paths migrated; old `include/` + `src/` + `tests/{unit,integration}` + legacy CMake block removed** |

### Core Commitments
- Spectral rendering with runtime-configured multi-channel wavelength packets (current GPU packet cap 32)
- Polarization tracking via Stokes vectors and Mueller matrices
- Wavefront path tracing on CUDA, SIMT-optimized
- Physical correctness over performance tricks
- Textures are spectral resource carriers (`HostTexture` → RGB CUDA texture object or explicit source-sample spectral grid), not display RGB

### Non-Goals (out of scope)
- CPU production integrator development; host code is limited to oracle, compilation, build, scheduling, and validation roles
- Adding features not in PLAN.md
- Random refactoring without a plan step
- OpenGL/Vulkan interactive viewport (CLI offline + Python future)
- OSL compiler (URE MaterialGraph is authoritative; MaterialX is an adapter)

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
| Constants/Macros | `kCamelCase` or `UPPER_SNAKE` | `kMaxPacketLanes`, `UR_CUDA_CHECK` |
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
- Always map CUDA errors through the backend-private structured runtime checker; never expose CUDA error types or reset/abort policy through installed headers
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
│   │   ├── include/ure/             # render/session public API and CUDA-private detail/
│   │   └── src/                     # CUDA executor, scene lowering, rendering implementation
│   ├── ure_runtime/                 # SDK-free runtime contracts (STATIC, pure C++)
│   │   ├── include/ure/runtime/      # device, resource, synchronization and upload contracts
│   │   └── src/                     # SDK-free descriptor and graph validation
│   ├── ure_vulkan/                  # Vulkan 1.3 compute/acceleration runtime (STATIC)
│   │   ├── include/ure/             # SDK-neutral Vulkan runtime factory
│   │   └── src/                     # private Volk/Vulkan implementation
│   ├── ure_d3d12/                   # Windows D3D12/DXR runtime (STATIC)
│   │   ├── include/ure/             # SDK-neutral D3D12 runtime factory
│   │   └── src/                     # private D3D12/DXGI implementation
│   ├── ure_sceneio/                 # Scene I/O (STATIC, pure C++)
│   │   ├── include/ure/             # scene_io.hpp, spd_loader.hpp, image_loader.hpp
│   │   └── src/                     # gltf_scene_frontend.cpp, spd_loader.cpp, image_loader.cpp
│   ├── ure_diag/                    # Diagnostics (STATIC, Phase Dx completed)
│   │   ├── include/ure/             # log.hpp, log_sink.hpp, timer.hpp
│   │   └── src/log.cpp              # Global state + sink routing
│   ├── ure_config/                  # Config (STATIC, pure C++, Phase I done)
│   │   ├── include/ure/             # config.hpp (RenderConfig, CliCommand, parse_cli, load_config)
│   │   └── src/config_impl.cpp      # JSON parser (nlohmann) + CLI11 subcommands
│   └── ure_physics/                 # Physics/Acoustic (STATIC, pure C++, optional)
│       └── include/ure/physics/     # physics_world.hpp, acoustic/...
│
├── apps/
│   └── ure_cli/src/main.cpp         # Thin orchestrator
│
├── tests/
│   ├── host/                        # Host tests (pure C++)
│   │   ├── test_world.cpp           #   39 tests (world/ECS)
│   │   ├── test_asset_pipeline.cpp  #   42 tests (BMP, SPD, missing texture)
│   │   └── test_gltf_frontend.cpp   #   55 checks (11 test cases, Phase G coverage)
│   └── gpu/                         # GPU tests (CUDA, 204+ total, 7 files)
│       ├── test_device.cu           # Device query & properties
│       ├── test_hardware.cu         # Hardware config + auto_configure
│       ├── test_math_functions.cu   # GPU math correctness
│       ├── test_spectral_pipeline.cu# Spectral sampling + RGB conversion
│       ├── test_render_basic.cu     # Basic rendering pipeline
│       ├── test_instance_hotupdate.cu# Transform hot-update
│       └── test_gpu_tangents.cu    #   21 checks (tangent upload, null fallback)
│
├── third_party/
│   ├── stb/stb_image.h              # v2.30, header-only (no stb_image_write)
│   ├── CLI11/CLI11.hpp              # CLI11 v2.x (header-only)
│   └── nlohmann/json.hpp            # nlohmann/json v3.x (header-only)
│
├── scenes/                          # Maintained glTF fixtures and benchmark inputs
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
# Configure and build all Release targets
.\scripts\build_x64.ps1 -BuildDir build_modular_x64 -Config Release

# Run the complete registered test gate
ctest --test-dir build_modular_x64 -C Release --output-on-failure

# Build selected targets without reconfiguration
.\scripts\build_x64.ps1 -BuildDir build_modular_x64 -Config Release -SkipConfigure -Targets test_gltf_frontend,gpu_test_tangents

# Run selected tests
ctest --test-dir build_modular_x64 -C Release -R "test_gltf_frontend|gpu_tangents" --output-on-failure
```

### Current Test Inventory
| Group | Registered CTest targets |
|-------|--------------------------|
| GPU core | `gpu_device`, `gpu_math`, `gpu_spectral`, `gpu_spectral_soa`, `gpu_hardware`, `gpu_render`, `gpu_instance`, `gpu_tangents`, `gpu_denoise`, `gpu_cuda_runtime`, `gpu_acceleration_contract`, `gpu_clustered_geometry` |
| GPU physics/contracts | `gpu_polarization`, `gpu_volume`, `gpu_contract`, `gpu_wave_optics` |
| Host core | `test_world`, `test_asset_pipeline`, `test_config`, `test_runtime_contract`, `test_acceleration_contract`, `test_clustered_geometry`, `test_resource_plan`, `test_execution_graph`, `test_multi_backend_schedule`, `test_spectral_oracle`, `test_wave_optics`, `test_integrator`, `test_mie_phase` |
| Host scene/material/session | `test_native_scene`, `test_native_scene_ir`, `test_native_procedural_graph`, `test_native_script_build`, `test_native_resource_catalog`, `test_native_solver_contract`, `test_native_simulation_contract`, `test_native_tooling`, `test_native_adapter`, `test_native_compiled_cache`, `test_native_validation_suite`, `test_gltf_frontend`, `test_material_graph`, `test_materialx_io`, `test_session`, `test_distributed_file_io` |
| Python | `test_pyure_smoke` |
| Vulkan | `vulkan_runtime`, `vulkan_acceleration` |
| D3D12 | `d3d12_runtime` |
| Multi-backend | `multi_backend_inventory` |
| **CTest total** | **50 registered tests** in `build_modular_x64` |

### Test Writing Rules
- GPU kernel tests: render a minimal scene (1 sphere + environment), produce 4x4 pixel block, compare against known-correct values
- Error tolerance: 1e-4f for unit tests, 0.5% perceptual difference for reference renders

---

## 5. Conversation Compaction & Session Management

### 5.1 Mandatory Re-read on Resume

After **EVERY** conversation compaction, context reset, session resume, tool merge, or dream cycle (memory consolidation), the agent **must**:

1. **Read this file (AGENTS.md) in full** — re-establish governance rules
2. **Read PLAN.md progressively** — locate the authoritative queue/current cursor first, then read only the current phase, its dependencies, and directly relevant status sections; never load the whole file into context
3. **Check `build_modular_x64/` last build output** — verify project still compiles before making changes

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

2. The first action after the summary is injected into the new context **must** be to re-read AGENTS.md in full and progressively retrieve the authoritative queue/current cursor plus directly relevant PLAN.md sections.

3. **Never** rely on the summary alone — it is a fallback for continuity. Reconcile it against the indexed, relevant PLAN.md sections without loading unrelated phases.

---

## 6. Workflow

Every work session must follow this sequence:

```
PLAN → IMPLEMENT → VERIFY → REVIEW → REPORT → COMMIT
```

### 6.1 Subagent Budget Governance

When the primary agent is from the GPT or Claude model families, the default execution mode is **single-agent**. Subagents consume limited quota quickly and are not a routine planning, implementation, debugging, review, or verification mechanism.

- Do not spawn or continue a subagent merely because delegation is available, a task is large, or an additional review might be useful.
- Use the primary agent's own repository inspection, tests, static audits, and self-review as the normal workflow and completion gate.
- A subagent is allowed only when the user explicitly requests delegation, or when the primary agent has identified a small, independent, bounded task with material parallel benefit that cannot be achieved comparably through local tools.
- Before starting an allowed subagent, state its exact scope and why the quota cost is justified. Prefer one focused subagent; do not create agent trees or let a subagent spawn further agents.
- Do not use subagents for reading AGENTS.md/PLAN.md, summarizing project context, routine code search, ordinary test execution, or duplicating the primary agent's review.
- Stop or avoid follow-up subagent turns once the bounded result is obtained. Subagent review is never mandatory for reporting or committing unless the user explicitly made it a requirement.

### 6.2 General Skill Template Proportionality

General-purpose skill templates are guardrails for weak or unfamiliar agents, not a mandatory ceremonial workflow for capable GPT or Claude primary agents. The primary agent may selectively skip or compress template steps that add no material safety, correctness, architectural, or verification value.

- Do not repeat approvals, execution-mode choices, handoff prompts, generic checkpoints, or intermediate commits when project context and user authorization already resolve them.
- Do not run ceremonial red tests whose only expected result is that a deliberately not-yet-created file, symbol, target, or scaffold is missing. A red test is valuable only when it can expose incorrect behavior, a regression, or a nontrivial contract boundary; otherwise implement the smallest coherent slice and verify its behavior directly.
- Prefer direct single-agent progress when the authoritative PLAN cursor, approved design, and verification contract are clear.
- Project governance remains binding: do not skip the authoritative PLAN scope, architecture boundaries, test evidence, self-review, REPORT approval, commit authorization, or explicit safety constraints.
- When a skill template conflicts with this project workflow, preserve the skill's substantive engineering intent while using the shortest project-compliant process.

### Step 1: PLAN
- Search PLAN.md for the authoritative queue/current cursor, then read only the active phase, dependencies, and relevant status sections
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
- ✅ **Phase E complete**: keep `kNumWavelengths = 4`, `.values.x/y/z/w`, RGB roundtrip APIs, old thin-film helpers, and dielectric transmission clamps from re-entering the codebase. See `docs/Phase_E_Spectral_Architecture.md` for final design history and the current K/M/W follow-up boundaries.
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
- **Generator**: Ninja through `scripts/build_x64.ps1` with the VS 2022 x64 toolchain
- **Build directory**: `build_modular_x64/`
- **Build config**: `Release` (for tests), `Debug` (for development)

### Build Commands
```powershell
# Configure
.\scripts\build_x64.ps1 -BuildDir build_modular_x64 -Config Release -SkipBuild

# Build specific target
.\scripts\build_x64.ps1 -BuildDir build_modular_x64 -Config Release -SkipConfigure -Targets <target_name>

# Build all
cmake --build build_modular_x64 --config Release

# Build and run a GPU test
cmake --build build_modular_x64 --config Release --target gpu_test_hardware
ctest --test-dir build_modular_x64 -C Release -R "^gpu_hardware$" --output-on-failure
```

---

## 10. Knowledge Summary — latest session

*This section is updated by conversation compaction / dream cycle mechanisms. See §5.2.*

| # | Session | Overview | Key Decisions & Findings |
|---|---------|----------|--------------------------|
| 1 | 2026-06-09 Dx | Phase Dx 收尾：migrate scene_factory.cpp (6处) + obj_loader.cpp (2处) | 264 tests all pass; 所有 std::cout/cerr 已清除或确认为进度条(Dx.7)/声学物理(范围外)/注释代码 |
| 2 | 2026-06-09 Cleanup | 迁移 GPU test includes (3 files) + CMakeLists.txt 移除旧 include/ 路径; 删除旧目录 include/ src/ tests/{unit,integration} 和遗留 CMake 构建块 | 264 tests all pass; 项目完全脱离旧 monolithic 架构 |
| 3 | 2026-06-09 Phase G | Phase G audit: 修正 URE_spectral_material 匹配 PLAN 规范 (SpectralMaterialExtension), extensionsUsed/Required 校验, tangent GPU 上传管线 (4 处) | 8 files, 92 insertions; 185 tests all pass |
| 4 | 2026-06-09 Phase G Tests | 编写全方位 Phase G 测试: host (11 cases, 55 checks) + GPU (3 cases, 21 checks); 修复 JSON 数组未闭合 bug; 全局 CMAKE_CUDA_FLAGS + /wd4819 消除 C4819 警告 | 2 test files + 2 CMakeLists.txt + 1 root CMakeLists.txt; host + GPU 全部通过 |
| 5 | 2026-06-09 Phase I | 配置系统: 下载 CLI11+json.hpp → third_party; 实现 JSON 四段配置(spectral/renderer/output/gpu); CLI11 子命令(render/info/list-devices/validate); 覆盖链(CLI > JSON > defaults); 重构 main.cpp 为 subcommand dispatch | 340 tests all pass; ure_config CMakeLists 链接 third_party; 保留完整 physics demo loop; `ure_cli list-devices` 输出 RTX 5060 Laptop GPU CC 12.0 8150 MB |
| 6 | 2026-06-10 Batch 2b Cleanup | C11 (GPU test memory leak) + M10 (render silent no-op) + C8 (AABB perf — 8-corner transform) + C10 (RingBuffer memory ordering — derive read from write_index + atomic_thread_fence) | 10 CTest all pass; DeviceMem RAII in test_framework.cuh; `render()` throws on null ctx; AABB O(N*21) → O(N*6+24); RingBuffer SPSC fence-correct |
| 7 | 2026-07-11 Workspace Hygiene | Removed obsolete build/output/IDE directories, retained reproducible glTF render fixtures, refreshed current documentation and validation scripts, and repaired two exposed GPU regressions | Reused mutually exclusive layered/composite material storage to reduce `shade_kernel` local stack pressure; corrected the instance hot-update fixture material offset and camera; Release build, Phase L/R audits, physics-optics gate, and 25/25 CTest passed |
| 8 | 2026-07-13 R-P6 | Completed production Mie volume resources and hardened the implementation after independent audit | Canonical phase/CDF normalization, retained-scene deep freeze, strict host validation/import budgets, high-x Csca/g/CDF convergence, direct extinction transport, independent GPU CDF bounds, nonzero comparative E2E and variance gates; sm_120 Release build, Phase L/R and physics-optics audits, and 26/26 CTest passed. Governance now requires progressive PLAN retrieval and single-agent-by-default GPT/Claude execution. |
| 9 | 2026-07-16 R-P3 | Completed production ReSTIR DI and bounded ReSTIR PT path reuse | Per-ray global sample identity, versioned actual suffix capture, surface/volume replay with PDF/Jacobian/Stokes metadata, isolated candidate accumulation, context-owned reservoirs, fail-loud specular boundary, three-scene bias/variance suite; Release build, Phase R audit, and 37/37 CTest passed |
| 10 | 2026-07-18 R-P4 | Closed production manifold visibility and differential geometry, then began SDS response work | Every solved chain edge uses formal traversal with typed occlusion; generalized geometry is reconstructed as endpoint differential area Jacobian times anchor geometry rather than the Newton determinant; planar/sphere/device/E2E stability gates pass. Current work adds true surface/light UV, smooth-delta eligibility, spectral-lane boundaries, Stokes response, and unbiased root selection. |
| 11 | 2026-07-18 R-P4 closure | Closed standalone specular-manifold correctness and benefit validation | Camera-direction dielectric transport fixed; exact anchored-delta support partition; independent wavefront technique AOV; glass/SDS/small-emitter/mixed suite passed with two positive time-to-error workloads; next cursor R-P5 |
| 12 | 2026-07-18 R-P5 implementation | Enabled production PSSMLT runtime while retaining the R-P5 cursor | Shared queue-owned primary-sample replay, independent GPU chains, bootstrap/burn-in/normalization, diagnostics, memory budgets, 64-bit multi-GPU shard identity, native/config/C/Python propagation, and deterministic GPU E2E are implemented. The difficult-scene benefit gate remained open. |
| 13 | 2026-07-19 R-P5 estimator audit | Hardened mutation/seeding and replaced the self-referential benefit metric | GPU and host now share wrapped symmetric Laplace small steps; bootstrap chains use deterministic stratified CDF resampling; time-to-error uses a fixed normalized-MSE target. SDS reaches 5% NMSE at 256 SPP/0.444s versus wavefront 1024 SPP/0.805s. Glass, small-emitter, and high-occlusion remain boundary failures; the second positive workload and complete multiplexed technique partition remain open. |
| 14 | 2026-07-22 R-P5 closure | Closed the initial PSSMLT evidence and hardened the BDPT boundary | Corrected spectral accumulator wavelength retention and camera reverse-PDF direction; rejected MLT+BDPT until sampled-lane subpaths share one wavelength primary sample. R-P7 later superseded the reference-correlated two-scene benefit claim with replicated disjoint-range evidence. |
| 15 | 2026-07-23 R-P7 MLT audit | Rebuilt the PSSMLT statistical gate around independent evidence | Four disjoint reference shards, four wavefront sample ranges, non-overlapping MLT chain-identity intervals, reference uncertainty and full-image replicate confidence intervals retain SDS small light as the positive workload and classify SDS/small-emitter/glass/high-occlusion as boundaries. |
| 16 | 2026-07-23 R-P7 closure | Closed Phase R on a clean committed tree | Release build, 37/37 CTest, Q/L/R and physics-optics gates, eight-category Closure, disjoint 4,096-SPP farm merge, and same-binary Nsight/VRAM evidence passed; cursor advanced to T.0. |
| 17 | 2026-07-23 T.0 | Froze the portable-runtime coupling ledger and regression boundary | Fourteen coupling categories cover build through validation with owners and migration batches; static audit prevents new CUDA SDK/native-handle leakage into backend-neutral surfaces and freezes four existing public-header debts. |
| 18 | 2026-07-26 T.1 | Established the backend identity, capability and selection contract | Backend-neutral identity/features/limits/budgets and driver/compiler metadata now span RenderConfig, JSON, CLI, C ABI and pyure; CUDA remains Auto/default, while unavailable backends and invalid adapter/feature/budget requests fail loudly. |
| 19 | 2026-07-26 T.2 | Selected and verified the portable kernel toolchain | Pinned Slang 2026.14 compiled spectral, Mueller, queue, BSDF, wave and traversal prototypes deterministically to PTX/SPIR-V/DXIL with reflection/debug/capability evidence; sm_120 cubins had zero spills, full measured occupancy and passed numerical execution. |
| 20 | 2026-07-26 T.3 | Established the backend-neutral runtime API | Added pure C++ `ure_runtime` contracts for typed resources, queues, timeline synchronization, modules/pipelines, dispatch DAGs and durable device-loss errors; host mock tests cover lifetime, alignment, overflow, synchronization and loss without GPU SDK headers. |
| 21 | 2026-07-26 T.4 | Migrated resource descriptors and CUDA native ownership | Stable resource IDs, typed layouts/residency/sparse tiles, deterministic upload plans and million-domain budget tests are SDK-free; public mutation APIs now consume SceneIR, CUDA scene/context/texture state is private, and distributed file v4 carries resource-set identity. |
| 22 | 2026-07-26 T.5 | Froze backend-neutral dispatch, queue and estimator execution semantics | Stable pass/epoch regions, active-count producers, indirect dispatch, barriers, async transfers and ordered PDF/estimator contracts cover wavefront, guiding, ReSTIR, advanced integrators, MLT and wave propagation; CUDA path/wave entrypoints validate graph identity and the independent SDK-free gate passes. |
| 23 | 2026-07-26 T.6 | Migrated the CUDA production backend behind runtime contracts | A private CUDA Device implements queues/timelines/resources/PTX DAG execution and structured failures; path, wave and multi-GPU lower stable execution graphs, public headers and root SDK-free builds no longer require CUDA, reference hashes and VRAM are unchanged, and measured render time remains inside the fail-loud regression gate. |
| 24 | 2026-07-26 T.7 | Established the Vulkan compute production foundation | A Vulkan 1.3 Device implements adapter/resource/descriptor/pipeline/cache/DAG/timeline and validation/loss contracts behind an SDK-neutral header; deterministic shared-semantic SPIR-V passed Windows NVIDIA/Intel and Linux CUDA-free execution gates. Full Vulkan rendering remains fail-loud until T.8 acceleration. |
| 25 | 2026-07-26 T.8 | Established the bounded Vulkan acceleration bridge | SDK-free provider/selection/hit contracts now drive private Vulkan BLAS/TLAS and real ray query with explicit compute fallback or rejection; CUDA production hit metadata, Windows NVIDIA/Intel, Linux CUDA-free, deterministic shader, lifetime/budget, and 45/45 CTest gates passed. Full verification also fixed a CUDA timeline wait race where a checkpoint could complete between two probes. Production acceleration construction remains Phase V and full Vulkan SceneIR rendering remains fail-loud. |
| 26 | 2026-07-28 T.9 | Established the optional Windows D3D12/DXR runtime | SDK-neutral public surface and private D3D12/DXGI implementation now cover adapter budgets, buffer/image/sampler resources, deterministic DXIL, typed descriptor heaps, compute/copy queues, cross-queue fences, DRED and bounded DXR 1.1 BLAS/TLAS with compute fallback. CUDA/Vulkan/D3D12 parity, native DXR, no-D3D12 isolation, deterministic shader and 46/46 CTest gates passed; full D3D12 SceneIR rendering and production acceleration remain later work. |
| 27 | 2026-07-28 T.10 | Established portable homogeneous and heterogeneous sample scheduling | SDK-free feature/precision/coherence/budget/semantic negotiation, canonical weighted ranges, backend-native resource cache identity and distributed v5 worker provenance now reject incompatible or overlapping contributions. CUDA private multi-GPU uses the shared scheduler; actual CUDA plus NVIDIA/Intel Vulkan/D3D12 inventory, v4 compatibility, SDK-free 4/4 and Release 48/48 gates passed. |
| 28 | 2026-07-28 T.11 | Closed Phase T with a unified cross-backend validation and performance gate | The machine-readable suite aggregates physical oracles, shared acceleration/framebuffer parity, CUDA exact reference and VRAM/throughput guards, variance/MSE convergence, device loss, budgets, caches and cold/warm launch evidence. CUDA/Vulkan are required, native RT/DXR is capability-driven, five actual workers and Release 48/48 passed; cursor advanced to V.0. |
| 29 | 2026-07-28 V.0 | Audited and froze the current GPU geometry acceleration boundary | The ledger identifies per-mesh midpoint BVH construction, fixed-stack overflow/drop risk, linear instance and triangle fallback, missing shadow-instance traversal/TLAS/refit/stats, unused host traversal and nonfunctional duplicated OptiX placeholders. Static hashes and a consumer allowlist prevent legacy paths from becoming production; cursor advanced to V.1. |
| 30 | 2026-07-28 V.1 | Established the acceleration configuration and fail-loud API boundary | Provider, build quality, update policy, clustered geometry, statistics and scratch budget now span C++/JSON/CLI/C ABI/pyure. Version-safe execution-config entry points preserve the old C struct layout; CUDA self-compute defaults remain unchanged and all not-yet-implemented provider/policy requests reject before rendering. Release 48/48 passed; cursor advanced to V.2. |
| 31 | 2026-07-28 V.2 | Hardened the CUDA self-compute BVH correctness baseline | Builder input/depth validation, checked shared closest/shadow traversal, mandatory stack/invalid fail-loud handling, robust AABB/tiny-triangle coverage and C++/C ABI/pyure acceleration statistics replaced silent fallback behavior. Exact Cornell hashes, Release 48/48 and Phase T/V gates passed; cursor advanced to V.3. |
| 32 | 2026-07-28 V.3 | Separated mesh BLAS from the instance TLAS | A checked world-space TLAS now replaces linear instance traversal for closest and shadow rays. Transform updates validate and refit retained topology while preserving BLAS allocations; versioned stats expose BLAS/TLAS memory, timing and visits. Multi-level instance, hot-update and exact CUDA reference gates passed; cursor advanced to V.4. |
| 33 | 2026-07-28 V.4 | Added measured SAH/SBVH and compact wide-node quality presets | Auto/fast retain reference-compatible BVH2; balanced emits 72-byte quantized BVH4 from binned object SAH; high quality emits 116-byte BVH8 with bounded spatial-reference duplication. A fixed 18,432-triangle GPU benchmark records build/trace/memory/work metrics and exact hit parity; C ABI v3 and pyure expose the added telemetry. Cursor advanced to V.5. |
| 34 | 2026-07-28 V.5 | Added bounded asynchronous acceleration construction, compact upload and memory budgets | Deterministic host batches respect conservative scratch reservations; final BLAS/TLAS data uses a two-entry pinned CUDA stream pipeline and rejects scratch/backend/device budget violations before OOM. C ABI v4, pyure and a machine-readable build/trace/VRAM report expose build wall/concurrency, upload, temporary, uncompacted and compact telemetry; cursor advanced to V.6. |
| 35 | 2026-07-28 V.6 | Productionized optional native RT provider construction | The SDK-free contract now covers multi-geometry build, compaction, refit/rebuild, scratch budgets and telemetry. Vulkan RT and DXR execute the lifecycle on available adapters; OptiX is SDK-optional, replaces the deleted false placeholders, and passed the actual provider gate against official v8.1.0 headers. Missing SDK/capability isolation remains tested; cursor advanced to V.7. |
| 36 | 2026-07-29 V.7 | Closed cross-provider traversal and hit parity | One SceneIR fixture now drives CUDA self-compute, actual OptiX IR raygen/miss/closest-hit, Vulkan RT and DXR. Closest/shadow visibility, non-uniform transforms, material/instance/primitive identity, UV/barycentric, geometric/shading normal, tangent/handedness and compact AOV semantics pass explicit thresholds with provider/compiler/driver evidence; cursor advanced to V.8. |
| 37 | 2026-07-29 V.8 | Established the clustered geometry resource and streaming contract | SDK-free deterministic meshlet construction preserves material/spectral/displacement/opacity/normal-field boundaries, conservative bounds, original primitive identity and multi-component physical LoD error. Canonical 16-byte GPU packing, required-page residency, budgeted partial upload and host/actual-CUDA fail-loud gates advance the cursor to V.9 while production cluster traversal stays disabled. |

### Consolidated Truth

- The authoritative build tree is `build_modular_x64` using Ninja and the VS 2022 x64 toolchain.
- Phase Q, Phase M, Phase R, and Phase T are complete; V.0-V.8 are complete and the authoritative construction cursor is V.9.
- The four generated glTF scenes and their three deterministic generator scripts are retained as project test assets.
- High-memory CUDA target compilation is limited by the Ninja `ur_cuda_heavy_compile` job pool (default depth 2); a 2026-07-28 stress run on the 16 GiB CUDA 13 workstation reduced the three heaviest translation-unit critical path from about 602 seconds serial to 362 seconds, while depth 3 was slower. Host and unrelated targets remain globally parallel.
