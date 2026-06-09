# UltraRender 升级路线图 (PLAN.md)

最后更新: 2026-06-09 (含 Phase P: 运行时数据管线)

本文档是唯一的行动纲领。所有开发工作必须严格按照此计划分阶段执行。不允许跳过阶段、合并阶段或擅自引入计划外改动。

---

## 总览

```
旧 Phase 1-4:   GPU 正确性修复                                    已完成
────────────────────────────────────────────────────────────────────
新 Phase 0:     硬件检测 + 自动配置                                 基础层
新 Phase F:     目录树重构 + CMake 库分离                            架构
新 Phase P:     运行时数据管线（持久场景+热更新+ECS/World）           数据架构
新 Phase G:     glTF 2.0 完整化 + 光谱扩展                          IO
新 Phase H:     资产管线 (stb_image + SPD)                          IO
新 Phase I:     配置系统 (JSON + CLI11 + 子命令)                     CLI
新 Phase A:     SoA 队列 + RenderConfig 集成                        渲染
新 Phase B:     多 GPU 单机                                         渲染
新 Phase C:     分布式契约标准化                                    分布式
新 Phase D:     分布式集成                                          分布式
新 Phase E:     N 通道光谱升级                                      光谱
```

---

## 架构概览

```
                      ┌──────────┐
                      │ ure_cli  │    ← EXE (薄壳, 只做编排)
                      └────┬─────┘
             ┌─────────────┤
       ┌─────▼────┐  ┌────▼─────┐  ┌──────▼──────┐
       │ ure_core │  │ure_sceneio│  │  ure_config │
       │ (CUDA)   │  │ (纯 C++)  │  │  (纯 C++)   │
       └────┬─────┘  └────┬─────┘  └──────┬──────┘
            │             │               │
            └──────┬──────┴──────┬────────┘
                   │             │
             ┌─────▼─────┐ ┌────▼──────┐
             │ ure_types │ │ure_physics│
             │(HEADER    │ │ (纯 C++)  │
             │ ONLY)     │ │           │
             └───────────┘ └───────────┘
```

**依赖方向朝下**，不反向循环。

| 库 | 类型 | CUDA? | 依赖 |
|----|------|-------|------|
| `ure_types` | INTERFACE (header-only) | 否 | 无 |
| `ure_core` | STATIC | 是 (CUDA 13+) | `ure_types` |
| `ure_sceneio` | STATIC | 否 | `ure_types` |
| `ure_config` | STATIC | 否 | `ure_types` |
| `ure_physics` | STATIC | 否 | `ure_types` |
| `ure_cli` | EXE | 运行时链接 CUDA | `ure_core` + `ure_sceneio` + `ure_config` |

`ure_types` 是纯头文件库，包含 `core/` 数学、`SceneIR`、`RenderConfig`、（Phase P 后）`World` 组件类型。**不包含任何 CUDA 代码**，因此 `ure_sceneio` / `ure_config` / `ure_physics` 无需链接 CUDA。这是关键设计：CUDA 污染被严格隔离在 `ure_core` 内部。

---

## 运行时数据架构（Phase P 目标状态）

```
                              World (ECS Registry)
                   ┌──────────────┼──────────────┐
                   │              │              │
             PhysicsSystem   RenderSystem    AudioSystem
                   │              │              │
         ┌─────────┤              │              │
     TransformPool  │              │              │
     (写入 position/rotation)     │              │
                   │              │              │
                   ▼              │              │
          TransformRingBuffer     │              │
          (double buffer, 3帧滞后) │              │
                   │              │              │
                   ├──────────────┤              │
                   │   (读取 transform)          │
                   ▼                             │
             GPU Transform Buffer                │
            (cudaMemcpyAsync, 不阻塞渲染)         │
                                                 │
                   ┌──────────────────────────────┘
                   │  ISpatialQuery (PhysicsWorld 实现)
                   ▼
         AcousticRayTracer
```

三系统关系:
- **Physics ↔ Render**：通过 TransformPool 间接通信，不直接写同一个结构体
- **Audio ↔ Physics**：通过 ISpatialQuery 接口查询，不直接依赖 PhysicsWorld 类型
- **Audio ↔ Render**：零耦合

---

## 目录树设计（目标状态）

