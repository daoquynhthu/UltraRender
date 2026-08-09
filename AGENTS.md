# AGENTS.md — UltraRender Project Governance

This file defines the rules, conventions, and workflow that any AI agent must follow when working on this project.

**Critical rule**: After every conversation compaction, context reset, session resume, tool merge, or dream cycle (memory consolidation), the agent **must read this file (AGENTS.md) in full, then use the PLAN.md index/search to read only the authoritative queue, current phase, dependencies, and directly relevant status sections** before taking project actions. Never load PLAN.md wholesale; it is intentionally long and must be accessed progressively.

---

## 1. Project Identity

**UltraRender** is a research-oriented spectral/polarimetric renderer whose current complete-scene reference backend is CUDA. Its forward roadmap develops automatic estimator composition, measurement reconstruction, a unified time-varying physical world, and differentiable inverse workflows without weakening the existing physically explicit boundaries. Phase PB currently has priority: it establishes a minimal client interaction grammar through one generated contract registry, a Windows x64 C ABI, and an isolated local worker without freezing those research subsystems.

### Architecture (Modular — Phase F Target State)

```
ure_types     — Header-only type library (INTERFACE). Vec3, Mat4, Quat, Ray, SceneIR, RenderConfig, World.
ure_runtime   — Backend-neutral GPU runtime contracts (STATIC, pure C++).
ure_transport — Backend-neutral observable, measure, estimator, Technique Graph, bounded support partition, composition, pilot qualification, portfolio scheduling, research-extension identity, uncertainty and compatibility contracts (STATIC, pure C++).
ure_research  — SDK-free research execution, transport experiment, joint-sample/reuse, artifact, comparison, capability, oracle and promotion contracts (STATIC, pure C++).
ure_reconstruction — SDK-free typed measurement, sufficient-statistics, statistical/sample reconstruction, canonical merge and checkpoint contracts (STATIC, pure C++).
ure_vulkan    — Vulkan 1.3 compute/acceleration runtime (STATIC, SDK-neutral public surface).
ure_d3d12     — Windows D3D12/DXR compute/acceleration runtime (STATIC, SDK-neutral public surface).
ure_core      — GPU rendering core (STATIC, CUDA 13+). Path tracer kernel, BVH, GPU driver, scene compiler.
ure_sceneio   — Scene I/O (STATIC, pure C++). glTF 2.0 parser, OBJ/legacy loader, stb_image, SPD loader.
ure_hydra     — Optional OpenUSD Hydra RenderDelegate plugin (MODULE; SDK-private to Phase U).
ure_diag      — Unified logging/diagnostics (INTERFACE, Phase Dx complete).
ure_config    — Config system (STATIC, pure C++, Phase I complete).
ure_physics   — Physics/acoustic (STATIC, pure C++, built optionally).
ure_cli       — Thin orchestrator EXE; links ure_core + ure_sceneio + ure_config.
```

### Phase PB Approved Target (implemented progressively)

```text
contracts      — Single source for public IDs, schemas, manifests, compatibility baselines and golden messages (PB.0-PB.1 implemented).
ure_public     — Generated C11-compatible loader/value headers; contains no renderer or backend implementation (PB.1 implemented as Candidate).
ure_contract   — Private adapter and product runtime DLL; translates public semantics to current internal modules (PB.2 current).
ure_worker     — Local Windows worker that loads the product runtime only through the public loader ABI (PB.4 target; PB.1 has only a renderer-free mock).
```

PB.0-PB.7 remain Candidate 0.x and create no stable public promise. The current `ure_c_api.h`, `pyure_native.dll`, and pyure ctypes surface remain legacy experimental during migration.

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
| V (GPU Acceleration) | Done | V.0-V.11 complete; unified local/farm validation freezes construction, traversal, memory, parity, dynamic and distributed evidence |
| W (Wave Optics Solver) | Done | W.0-W.12 complete within the declared production/reference boundary; unified physical, API, fail-loud, distributed and static validation closed |
| U (USD/Hydra Adapter) | Done | U.1-U.6 complete; schema adapter, actual-OpenUSD delegate, mesh/material conversion, progressive RenderSession bridge and strict native-to-USDA export closed |
| HO (High-order capabilities) | Paused after HR.2 | HO.0-HO.2, HT.0-HT.5 and HR.0-HR.2 complete; HR.3 resumes after Phase PB |
| PB (Public Boundary) | In progress | PB.0-PB.1 complete; current cursor: `PB.2 — Windows x64 loader ABI and candidate runtime product` |
| **Cleanup** | **Done** | **GPU tests include paths migrated; old `include/` + `src/` + `tests/{unit,integration}` + legacy CMake block removed** |