```
E:\Render Engine\
│
├── CMakeLists.txt                          # 顶层: add_subdirectory() 各模块
│
├── cmake/
│   └── FetchVendors.cmake                  # 下载第三方库 (stb, CLI11, json)
│
├── libs/
│   │
│   ├── ure_types/                          # ─── 纯头文件类型库 (INTERFACE) ───
│   │   ├── CMakeLists.txt                  # add_library(ure_types INTERFACE)
│   │   └── include/ure/
│   │       ├── core/
│   │       │   ├── vector.hpp              # ⬅ include/core/vector.hpp
│   │       │   ├── matrix.hpp              # ⬅ include/core/matrix.hpp
│   │       │   ├── quaternion.hpp          # ⬅ include/core/quaternion.hpp
│   │       │   ├── ray.hpp                 # ⬅ include/core/ray.hpp
│   │       │   ├── aabb.hpp                # ⬅ include/core/aabb.hpp
│   │       │   └── interaction.hpp         # ⬅ include/core/interaction.hpp
│   │       ├── scene_ir.hpp                # ⬅ include/scene/scene_ir.hpp
│   │       ├── render_config.hpp           # ⬅ Phase 0 (无 CUDA 依赖)
│   │       └── world.hpp                   # ⬅ Phase J: World + ComponentPool (新增)
│   │
│   ├── ure_core/                           # ─── GPU 渲染核心 (STATIC) ───
│   │   ├── CMakeLists.txt                  # CUDA + CXX 混合编译
│   │   ├── include/ure/
│   │   │   ├── api.hpp                     # GpuEngine 公共类
│   │   │   ├── gpu_structs.hpp             # ⬅ include/gpu/gpu_structs.hpp
│   │   │   ├── gpu_math_functions.cuh      # ⬅ include/gpu/gpu_math_functions.cuh
│   │   │   ├── gpu_spectrum_utils.cuh      # ⬅ include/gpu/gpu_spectrum_utils.cuh
│   │   │   ├── path_tracer_sampling.cuh    # ⬅ include/gpu/path_tracer_sampling.cuh
│   │   │   ├── gpu_hardware.hpp            # ⬅ Phase 0 (含 CUDA 类型)
│   │   │   ├── gpu_scene_loader.hpp        # ⬅ include/gpu/gpu_scene_loader.hpp
│   │   │   ├── material_library.hpp        # ⬅ include/gpu/material_library.hpp
│   │   │   ├── bvh_builder.hpp             # ⬅ include/gpu/bvh_builder.hpp
│   │   │   ├── gpu_driver.hpp              # ⬅ include/gpu/gpu_driver.hpp
│   │   │   ├── gpu_scene_compiler.hpp      # ⬅ include/api/gpu_scene_compiler.hpp
│   │   │   ├── scene_ir_compiler.hpp       # ⬅ include/api/scene_ir_compiler.hpp
│   │   │   ├── instance_desc.hpp           # ⬅ F.6: GpuInstanceDesc (新增)
│   │   │   ├── instance_transform.hpp      # ⬅ F.6: GpuInstanceTransform (新增)
│   │   │   └── spectral/
│   │   │       ├── spectral.hpp            # ⬅ include/spectral/spectral.hpp
│   │   │       └── cie_data.hpp            # ⬅ include/spectral/cie_data.hpp
│   │   │
│   │   └── src/
│   │       ├── path_tracer_kernel.cu       # ⬅ src/gpu/path_tracer_kernel.cu
│   │       ├── path_tracer_material.cu     # ⬅ src/gpu/path_tracer_material.cu
│   │       ├── path_tracer_raygen.cu       # ⬅ src/gpu/path_tracer_raygen.cu
│   │       ├── path_tracer_denoise.cu      # ⬅ src/gpu/path_tracer_denoise.cu
│   │       ├── path_tracer_post.cu         # ⬅ src/gpu/path_tracer_post.cu
│   │       ├── gpu_driver.cu               # ⬅ src/gpu/gpu_driver.cu
│   │       ├── gpu_hardware.cu             # ⬅ Phase 0
│   │       ├── gpu_scene_loader.cpp        # ⬅ src/gpu/gpu_scene_loader.cpp
│   │       ├── gpu_engine_impl.cpp         # ⬅ src/api/gpu_engine_impl.cpp
│   │       ├── gpu_scene_compiler.cpp      # ⬅ src/api/gpu_scene_compiler.cpp
│   │       ├── scene_ir_compiler.cpp       # ⬅ src/api/scene_ir_compiler.cpp
│   │       ├── bvh_builder.cpp             # ⬅ src/gpu/bvh_builder.cpp
│   │       └── bvh_accelerator.cpp         # ⬅ src/accelerators/bvh_accelerator.cpp
│   │
│   ├── ure_sceneio/                        # ─── 场景格式 + 资产 (STATIC, 纯 C++) ───
│   │   ├── CMakeLists.txt
│   │   ├── include/ure/
│   │   │   └── scene_io.hpp                # load_scene(), load_image(), load_spd()
│   │   └── src/
│   │       ├── gltf_scene_frontend.cpp     # ⬅ src/api/gltf_scene_frontend.cpp
│   │       ├── scene_ir_frontend.cpp       # ⬅ src/api/scene_ir_frontend.cpp
│   │       ├── scene_frontend.cpp          # ⬅ src/api/scene_frontend.cpp
│   │       ├── scene_parser.cpp            # ⬅ src/api/scene_parser.cpp
│   │       ├── procedural.cpp              # ⬅ src/api/procedural.cpp
│   │       ├── image_loader.cpp            # ⬅ src/io/image_loader.cpp (stb)
│   │       ├── image_saver.cpp             # ⬅ src/io/image_saver.cpp
│   │       ├── spd_loader.cpp              # 新建
│   │       └── scene/
│   │           ├── scene_ir.cpp            # ⬅ src/scene/scene_ir.cpp
│   │           ├── scene.cpp               # ⬅ src/scene/scene.cpp
│   │           ├── scene_factory.cpp       # ⬅ src/scene/scene_factory.cpp
│   │           ├── camera.cpp              # ⬅ src/scene/camera.cpp
│   │           ├── mesh.cpp                # ⬅ src/scene/mesh.cpp
│   │           ├── sphere.cpp              # ⬅ src/scene/sphere.cpp
│   │           ├── triangle.cpp            # ⬅ src/scene/triangle.cpp
│   │           └── obj_loader.cpp          # ⬅ src/scene/obj_loader.cpp
│   │
│   ├── ure_config/                         # ─── 配置系统 (STATIC, 纯 C++) ───
│   │   ├── CMakeLists.txt
│   │   ├── include/ure/
│   │   │   └── config.hpp                  # CLI 解析 + JSON 配置 + 覆盖链
│   │   └── src/
│   │       └── config_parser.cpp           # 新建
│   │
│   └── ure_physics/                        # ─── 物理/声学 (STATIC, 纯 C++) ───
│       ├── CMakeLists.txt
│       ├── include/ure/physics/
│       │   ├── physics_world.hpp           # ⬅ include/physics/physics_world.hpp
│       │   ├── fluid_system.hpp            # ⬅ include/physics/fluid_system.hpp
│       │   ├── rigid_body.hpp              # ⬅ include/physics/rigid_body.hpp
│       │   ├── collider.hpp                # ⬅ include/physics/collider.hpp
│       │   ├── physics_events.hpp          # ⬅ include/physics/physics_events.hpp
│       │   ├── marching_cubes.hpp          # ⬅ include/physics/marching_cubes.hpp
│       │   ├── ispatial_query.hpp          # ⬅ F.9: 空间查询抽象接口 (新增)
│       │   └── acoustic/
│       │       ├── acoustic_system.hpp     # ⬅ include/acoustic/acoustic_system.hpp
│       │       ├── acoustic_ray_tracer.hpp # ⬅ include/acoustic/acoustic_ray_tracer.hpp
│       │       ├── spatial_processor.hpp   # ⬅ include/acoustic/spatial_processor.hpp
│       │       ├── modal_factory.hpp       # ⬅ include/acoustic/modal_factory.hpp
│       │       └── types.hpp               # ⬅ include/acoustic/types.hpp
│       └── src/
│           ├── physics_world.cpp           # ⬅ src/physics/physics_world.cpp
│           ├── fluid_system.cpp            # ⬅ src/physics/fluid_system.cpp
│           ├── marching_cubes.cpp          # ⬅ src/physics/marching_cubes.cpp
│           ├── acoustic_system.cpp         # ⬅ src/acoustic/acoustic_system.cpp
│           ├── acoustic_ray_tracer.cpp     # ⬅ src/acoustic/acoustic_ray_tracer.cpp
│           ├── spatial_processor.cpp       # ⬅ src/acoustic/spatial_processor.cpp
│           └── modal_factory.cpp           # ⬅ src/acoustic/modal_factory.cpp
│
├── apps/                                    # ─── 可执行程序 ───
│   └── ure_cli/
│       ├── CMakeLists.txt                   # EXE, 链接 ure_core + ure_sceneio + ure_config
│       └── src/
│           └── main.cpp                     # ⬅ src/main.cpp (Phase J 重写: ECS 编排)
│
├── tests/
│   ├── gpu/                                 # ─── GPU 测试 (链接 ure_core) ───
│   │   ├── CMakeLists.txt
│   │   ├── test_framework.cuh
│   │   ├── test_device.cu
│   │   ├── test_math_functions.cu
│   │   ├── test_spectral_pipeline.cu
│   │   ├── test_render_basic.cu
│   │   └── test_hardware.cu
│   │
│   └── host/                                # ─── Host 测试 (纯 C++, 链接 ure_sceneio/ure_config)
│       ├── CMakeLists.txt
│       ├── test_config.cpp
│       ├── test_scene_io.cpp
│       ├── test_spd_loader.cpp
│       └── test_distributed_contract.cpp
│
├── third_party/                             # ─── Vendored 依赖 ───
│   ├── stb/
│   │   ├── stb_image.h
│   │   └── stb_image_write.h
│   ├── CLI11/
│   │   └── CLI11.hpp
│   └── nlohmann/
│       └── json.hpp
│
├── scenes/                                  # ─── 场景文件资源 ───
│   ├── *.gltf / *.glb
│   ├── *.scene                              # 遗留格式, 过渡期后废弃
│   └── textures/
│
├── include/                                 # ═══ 旧目录: Phase F 前使用, 后冻结 ═══
│   ├── gpu/  api/  scene/  io/  core/  spectral/
│   ├── accelerators/  materials/  integrators/  physics/  acoustic/
│   └── ...
│
└── src/                                     # ═══ 旧目录: Phase F 后冻结 ═══
    ├── gpu/  api/  scene/  io/  main.cpp
    ├── accelerators/  materials/  integrators/  physics/  acoustic/
    └── ...
```

---

## 执行顺序

```
Phase 0 ─→ Phase F ─┬──→ Phase P ───→ Phase A ─┬──→ Phase B ─┐
                     │                           │            ├──→ Phase D
                     ├──→ Phase G ──┐            │            │
                     │              ├──→ Phase I └──→ Phase C ┘
                     └──→ Phase H ──┘                         │
                                                               ▼
                                                            Phase E
```

Phase G / Phase H / Phase I / Phase C 与 Phase J 无依赖关系，可并行执行。

### 依赖保证

- **Phase 0** (硬件检测) → 纯新增，不依赖现存代码
- **Phase F** (目录树 + CMake) → 将 Phase 0 文件迁入新位置
- **Phase P** (运行时数据管线) → 依赖 Phase F 完成；为 Phase A/B/C 的前置
- **Phase G/H** (格式/资产) → 依赖 Phase F 的目录结构，与 Phase J 可并行
- **Phase I** (配置) → 依赖 Phase G/H 的资产管线，与 Phase J 可并行
- **Phase A** (SoA 队列) → 依赖 Phase P 的 Transform/World 架构 + Phase 0 的 RenderConfig
- **Phase C** (契约) → 无依赖，可插队
- **Phase E** (N 通道光谱) → 依赖 Phase A (SoA) + Phase G (glTF 光谱扩展)，放最后

---

## ████████ 旧 Phase 1-4（已完成） ████████

- Phase 1: 能量守恒 + scatter 统一 (commit 5327cdf)
- Phase 2: 嵌套 IOR + NEE Dielectric (commit 50b9a08)
- Phase 3: GPU 测试基础设施 (commit 6187cf1)
- Phase 4: 代码模块化 + 访问器 + raygen 提取 (commit c335ab0)

---

## ████████ Phase 0: 硬件检测 + 自动配置 ████████

**目标**: 建立硬件查询和自动调参能力，不改任何渲染逻辑。

**位置**: 当前在旧目录 `include/gpu/` `src/gpu/`，Phase F 时搬到 `libs/ure_core/`。

**文件**: `gpu_hardware.hpp`, `gpu_hardware.cu`, `render_config.hpp`, `test_hardware.cu`

### Step 0.1 — GpuHardwareInfo + query_hardware()

```cpp
struct GpuHardwareInfo {
    int device_count;
    int sm_count;
    int cc_major, cc_minor;
    size_t total_global_memory;
    size_t l1_cache_per_sm;
    int max_threads_per_block;
    int warp_size;
    float memory_bandwidth_gb_s;
};
GpuHardwareInfo query_hardware(int device_id = 0);
void print_hardware_info(const GpuHardwareInfo& hw);
```

使用 `cudaDeviceGetAttribute()` API，兼容 CUDA 13+。

### Step 0.2 — RenderConfig + auto_configure()

```cpp
struct RenderConfig {
    int queue_capacity;
    int max_trace_depth;
    int num_wavelengths;
    int wg_size;
    int rays_per_block;
    int samples_per_pass;
    int num_gpus_to_use;
};
RenderConfig auto_configure(const GpuHardwareInfo& hw, int w, int h, int scene_N);
```

硬件上限:

| VRAM | N 上限 | 队列上限 |
|------|:------:|:--------:|
| < 6 GB | 8 | 1M |
| 6-16 GB | 64 | 4M |
| 16-32 GB | 128 | 8M |
| 32-64 GB | 256 | 16M |
| > 64 GB | 512 | 32M |

### Step 0.3 — 测试

6 个测试覆盖全部 VRAM 档次 + 场景 N 低于上限的情况。

---

## ████████ Phase F: 目录树重构 + CMake 库分离 ████████

**目标**: 从单 EXE 拆分为多库。不改变运行时数据/算法逻辑。

**完成判据**: F.1-F.5（目录结构 + CMake + 基础编译验证）

### Step F.1 — 创建目录结构