### Core Commitments
- Spectral rendering with runtime-configured multi-channel wavelength packets (current GPU packet cap 32)
- Polarization tracking via Stokes vectors and Mueller matrices
- Wavefront path tracing on CUDA, SIMT-optimized
- Physical correctness over performance tricks
- Textures are spectral resource carriers (`HostTexture` → RGB CUDA texture object or explicit source-sample spectral grid), not display RGB
- The high-order route preserves observable, measure, time, units, uncertainty and provenance instead of flattening them into RGB or mode enums
- Research may explore provisional algorithms; only evidence-backed Experimental/Production results may enter default execution paths

### Non-Goals (out of scope)
- CPU production integrator development; host code is limited to oracle, compilation, build, scheduling, and validation roles
- Adding features not in PLAN.md
- Random refactoring without a plan step
- Implementing an in-repository GUI or interactive viewport; the existing `gui/` tree is abandoned and MUST NOT be inspected, maintained, migrated, tested, or used as a design input. Future Studio/editor work is an independent PB client.
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
│   ├── ure_transport/               # SDK-free high-order transport semantics (STATIC, pure C++)
│   │   ├── include/ure/transport/   # semantics, Technique Graph, composition, pilot and portfolio contracts
│   │   └── src/                     # validation, composition, pilot qualification, scheduling, drift and shards
│   ├── ure_research/                # SDK-free executable research substrate (STATIC, pure C++)
│   │   ├── include/ure/research/    # manifest, artifact, experiment, transport, capability, oracle and promotion contracts
│   │   └── src/                     # deterministic allocation, storage, transport registry, comparison and validation
│   ├── ure_reconstruction/          # SDK-free measurement substrate (STATIC, pure C++)
│   │   ├── include/ure/reconstruction/ # typed planes, statistical/sample reconstruction, merge and checkpoint contracts
│   │   └── src/                     # schema validation, reconstruction research, sufficient statistics and authenticated storage
│   ├── ure_vulkan/                  # Vulkan 1.3 compute/acceleration runtime (STATIC)
│   │   ├── include/ure/             # SDK-neutral Vulkan runtime factory
│   │   └── src/                     # private Volk/Vulkan implementation
│   ├── ure_d3d12/                   # Windows D3D12/DXR runtime (STATIC)
│   │   ├── include/ure/             # SDK-neutral D3D12 runtime factory
│   │   └── src/                     # private D3D12/DXGI implementation
│   ├── ure_sceneio/                 # Scene I/O (STATIC, pure C++)
│   │   ├── include/ure/             # scene_io.hpp, spd_loader.hpp, image_loader.hpp
│   │   └── src/                     # gltf_scene_frontend.cpp, spd_loader.cpp, image_loader.cpp
│   ├── ure_hydra/                   # Optional OpenUSD/Hydra plugin (MODULE)
│   │   ├── src/                     # HdURE delegate and renderer plugin
│   │   └── resources/               # OpenUSD plugInfo metadata
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
└── tools/                           # Dev tools
```

**Production development happens in `libs/`, `apps/`, and `tests/`; Phase PB also owns `contracts/`, its code generator under `tools/`, and validation scripts.** The old monolithic `include/` and `src/` directories have been removed.
The repository `gui/` tree is abandoned and outside all development, audit, migration, test and architecture scope. Do not read it to infer frontend requirements.

---

## 4. Testing

### Mandatory Rules
- Every production code change must be verifiable; Research changes require the scoped reproducibility evidence defined by PLAN.md
- The relevant test gate must be **green** before reporting completion or graduation of any production phase
- GPU tests must be written for any kernel modification
- Host tests must continue to pass after production changes; isolated Research artifacts must not break the maintained build

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
| GPU core | `gpu_device`, `gpu_math`, `gpu_spectral`, `gpu_spectral_soa`, `gpu_hardware`, `gpu_render`, `gpu_instance`, `gpu_tangents`, `gpu_denoise`, `gpu_cuda_runtime`, `gpu_acceleration_contract`, `gpu_clustered_geometry`, `gpu_cluster_lod`, `gpu_dynamic_geometry`, `gpu_support_measure_composition`, `gpu_pilot_statistics`, `gpu_automatic_integrator` |
| GPU physics/contracts | `gpu_polarization`, `gpu_volume`, `gpu_contract`, `gpu_wave_optics` |
| Host core | `test_world`, `test_asset_pipeline`, `test_config`, `test_runtime_contract`, `test_high_order_semantics`, `test_research_substrate`, `test_technique_graph`, `test_support_measure_graph`, `test_pilot_qualification`, `test_portfolio_scheduler`, `test_transport_research`, `test_automatic_integrator`, `test_measurement_bundle`, `test_statistical_reconstruction`, `test_sample_reconstruction`, `test_acceleration_contract`, `test_clustered_geometry`, `test_cluster_lod`, `test_dynamic_geometry`, `test_resource_plan`, `test_execution_graph`, `test_multi_backend_schedule`, `test_spectral_oracle`, `test_wave_optics`, `test_local_fullwave`, `test_integrator`, `test_mie_phase` |
| Host scene/material/session | `test_native_scene`, `test_native_scene_ir`, `test_native_procedural_graph`, `test_native_script_build`, `test_native_resource_catalog`, `test_native_solver_contract`, `test_native_simulation_contract`, `test_native_tooling`, `test_native_adapter`, `test_usd_schema_adapter`, `test_native_compiled_cache`, `test_native_validation_suite`, `test_gltf_frontend`, `test_material_graph`, `test_materialx_io`, `test_session`, `test_distributed_file_io`, `test_distributed_wave_io` |
| Python | `test_pyure_smoke` |
| Vulkan | `vulkan_runtime`, `vulkan_acceleration` |
| D3D12 | `d3d12_runtime` |
| Multi-backend | `multi_backend_inventory` |
| Public boundary | `test_public_boundary_audit`, `test_contract_registry`, `test_public_headers_cpp`, `test_contract_codegen_compare`, `test_contract_schema_conform`, `test_public_loader_header_mirror`, `test_public_registry_header_mirror`, `test_contract_codegen_negative`, `test_golden_message_mirror`, `test_mock_worker_external_client` |
| Optional Hydra build | `test_hydra_render_delegate`, `test_hydra_plugin_discovery`, `test_hydra_mesh_rprim`, `test_hydra_material_sprim`, `test_hydra_render_buffer`, `test_hydra_progressive_render`, plus SDK-only `test_usda_export` |
| **CTest total** | **81 registered tests** in `build_modular_x64` |

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

### 6.3 Research Maturity Governance

The authoritative high-order PLAN distinguishes **Research**, **Experimental**, and **Production** maturity. Engineering gates control graduation, not whether an unresolved research question may be investigated.

- Research work must preserve a reproducible capsule: hypothesis, input/seed, baseline, metric, artifact identity, result and known failure domain. It does not require full ABI, backend, documentation or complete CTest closure unless it changes maintained production code.
- Experimental work must be explicitly opt-in, state its applicability and bias class, and carry repeated independent statistical or physical evidence. It cannot be described as a default capability.
- Production work must satisfy lifecycle, budget, fail-loud, API, backend, regression and documentation requirements relevant to its scope.
- A negative research result is valid evidence and may close a branch. Do not add production scaffolding merely to preserve a disproven approach.
- Existing fail-loud behavior may be permanent physical/mathematical policy, a resource boundary, missing evidence or accidental debt. Classify it through HO.0 before removal.

### 6.4 Public Contract Stability Governance

Phase PB follows `docs/Public_API_ABI_Architecture.md` and `docs/PB_Public_Boundary_PLAN.md` under the root PLAN cursor.

- PB.0-PB.7 artifacts are Candidate 0.x and MUST NOT be described as a stable ABI, stable protocol, supported public release, or compatibility promise.
- The stable Core is limited to interaction grammar and lifecycle. Integrators, MaterialGraph, SceneIR, RenderConfig, MeasurementBundle, WorldState, GPU scheduling, model formats, solvers and research algorithms remain internal or independently versioned extensions.
- Contract stability (`Core`/`StableExtension`/`UnstableExtension`), evidence maturity (`Research`/`Experimental`/`Production`), and runtime state (`Compiled`/`Available`/`Enabled`/`Applicable`) are independent axes.
- Before adding a Core field or function, prove that a versioned schema, capability, or extension cannot express the requirement. Otherwise reject the Core expansion.
- Exact-build Research extensions bind registry, runtime, provider and artifact identities and carry no cross-release compatibility promise.
- Breaking stable changes require a side-by-side runtime major and explicit migration/support decision; never mutate a published major in place.
- Core ABI 1.0 and Worker Protocol 1.0 may be declared only at PB.8 after mixed-version binary/protocol, lifetime, security, external-client E2E and documentation gates, plus explicit user approval.
- The current C API and pyure binding remain legacy experimental during migration. PB does not authorize their deletion.
- The abandoned repository GUI is never a contract consumer or evidence source. External clients integrate through generated SDK fixtures, the mock worker, and packaged runtimes.
- PB.0 maintains a complete Public Interaction Surface Ledger covering native formats/tooling, adapters, C++/C/Python/CLI surfaces, distributed/farm/provider contracts, installed headers, and excluded historical designs. PB.8 requires zero unclassified, duplicate-authority, bypass, or unresolved-migration entries.
- The user granted standing authorization on 2026-08-08 for autonomous commits that remain inside the approved PB plan after local VERIFY/REVIEW. Per-slice approval and progress prompts are not required; push, tag and public release still require separate authorization.

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
- **Compiler**: MSVC 19.51 through Visual Studio 2026 18.8.2
- **Windows SDK**: 10.0.28000
- **CUDA**: 13.3
- **GPU**: RTX 5060 Laptop (CC 12.0, 8 GB VRAM, 26 SMs)
- **Generator**: Ninja through `scripts/build_x64.ps1` with the Visual Studio 2026 x64 toolchain
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
| 38 | 2026-07-29 V.9 | Added physical-error cluster LoD selection | Shared host/CUDA selection evaluates ray footprint and camera/diffuse/glossy/specular/shadow/caustic path class against position, displacement, normal, opacity and spectral error while preserving resource boundaries and residency. A 256-ray CUDA gate proves unsafe preview proxies break every shadow/reflection visibility query while the physical selector preserves all exact results and still selects diffuse coarse LoD; cursor advanced to V.10. |
| 39 | 2026-07-29 V.10 | Added dynamic and deforming geometry lifecycle | SDK-free planning classifies rigid, deforming and topology-changing resources into TLAS/BLAS refit, rebuild, cluster-bounds refit or recluster actions with capability-aware fail-loud policy. SceneDiff mesh mutations are validated and transactional; CUDA executes rigid refit/rebuild and conservative full BLAS/TLAS rebuild for deformation/topology, with an 8×8 depth-AOV correctness and timing report; cursor advanced to V.11. |
| 40 | 2026-07-29 V.11 | Closed Phase V with unified acceleration validation | Stable local/farm reports aggregate dense build/trace/VRAM, async construction, four-provider parity, physical cluster LoD, dynamic updates and distributed v5 resource/worker/cache provenance. Negative report fixtures, five actual heterogeneous workers, static/documentation gates and Release 54/54 passed; cursor advanced to W.2. |
| 41 | 2026-07-29 W.2 | Integrated the explicitly enabled diffraction camera into the CUDA wavefront film | Normalized 360–830 nm PSF banks cover circular and regular-blade pupils, defocus and sensor-pixel quadrature. Terminal spectral contributions accumulate before CIE conversion; unsupported wave/integrator combinations fail before allocation. JSON/CLI, native URSC, C ABI v2 and pyure propagate optics; Release 54/54 and W.2/Q.7/physics-optics/schema gates passed; cursor advanced to W.5. |
| 42 | 2026-07-29 W.5 | Added production radiometric diffractive MaterialGraph operators | Grating, sinusoidal phase mask, ideal zone plate, blazed DOE and bounded RCWA/FMM tables now share SDK-free/native/MaterialX contracts, strict joint Jones passivity, deterministic host/device interpolation, UV-tangent order transport, Stokes response and fail-loud gates. Release 54/54 and W.5/schema/documentation gates passed; cursor advanced to W.6. |
| 43 | 2026-07-29 W.6 | Added bounded fluorescence and phosphorescence transport | SceneIR/native/MaterialX preserve normalized Stokes-shift excitation-emission resources. Forward host sampling and adjoint CUDA camera transport conserve radiant energy, update joint wavelength PDFs, preserve detector wavelength separately from transport wavelength, depolarize, retain medium identity and carry exponential lifetime delay. Unsupported modes and immutable hot updates fail loudly; cursor advanced to W.7. |
| 44 | 2026-07-29 W.7 | Established bounded partial-coherence reference transport | Hermitian PSD cross-spectral density, Gaussian-Schell sources, deterministic coherent realizations, Jones/OPL generalized rays, temporal/interferometric oracles and speckle statistics now share a bounded host contract. CUDA reduces weighted ensembles to mutual intensity, while raw-field film merge preserves coherent-before-incoherent averaging. Production partial-coherence sessions and serialized coherent farm frames remain fail-loud; cursor advanced to W.9. |
| 45 | 2026-07-29 W.9 | Established spectral anisotropic modal-segment transport | Positive-definite dielectric-impermeability and passive extinction tensors now drive transverse displacement eigenmodes and one exact complex generator for birefringence, dichroism and optical activity. Principal/biaxial, uniaxial, liquid-crystal and stress-optic factories, spectral interpolation and CUDA parity are bounded and fail closed. Scene-integrated interfaces, walk-off/ray splitting and production Jones queues remain unavailable; cursor advanced to W.10. |
| 46 | 2026-07-29 W.10 | Established bounded local full-wave coupling | Versioned SDK-free byte envelopes negotiate RCWA/FDTD/FEM/BEM/FMM/DDA/S-matrix providers by capability, binary and semantic identity. Exact-grid passive Jones tables, solver convergence/error/budget evidence and request/provider-bound deterministic cache entries fail closed before W.5 host/CUDA consumption. No solver, ambient subprocess policy or scene-scale Maxwell discretization is claimed; cursor advanced to W.11. |
| 47 | 2026-07-29 W.11 | Established coherent distributed sufficient-statistics transport | Distributed v6 distinguishes radiance, complex field, mutual intensity and coherent realization with phase/layout/source/group/range provenance. Content-digested complex/CSD files, transactional merge, overlap/corruption rejection and W.7 coherent-before-incoherent reduction prevent field-to-RGB flattening. Production coherent workers remain unavailable; cursor advanced to W.12. |
| 48 | 2026-07-29 W.12 | Closed the bounded Phase W validation program | A versioned validation suite binds source/artifact identities to analytic diffraction, complex thin-film phase, spectral/UV PDF, Stokes/Jones, fluorescence, energy, coherent merge order, fail-loud and API-parity evidence plus Release 56/56 and static gates. Production coherent scene transport remains unavailable by contract; cursor advanced to U.1. |
| 49 | 2026-07-29 U.1 | Established the USD-to-native schema adapter boundary | SDK-free normalized stage snapshots map units, axes, static meshes/spheres, cameras, basic MaterialGraph nodes, mesh rigid bodies and strong spectral resource metadata into validated Phase Q archives. Unsupported schemas, animation, non-TRS transforms and lossy geometry fail loudly; OpenUSD/Hydra integration remains U.2 onward. |
| 50 | 2026-07-29 U.2 | Established the actual OpenUSD Hydra delegate/plugin foundation | Optional `HdURE` derives from OpenUSD 25.05 `HdRenderDelegate`, owns resource/settings/stats lifecycle and is dynamically discovered through plugInfo. Explicit SDK roots prevent ambient coupling; the plugin advertises non-ready with no prim types until U.3/U.5, and two isolated SDK tests pass. |
| 51 | 2026-07-29 U.3 | Added the actual Hydra mesh RPrim mapping | `HdUREMesh` triangulates polygonal topology through OpenUSD, resolves indexed constant/uniform/vertex/varying/face-varying normals and UVs into immutable SceneIR geometry, retains exact affine transform/material/visibility/double-sided state, and updates revisions without rebuilding geometry for transform-only changes. Subdivision, instancing, invalid domains and non-finite or degenerate inputs reject explicitly; the optional SDK gate now has three tests and the cursor advances to U.4. |
| 52 | 2026-08-01 U.4 | Added bounded Hydra material conversion with explicit loss accounting | Actual `HdMaterial` SPrims normalize legacy/new Hydra networks and convert URE adapter nodes, Preview Surface, UV textures and primvar sets into immutable MaterialGraph resources. Accepted and rejected networks retain structured loss reports; unsupported connected semantics fail closed and dynamic updates revise state. Four actual-OpenUSD SDK tests and the 57/57 native-sm_120 main gate pass on Visual Studio 2026 18.8.2, MSVC 19.51, CUDA 13.3 and Windows SDK 10.0.28000; cursor advances to U.5. |
| 53 | 2026-08-01 U.5 | Connected Hydra viewport execution to the native progressive session | Actual camera, float render-buffer and render-pass objects lower retained mesh/material state into validated SceneIR, bake exact affine transforms, execute one synchronous CUDA `RenderSession` pass per Hydra execute, publish Beauty/existing AOVs and reset on camera changes. The GPU-enabled plugin is selectable without OpenGL/Vulkan/window contexts; six actual-OpenUSD tests, install discovery and static gates pass; cursor advances to U.6. |
| 54 | 2026-08-01 U.6 | Closed Phase U with deterministic native-to-USDA export | SDK-free export maps bounded Preview Surface materials, shared mesh prototypes/instances, spheres, camera and rigid metadata to canonical USDA. Strict mode rejects every loss; documented-loss mode requires a durable JSON report, and unsupported native semantics remain errors. `.ure/.urescene` and explicitly selected `.urepkg` scenes, actual CLI execution, atomic publication and real OpenUSD resolution are gated; the unified Phase U suite advances the cursor to Phase X. |
| 55 | 2026-08-01 HO route switch | Replaced the completed construction roadmap with the high-order research and implementation program | The legacy PLAN is archived. The new authority begins at `HO.0 — 能力债务普查与研究基线`, keeps Phase X frozen, and coordinates automatic transport portfolios, MeasurementBundle/reconstruction, unified WorldState/coupling, and differentiable inverse workflows under Research/Experimental/Production maturity. |
| 56 | 2026-08-01 HO.0 | Closed the executable capability and research baseline | A source-bound ledger uniquely classifies 92 explicit diagnostics into nine active groups and records one resolved stale Hydra group. Eight integrators, ten measurement gaps, eleven state owners, Research Capsule v1 positive/negative examples and seven benchmark families are machine-validated; the cursor advances to `HO.1 — 统一语义与架构合同`. |
| 57 | 2026-08-01 HO.1 | Established executable unified semantic contracts | Shared semantic identities, SI dimensions/units and rational time values plus the SDK-free `ure_transport` module define typed observables, canonical measures/support, estimator density/normalization/correlation/bias, uncertainty and five-state compatibility. Static, host, independent SDK-free, installed-package and Release 58/58 gates pass; the cursor advances to `HO.2 — 可执行研究底座`. |
| 58 | 2026-08-01 HO.2 | Established the executable research substrate | SDK-free `ure_research` now provides topology-neutral deterministic sample/counter allocation, indexed and authenticated partial artifact reads, replicated confidence comparison, maturity-aware capability negotiation, bounded oracle hooks and typed promotion evidence. Static, host, independent SDK-free, installed-package and Release 59/59 gates pass; the cursor advances to `HT.0 — 现有积分器描述化`. |
| 59 | 2026-08-01 HT.0 | Described the legacy integrator surface as an executable Technique Graph | SDK-free descriptors cover wavefront, guiding, ReSTIR DI/PT, SMS, BDPT, VCM and PSSMLT with typed estimator/resource semantics, stable graph identities and mathematical/resource/unimplemented rejection classes. The legacy preset preserves current routes while a static ledger freezes new mode-only estimator decisions. Independent SDK-free 7/7, installed-package and Release 60/60 gates pass; the cursor advances to `HR.0 — MeasurementBundle / Feature Film`. |
| 60 | 2026-08-01 HR.0 | Established the typed MeasurementBundle and feature-film data boundary | SDK-free schemas preserve typed observables, units, validity, provenance and explicit retention loss. Canonical distributed merge recomputes ESS/variance/covariance from sufficient statistics, while self-contained authenticated checkpoints support front-index inspection and partial plane reads. Independent SDK-free 8/8, installed-package and Release 61/61 gates pass; the cursor advances to `HT.1 — Support/Measure Graph 与组合器`. |
| 61 | 2026-08-01 HT.1 | Established exact bounded support and measure-aware estimator composition | Finite path grammars compile to deterministic automata and an exact target/technique product partition with witnessed hole/outside-target rejection. Canonical Jacobians, balance/power MIS, GRIS provenance, independent contributions, normalized MCMC replicates and strict output layers share one SDK-free plan plus CUDA packed execution. Independent SDK-free 9/9, installed-package and Release 63/63 gates pass; the cursor advances to `HT.2 — Pilot 统计与自动资格判定`. |
| 62 | 2026-08-01 HT.2 | Established provenance-bound pilot statistics and automatic qualification | Per-technique cost, variance, paired covariance, tail risk, ESS and memory evidence bind graph/world/snapshot/support identity. Independent, cross-fitted and probability-corrected pilot policies protect adaptive selection; automatic scene/event/backend/budget/output-layer decisions replace user mode knowledge while bounded expert overrides remain experimental. CUDA ingestion, SDK-free 10/10, installed package and Release 65/65 gates pass; the cursor advances to `HT.3 — 在线 portfolio 调度`. |
| 63 | 2026-08-01 HT.3 | Established deterministic online portfolio scheduling | Content-bound tile/wavelength/time/device/sample/chain domains now receive cost/covariance-aware integer allocations with exploration, starvation recovery and MCMC chain separation. Drift triggers local/global re-pilot; compatible worker shards require exact coverage; MeasurementBundle merge/checkpoint preserves the schedule identity with v1 read compatibility. SDK-free 11/11, installed package and Release 66/66 gates pass; the cursor advances to `HT.4 — 新积分器与 proposal 研究平台`. |
| 64 | 2026-08-01 HT.4 | Established the transport research platform | Capsule-bound ResearchExtension nodes, mechanism-specific joint samples, world/reuse validity, explicit opt-in graph materialization and replicated assessments now form an SDK-free research path. An eight-replicate analytic control variate materially reduced equal-cost variance but remains Research; ordinary sample covariance across an MCMC chain is recorded as a negative result. SDK-free 12/12, installed-package and Release 67/67 gates pass; the cursor advances to `HT.5 — 自动积分系统闭环`. |
| 65 | 2026-08-01 HT.5 | Closed the automatic integration system | Objective-bound automatic plans preserve graph/support/schedule/world provenance, coverage, normalization and uncertainty. The CUDA bridge uses disjoint pilot/production ranges, defensive wavefront coverage, sequential endpoint contexts, precision-weighted unbiased Beauty aggregation and explicit AOV/budget boundaries. Three scenes with independent repeats validate statistics, tail, time, measured/estimated VRAM and reordered merge; SDK-free 13/13, installed package and Release 69/69 gates pass. Cursor advances to `HR.1 — 统计重建基线`. |
| 66 | 2026-08-01 HR.1 | Established the training-free statistical reconstruction baseline | Content-bound raw estimates, ESS/variance, tail evidence, motion/time confidence, disocclusion tests and physical Spectrum/Stokes filtering now produce uncertainty, support and rejection provenance without display-domain flattening. Bounded CUDA temporal/à-trous kernels match the SDK-free host oracle; missing complete-scene planes prevent silent default activation. SDK-free 14/14, installed package and Release 70/70 gates pass; cursor advances to `HR.2 — Sample-level 与光谱/偏振重建`. |
| 67 | 2026-08-08 HR.2 | Established the sample-level spectral/polarimetric reconstruction Research boundary | Content-bound records preserve technique/path/spectral/phase metadata; canonical analytic splatting and explicitly opted-in batch/provider/artifact-bound external kernel/point-set/hybrid outputs remain Research. Spectrum observation, Stokes cone, Jones gauge, OOD, calibration, adversarial and positive/negative capsule gates pass. SDK-free 15/15, installed package and Release 71/71 gates pass; cursor advances to `HR.3 — Learned proposal 与 neural control variate`. |
| 68 | 2026-08-08 PB route switch | Approved the minimal public client boundary and suspended HR.3 until it closes | One generated contract registry will drive a Windows x64 C ABI and local Named Pipe/shared-memory worker. PB.0-PB.7 are Candidate 0.x; stable Core freezes lifecycle grammar, not features. The legacy C API remains experimental, the repository GUI is abandoned, and only PB.8 may declare ABI/Protocol 1.0 after mixed-version, security, lifetime and external-client gates. |
| 69 | 2026-08-08 PB.0 | Closed the complete public interaction-surface and legacy compatibility baseline | The machine-validated ledger assigns 25 native/adapter/client/distributed/provider/historical surfaces to 14 unique authority domains with explicit convergence gates. The 534-line/55-function legacy C header and SHA-bound DLL classify 2,030 exports (55 intended C, 1,970 C++, 5 other accidental); a retained C11 binary client, deterministic report and negative fixtures plus Release 72/72 close PB.0. |
| 70 | 2026-08-09 PB.1 | Closed the generated Candidate 0.1 SDK and mock frontend kit | A strict 53-entry registry, domain-separated digest, deterministic generator, C11 headers, explicit-ID FlatBuffers schemas, manifests/reference and 12 golden exchanges form a self-contained staging package. A real future-schema unknown field, malformed/truncated/oversized inputs, schema conformance, clean regeneration, renderer-free mock harness/worker and standalone C client are gated. The legacy DLL gained `/Brepro` after two-relink proof exposed and removed its timestamp drift; its refreshed 2,029-export baseline retains all 55 intended C exports and drops one accidental C++ export. The cursor advances to PB.2 without creating a stable promise or real runtime DLL. |

### Consolidated Truth

- The authoritative build tree is `build_modular_x64` using Ninja and the Visual Studio 2026 x64 toolchain.
- Phase Q, Phase M, Phase R, Phase T, Phase V, the declared bounded scope of Phase W, Phase U, HO.0-HO.2, HT.0-HT.5, HR.0-HR.2 and PB.0-PB.1 are complete. HR.3 is suspended and the authoritative cursor is `PB.2 — Windows x64 loader ABI and candidate runtime product`. The approved architecture and detailed execution authority are `docs/Public_API_ABI_Architecture.md` and `docs/PB_Public_Boundary_PLAN.md`. PB establishes a minimal client boundary, not the former Phase X plugin ecosystem; PB.0-PB.7 remain Candidate 0.x with no stable promise.
- The CUDA automatic bridge does not yet populate every high-order measurement plane, HR.1/HR.2 reconstruction remains explicit, and HR.2 ships no trained model or production inference ABI. Future high-order capabilities enter the public boundary through independently versioned extensions instead of expanding stable Core.
- The repository `gui/` tree is abandoned and excluded from inspection, development, migration and acceptance evidence. A future Studio/editor is an independent client of generated PB fixtures and packages.
- The four generated glTF scenes and their three deterministic generator scripts are retained as project test assets.
- High-memory CUDA target compilation is limited by the Ninja `ur_cuda_heavy_compile` job pool. The default is memory-aware: depth 1 below 24 GiB and depth 2 otherwise. CUDA 13.3 exposed multi-`ptxas` allocation failure on the 16 GiB workstation, so its current stable default is 1 while host and unrelated targets remain globally parallel. CUDA architecture defaults to the local native GPU unless explicitly overridden for release or farm builds.