创建 `libs/`, `apps/`, `cmake/`, `third_party/`, `tests/host/` 及其子目录。

### Step F.2 — 复制文件到新位置

严格按上方目录树复制。**旧目录保留不动**，确保旧 CMake 仍能编译。

### Step F.3 — 编写新 CMakeLists.txt

**顶层**:
```cmake
cmake_minimum_required(VERSION 3.25)
project(UltraRender VERSION 1.0.0 LANGUAGES CXX CUDA)

option(UR_BUILD_TESTS "Build tests" ON)
option(UR_BUILD_CLI "Build CLI" ON)
option(UR_BUILD_PHYSICS "Build physics/acoustic" ON)

add_subdirectory(libs/ure_types)

add_subdirectory(libs/ure_config)
add_subdirectory(libs/ure_sceneio)
add_subdirectory(libs/ure_core)

if(UR_BUILD_PHYSICS)
    add_subdirectory(libs/ure_physics)
endif()

if(UR_BUILD_CLI)
    add_subdirectory(apps/ure_cli)
endif()

if(UR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/gpu)
    add_subdirectory(tests/host)
endif()
```

**`libs/ure_types/CMakeLists.txt`**:
```cmake
add_library(ure_types INTERFACE)
target_include_directories(ure_types INTERFACE include)
```

**`libs/ure_core/CMakeLists.txt`**:
```cmake
add_library(ure_core STATIC
    src/path_tracer_kernel.cu
    src/path_tracer_raygen.cu
    src/path_tracer_denoise.cu
    src/path_tracer_post.cu
    src/gpu_driver.cu
    src/gpu_hardware.cu
    src/gpu_scene_loader.cpp
    src/gpu_engine_impl.cpp
    src/gpu_scene_compiler.cpp
    src/scene_ir_compiler.cpp
    src/bvh_builder.cpp
    src/bvh_accelerator.cpp
)
# 注意: path_tracer_material.cu 被 kernel.cu 末尾 #include，不单独编译
target_include_directories(ure_core PUBLIC include)
target_link_libraries(ure_core PUBLIC ure_types ${CUDA_LIBRARIES})
set_target_properties(ure_core PROPERTIES CUDA_ARCHITECTURES "all-major")
```

**`libs/ure_sceneio/CMakeLists.txt`**:
```cmake
add_library(ure_sceneio STATIC
    src/gltf_scene_frontend.cpp
    src/scene_ir_frontend.cpp
    src/scene_frontend.cpp
    src/scene_parser.cpp
    src/procedural.cpp
    src/image_loader.cpp
    src/image_saver.cpp
    src/scene/scene_ir.cpp
    src/scene/scene.cpp
    src/scene/scene_factory.cpp
    src/scene/camera.cpp
    src/scene/mesh.cpp
    src/scene/sphere.cpp
    src/scene/triangle.cpp
    src/scene/obj_loader.cpp
)
target_include_directories(ure_sceneio PUBLIC include)
target_link_libraries(ure_sceneio PUBLIC ure_types)
# 无 CUDA 依赖
```

**`apps/ure_cli/CMakeLists.txt`**:
```cmake
add_executable(ure_cli src/main.cpp)
target_link_libraries(ure_cli PRIVATE ure_core ure_sceneio ure_config)
```

**`tests/gpu/CMakeLists.txt`**: 每个 test 直接 `target_link_libraries(ure_core)`。

**`tests/host/CMakeLists.txt`**: 每个 test 链接 `ure_sceneio` 或 `ure_config`。

### Step F.4 — 旧 CMakeLists.txt 退役

在顶层新增 `option(UR_USE_OLD_BUILD "Use legacy build" OFF)`，旧 CMakeLists.txt 内容只在 `UR_USE_OLD_BUILD=ON` 时编译。最终删除。

### Step F.5 — 验证

- 所有 GPU 测试在新 CMake 下通过
- `ure_cli --help` 输出正确
- `ure_types` 被所有 lib 引用且无编译歧义

（本阶段不再包含运行时架构改动；该类工作统一前移到 Phase P 执行。）

---

## ████████ Phase P: 运行时数据管线（持久场景 + 热更新 + ECS/World） ████████

**目标**: 将单程序式的场景重建流程重构为工业级运行时管线：静态数据一次上传、动态变换热更新、组件化数据模型、声学空间查询解耦、稳定公共 API。

### P.1 — GPU Instance 分离（desc 静态 + transform 动态）

问题: `GpuInstance` 同时承载静态字段和动态字段，导致每帧全量重建。

方案: 新增结构体并调整访问路径：

```cpp
// libs/ure_core/include/ure/instance_desc.hpp
struct GpuInstanceDesc { int mesh_index; int material_index; };

// libs/ure_core/include/ure/instance_transform.hpp
struct GpuInstanceTransform { GpuMat4 transform, inverse_transform; GpuVec3 min_pt, max_pt; };
```

改动: kernel 读取从 `instances[i].transform` 改为 `instance_transforms[i].transform`；scene 编译输出 desc + transform 两路。

### P.2 — GPU Transform 热更新 API

新增 API：

```cpp
void update_instance_transforms(GpuContext* ctx, const GpuInstanceTransform* transforms, int count); // 单次 cudaMemcpyAsync
```

`IRenderEngine::load_scene()` 首帧全量初始化，后续每帧仅调用 `update_instance_transforms()` + `reset_accumulation()`。

### P.3 — Transform Ring Buffer（双/三缓冲，3 帧滞后）

引入无锁环形缓冲区，物理线程写入、渲染线程读取；确保读写不同帧索引，避免一致性与同步开销。

### P.4 — World/ECS 组件池（替换 Scene 作为运行时总线）

在 `ure_types/include/ure/world.hpp` 定义：`TransformComponent`、`GeometryComponent`、`PhysicsComponent`、`AudioComponent` 与 `World` 容器；`apps/ure_cli/main.cpp` 改用 World 协调三系统。

### P.5 — ISpatialQuery 抽象（声学 ↔ 物理 解耦）

新增 `libs/ure_physics/include/ure/physics/ispatial_query.hpp`；`PhysicsWorld : ISpatialQuery`；`AcousticRayTracer` 仅依赖接口，便于后续替换空间加速结构。

### P.6 — 类型统一（Vec3/Quat 一致化）

删除 `ure::Vec3`，统一使用 `ure::core::Vec3<float>`；渲染实体旋转统一采用 `ure::core::Quat`，去除 Euler 胶水转换。

### P.7 — 公共 API 契约（库级对外接口，不依赖 CLI）

为每个库提供稳定公共头文件（向后兼容）：

- `ure_core/include/ure/render.hpp`: `create_gpu_renderer()`, `load_scene_once()`, `update_transforms()`, `render_pass()`, `get_framebuffer()`
- `ure_physics/include/ure/physics.hpp`: `create_world()`, `step(dt)`, `get_transform_snapshot()`, `ray_cast()`
- `ure_sceneio/include/ure/scene_io.hpp`: `load_scene()`, `load_image()`, `load_spd()`
- `ure_config/include/ure/config.hpp`: `load_config()`, `parse_cli()`, `make_render_config()`

可选：提供 `ure_c_api.h` 的 C 接口以支持 Python/C#/Unity 绑定。

### P.8 — 编排层（ure_cli 仅作参考实现）

`ure_cli` 负责线程/帧同步/资产热加载/配置热重载；不承载业务能力，所有能力通过公共 API 输出给外部可复用。

完成判据：
- 物理动画模式下不再触发 GPU 全量重建
- 每帧仅上传 transform 缓冲区（一次 H2D 复制）
- 外部应用可不经 `ure_cli` 直接链接库调用渲染/物理/声学能力

---

## ████████ Phase J: ECS/World + Transform 管线 ████████

**目标**: 将 God Object `Scene` 替换为基于 Entity-Component 的数据模型，配合双缓冲 Transform 管线，彻底解耦物理/渲染/声学三系统。

**设计总览**:

```cpp
using EntityId = uint32_t;

// 组件池 — SoA 布局
struct TransformComponent {
    ure::core::Vec3<float> position;
    ure::core::Quat rotation;
    ure::core::Vec3<float> scale;
};

struct GeometryComponent {
    std::shared_ptr<Mesh> mesh;
    std::shared_ptr<Material> material;
};

struct PhysicsComponent {
    RigidBodyConfig config;    // 仅初始化用
};

struct AudioComponent {
    int material_id;
    AcousticMaterial acoustic_mat;
    ure::core::Vec3<float> dimensions;
    int modal_body_id;  // 内部关联
};

// World — 组件容器
struct World {
    // 活跃实体列表
    std::vector<EntityId> entities;
    
    // 组件池
    std::vector<TransformComponent> transforms;
    std::vector<GeometryComponent> geometries;
    std::vector<PhysicsComponent> physics;
    std::vector<AudioComponent> audio;
    
    // 非 ECS 全局配置
    RenderConfig render_config;
    PhysicsConfig physics_config;
    AudioConfig audio_config;
    Camera camera;
    
    // 快速查找
    std::unordered_map<EntityId, size_t> entity_to_index;
    
    // 分配新实体
    EntityId create_entity();
    void remove_entity(EntityId id);
};
```

### Step J.1 — 定义 World + Component 类型

- 在 `ure_types/include/ure/world.hpp` 中定义 `World` 及所有组件结构体
- 不依赖任何 CUDA 类型
- 添加 EntityId、组件池迭代器、实体生命周期方法

### Step J.2 — 场景加载 → World 填充

- `ure_sceneio` 的 `SceneFrontend` 解析 glTF / 遗留格式后 → 填充 `World` 而非 `Scene`
- 现有 `Scene` struct 仍然保留作为过渡载具（但不再参与运行时数据传递）
- `main.cpp` 中场景初始化代码改为填充 `World` 组件池

### Step J.3 — Transform Ring Buffer

- 引入双缓冲（或三缓冲）Transform 池:

```cpp
struct TransformRingBuffer {
    static constexpr int kMaxFrames = 3;
    int write_frame = 0;
    int read_frame = 0;
    
    std::vector<GpuInstanceTransform> cpu_transforms[kMaxFrames];
    // PhysicsSystem 写入 write_frame
    // RenderSystem 读取 read_frame
};
```

- Physics 步进: 写入 `cpu_transforms[write_frame]`
- Render 步进前: `cudaMemcpyAsync` 将 `cpu_transforms[read_frame]` 上传到 GPU
- 帧切换: `write_frame = (write_frame + 1) % 3`，确保 read 永远不追 write

### Step J.4 — 物理循环改写

- 物理循环不再直接写 `scene.entities[i].position` 和 `scene.entities[i].rotation`
- 改为: `world.transforms[entity_idx].position = body->position`
- Marching Cubes 结果: 替换 `world.geometries[fluid_entity_idx].mesh` 后，更新 GPU mesh 引用（或重建轻量级 descriptor）

### Step J.5 — Transform 管线集成

- `engine->load_scene()` 首次调用：上传所有静态 GPU 数据（meshes, materials, descs, BVH）
- 每帧物理循环: `update_instance_transforms(ctx, ring_buffer.cpu_transforms[ring_buffer.read_frame], count)`
- 消除 `load_scene()` 全量重建

### Step J.6 — 验证

- 物理动画输出与 Phase F.5 参考帧一致
- GPU 测试全部通过
- 性能: 物理帧不再触发 GPU 全量重建（消除 ~20 次 cudaMalloc + cudaMemcpy per frame）

---

## ████████ Phase G: glTF 2.0 完整化 + 光谱扩展 ████████

**目标**: glTF 2.0 主格式，补齐材质，定义 URE_spectral_material。

### Step G.1 — 审核现有 GltfSceneFrontend

列出已支持/缺失的 glTF 节点类型。

### Step G.2 — 补齐材质

| glTF 属性 | GpuMaterial 字段 |
|-----------|-----------------|
| baseColorFactor | albedo |
| metallicFactor | roughness (1-m) |
| roughnessFactor | roughness |
| baseColorTexture | texture_index |
| metallicRoughnessTexture | texture_index |
| emissiveFactor | emission |
| emissiveTexture | emission_texture_index |
| normalTexture | 生成 tangent |

### Step G.3 — 场景图

节点变换层级、mesh 关联、camera。

### Step G.4 — URE_spectral_material extension

```json
{
    "extensionsUsed": ["URE_spectral_material"],
    "materials": [{
        "extensions": {
            "URE_spectral_material": {
                "spectralBands": 64,
                "albedoSPD": "textures/gold.spd",
                "emissionSPD": "lights/d65.spd",
                "dispersion": 0.15
            }
        }
    }]
}
```

解析 → `SceneIR.material_extensions`。

### Step G.5 — Fallback

非 glTF 文件 → legacy 解析器 + deprecation warning。

---

## ████████ Phase H: 资产管线 ████████

**目标**: stb_image 替换 BMP-only 解析器，SPD 加载。

### Step H.1 — stb_image

`third_party/stb/stb_image.h` + `stb_image_write.h`。支持 PNG/JPG/BMP/TGA/HDR/EXR。

### Step H.2 — SPD 加载器

格式: `wavelength value` 每行。插值到 `RenderConfig.num_wavelengths`。

### Step H.3 — 错误处理 + 搜索路径

加载失败 → 粉色棋盘格 + 日志。搜索路径: glTF 相对目录 + `URE_TEXTURE_PATH`。

---

## ████████ Phase I: 配置系统 ████████

**目标**: JSON 配置文件 + CLI11 + 子命令。

### Step I.1 — 集成 CLI11 + nlohmann/json

Header-only, `third_party/`。

### Step I.2 — JSON 格式

```json
{
    "spectral": { "bands": 64, "spd_search_paths": ["./spds/"] },
    "renderer": { "max_depth": 50, "rr_min_prob": 0.05, "spp": 1024 },
    "output": { "file": "output.exr", "tonemap": "aces", "format": "exr" },
    "gpu": { "device_ids": [0], "wavefront_capacity": 0 }
}
```

### Step I.3 — CLI 子命令

```
urender render scene.gltf [-c cfg.json] [--spp N] [-o path]
urender info scene.gltf
urender list-devices
urender validate scene.gltf
```

### Step I.4 — 覆盖链

CLI args > JSON file > defaults

---

## ████████ Phase A: SoA 队列 + RenderConfig 集成 ████████

**目标**: 队列分配使用 RenderConfig，光谱维度动态化。

### Step A.1 — alloc_ray_queue 加入 N

Queue throughputs 从 `GpuSpectrum[]` 改为 `float spectral_vals[], spectral_wavelengths[]`。

### Step A.2 — GpuEngine 接受 RenderConfig

`num_wavelengths`, `queue_capacity` 替换硬编码值。

### Step A.3 — 验证

全部 GPU 测试通过，渲染结果与 Phase 4 末一致。

---

## ████████ Phase B: 多 GPU 单机 ████████

**目标**: 单机多卡，按样本分区。

### Step B.1 — GpuMultiContext + init/render API

`cudaSetDevice(i)` 循环初始化，每 GPU 独立 `render_pass()` → host 合并。

### Step B.2 — 向后兼容

`GpuEngine` 单 GPU 接口不变。

---

## ████████ Phase C: 分布式契约 ████████

**目标**: 定义分布式渲染数据契约，不涉及网络实现。

```cpp
struct DistributedSampleRange { int node_id, sample_start, sample_count, width, height; };
struct DistributedFrameBuffer { int width, height, total_samples; float* data; };
void merge_partial_framebuffer(DistributedFrameBuffer& accum, const DistributedFrameBuffer& incoming);
```

单元测试验证合并顺序无关性。

---

## ████████ Phase D: 分布式集成 ████████

**目标**: 连接分布式契约到传输层。MPI 后端示例 + 文件 I/O 后端。

---

## ████████ Phase E: N 通道光谱升级 ████████

**目标**: 运行时 N，SPD 输入，色散，Mueller 矩阵。

N 来自 `RenderConfig.num_wavelengths`，与 Phase 0 AutoConfig 正交。

### Step E.1 — 消除 121 处 `.values.x/y/z/w`

### Step E.2 — SoA 光谱 + RGB 纹理不变

### Step E.3 — 运行时 N

### Step E.4 — SPD 输入

### Step E.5 — 色散 + Mueller

---

## 当前状态

```
旧 Phase 1: ████████████ 已完成 (5327cdf)
旧 Phase 2: ████████████ 已完成 (50b9a08)
旧 Phase 3: ████████████ 已完成 (6187cf1)
旧 Phase 4: ████████████ 已完成 (c335ab0)

新 Phase 0: ████████████ 已完成 (gpu_hardware.cu fix + auto_configure 移植 + 测试通过)
新 Phase F: ████████████ 已完成 (F.1-F.5)
新 Phase P: ████████████ 已完成 (P.1-P.8 全部完成)
新 Phase G: ░░░░░░░░░░░░ 未开始
新 Phase H: ░░░░░░░░░░░░ 未开始
新 Phase I: ░░░░░░░░░░░░ 未开始
新 Phase A: ░░░░░░░░░░░░ 未开始
新 Phase B: ░░░░░░░░░░░░ 未开始
新 Phase C: ░░░░░░░░░░░░ 未开始
新 Phase D: ░░░░░░░░░░░░ 未开始
新 Phase E: ░░░░░░░░░░░░ 未开始
```

## 预估总工期

| Phase | 工期 | 依赖 | 性质 |
|-------|:----:|------|------|
| Phase 0 | 3 天 | 无 | 纯新增 |
| Phase F | 5 天 | Phase 0 | 搬文件+CMake |
| Phase P | 8 天 | Phase F | 运行时数据管线 |
| Phase G | 5 天 | Phase F | 与 H/P 可并行 |
| Phase H | 4 天 | Phase F | 与 G/P 可并行 |
| Phase I | 4 天 | Phase G+H | 与 P 可并行 |
| Phase A | 5 天 | Phase P+0 | SoA 队列 |
| Phase B | 4 天 | Phase A | 多 GPU |
| Phase C | 3 天 | 无 | 契约,可插队 |
| Phase D | 3 天 | Phase C | 分布式 |
| Phase E | 10 天 | Phase A+G | N 通道光谱 |

---

## 与独特高级特性的兼容性

| 特性 | 设计中位置 | 冲突风险 |
|------|-----------|---------|
| N 通道光谱 (运行时指定) | RenderConfig.num_wavelengths → Phase E | 零冲突 |
| CIE 解析函数 | ure_core/spectral/ | 零冲突 |
| SoA 队列 | Phase A, ure_core 内部 | 零冲突 |
| Wavefront 渲染 | ure_core 核心算法 | 零冲突 |
| 嵌套介质 IOR | 已在 kernel.cu → ure_core | 零冲突 |
| Mueller 矩阵 | Phase E Step E.5 | 零冲突 |
| SPD 输入 | Phase H, ure_sceneio | 零冲突 |
| RGB→高斯上采样 | gpu_spectrum_utils.cuh → ure_core | 零冲突 |
| 物理/声学 | ure_physics 独立库 + ISpatialQuery | 零冲突 |
| ECS/World 数据模型 | Phase P, ure_types | 零冲突 |
| GPU Transform 热更新 | Phase P, ure_core | 零冲突 |

---

## 依赖图

```
Phase 0 (硬件检测)
    │ 无依赖,先写代码
    ▼
Phase F (目录树+CMake)
    │ F.1-F.5: 已完成 (2026-06-09)
    ├──────────┬──────────┬──────────┐
    ▼          ▼          ▼          ▼
Phase P    Phase G    Phase H    Phase I
(管线)     (glTF)     (资产管线)  (配置,可并行)
    │          │          │
    ├──────────┴──────────┘
    ▼
Phase A (SoA 队列)
    │
    ├─────────────────┐
    ▼                 ▼
Phase B (多GPU)   Phase C (分布式契约)
    │                 │
    ├────────┬────────┘
    ▼        ▼
Phase D (分布式集成)
    │
    ▼
Phase E (N通道光谱)
```

并行建议:
- Phase G / H / I 可同步进行，无交叉依赖
- Phase P 与 Phase G / H / I 无依赖关系
- Phase C（契约）可随时插入，无依赖
- 建议优先推进 Phase P，因为此后所有渲染核心改造 (A/B/E) 都依赖它

---

## ████████ 远期规划（Phase E 之后 / 与中短期并行） ████████

### 我们的定位

```
UltraRender = 光谱渲染器 + 物理模拟 + 声学合成
              ├── 不是 Cycles 竞品
              ├── 不是 Arnold 替代品
              └── 是科学可视化/汽车/建筑/影视预视的垂直工具
                  └── 核心竞争力: N 通道光谱 + Mueller 偏振 + 物理声学耦合
```

### 工业成熟度目标（5 个维度）

| 维度 | 当前 (Phase 0/F) | Phase E 完成后 | 远期目标 |
|------|------------------|---------------|---------|
| 场景描述 | legacy .scene | glTF 2.0 + URE 扩展 | USD + Hydra 委托 |
| API 可集成性 | CLI only | C++ 公共 API | Python API + 脚本 |
| 材质系统 | 硬编码 BSDF | glTF PBR + URE 光谱 | 节点图 (MaterialX/OSL) |
| 交互性 | 离线帧 → BMP | 渐进式渲染 | 视口交互 + 热重载 |
| 分发 | 单机 CUDA | 单机 + 多卡 | 分布式 (网络/云) |

### 新增远期 Phase

```
Phase E ──→ Phase S ──→ Phase M ──→ Phase U ──→ Phase K (持续)
               │
               ├──→ Phase X (可并行)
               │
               └──→ Phase C/D (分布式, 可并行)
```

与中短期的关系：Phase S 和 Phase X 的部分工作可在 Phase E 完成后立即开始；Phase M 依赖 Phase G 的材质扩展 + Phase E 的光谱引擎；Phase U 依赖 Phase S 的稳定 API。

---

### Phase S — Session API + 脚本化

**目标**: 为 UltraRender 提供工业标准的会话管理和脚本能力，使其他应用（Maya/Houdini/Python 脚本）可直接调用渲染能力。

**设计**:

```cpp
// 核心会话接口 (ure_core/include/ure/session.hpp)
class RenderSession {
public:
    static RenderSession create(const RenderConfig& cfg);
    void load_scene(const SceneIR& scene);             // 初始加载
    void mutate_scene(const SceneDiff& diff);           // 增量更新
    void start_render(bool progressive);                // 开始渲染
    void cancel();                                      // 取消
    RenderProgress get_progress() const;                // 进度查询
    std::vector<float> get_framebuffer() const;         // 帧缓冲
    std::vector<float> get_aov(const char* name) const; // AOV 输出
    
    // 交互渲染
    void update_camera(const Camera& cam);
    void pause(); void resume();
};
```

**子步骤**:

| Step | 内容 | 文件 |
|------|------|------|
| S.1 | 定义 `RenderSession` 接口 + 实现 | `session.hpp`, `session.cpp` |
| S.2 | 定义 `SceneDiff` 增量变更描述（增/删/改 entity，改 transform，改材质） | `scene_diff.hpp` |
| S.3 | C API：`ure_session_create()`, `ure_session_render()`, `ure_session_get_frame()` 等 | `ure_c_api.h`, `ure_c_api.c` |
| S.4 | Python binding：pybind11 绑定 C API，发布 `pyure` 包 | `pyure/` |
| S.5 | 渐进式渲染：`render_pass()` 循环 + 交互相机回调 | `session.cpp` |
| S.6 | AOV 系统：法线/深度/反照率/UV/运动矢量 独立输出 | `aov.hpp`, kernel 扩展 |

**示例用法**:

```python
# Python 脚本直接调用 (pip install pyure)
import pyure

session = pyure.create_session({
    "spectral": {"bands": 64},
    "output": {"width": 1920, "height": 1080}
})

scene = pyure.load_scene("scene.gltf")
session.load_scene(scene)

for i in range(256):
    session.render_pass()
    if i % 16 == 0:
        print(f"SPP: {session.get_progress().spp}")
        session.get_framebuffer().save(f"preview_{i:04d}.exr")

session.get_framebuffer().save("final.exr")
```

**核心原则**: Python API 不包装 CLI，而是直接链接 `ure_core`/`ure_sceneio` 的 C API。

---

### Phase M — 材质系统

**目标**: 从硬编码 BSDF 升级为基于节点图的材质系统，兼容行业标准。

**现有基础**:
- `GpuMaterial` 已支持 Lambertian / Metal / Dielectric / Light / Cloth
- 纹理通过 `GpuTexture` 支持
- 介质参数已在材质中（Phase 3 体积散射）

**升级路径**:

```cpp
// 远期: 材质节点图
struct MaterialNode {
    NodeType type;  // bsdf_lambert, bsdf_ggx, add, mix, texture, const, ...
    std::vector<InputPin> inputs;
    std::vector<OutputPin> outputs;
    std::vector<float> params;
};

struct MaterialGraph {
    std::vector<MaterialNode> nodes;
    std::vector<Connection> connections;
    OutputPin final_output;  // 连接到 surface
};
```

**子步骤**:

| Step | 内容 | 前置依赖 |
|------|------|---------|
| M.1 | 设计节点图 IR（不依赖 OSL，自定义格式） | Phase G (glTF 材质) |
| M.2 | GPU 编译：节点图 → GpuMaterial + 内核参数 | Phase E (SoA 光谱) |
| M.3 | MaterialX 导入（`mtlx` → 节点图 IR） | M.1 |
| M.4 | MaterialX 导出（节点图 IR → `mtlx`） | M.1 |
| M.5 | 材质预设库（金属/玻璃/皮肤/织物/汽车漆） | M.2 |

**关键决策**: 不实现 OSL 编译器。OSL 在 GPU 上的支持极其复杂（需要 LLVM/SPIR-V 编译），维护成本远高于收益。我们的自定义节点图覆盖所有所需 BSDF 组合，且可直接编译为 CUDA 内核参数。

---

### Phase U — USD/Hydra 集成

**目标**: 使 UltraRender 可作为 USD 流程中的渲染后端，通过 Hydra 委托被 Maya/Houdini/Katana 等应用调用。

**架构**:

```
DCC (Maya/Houdini) → USD Stage → Hydra RenderDelegate → ure_core
                                      │
                                  HdURE (我们的委托)
                                      │
                                  ure_session.cpp
                                      │
                                  ure_c_api.h
```

**子步骤**:

| Step | 内容 | 复杂度 |
|------|------|--------|
| U.1 | USD schema 定义：`URE_spectral_material`, `URE_physics`, `URE_acoustic` | 中 |
| U.2 | Hydra RenderDelegate 骨架：`HdURE` 继承 `HdRenderDelegate` | 高 |
| U.3 | RPrim 支持：`HdMesh` → 我们场景 | 高 |
| U.4 | 材质转换：USD `Material` + `URE_spectral_material` → 节点图 + GpuMaterial | 中 |
| U.5 | 交互渲染：Hydra 视口 → 渐进式渲染会话 | 高 |
| U.6 | 场景导出：`SceneIR → .usda` | 低 |

**USDA 示例**:

```usda
#usda 1.0
over "GlassCup" (
    prepend apiSchemas = ["UREPhysicsAPI", "URESpectralMaterialAPI"]
)
{
    uniform token ure:physics:colliderType = "sphere"
    uniform float ure:physics:mass = 1.5
    uniform int ure:spectral:bands = 64
    uniform asset ure:spectral:albedoSPD = @spds/glass.spd@
}
```

**核心约束**: 不依赖 OpenGL/Vulkan 上下文。Hydra 委托需要 GPU 上下文时的交互由 `ure_session` 内部管理。

---

### Phase X — 插件系统

**目标**: 允许第三方扩展渲染管线，无需修改核心代码。

**插件类型**:

```
- Procedural: 运行时生成几何体（类似 Arnold AtProcedural）
- Filter: 帧后处理（色调映射、颜色分级、LUT）
- Shader: 自定义 BSDF/光照函数（注意不依赖 OSL 编译）
- Output: 自定义帧输出格式
```

**API**:

```cpp
// include/ure/plugin_api.h
extern "C" {
URE_EXPORT int ure_plugin_version();
URE_EXPORT void ure_register_procedural(ProceduralAPI* api);
URE_EXPORT void ure_register_filter(FilterAPI* api);
URE_EXPORT void ure_register_shader(ShaderAPI* api);
URE_EXPORT void ure_register_output(OutputAPI* api);
}
```

**延迟决定**: 插件系统依赖稳定的 Session API (Phase S) 和材质系统 (Phase M)，建议 Phase E 之后再设计。

---

### Phase K — 内核持续优化（全周期）

**目标**: 渲染内核的性能/质量/功能改进，不是单次 Phase 而是长期迭代。

**子步骤**:

| Step | 内容 | 时机 |
|------|------|------|
| K.1 | SBVH / 多级 BVH（提高大场景遍历效率） | Phase A 后 |
| K.2 | OptiX denoiser 集成（替代 current denoise.cu） | Phase E 后 |
| K.3 | Wavefront occupancy auto-tuning（基于 SM 计数） | Phase A 后 |
| K.4 | Light tree / light picking importance sampling | Phase E 后 |
| K.5 | ReSTIR DI / PT（直接光照重用，提升收敛速度） | 远期考虑 |
| K.6 | Spectral MIS（波长采样的多重重要性采样） | Phase E 内 |

---

### 完整的全生命周期路线图

```
Phase 0 ─→ Phase F ─┬──→ Phase P ─┬──→ Phase A ─┬──→ Phase B ─┬──→
                     │             │              │             │
                     ├──→ G/H/I ───┘              │             │
                     │                            │             │
                     └──→ (Phase C 可随时插队)     │             │
                                                   ▼             ▼
                                              Phase E ───→ Phase S
                                                                 │
                                                  ┌──────────────┤
                                                  ▼              ▼
                                             Phase M         Phase X
                                                  │              │
                                                  └──────┬───────┘
                                                         ▼
                                                    Phase U
                                                         │
                                                    ──────┴──────→ (K.1–K.6 持续)
```

### 差异化战略总结

| 不做 | 做 |
|------|----|
| CPU 渲染后端 | CUDA GPU 专用，极致 SIMT 优化 |
| OSL 编译器 | 自定义节点图 + MaterialX 兼容 |
| OpenGL/Vulkan 合成 | CLI 离线 + Python 脚本输出 |
| DCC 插件（Maya/Houdini） | Hydra 委托（任何 Hydra 宿主） |
| 通用 RGB 渲染器 | **光谱渲染 + Mueller 偏振 + 物理声学** |
| 实时游戏渲染 | 交互渐进式 + 离线帧序列 |

### 当前 PLAN.md 中加入远期规划的意义

1. **Phase P/S.1 的 Session API 接口设计需要远期考虑**：今天写的 `update_instance_transforms()` 签名必须被 `RenderSession::mutate_scene()` 调用。接口预对齐，避免远期返工。
2. **Phase G 的 URE_spectral_material extension 就是 Phase M 的输入**：glTF 扩展格式必须兼容远期节点图。
3. **Phase C/D 的分布式契约必须考虑 SceneDiff 的网络序列化**。
