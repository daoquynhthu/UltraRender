# UltraRender 升级路线图 (PLAN.md)

最后更新: 2026-06-14 (Phase M SceneIR-only cleanup and glTF visual gate)

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
新 Phase B:     多 GPU 单机                                         已完成
新 Phase C:     分布式契约标准化                                    已完成
新 Phase D:     分布式集成                                          已完成
新 Phase E:     N 通道光谱升级                                      已完成
远期 Phase S:   Session API + 脚本化                                已完成
远期 Phase M:   材质系统                                             进行中
```

---

## XPU 架构宣言

### 我们已经是 XPU

CPU 和 GPU 各自承担不同的职责，通过有锁或无锁队列解耦：

```
CPU (physics / scene update):
    physics step → build_transforms → begin_write → memcpy → end_write → advance
                                                                              │
                                        SPSC RingBuffer (release fence chain)
                                                                              ▼
GPU (tracing / shading):
    begin_read (acquire) → upload transforms → render_pass → resolve
```

这是**窄 XPU** — 只有 instance transform 走动态管线。其他场景数据（mesh、material、sphere、light_index、texture、BVH）在 `load_scene_ir()` 首次加载时一次性上传，此后只读。

### 核心模式：SPSC RingBuffer

`TransformRingBuffer`（`libs/ure_core/include/ure/transform_ring_buffer.hpp`）定义了标准的 CPU→GPU 动态数据传输契约：

```
Writer (CPU):
    begin_write()  → 获取当前写帧指针
    memcpy/store   → 填充数据
    end_write()    → atomic_thread_fence(release)
    advance()      → write_index.store(release)
    
Reader (GPU host path):
    begin_read()   → write_index.load(acquire) → 推导读帧
    cudaMemcpy     → 上传到 GPU VRAM
    end_read()     → 无操作（读帧始终派生自 write_index）
```

这个模式可以原样复用于任何 CPU→GPU 动态数据通道。

### 当前动态能力范围

| 动态数据 | 通道 | 状态 |
|---------|------|------|
| Instance transform | TransformRingBuffer | ✅ 已实现 |
| 材质参数更新 | SceneDiff material mutation → `IRenderEngine::update_materials()` → GPU header/SoA upload; textured/resource mutation → full GPU reload | ✅ 参数热更新已实现；texture/resource rebinding 已按低频资源变更走 retained scene full reload，后续高频资源流式更新仍可升级为专用资源队列 |
| 实例增删 | 无 | ❌ 未实现 |
| 光源增删 | 无 | ❌ 未实现 |
| 流体粒子 → GPU sphere 同步 | 无 | ❌ 未实现 |
| 变形几何 / BVH 增量更新 | 无 | ❌ 未实现 |

**这些缺失不是架构债** — 每个都可以通过新增一个 `XxxRingBuffer` 或 `XxxUpdateQueue` 来填补，无需改动现有代码。TransformRingBuffer 的 SPSC fence 模式是通用模板。

### 动态流体的典型路径（未来参考）

```
Physics step (CPU):
    fluid_system->step(dt)                  → 更新粒子位置
    build_fluid_spheres(spawn, destroy)      → 计算 GPU sphere 增删
    sphere_queue.begin_write()               → 获取写指针
    memcpy(spawn_spheres, destroy_indices)   → 填充增删命令
    sphere_queue.end_write() + advance()     → release fence + release store

Render thread (CPU→GPU):
    sphere_queue.begin_read()                → acquire load
    cudaMemcpy to GPU sphere pool            → 更新
    update_sphere_counts(new_count)          → 调整 sphere_count
    reset_accumulation()
```

**关键原则**：CPU 永远不读 GPU 数据（除非帧结束 resolve framebuffer）。GPU 永远不写 CPU 数据。SPSC 方向始终是 **CPU → GPU**。

### 为什么不扩展现有接口而用新 Queue

现有 `IRenderEngine` 的 `update_transforms()` 是 `virtual` 方法，参数是裸指针 `const GpuInstanceTransform*`。扩展这个接口为 `update_materials()`、`add_instance()` 等是合理的，但：

1. 每种动态数据可能需要不同的更新语义（替换 vs 追加 vs 删除）
2. 批量写入 + SPSC 解耦比单次虚函数调用更适合高频多数据类型
3. RingBuffer 模式天然支持多生产者（多个 CPU 线程）— 如果未来需要

### 为什么不是纯 GPU 物理/声学

| 放在 CPU 的原因 | 依据 |
|----------------|------|
| **物理/声学算法有大量分支、间接、小批量操作** — GPU SIMT 不擅长 | 碰撞检测、稀疏粒子搜索、模态合成 |
| **场景数据已经驻留在 CPU 内存** — physics 可以读取 SceneIR 和 World | 零数据搬运 |
| **物理步频 (~120 Hz) 和渲染步频 (~1-30 Hz) 不同** — 自然解耦 | RingBuffer 隐藏延迟 |
| **CUDA CC 12.0 硬件调度器没有物理专用单元** | 与渲染竞争 SM 资源 |

### 对大卡/多卡/分布式升级的影响

| 升级场景 | XPU 架构的影响 |
|---------|---------------|
| **更大单卡** | 无影响。CPU 侧 unchanged，GPU 侧受益于更多 SM 和 VRAM |
| **多卡（单节点）** | 需要每卡各自的 `GpuContext` + 各自的 RingBuffer 读取。CPU 写入一次，broadcast 到 N 个 buffer。SPSC 模式天然支持 1:N fanout |
| **分布式（多节点）** | RingBuffer 的 shared-memory 实现需要替换为网络消息（ZeroMQ / MPI）。但 **SPSC release/acquire fence chain 的设计完全可复用**：网络接收端作为 writer，渲染线程作为 reader |

**结论**：窄 XPU 不是"把架构做死了"。它是刻意选择的 CPU/GPU 分工边界，有清晰的扩展路径到全动态管线、多卡和分布式。任何未来功能都可以在不破坏现有数据路径的前提下增量添加。

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

## 目录树（当前状态 — 旧目录已删除）

```
E:\Render Engine\
│
├── CMakeLists.txt                          # 仅含模块化构建（旧构建块已移除）
│
├── cmake/
│   └── UltraRenderConfig.cmake.in          # CMake package export
│
├── libs/
│   ├── ure_types/                          # 纯头文件类型库 (INTERFACE)
│   │   ├── CMakeLists.txt
│   │   └── include/ure/{core/,world.hpp,scene_ir.hpp,render_config.hpp}
│   │
│   ├── ure_core/                           # GPU 渲染核心 (STATIC, CUDA)
│   │   ├── CMakeLists.txt
│   │   ├── include/ure/                    # gpu_structs.hpp, gpu_driver.hpp, spectral/, ...
│   │   └── src/                            # path_tracer_kernel.cu, gpu_driver.cu, ...
│   │
│   ├── ure_sceneio/                        # 场景 I/O (STATIC, 纯 C++)
│   │   ├── CMakeLists.txt
│   │   ├── include/ure/                    # scene_io.hpp, spd_loader.hpp, ...
│   │   └── src/                            # gltf_scene_frontend.cpp, image_loader.cpp, scene/...
│   │
│   ├── ure_diag/                           # 诊断系统 (STATIC, Phase Dx)
│   │   ├── CMakeLists.txt
│   │   ├── include/ure/                    # log.hpp, log_sink.hpp, check_cuda.hpp, timer.hpp
│   │   └── src/log.cpp
│   │
│   ├── ure_config/                         # 配置系统 (STATIC, 纯 C++)
│   │   ├── CMakeLists.txt
│   │   ├── include/ure/config.hpp
│   │   └── src/config_parser.cpp
│   │
│   └── ure_physics/                        # 物理/声学 (STATIC, 纯 C++, 可选)
│       ├── CMakeLists.txt
│       ├── include/ure/physics/            # physics_world.hpp, acoustic/...
│       └── src/                            # physics_world.cpp, acoustic_system.cpp, ...
│
├── apps/ure_cli/src/main.cpp               # 薄壳编排器 EXE
│
├── tests/
│   ├── gpu/                                # GPU 测试 (11 个, 14 CTest)
│   │   ├── CMakeLists.txt
│   │   ├── test_framework.cuh
│   │   ├── test_device.cu
│   │   ├── test_hardware.cu
│   │   ├── test_math_functions.cu
│   │   ├── test_spectral_pipeline.cu
│   │   ├── test_render_basic.cu
│   │   ├── test_instance_hotupdate.cu
│   │   ├── test_gpu_tangents.cu
│   │   ├── test_gpu_denoise.cu
│   │   ├── test_gpu_polarization.cu
│   │   ├── test_gpu_volume.cu
│   │   └── test_distributed_contract.cu
│   │
│   └── host/                               # 主机测试 (2 个, 81 测试)
│       ├── CMakeLists.txt
│       ├── test_world.cpp
│       └── test_asset_pipeline.cpp
│
├── third_party/stb/stb_image.h             # v2.30
├── scenes/                                 # 场景文件
├── docs/                                   # 文档
├── scripts/                                # 脚本
├── tools/                                  # 工具
└── gui/                                    # GUI 层
```
**注意：** 旧 `include/` 和 `src/` 目录已删除。所有代码仅在 `libs/` 和 `apps/` 中开发。

---

## 执行顺序

```
Phase 0 ─→ Phase F ─┬──→ Phase P ───→ Phase A ─┬──→ Phase B ─┐
                     │                           │            ├──→ Phase D
                     ├──→ Phase G ──┐            │            │
                     │              ├──→ Phase I └──→ Phase C ┘
                     ├──→ Phase H ──┘                         │
                     │                                        ▼
                     └──→ Phase Dx ──────────→  Phase E ──────┘
                     (诊断贯穿所有阶段)            │
                                                  ├── E.0 前置测试
                                                  ├── E.1 核心类型重构
                                                  ├── E.2 物理精确转换
                                                  ├── E.3 运行时 N
                                                  ├── E.4 SPD 输入
                                                  └── E.5 色散+Mueller
```

Phase G / Phase H / Phase I / Phase C 与 Phase J 无依赖关系，可并行执行。

### 依赖保证

- **Phase 0** (硬件检测) → 纯新增，不依赖现存代码
- **Phase F** (目录树 + CMake) → 将 Phase 0 文件迁入新位置
- **Phase P** (运行时数据管线) → 依赖 Phase F 完成；为 Phase A/B/C 的前置
- **Phase G/H** (格式/资产) → 依赖 Phase F 的目录结构，与 Phase J 可并行
- **Phase Dx** (诊断管线) → 依赖 Phase F 的目录结构，依赖 Phase H 的 spd_loader 命名惯例；与 Phase G/H/I/P 均可并行
- **Phase I** (配置) → 依赖 Phase G/H 的资产管线，与 Phase J 可并行
- **Phase A** (SoA 队列) → 依赖 Phase P 的 Transform/World 架构 + Phase 0 的 RenderConfig
- **Phase C** (契约) → 无依赖，可插队
- **Phase E** (N 通道光谱) → 已完成。它依赖 Phase A (SoA) + Phase G (glTF 光谱扩展) + Batch 4 测试 (OT1/OT5/OT6)，并已完成 E.0-E.5 的 runtime-N、SPD、色散、Mueller、wavelength PDF 与静态审计门禁。后续 specular manifold、rough dielectric BTDF、advanced spectral MIS 和材质系统进入 Phase K/M，不再作为 Phase E 阻塞项。

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
    src/scene_frontend.cpp
    src/scene_parser.cpp
    src/image_loader.cpp
    src/image_saver.cpp
    src/scene/scene_ir.cpp
    src/scene/scene.cpp
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

旧 CMake 构建块已删除；当前仅保留模块化 CMake。

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

`IRenderEngine::load_scene_ir()` 首帧全量初始化，后续每帧仅调用 `update_instance_transforms()` + `reset_accumulation()`。

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

- `ure_core/include/ure/render.hpp`: `create_gpu_renderer()`, `load_scene_ir()`, `update_transforms()`, `render_pass()`, `get_framebuffer()`
- `ure_physics/include/ure/physics.hpp`: `create_world()`, `step(dt)`, `get_transform_snapshot()`, `ray_cast()`
- `ure_sceneio/include/ure/scene_io.hpp`: `load_image()`, `load_spd(path, N)`, `load_spd(path, RenderConfig)`
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

- `engine->load_scene_ir()` 首次调用：上传所有静态 GPU 数据（meshes, materials, descs, BVH）
- 每帧物理循环: `update_instance_transforms(ctx, ring_buffer.cpu_transforms[ring_buffer.read_frame], count)`
- 消除 SceneIR transform-only 更新中的全量重建

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

非 glTF/GLB 文件 → fail-loud。旧文本场景格式已在 Phase M cutoff 中删除，不再作为 fallback 或兼容路径维护。

---

## ████████ Phase H: 资产管线 ████████

**目标**: stb_image 替换 BMP-only 解析器，SPD 加载器（为 Phase E 预埋）。

**架构上下文**: 
- 纹理不是 display RGB，而是光谱数据传递：`HostTexture` → `GpuTexture(GpuSpectrum[])` → kernel `sample_texture()` → `rgb_to_spectrum()` → Stokes×Mueller→volume→accum
- Phase E.3 后，GPU 输入层已使用显式 RGB→N 通道重建和材质 SoA 上传；直接 SPD→材质光谱场接入仍属于 E.4。
- 薄膜干涉的彩虹色缺失（HANDOVER_GUIDE §3.1）根因之一是光谱通道不足（4通道），SPD loader 是 Phase E N-channel 的前置基础设施
- AcousticMaterial 是独立数据类型（density/youngs_modulus），不与渲染 Material 共用资产管线 — Phase H 不涉及
- 流体网格（MarchingCubes）UV={0,0}，材质由编排层外部指派 — Phase H 不涉及

### Step H.1 — stb_image 替换 BMP-only 解析器

`third_party/stb/stb_image.h`（仅 header-only，不包含 stb_image_write）。

替换 `image_loader.cpp` 中 86 行 BMP 手动解析器为 `stbi_loadf(path, &w, &h, &ch, 3)`。

支持格式：PNG / JPG / BMP / TGA / HDR。

**数据流（Phase E.3 后）**:
```
stbi_loadf → float RGB → HostTexture {width, height, channels=3, vector<float>}
  → apply_image_color_space(sRGB→linear)     ← 已有
  → GpuSceneCompiler::compile() cache_texture  ← 已有，不动
  → GPU upload:
      channels == 3: RGB → N-channel GpuSpectrum[] + cudaArray<float4> texObj
      channels == RenderConfig::num_wavelengths: direct spectral GpuSpectrum[] data path
  → kernel: sample_texture(wavelengths, num_spec)
      RGB texObj path uses hardware filtering + runtime-N reconstruction
      spectral data path uses explicit N-channel bilinear interpolation
```

CMake：给 `ure_sceneio` 加 `target_include_directories(... PRIVATE ${CMAKE_SOURCE_DIR}/third_party)`。

### Step H.2 — SPD 加载器（为 Phase E 预埋）

新建 `libs/ure_sceneio/src/spd_loader.cpp` + `libs/ure_sceneio/include/ure/spd_loader.hpp`。

接口：
```cpp
namespace ure::spectral {

struct SPDData {
    std::vector<float> lambdas;  // 波长 (nm)
    std::vector<float> values;   // 功率值
};

SPDData load_spd_file(const std::string& path);

// 重采样到 N 个均匀波长，范围 [lambda_min, lambda_max]
std::vector<float> resample_uniform(const SPDData& spd, int n,
                                    float lambda_min = 400.0f,
                                    float lambda_max = 700.0f);

}
```

`scene_io.hpp` 提供 `load_spd(path, int num_wavelengths)` 和 `load_spd(path, RenderConfig)`，调用内部 `load_spd_file` + `resample_uniform(raw, num_wavelengths)`。无参 `load_spd(path)` 已删除，调用方必须显式给出 runtime-N。

格式：每行 `波长 值`（空格/Tab分隔，`#` 开头注释行跳过，空行跳过）。

**Phase E 兼容性**: `resample_uniform` 不再有隐藏 4 通道默认值；调用者必须显式传入运行时 N 或 `RenderConfig`。

### Step H.3 — 缺失纹理回退（内建，不改 kernel）

- CPU 端：`load_image` 失败 → `stbi_failure_reason()` 日志 + return false → `cache_texture` 保持 `texture_index = -1`
- kernel 端（已有）：`sample_texture()` 遇 `tex_idx < 0` → 粉色错误返回
- **不新增 pink checkerboard 生成代码**：引擎不需要 DCC 式视觉反馈，静默回退到 material.albedo 更符合光谱渲染器哲学

### Step H.4 — 测试

| 测试 | 覆盖 |
|------|------|
| `test_stb_image_loader` | 加载 PNG/JPG/HDR → HostTexture 宽高/数据正确 |
| `test_spd_loader` | 创建临时 .spd 文件 → 加载 → 重采样 → 值正确 |
| `test_missing_texture` | 错误路径 → false + `texture_index` 保持 -1 |

---

## ████████ Phase Dx: 分级全局统一诊断日志管理系统 ████████

**目标**: 建立全引擎统一的分级诊断日志系统，消除 ~225 处散落 `std::cerr`/`std::cout`/`printf`，提供结构化日志、CUDA 错误抽象、RAII 性能计时器、运行期级别控制。

**范围约束**: 本阶段仅处理光学/渲染核心模块（`ure_core`、`ure_sceneio`、`ure_config`、`ure_cli`）的诊断迁移。声学（`ure_physics/acoustic`）和物理（`ure_physics/physics`）模块的诊断迁移保留为扩展点，Phase Dx 定义完 API 后不做迁移。

**可扩展设计**:  
- `Tag` 枚举预留 `Physics` / `Acoustic` 值，但本阶段不迁移对应模块
- `log_sink.hpp` 的 `Sink` 接口是多态设计，后续可新增网络 sink、数据库 sink 等
- `check_cuda.hpp` 的策略枚举（`Log` / `Return` / `Abort`）保留扩展，后续可增加 `Retry` 策略
- 日志格式字符串由 `log_impl()` 集中控制，不会因后续新增模块而碎片化

### 架构

```
libs/ure_diag/ (header-only INTERFACE, 零依赖)
├── CMakeLists.txt
└── include/ure/
    ├── log.hpp           # 日志核心: Level, Tag, UR_LOG_* 宏
    ├── log_sink.hpp      # ConsoleSink, FileSink, MultiSink, CallbackSink
    ├── check_cuda.hpp    # UR_CUDA_CHECK / UR_CUDA_TRY / UR_CUDA_LOG
    └── timer.hpp         # ScopedTimer, ManualTimer
```

所有 lib 的 CMakeLists.txt 加 `target_link_libraries(... PUBLIC ure_diag)`。

### Step Dx.1 — 日志核心 API (`log.hpp`)

```cpp
namespace ure::log {

enum class Level : uint8_t { Trace, Debug, Info, Warn, Error, Fatal };
enum class Tag  : uint8_t { None, CLI, GPU, SceneIO, Config, Core,
                            Physics, Acoustic, Test };  // Physics/Acoustic 预留

void set_min_level(Level lvl) noexcept;
Level min_level() noexcept;

// 内部实现，通过宏调用
void log_impl(Level level, Tag tag, std::string_view msg,
              const std::source_location& loc = std::source_location::current());

} // namespace ure::log
```

**双重过滤机制**:
- **编译期**: `#define UR_LOG_LEVEL`（CMake 传入），`if constexpr` 完全消除低于阈级的调用，零指令开销
- **运行期**: `set_min_level()`，用于 `--verbose` / `--quiet` 切换

**宏接口**:
```cpp
#define UR_LOG_TRACE(tag, ...)  UR_LOG_IF(Trace, tag, __VA_ARGS__)
#define UR_LOG_DEBUG(tag, ...)  UR_LOG_IF(Debug, tag, __VA_ARGS__)
#define UR_LOG_INFO(tag, ...)   UR_LOG_IF(Info,  tag, __VA_ARGS__)
#define UR_LOG_WARN(tag, ...)   UR_LOG_IF(Warn,  tag, __VA_ARGS__)
#define UR_LOG_ERROR(tag, ...)  UR_LOG_IF(Error, tag, __VA_ARGS__)
#define UR_LOG_FATAL(tag, ...)  UR_LOG_IF(Fatal, tag, __VA_ARGS__)
```

**编译期级别控制**:
```cmake
# CMakeLists.txt
target_compile_definitions(ure_diag INTERFACE
    $<$<CONFIG:Debug>:UR_LOG_LEVEL=0>    # Debug: Trace+
    $<$<CONFIG:Release>:UR_LOG_LEVEL=2>  # Release: Info+
)
```

**输出格式**（ConsoleSink 用 `std::print`，C++23）:
```
[2026-06-09 15:30:01.234][INFO ][GPU] Uploaded mesh: teapot (24576 tris)
[2026-06-09 15:30:01.345][WARN ][SceneIO] Texture not found: spds/gold.spd
[2026-06-09 15:30:01.456][ERROR][Core] CUDA error 2 in 'cudaMalloc'
```

**控制台颜色**（Windows `SetConsoleTextAttribute`）:

| Level | 颜色 |
|-------|------|
| Trace | 灰 (FOREGROUND_INTENSITY) |
| Debug | 青 (GREEN\|BLUE\|INTENSITY) |
| Info | 白 (默认) |
| Warn | 黄 (RED\|GREEN\|INTENSITY) |
| Error | 红 (RED\|INTENSITY) |
| Fatal | 红底白字 |

### Step Dx.2 — 输出目标 (`log_sink.hpp`)

```cpp
namespace ure::log {

class Sink {
public:
    virtual ~Sink() = default;
    virtual void write(Level level, Tag tag,
                       const char* file, int line, const char* func,
                       std::string_view message) = 0;
    virtual void flush() {}
};

class ConsoleSink : public Sink { /* stderr, 彩色输出 */ };
class FileSink    : public Sink { /* 文件轮转: max_bytes + max_files */ };
class MultiSink   : public Sink { /* 组合多个 sink */ };
class CallbackSink : public Sink { /* std::function 回调，供 Python/C# 绑定 */ };

} // namespace ure::log
```

### Step Dx.3 — CUDA 错误检查统一接口 (`check_cuda.hpp`)

```cpp
namespace ure::diag {

enum class CudaPolicy { Log, Return, Abort };

// 单行调用宏，自动捕获 source_location
cudaError_t cuda_check(cudaError_t err,
                       const std::source_location& loc = std::source_location::current(),
                       CudaPolicy policy = CudaPolicy::Abort);

} // namespace ure::diag

#define UR_CUDA_CHECK(expr)  ure::diag::cuda_check((expr), std::source_location::current(), ure::diag::CudaPolicy::Abort)
#define UR_CUDA_TRY(expr)    ure::diag::cuda_check((expr), std::source_location::current(), ure::diag::CudaPolicy::Return)
#define UR_CUDA_LOG(expr)    ure::diag::cuda_check((expr), std::source_location::current(), ure::diag::CudaPolicy::Log)
```

替换 `path_tracer_kernel.cu` 中现有 `check_cuda()` + `checkCudaErrors` 宏，消除 `exit(99)` 硬编码。
测试文件保持现有 `CHECK_CUDA()` 宏（有 `g_tests_failed` 计数器），不做替换。

### Step Dx.4 — RAII 作用域计时器 (`timer.hpp`)

```cpp
namespace ure::diag {

template <typename Clock = std::chrono::high_resolution_clock>
class ScopedTimer {
    const char* name_;
    ure::log::Tag tag_;
    Clock::time_point start_;
public:
    ScopedTimer(const char* name, ure::log::Tag tag);
    ~ScopedTimer();  // 析构时自动用 UR_LOG_INFO 记录耗时
};

} // namespace ure::diag

#define UR_SCOPE_TIMER(tag)  ure::diag::ScopedTimer _ure_timer(__FUNCTION__, tag)
```

用法:
```cpp
void upload_scene() {
    UR_SCOPE_TIMER(ure::log::Tag::GPU);
    // ... → 析构时: [INFO][GPU] upload_scene completed in 342.5 ms
}
```

提供 `ManualTimer` 支持手动 start/stop 和区间测量。

### Step Dx.5 — CMake + 目录集成

- 新建 `libs/ure_diag/CMakeLists.txt`（`add_library(ure_diag INTERFACE)`）
- 顶层 `CMakeLists.txt` 加 `add_subdirectory(libs/ure_diag)`（在 `ure_types` 之后，在其他 lib 之前）
- 每个下游 lib 加 `target_link_libraries(... PUBLIC ure_diag)`
- 现有 `check_cuda()` 定义从 `path_tracer_kernel.cu` 移到 `check_cuda.hpp`

### Step Dx.6 — 批量迁移现有输出（光学模块）

| 批次 | 文件 | 旧模式 | 新模式 | 约改数 |
|------|------|--------|--------|:------:|
| a | `spd_loader.cpp`, `image_loader.cpp` | `std::cerr << "[H] WARNING:"` | `UR_LOG_WARN(SceneIO, ...)` | 3 |
| b | `gltf_scene_frontend.cpp` | `std::cerr << "[SceneParser] Error/Warning"` | `UR_LOG_ERROR/WARN(SceneIO, ...)` | 9 |
| c | `gpu_engine_impl.cpp` | `std::cerr/cout` | `UR_LOG_ERROR/INFO(Core, ...)` | 4 |
| d | `path_tracer_kernel.cu`（主机端 25 处） | `std::cout/cerr` + 手动 chrono | `UR_LOG_*(GPU, ...)` + `UR_SCOPE_TIMER` | 25 |
| e | `gpu_driver.cu`, `gpu_hardware.cu` | `printf/cout` | `UR_LOG_*(GPU, ...)` | 10 |
| f | `gpu_driver_stub.cpp` | `std::cout` | `UR_LOG_*(GPU, ...)` | 6 |
| g | `apps/ure_cli/src/main.cpp` | `std::cout` + 手动 chrono | `UR_LOG_*(CLI, ...)` + `UR_SCOPE_TIMER` | 22 |
| h | 旧目录 `src/`（~80 处） | `std::cerr/cout` | **不动**（冻结） | 0 |
| | **小计（光学模块）** | | | **~79** |

**声学/物理模块不做迁移**（预留 `Tag::Physics` / `Tag::Acoustic`，后期按需接入）。

### Step Dx.7 — 进度条处理

进度条（`\r` 覆盖输出）保留独立于日志系统的行为：
- 日志走 `stderr`（彩色，不可覆盖）
- 进度条走 `stdout`（`\r` 覆盖 + `std::print("...")` + `fflush`）
- 两者不冲突

### Step Dx.8 — CLI 日志控制

在 `apps/ure_cli/src/main.cpp` 接入 `--verbose` / `--quiet`（Phase I 配置系统的一部分）：

```cpp
if (verbose)       ure::log::set_min_level(ure::log::Level::Debug);
else if (quiet)    ure::log::set_min_level(ure::log::Level::Error);
else               ure::log::set_min_level(ure::log::Level::Info);
```

### Step Dx.9 — 启用 DEVICE_LOG 并接入主机日志

现有 `path_tracer_kernel.cu` 的 `DEVICE_LOG` / `flush_debug_log`（`#define DEBUG_ENABLED 0` 禁用中）：
- `DEBUG_ENABLED` 改为与 `UR_LOG_LEVEL` 联动（`UR_LOG_LEVEL <= 1` 时启用）
- `flush_debug_log` 输出从 `printf` 改为调用 `UR_LOG_DEBUG(GPU, ...)`

### 标签映射（光学模块迁移前后）

| 当前标签 | 新 `Tag` 枚举 | 新格式示例 |
|----------|--------------|-----------|
| `[H]` | `SceneIO` | `[SceneIO]` |
| `[SceneParser]` | `SceneIO` | `[SceneIO]` |
| `[GltfSceneFrontend]` | `SceneIO` | `[SceneIO]` |
| `[WavSaver]` | `SceneIO` | `[SceneIO]` |
| `[GPU]` | `GPU` | `[GPU]` |
| `[GpuRenderEngine]` | `Core` | `[Core]` |
| `[GPU Hardware]` | `GPU` | `[GPU]` |
| `[GPU Stub]` | `GPU` | `[GPU]` |
| `[Main]` | `CLI` | `[CLI]` |
| `[Progress]` | `CLI` | `[CLI]` |
| `[Step]` | `CLI` | `[CLI]` |
| `[CRITICAL ERROR]` | `Core` | `[Core]` + `FATAL` |
| `[Output]` | `CLI` | `[CLI]` |
| `[Fluid]` | `CLI` | `[CLI]` |
| `[Acoustic]` | **预留** | 本阶段不动 |
| `[Physics]` | **预留** | 本阶段不动 |

### 完成判据

1. `libs/ure_diag/` 编译通过，所有下游 lib 成功链接
2. 全部 45 主机测试 + 183 GPU 测试通过（测试文件自身输出保持不变）
3. 迁移后的输出格式统一为 `[timestamp][LEVEL][Tag] message`
4. Release 编译中 Trace/Debug 调用被完全编译消除（反汇编验证或 size diff）
5. `--verbose` 显示 Debug 消息，`--quiet` 仅显示 Error+
6. 声学/物理模块无任何诊断代码被修改

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

## ████████ Batch Cleanup: 审计修复 ████████

**目标**: 修复全方位审计 + 光学特性审计发现的问题。

### Batch 1 — 行为 bug 修复 ✅

| ID | 问题 | 修复 |
|----|------|------|
| C1 | buffer bounds/index check (glTF frontend) | 越界验证 |
| C2 | index 越界 (glTF frontend) | size 检查 |
| C3 | stod try-catch (glTF frontend) | 异常捕获 |
| C5 | FileSink::rotate 首轮 crash | 空目录守卫 |
| C6 | 物理参数字段数据丢失 | `merge_physics_params` |
| C12 | test 逆矩阵错误 | 显式求逆 |
| C15 | 假警报 | 确认已有实现 |

### Batch 2 — 简单修复 ✅

| ID | 问题 | 修复 |
|----|------|------|
| C4 | int 溢出 (image_loader) | cast 检查 |
| C7 | const_cast minor | 移除 |
| C9 | hardcoded 7 → `kDefaultMaterialCount` | 常量替换 |
| C11 | GPU 测试 error path 内存泄漏 | `DeviceMem` RAII guard |
| C14 | GPU 测试未注册 CTest | 7 个 `add_test()` |
| M6 | `cuda_runtime.h` 无 `#ifdef USE_CUDA` | 守卫 |
| M7 | 冗余日志设置 | 删除 |
| M8 | `create_cpu_engine()` 死声明 | 删除 |
| M9 | `create_gpu_engine` 委托 | 别名 |

### Batch 2b — 架构级修复 ✅

| ID | 问题 | 修复 |
|----|------|------|
| C8 | AABB 全 vertex 遍历每帧 | 改为计算 local AABB + 8 corner 变换 (O(N*21) → O(N*6+24)) |
| C10 | RingBuffer memory ordering | 消除 read_index，从 write_index 推导；`end_write()` 加 release fence；`begin_read()` acquire load |
| M10 | `render()` 静默 no-op | 改为 `throw std::runtime_error` |

### Batch 3 — 质量提升 (光学审计)

| ID | 问题 | 修复 | 影响远期 |
|----|------|------|---------|
| O1 | 色散/薄膜 `attenuation` 覆盖 `mat.albedo` 颜色 (material.cu:226) | ✅ 已修复 `attenuation = mat.albedo * GpuSpectrum(b_val)` | — |
| O2 | `MuellerMatrix` struct + `polarizer()`/`apply()` 死代码 (gpu_structs.hpp:124-161) | ✅ 已删除 | — |
| O3 | `refract()` 声明未调用 (decl.cuh:56-62) | ✅ 已删除 | — |
| O4 | `random_in_unit_sphere()` 定义未使用 (sampling.cuh) | ✅ 已删除 | — |
| O5 | `power_heuristic()` 定义未使用 (math_functions.cuh) | ✅ 已删除 (`test_math_functions.cu` 同步清理) | — |
| O6 | `medium_max_distance` 字段声明未使用 (GpuScene) | ✅ 已在 `shade_kernel` 内实现距离约束 (`t_medium < max_allowed`) | — |
| O7 | 旧 `get_thin_film_interference` 缺少 TIR 检查 (polarization.cuh) | ✅ 旧 helper 已删除；thin-film 统一迁移到 boundary evaluator 的 Airy 形式 | — |
| O8 | 旧 `get_thin_film_interference` 使用单次反射近似而非完整 Airy 求和 (polarization.cuh) | ✅ 旧 helper 已删除；dielectric/conductor thin-film 均走 complex Airy boundary evaluator | — |
| O9 | `xyz_to_rgb` 后无伽马校正 (gpu_spectrum_utils.cuh:75-79) | 设计决策：线性帧缓冲交由外部色调映射 | — |
| O10 | `dispersion_clamp` 参数语义不明确 (path_tracer_material.cu:223-226) | ✅ 死代码路径已删除（始终是 `min(1, ≥5) = 1` 无操作）；保留注释 | Phase E (N 通道色散) |
| O11 | Dielectric `pdf_bsdf()` 返回 0 (path_tracer_bsdf.cuh:32-34) | ✅ 添加 TODO + 文档化为 delta 近似；留存 Phase M.2 | Phase M.2 |
| O12 | Metal `eval_bsdf` 消光检查多算 w 通道 (path_tracer_bsdf.cuh:76) | ✅ 已修复：先算 `ext_len_sq = x²+y²+z²` 再比较 | — |

### Batch 4 — 测试补充 (光学审计)

| ID | 覆盖目标 | 说明 | 前置条件 |
|----|---------|------|---------|
| OT1 | 偏振/边界: `rotate_stokes`, `apply_mueller_reflection_*`, dielectric/conductor/thin-film boundary evaluator | ✅ `test_gpu_polarization.cu` 已扩展覆盖 boundary evaluator、conductor thin-film 零厚度回归、conductor/thin-film Mueller-boundary 一致性 (65 断言, CTest gpu_polarization) | — |
| OT2 | Metal/Dielectric BSDF: `eval_bsdf` + `pdf_bsdf` + `scatter()` | 扩展 `test_render_basic.cu` 或新建 | **Phase M.2 前必补** |
| OT3 | Cloth BSDF: eval/pdf + scatter | 同上 | Phase M.2 前 |
| OT4 | 体积散射: 透明度 (Beer-Lambert) + 自由路径采样 | ✅ `test_gpu_volume.cu` 已新建 (2 测试, CTest gpu_volume) | — |
| OT5 | 光谱管道: 抖动波长路径（匹配实际渲染器代码） | 扩展 `test_spectral_pipeline.cu` | Phase E 前 |
| OT6 | `rgb_coeff_to_spectrum()` + `emission_to_spectrum()` | 扩展 `test_spectral_pipeline.cu` | Phase E 前 |
| OT7 | 色散/薄膜散射代码路径（`scatter()` 内 dielectric 分支） | 扩展 `test_render_basic.cu` | Phase M.2 前 |

### 已知限制（已审计，非待修复）

| ID | 限制 | 原因 | 跟踪 |
|----|------|------|------|
| L1 | `spectrum_to_xyz` Riemann 和假设样本位于 bin 中心，忽略抖动偏移 | 4 样本下足够近似；Phase E N 通道升级时改为重要性采样+数值积分校正 | Phase E.1 |
| L2 | 测试与运行时波长不一致：测试用固定 450/550/650/750nm (Δλ=100)，运行时用 360-830nm 抖动 (Δλ=117.5) | 现有测试只验证相对顺序，放宽断言容忍此差异。Phase E 前统一测试波长生成逻辑 | OT5 |
| L3 | `xyz_to_rgb` 输出线性 sRGB，无伽马校正 | 帧缓冲未色调映射，由外部管线负责。文档化为契约 | O9 |

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

完成状态（2026-06-13）: 已完成当前单机多 GPU 边界。`gpu_multi_driver.hpp/.cu` 提供 `MultiGpuContext`、`init_multi_gpu_renderer()`、`render_pass_multi_gpu()`、`copy_frame_buffer_multi_gpu()` 和 `free_multi_gpu_renderer()`；每个 GPU 持有独立 `GpuContext`，按 sample-space offset 分配 disjoint sample slice，device 0 合并 per-device accumulation/sample-count buffers 后复用现有 framebuffer copy path。`RenderConfig::num_gpus_to_use` 控制使用数量，并保留单 GPU renderer API 不变。当前硬件/CI 只有单卡，因此验证以 Release build + 17/17 CTest + warning/error scan 覆盖编译和单卡兼容；真实多卡性能/peer-copy topology benchmark 留作 Phase K 性能验证，不作为 Phase B API 完成阻塞。

---

## ████████ Phase C: 分布式契约 ████████

**目标**: 定义分布式渲染数据契约，不涉及网络实现。

```cpp
struct DistributedSampleRange { int node_id, node_count, sample_start, sample_count, total_samples, width, height; };
struct DistributedFrameBuffer { int width, height, total_samples; float* data; };
DistributedSampleRange make_sample_range(int node_id, int node_count, int total_samples, int width, int height);
bool validate_sample_range(const DistributedSampleRange& range);
void merge_partial_framebuffer(DistributedFrameBuffer& accum, const DistributedFrameBuffer& incoming);
```

完成状态（2026-06-13）: 已完成。契约包含 deterministic sample-range partition、range validation、framebuffer dimension/null/sample-count/overflow validation，以及 order-independent accumulated RGB merge。Release 下不再依赖 `assert` 阻断错误输入；非法输入会抛出标准异常，避免分布式节点静默合并错误帧。`gpu_test_contract` 覆盖分片完整无重叠、非法分片、merge 交换律/结合律/单位元、尺寸不匹配、空 data、负 sample count 和 sample count overflow；当前 targeted gate 为 145/0，build warning/error scan 为空。

---

## ████████ Phase D: 分布式集成 ████████

**目标**: 连接分布式契约到传输层。MPI 后端示例 + 文件 I/O 后端。

完成状态（2026-06-13）: 已完成当前无外部依赖的文件 I/O backend。`distributed_file_io.hpp/.cpp` 定义 sample-range 和 framebuffer 二进制交换格式，包含 magic/version、尺寸、sample count 和 payload 校验；读入 framebuffer 使用 owning `DistributedFrameBufferStorage`，merge workflow 复用 Phase C 的 Release-safe `merge_partial_framebuffer()` 契约。当前不引入 MPI/ZeroMQ 依赖，后续网络后端只需替换 transport，不改变分布式数据契约。`test_distributed_file_io` 覆盖 sample range roundtrip、framebuffer roundtrip、文件级合并、坏 magic/截断 payload、无效 range 和 null framebuffer；targeted gate 35/0，build warning/error scan 为空。

---

## ████████ Phase E: N 通道光谱升级 ████████

**目标**: 物理精确的 N 通道光谱渲染引擎，消除所有 RGB 简化和两套光谱体系的矛盾。

**背景**:
[Phase E 光谱架构设计文档](docs/Phase_E_Spectral_Architecture.md) 定义了完整的架构决策。
本节是该文档的可执行摘要，包含精确的子步骤和文件级修改清单。

**核心设计决策**:
1. **删除 `GpuSpectrum::to_rgb()` 和 `GpuSpectrum::from_rgb()`** — 这两个函数是物理错误的根源。
   `to_rgb()` 做纯截断而非 CIE 积分；`from_rgb()` 不做光谱重建。渲染管线内不再进行 RGB→光谱→RGB 往返。
2. **GpuMaterial 拆分为标量 Header + 6 个 SoA 光谱场** — `GpuMaterialHeader` (48B 固定大小) +
   `d_mat_albedo[N×mat_count]` 等 6 个 `float*` 数组。消除 fat struct 冗余，使 N 运行时可变。
3. **ShadowQueue radiance 改为 SoA `float* radiance_vals`** — 同 RayQueue 的 throughput 模式。
4. **`kNumWavelengths = 4` 常量删除** — 所有光谱长度由 `GpuScene::num_spec`（运行时 `int`）决定。
5. **体积透射率逐通道计算** — 不经过 RGB 往返：`tr[c] = expf(-sigma_t[c] * d)`。
6. **thin-film / 色散遍历 N 通道** — 不再硬编码 650/550/450nm。

### Step E.0 — 前置测试

| 测试 | 内容 | 文件 |
|------|------|------|
| OT5 | 光谱管道 N≠4 测试（扩展 test_spectral_pipeline.cu） | `tests/gpu/test_spectral_pipeline.cu` |
| OT6 | `rgb_coeff_to_spectrum()` + `emission_to_spectrum()` 测试 | `tests/gpu/test_spectral_pipeline.cu` |
| L2 | 统一测试波长生成逻辑（OT5 内部） | `tests/gpu/test_spectral_pipeline.cu` |

### Step E.1 — 核心类型重构（最关键）

**目标**: 用 SoA/N 通道布局替换 `float4` 硬编码布局。

**子步骤**:

| # | 内容 | 文件 | 操作 |
|---|------|------|------|
| 1.1 | 删除 `GpuSpectrum` 的 `float4 values; float4 wavelengths;` 成员和所有运算符 | `libs/ure_core/include/ure/gpu_structs.hpp:126-220` | 重写整个 struct |
| 1.2 | `GpuSpectrum` 改为空壳类型或直接删除，GPU 端以 `float*` + `int num_spec` 替代 | `gpu_structs.hpp` | 删除 |
| 1.3 | `GpuMaterial` 拆分为 `GpuMaterialHeader` (48B) + SoA 光谱数组 | `gpu_structs.hpp:259-280` | 拆分 |
| 1.4 | `d_mat_albedo`, `d_mat_metal_eta`, `d_mat_extinction`, `d_mat_medium_scattering`, `d_mat_medium_absorption`, `d_mat_emission` 加入 `GpuScene` | `gpu_structs.hpp:353-358` | 新增 |
| 1.5 | `ShadowQueue::radiance` (`GpuSpectrum*`) → `radiance_vals` (`float*`, SoA) | `gpu_structs.hpp:385` | 修改类型 |
| 1.6 | 所有 `cudaMalloc` 对 `sizeof(GpuSpectrum)` 的使用改为 `num_spec * sizeof(float)` | `host_api.cu` | 批量替换 |
| 1.7 | `alloc_ray_queue` 验证：`throughput_vals` 已是 `float*` SoA | 仅检查 | ✅ 不改 |
| 1.8 | `load_throughput` / `store_throughput` 移除 `#pragma unroll 4` | `path_tracer_decl.cuh:62,70` | 编辑 |
| 1.9 | `render_frame_gpu`（旧 API）删除，所有调用者迁移 | `host_api.cu:674-1315` | 删除函数 |

**验证**: CTest 14/14 仍全绿（因为只是类型重命名，kernel 逻辑未变）。

### Step E.2 — 物理精确转换

**目标**: 修复 🔴 级物理错误（#1-#8），消除所有 RGB 往返。

**子步骤**:

| # | 问题 | 修复 | 文件 |
|---|------|------|------|
| 2.1 | `GpuSpectrum::to_rgb()` 截断前 3 通道 | **删除 `to_rgb()`**。显示输出端显式调用 `spectrum_to_xyz(N)`→`xyz_to_rgb()`。删除 22 处调用点 | `gpu_structs.hpp`, `wavefront.cuh`(×18), `bsdf.cuh`(×4) |
| 2.2 | `from_rgb()` 不做光谱重建 | **删除 `from_rgb()`**。host 材质编译改为 `rgb_to_spectrum(out, rgb, lambdas, N)` | `gpu_structs.hpp`, `compiler.cpp`(×7), `loader.cpp`(×1), `host_api.cu`(×2) |
| 2.3 | 体积透射 RGB 往返 | 每通道独立 `tr[c] = expf(-sigma_t_values[c] * d)` | `wavefront.cuh:312-328, 593-597` |
| 2.4 | 消光检查引用 `.w` 通道 | `isfinite(throughput[c])` 循环 N 次 | `wavefront.cuh:638-639` |
| 2.5 | `spectrum_to_xyz` 硬编码 /4.0 | 归一化改为 `domain_width / (float)num_spec` | `gpu_spectrum_utils.cuh:67` |
| 2.6 | `rgb_to_spectrum` 写 4 个值 | 改为 N 通道循环 + 正确 CIE 积分 | `gpu_spectrum_utils.cuh:96-127` |
| 2.7 | thin-film 硬编码 650/550/450nm | 遍历 N 通道，`λ[c]` 作参数 | `polarization.cuh`, `material.cu:138-140,298-300` |
| 2.8 | 色散通道索引 R/G/B | 遍历 N 通道，`c` 为索引 | `material.cu:200-226` |
| 2.9 | `dispersion_clamp` 实现或删除 | 评估后决策 | `material.cu:13,223` |
| 2.10 | Mueller 矩阵标量 → 光谱 | 设计决策（E.5 详细） | `polarization.cuh` |

### Step E.3 — 运行时 N

**目标**: 所有光谱长度的运行时配置生效。

**子步骤**:

| # | 内容 | 文件 | 操作 |
|---|------|------|------|
| 3.1 | 删除 `kNumWavelengths = 4`（9 处引用） | `gpu_structs.hpp:127`, `host_api.cu:1081`, `tests/` | 批量删除/替换 |
| 3.2 | `generate_rays_kernel` `float4 ray_wavelengths` → SoA `throughput_wavelengths[c * capacity + idx]` | `path_tracer_raygen.cu`, `tests/gpu/test_spectral_pipeline.cu` | ✅ 已完成；新增 N=8 `test_raygen_runtime_wavelength_count`，`gpu_test_spectral` 319 checks 通过 |
| 3.3a | 新增不返回 `GpuSpectrum[4]` 的 N 通道数组工具函数 | `gpu_spectrum_utils.cuh`, `tests/gpu/test_spectral_pipeline.cu` | ✅ 已完成；`rgb_to_spectrum(float*, float*, ..., const float*, int)` / `rgb_coeff_to_spectrum` / `emission_to_spectrum` / `spectrum_to_xyz(float*, float*, int)`，N=8 `test_array_spectrum_helpers_n8` 覆盖；`gpu_test_spectral` 391 checks 通过 |
| 3.3b | 所有内核参数 `float4 wavelengths` → `const float* wavelengths, int num_spec` | `bsdf.cuh`, `wavefront.cuh`, `gpu_spectrum_utils.cuh`, `tests/gpu/test_spectral_pipeline.cu` | ✅ 已完成；`eval_bsdf()`、`sample_texture()` 和 GPU spectrum helper 测试均迁移到 pointer+N；`gpu_spectrum_utils.cuh` 的 `float4` overload 已删除，`gpu_test_spectral` 12 tests / 469 checks 通过 |
| 3.3c | 过渡期 `GpuSpectrum` 从 4 通道扩容到 `kMaxSpectralChannels=32`，避免 runtime-N 迁移期间 N>4 越界 | `gpu_structs.hpp`, `path_tracer_material.cu`, `path_tracer_wavefront.cuh` | ✅ 已完成；这是迁移安全层，不是最终架构；后续仍必须收敛到 SoA/数组访问 |
| 3.3d | material SoA 读取、scatter 物理循环、ShadowQueue radiance 读写使用 `scene.num_spectral_channels` | `gpu_material_helpers.cuh`, `path_tracer_material.cu`, `path_tracer_wavefront.cuh`, `tests/gpu/test_spectral_pipeline_soa.cu` | ✅ 已完成；新增/升级 N=8 `test_mat_soa_load_n8` 与 `test_sq_extend_nonuniform`，`gpu_test_spectral_soa` 6 tests / 194 checks 通过 |
| 3.3e | `eval_bsdf()`、`sample_texture()`、直接光和 emissive surface 路径移除 `make_float4(throughput.wavelengths[0..3])` | `path_tracer_bsdf.cuh`, `path_tracer_wavefront.cuh`, `tests/gpu/test_spectral_pipeline_soa.cu` | ✅ 已完成；新增 N=8 `test_sample_texture_invalid_n8` 与 `test_eval_bsdf_metal_n8`，`gpu_test_spectral_soa` 8 tests / 257 checks 通过 |
| 3.4 | `#pragma unroll 4` 移除（1.8 已做） | `decl.cuh` | 已覆盖 |
| 3.5 | `render_frame_gpu` 删除（1.9 已做） | `host_api.cu`, `gpu_driver.hpp` | ✅ 已完成；2026-06-11 targeted gate 通过，当前总门禁已推进到 Release build + 17/17 CTest |
| 3.6 | `host_api.cu:433` 纹理上传改为 N 通道 | `host_api.cu`, `host_texture.hpp`, `image_loader.cpp`, `gpu_structs.hpp`, `wavefront.cuh`, `tests/gpu/test_spectral_pipeline_soa.cu` | ✅ 已完成；`HostTexture::channels` / `GpuTexture::channels` 区分 RGB 与显式光谱纹理，RGB 保留 `texObj`，N 通道光谱纹理走 `GpuTexture::data` + N 通道双线性采样；新增 `test_sample_texture_spectral_data_n8`，`gpu_test_spectral_soa` 9 tests / 283 checks 通过 |
| 3.7 | CPU 端 `resample_uniform` 默认参数改为 `RenderConfig::num_wavelengths` | `spd_loader.hpp`, `scene_io.hpp`, `scene_io_api.cpp`, `tests/host/test_asset_pipeline.cpp` | ✅ 已完成；`resample_uniform` 不再有隐藏 4 通道默认值，`load_spd(path, int)` / `load_spd(path, RenderConfig)` 接受 runtime N；新增 `test_scene_io_load_spd_runtime_n`，`test_asset_pipeline` 7 tests / 48 checks 通过 |

### Step E.4 — SPD 输入

**目标**: 接入完整 SPD→GpuSpectrum 管线。

**子步骤**:

| # | 内容 | 文件 |
|---|------|------|
| 4.1 | glTF `URE_spectral_material` 的 `albedoSPD`/`emissionSPD` 解析为相对 glTF 文件目录的规范路径 | `gltf_scene_frontend.cpp` | ✅ 已完成 |
| 4.2 | `GpuSceneCompiler::compile(scene_ir, RenderConfig)` 按 runtime N 的光谱 bin 中心重采样 SPD，填充 `GpuMaterialData::{albedo,emission}` | `gpu_scene_compiler.hpp/.cpp` | ✅ 已完成 |
| 4.3 | `RenderEngineFactory::create_gpu_renderer(RenderConfig)` + CLI 配置传递，确保 `cfg.spectral.bands` 进入 SceneIR 编译和 host upload | `render.hpp`, `gpu_engine_impl.cpp`, `main.cpp` | ✅ 已完成 |
| 4.4 | HostTexture 支持 N 通道（或新增 `SpectralTexture` 类型） | `host_texture.hpp`, `host_api.cu:433` | ✅ E.3 已完成 carrier；E.4 的 SPD/glTF 光谱输入已进入 material SoA，纹理 carrier 保留为 post-E 性能/资产路径扩展 |
| 4.5 | 测试：SPD 加载 + compiler runtime-N + renderer load/render-pass 验证 | `tests/host/test_gltf_frontend.cpp` | ✅ 已完成；新增 `test_spectral_spd_compiles_runtime_n`，`test_gltf_frontend` 12 cases / 141 checks |

### Step E.5 — 色散 + Mueller

**目标**: 物理精确的色散和偏振光谱化。

2026-06-11 并行物理审查结论：当前代码中存在系统性光学估计器风险，E.5 不允许以 packet 内平均/hero-channel 近似作为最终完成标准。已经修复可局部证明的偏置项：SceneIR RGB fallback 和旧 `GpuMaterialData` RGB upload 均不再把 RGB triple 当作 spectral slots；metal scatter attenuation 使用 per-channel conductor Fresnel；light-list 遍历全部 runtime-N emission 通道；volume no-scatter 权重使用 proposal `exp(-sigma_t_avg * t_hit)`。E.5 架构已明确采用 **hybrid packet + spectral lane split**：非方向相关事件保持 packet；色散 dielectric/临界角/TIR 等波长决定方向的 delta 事件拆成 per-channel lanes，并可确定性 split reflection/transmission；Stokes 改为 channel-major SoA。

2026-06-12 物理公式并行审查补充：确认 3 个 P0、6 个 P1/P2 风险。已立即处理 P0-1/P0-2/P0-3、P1 film-substrate TIR phase、transparent shadow 出射 TIR、VNDF metal continuation weight、lane-mode volume proposal 与 P2-RR：raygen wavelength 从 bin 内 jitter 改为 bin center，和 host 侧 material/medium/texture SoA 的采样语义一致；n/k metal scatter 不再把 baseColor 乘入 Fresnel continuation，和 `eval_bsdf()` 的 conductor 语义对齐；thin-film dielectric boundary evaluator 返回 complex `r_s/r_p/t_s/t_p`、power `R/T` 和 transmission phase，scatter/lane split/shadow 全部消费同一 result；film-substrate TIR 使用 complex Fresnel amplitude 保留相位；transparent shadow 在直线近似下维护同一 dielectric 的 enter/exit 状态，出射临界角会 TIR 阻断；metal VNDF continuation 改为 `F * G1(L)`，即 `eval_bsdf * cos / pdf` 的闭式结果；lane-mode volume free-flight/no-scatter proposal 使用 active channel `sigma_t`，packet mode 继续使用平均 proposal；Russian roulette survival 改为非负 spectral max proxy，不再使用 `spectrum_to_xyz -> xyz_to_rgb -> max RGB`。后续追加审查又发现 5 个 blocker，并已收束：dielectric radiance transport scale 改为 `(eta_i / eta_t)^2`、Stokes convention 固定为 `Q = I_s - I_p`、ShadowQueue direct-light lane estimator 携带 wavelength pdf、thin-film metal scatter/eval_bsdf 共用 reflectance helper、lane-mode metal polarization 使用 active channel eta/k。仍保留为明确后续边界的是：transparent shadow 嵌套 medium stack/折射方向、specular manifold/refractive shadow path、更完整 white-furnace/MIS 场景验证、rough dielectric microfacet BTDF 替代当前 normal jitter 近似。

**子步骤**:

| # | 内容 | 文件 |
|---|------|------|
| 5.0 | 光学审查前置纠偏：RGB fallback/runtime-N upload、metal per-channel Fresnel、long-wavelength emission light-list、volume no-scatter PDF | `gpu_scene_compiler.cpp`, `host_api.cu`, `material.cu`, `wavefront.cuh` | ✅ 已完成，测试覆盖 |
| 5.1 | Path spectral state：`RayQueue` 增加 `spectral_mode` / `active_channel` / `wavelength_pdf`，Stokes 改为 `stokes_i/q/u/v` SoA | `gpu_structs.hpp`, `decl.cuh`, `raygen.cu`, `wavefront.cuh`, `host_api.cu` | ✅ 队列状态与传播已落地；packet scatter 输入使用 channel-average Stokes，输出按 channel 写回 metal/dielectric boundary Mueller；lane split 生成仍属 5.3 |
| 5.2 | Spectral boundary evaluator：统一 dielectric/conductor/thin-film 的 per-channel Fresnel、complex amplitude、Mueller 输入 | `polarization.cuh`, `boundary.cuh`, `material.cu` | 完成：`path_tracer_boundary.cuh` 已落地并接入 scatter/eval_bsdf/shadow policy；`DielectricSurfaceBoundary` 统一裸 dielectric 与 thin-film dielectric 的 complex amplitude、power R/T、eta Jacobian 和 transport scale；conductor、thin-film reflection 与 thin-film transmission Mueller 已从 boundary complex amplitude 派生；packet metal/dielectric Stokes 按 channel 写回 |
| 5.3 | Dispersive dielectric lane split：色散/临界角 delta interface 生成 per-channel reflected/transmitted lanes；队列 overflow 诊断化 | `material.cu`, `wavefront.cuh` | 完成：packet dispersive/thin-film dielectric deterministic split 为 per-channel lanes；N=8 临界角 split、host-visible `overflow_count`、medium transition helper 和真实 lane split medium output 均已覆盖 |
| 5.4 | Transport mode/Jacobian：删除 dielectric transmission clamp，明确 radiance/importance transport 下 eta 权重 | `material.cu`, boundary evaluator | 完成：旧 scatter 的 `radiance_scale > 1.5` clamp 已删除；radiance/importance eta scale 已进入 `DielectricSurfaceBoundary`，scatter 与 spectral lane split 通过 `select_boundary_transport_scale()` 消费同一协议，不再手写 eta²；最终测试锁定 air→glass radiance attenuation、glass→air inverse scale、slab 往返 scale=1、unpolarized transmission transport weight、两次真实 `scatter()` 穿 slab 的 eta scale 相消，以及 bare dielectric / dielectric thin-film / TIR surface 的 `R+T=1` boundary furnace 门禁 |
| 5.5 | Transparent shadow/NEE policy：删除 scalar Schlick visibility；specular dielectric 不再用直线 shadow 伪造连接 | `wavefront.cuh`, shadow queue | 完成：NEE shadow 遇到 specular dielectric 直接阻断，避免错误 straight-through refractive connection；正入射与离轴 specular dielectric blocker tests 已覆盖；未来玻璃直接光属于 specular manifold / refractive shadow path 后续设计 |
| 5.6 | K.6 光谱 MIS/RR/XYZ：显式 wavelength PDF、spectral RR importance、XYZ 权重 | `wavefront.cuh`, `gpu_spectrum_utils.cuh` | 完成：RR survival 已改为 spectral max proxy，CIE 1931 2-degree table 已从 CIE 018:2019 官方 1nm CSV 重采样为 5nm CPU/GPU 表；新增 `sampled_spectrum_to_xyz()`，lane contribution 使用 explicit `bin_width / wavelength_pdf`，deterministic lane split 将 lane throughput 乘以 pdf 保持与 packet quadrature 等价；N=32 white RGB roundtrip、equal-energy E、D65 SPD whitepoint、sampled/packet PDF 等价和 narrowband RR 门禁已补 |

**E.5 完成门禁**:
- Tests: analytic Fresnel normal incidence、Brewster、TIR phase、thin-film Airy、两波长临界角 split、N=8 split count、specular dielectric shadow blocking、D65/equal-energy XYZ、narrowband RR unbiasedness。
- Searches: `rg` 必须确认无 packet-wide dispersive refraction、无 dielectric transmission clamp、transparent shadow 不再使用 scalar Schlick。
- Build: Release build、17/17 CTest、build log warning/error scan、`git diff --check` 全绿。

### Phase E.5 finite closure plan

从 2026-06-12 起，E.5 剩余问题固定收束为 6 个大步。除非发现会推翻 hybrid packet + spectral lane split 架构的 P0 物理错误，不再新增 E.5 子阶段；新发现的问题必须归入下表最近的 bucket，并在该 bucket 内解决或记录为明确的后续 Phase。

| Closure step | 必须完成到哪里 | 完成证据 |
|--------------|----------------|----------|
| C1 Medium / IOR transition closure | spectral lane split 后的 medium state、enter/exit IOR、TIR 和普通非色散 dielectric path 都有一致语义；specular dielectric shadow policy 保持 blocker，不再尝试直线折射修补 | ✅ `next_dielectric_medium_index()` 成为普通 scatter path 与 lane split 的唯一 transition helper；GPU tests 覆盖 enter/exit/nested/reflection state machine 和真实 lane split medium output；`gpu_test_spectral_soa` 519/0 |
| C2 Optical material semantics closure | material 的 measured n/k、dielectric eta、thin-film、baseColor/F0 fallback 语义收敛为单一规则；有 n/k 时 albedo 不再二次染色，无 n/k 时 fallback 明确 | ✅ `ConductorMaterialSemantics` 统一 BSDF/scatter/Stokes 判定；measured conductor 由 nonzero k 启用，eta 可来自 spectral eta 或 material fallback，k=0 时使用 albedo/F0 fallback；GPU + compiler tests 覆盖 |
| C3 Transport / reciprocity / white furnace closure | dielectric/conductor/thin-film 的 power、radiance transport、importance transport 不再混用；surface/slab 的 reciprocity 和 furnace 行为有测试证据 | ✅ boundary furnace `R+T=1` 覆盖 bare dielectric、dielectric thin-film、TIR 与 reverse interface；slab reciprocity 覆盖两次真实 `scatter()` 后 eta scale 相消；无 dielectric transmission clamp；radiance scale 方向锁定为 air→glass attenuation、glass→air inverse；`gpu_test_polarization` 126/0，构建日志 warning/error scan 为空 |
| C4 Spectral sampling / MIS / photometry closure | `wavelength_pdf` 成为 resolve/RR/MIS 的显式输入；D65 SPD、equal-energy E、narrowband RR 都有门禁；packet/lane contribution 的 PDF 语义闭合 | ✅ `sampled_spectrum_to_xyz()` 明确 lane estimator 为 `value * bin_width / wavelength_pdf`；ShadowQueue carries spectral mode / active channel / wavelength pdf；deterministic split lane 预乘 pdf，与 packet quadrature 等价；GPU tests 覆盖 D65 SPD -> sRGB white、equal-energy E、narrowband RR、sampled/packet PDF 等价和 lane pdf carrier；`gpu_test_spectral` 569/0、`gpu_test_spectral_soa` 633/0、`gpu_test_render` 281/0 |
| C5 Transitional API removal / static audit closure | Phase E 过渡桥接不再遮蔽架构目标：固定 4 通道路径、`.values.x/y/z/w`、scalar Stokes device 依赖、过时 helper/API 全部审计 | ✅ code search 对 `kNumWavelengths`、`.values.x/y/z/w`、`.wavelengths.x/y/z/w`、`.to_rgb()`、`from_rgb(`、`spd_from_rgb(`、旧 thin-film helper 和 dielectric clamp 全部清零；CPU `SampledSpectrum::from_rgb/to_rgb` 已删除；旧 scene factory 已在 Phase M 删除；`test_asset_pipeline` 48/0、`test_gltf_frontend` 185/0、`gpu_test_spectral` 569/0 |
| C6 Final E.5 audit and documentation closure | E.5 状态从“进行中”改为“完成”；所有新增风险、技术债、后续 Phase 边界写入 PLAN 和 Phase E 文档 | ✅ Release build、17/17 CTest、warning/error scan、`git diff --check`、E.5 搜索门禁全绿；已进入提交边界 |

测试节奏：每个 closure step 内只跑针对性 build/test 和必要的 warning filter；每个 closure step 结束必须跑该 step 的 targeted GPU/host tests。只有 C6 才跑完整 Release + CTest + 全量搜索门禁。

### 修改波及范围统计

| 统计项 | 数量 | 说明 |
|--------|------|------|
| 直接修改的源文件 | 14 | `gpu_structs.hpp`, `gpu_spectrum_utils.cuh`, `gpu_driver.hpp`, `gpu_context.hpp`, `gpu_scene_loader.hpp`, `gpu_scene_compiler.hpp`, `gpu_scene_compiler.cpp`, `path_tracer_host_api.cu`, `path_tracer_material.cu`, `path_tracer_decl.cuh`, `path_tracer_bsdf.cuh`, `path_tracer_wavefront.cuh`, `path_tracer_raygen.cu`, `polarization.cuh` |
| `.values.x/y/z/w` 访问点 | 123 | 全部替换为循环索引访问 |
| 删除的成员函数 | 22 | `to_rgb()` 调用点 + `from_rgb()` 调用点 |
| 新增的行数 | ~600 | SoA 分配 + 循环访问 + CIE 积分 |
| 删除的行数 | ~300 | float4 运算 + 旧 API + 死代码 |

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
新 Phase H: ████████████ 已完成 (stb_image + SPD loader + 测试通过)
新 Phase Dx: ████████████ 已完成 (264 测试通过，~73 站点迁移完毕)
新 Phase G: ████████████ 已完成 (审计修复 5 项差距 + 全方位测试: host 11 用例/55 检查 + GPU 3 用例/21 检查; 全绿)
新 Phase I: ████████████ 已完成 (JSON + CLI11 + 子命令 + 覆盖链)
新 Phase A: ████████████ 已完成 (SoA 队列 + RenderConfig 集成; 13/13 CTest 全绿)
新 Phase B: ████████████ 已完成 (`MultiGpuContext` + sample-space partition + merged framebuffer copy; single-GPU API unchanged)
新 Phase C: ████████████ 已完成 (sample range partition + deterministic framebuffer merge + Release-safe validation; gpu_test_contract 145/0)
新 Phase D: ████████████ 已完成 (file I/O backend + checked range/framebuffer serialization + merge workflow; test_distributed_file_io 35/0)
新 Phase E: ██████████ 完成（E.0-E.4 已完成；E.5 C1-C6 已完成 medium/IOR transition、conductor material semantics、transport reciprocity + boundary furnace、explicit wavelength PDF + D65/equal-energy/RR photometry、transitional API static audit、full Release build + 17/17 CTest + warning/error scan + search gate）
远期 Phase S: ████████████ 已完成 (`RenderSession` + `SceneDiff` + progressive worker + AOV + C ABI + pyure mutation workflow; test_session 188/0, test_pyure_smoke 通过)
远期 Phase M: ░░░░░░░░░░░░ 未开始 (材质节点图 / MaterialX / 预设库；依赖 Phase E + S 稳定边界)

旧目录清理: ████████████ 已完成 (include/ + src/ + tests/{unit,integration} 删除; CMakeLists.txt 遗留构建块移除)
```

当前技术债记录（2026-06-12）:
- Release build warning 债已清理：`C4100` 使用参数修正；`C4324` 仅在有意对齐的 GPU ABI 声明/实现范围内局部屏蔽；GPU test `LNK4098` 通过 `CUDA_RUNTIME_LIBRARY None` + `CUDA::cudart` shared runtime 链接消除；CLI `C4819` 通过 `/utf-8` 消除。`build_modular/last_release_build.log` 无 `warning/警告` 命中。
- Phase E.3 过渡期 `GpuSpectrum` 扩容到 32 通道会提高局部对象大小和潜在 register/local memory 压力；这是为避免 N>4 迁移期间越界的临时安全层，不是最终 SoA 架构。`float4 wavelengths` helper 调用面已删除，后续迁移应继续消除 `GpuSpectrum` 返回值和局部桥接对象。Raygen 当前固定使用 bin center wavelength，保证和 host 编译的 material/medium/texture SoA 语义一致；未来若恢复 bin jitter，必须先把材质/介质/SPD 改成按 ray wavelength 动态采样或可插值表示。
- Phase E.3 显式光谱纹理当前通过 `GpuTexture::data` 手写双线性采样实现，而不是 layered texture object；这是正确性优先的 N-channel carrier，后续可作为性能优化替换为分层 CUDA texture。
- Phase E.4 发现并修复配置链断点：`RenderEngineFactory::create_gpu_renderer()` 过去无法接收 `RenderConfig`，CLI `cfg.spectral.bands` 不会进入 renderer。已新增 config 重载并在 CLI 中传递 `spectral.bands` / queue capacity / max depth。后续配置审计应继续检查 `scene_ir.width/height` 与 CLI override 的合流点。
- Phase E.5 并行审查确认的数学债已在 C1-C6 和后续 blocker pass 内收束：packet 色散不再由 hero wavelength 单独决定整包方向，dispersive/thin-film dielectric 首段会 deterministic split；旧 dielectric transmission clamp 已删除，radiance transport 方向锁定为 `(eta_i / eta_t)^2`，importance 为其逆，air→glass 衰减、glass→air 放大，slab 往返相消；Stokes convention 固定为 `Q = I_s - I_p`；conductor/thin-film reflection 与 thin-film transmission Mueller 已从 boundary complex amplitude 派生，packet metal/dielectric 输出 Stokes 已按 channel 写回且 packet sampling 输入使用 channel-average Stokes；lane contribution 和 ShadowQueue direct lighting 已使用 explicit wavelength PDF estimator。specular manifold NEE、rough dielectric microfacet BTDF、RGB/photometry fallback 精度和 volume spectral proposal 方差仍是明确后续边界，不再作为 E.5 完成阻塞。
- 2026-06-13 物理第一性审查重新打开 4 个 correctness blocker 与 2 个完整性边界：lane-mode dielectric 后续界面不能回落到 packet hero-channel；dielectric interface 不能被 baseColor/albedo 染色；rough metal 的 VNDF sampling/eval/pdf 必须同分布；sphere-light MIS reverse PDF 必须匹配实际 solid-angle sampling；rough dielectric 仍需 microfacet BSDF/BTDF；specular dielectric direct lighting 仍需 specular manifold / refractive shadow path。本批已修复 4 个 correctness blocker：lane active-channel、dielectric tint 语义、sphere-light MIS PDF、rough metal `alpha = roughness^2` + exact Smith GGX 一致性，并新增 targeted GPU regressions。rough dielectric path continuation 已从 normal jitter 替换为 GGX visible microfacet reflection/transmission；reflection/transmission lobe 已进入 `eval_bsdf()` / `pdf_bsdf()` / direct-light MIS，scatter 也返回非 delta PDF；rough thin-film/dispersive dielectric 已从 smooth specular lane split 中移出，进入同一 microfacet BSDF 路径，per-channel lobe 会按 wavelength 重新计算 dispersive IOR、thin-film boundary 和 transmission Jacobian；shade direct-light gate 已允许 rough dielectric BTDF 半球并按出射侧偏移 shadow origin；transmission continuation 已用 `eval_bsdf * abs(cos) / pdf_bsdf` 等式锁定同分布。完整 furnace/reference scenes 与 specular manifold 仍是下一批大范围物理设计入口。历史真实 CLI 视觉 smoke 已随旧文本场景 cutoff 删除；后续视觉 smoke 必须迁移到 glTF/GLB。
- 2026-06-13 用户级可靠性收敛：Release 默认配置已实跑通过；runtime spectral channel contract 显式收紧为 `[8, 32]`，核心默认 N=8，CLI 默认 N=32，4 通道会在 CLI/compiler/GPU init 层拒绝；CLI 输出新增 Radiance HDR (`--format hdr` / `.hdr`)；SceneFrontend 只分派 `.gltf/.glb`，未知扩展均 fail-loud，direct GltfSceneFrontend 也拒绝非 glTF 输入。
- Host interactive API 的自定义 `spheres/materials` 当前仍与默认材质表合并，外部传入 sphere 的 `material_index` 指向合并后索引而非传入 materials 的局部索引。新测试按当前语义覆盖 long-wavelength light-list；后续 API 清理应显式定义并测试 material index offset 规则。
- Phase E.5.1 已将 `RayQueue` 从 scalar `StokesVector*` 迁移到 channel-major `stokes_i/q/u/v` SoA，并新增 `spectral_modes` / `active_channels` / `wavelength_pdfs`。packet scatter 的输入 Stokes 现在使用 channel-average，输出通过 `store_packet_scattered_stokes()` 按通道写回 metal/dielectric boundary Mueller；真正的 dispersive lane child ray 生成在 E.5.3。
- Phase E.5.2 已新增 `path_tracer_boundary.cuh`，集中 dielectric/conductor/thin-film boundary 计算；`DielectricSurfaceBoundary` 统一裸 dielectric 与 dielectric thin-film 的复振幅、power R/T、eta Jacobian 和 radiance/importance transport scale，`scatter()`、spectral lane split 和 transparent shadow visibility 均消费同一 surface result。旧 `conductor_fresnel_reflectance()`、`get_dielectric_thin_film_reflectance()` 和 `get_thin_film_interference()` 已删除；`apply_mueller_reflection_conductor()` 和 dielectric thin-film reflection 现在直接从 boundary 复振幅派生 Mueller 项；metal thin-film scatter 现走 `eval_thin_film_conductor_boundary()`，albedo-F0 模式临时映射为等效 real eta，后续需以明确材质语义替代。
- Phase E.5.2 金属语义已收紧：当 material 提供 n/k (`extinction` 非零) 时，complex Fresnel 是 conductor 反射的唯一颜色来源，`baseColor/albedo` 不再二次染色 scatter continuation；无 n/k 时 baseColor 仍作为 Schlick F0 fallback。`test_metal_scatter_uses_per_channel_conductor_fresnel` 已改用非均匀 albedo 并验证 `attenuation / Fresnel` 跨 channel 恒定。
- Phase E.5 metal VNDF 权重已和 `eval_bsdf()/pdf_bsdf()` 对齐：visible-normal PDF 下 continuation weight 改为 `F * G1(L)`，不再使用额外 `VdotH/(NdotH*NdotV)` 因子。`test_metal_scatter_uses_per_channel_conductor_fresnel` 现在直接验证 scatter attenuation 等于 per-channel conductor Fresnel 乘 `smith_G1(NdotL)`。
- Phase E.5.6 已收束：`shade_kernel` 的 Russian roulette survival 与 transparent shadow radiance early-out 使用 `spectral_survival_probability()`，以非负 spectral max 作为代理，不再经过 display RGB。`test_spectral_survival_probability` 覆盖 430/700/820nm 窄谱和负通道混合情形。`test_white_roundtrip` 现在对 N=32 runtime wavelengths 施加 RGB→spectrum→XYZ→sRGB 白点近中性门禁；N=4 只保留 smoke，不作为白点正确性证据。CIE 1931 2-degree Y/Z 表过去在长波段错误衰减，已替换为 CIE 018:2019 官方 1nm CSV 重采样 5nm 表，CPU/GPU normalization 改为从 Y 积分派生；`test_equal_energy_xyz_normalization` 锁定 equal-energy E 的 XYZ≈1 和 chromaticity≈1/3。新增 `sampled_spectrum_to_xyz()`，lane contribution 使用 `bin_width / wavelength_pdf`，deterministic split lane throughput 预乘 pdf，因此所有 lane 求和与 packet quadrature 等价；`test_d65_spd_whitepoint` 使用 CIE D65 10nm 表验证 N=32 采样后的 xy≈(0.3127,0.3290) 且线性 sRGB 近中性。
- Phase E.5 volume proposal 已按 spectral mode 区分：packet mode 使用 `sigma_t_avg` 作为 proposal 并保留 per-channel MIS 权重；lane mode 使用 active channel 的 `sigma_t`，避免单通道路径仍被全包平均介质控制。`test_lane_no_scatter_proposal_weight` 覆盖 active-channel proposal 与 packet proposal 不同的极端 sigma_t case。
- Phase E.5.3 已在 `shade_kernel` 的旧 `scatter()` 前增加 deterministic split 路径：packet + dispersive/thin-film dielectric 会生成 per-channel reflected/transmitted lane，lane throughput 只保留 active channel，并使用 `SpectralRayModeLane` / `active_channels` 传播。N=8 临界角测试已覆盖同一 packet 中短波 TIR 与长波透射并存；`RayQueue::overflow_count` 已提供 host-visible overflow 诊断。普通非色散 dielectric 仍走旧 scatter 路径；medium transition 已在 C1 通过 `next_dielectric_medium_index()` 收束。
- Phase E.5.5 policy 已修正：旧 transparent shadow straight-through dielectric visibility 会在 NEE 中伪造不存在的 specular refractive connection，现已删除。`extend_shadow_kernel` 遇到 specular dielectric 直接阻断；`test_sq_extend_specular_dielectric_blocks` 和 `test_sq_extend_off_axis_specular_dielectric_blocks` 覆盖正入射和离轴 glass blocker。后续若要恢复玻璃后的直接光，必须实现 specular manifold / refractive shadow path，而不是在 ShadowQueue 上直线折射修补。
- Phase E.5 C1 已收束：dielectric medium transition 不再在 ordinary scatter path 和 dispersive lane split 中各自手写。新增 `next_dielectric_medium_index()` 统一判定 reflection/no-crossing、air→material enter、material→air exit 和 nested replacement；`test_dielectric_medium_transition_helper` 覆盖状态机，`test_lane_split_medium_transition` 覆盖真实 split 输出中 reflected lane 保持原 medium、transmitted lane 进入 dielectric material medium。针对性门禁：`gpu_test_spectral_soa` 519/0，构建日志 warning/error scan 为空。C1 不实现 refractive shadow path；specular dielectric shadow 继续 blocker policy，恢复玻璃直接光归 specular manifold 后续设计。
- Phase E.5 C2 已收束：conductor 材质语义由 `ConductorMaterialSemantics` 集中判定，`eval_bsdf()`、`scatter()` 和 packet Stokes 写回不再各自检查 `metal_eta/extinction`。规则固定为：`extinction/k` 非零表示 measured conductor；若 measured conductor 且 `metal_eta` 非零则使用 spectral eta，否则使用 material scalar `ior`；若 `k=0` 则使用 albedo/F0 fallback，`metal_eta` 不会意外启用 measured conductor。`test_conductor_material_semantics` 覆盖三种 device 判定，`test_metal_coefficients_compile_as_physical_carriers` 覆盖 compiler 物理系数 carrier 和 fallback metal 的 zero-k 语义。针对性门禁：`gpu_test_spectral_soa` 534/0，`test_gltf_frontend` 185/0，构建日志 warning/error scan 为空。
- Phase E.5 C3 已收束：dielectric transport 现在以 boundary result 为唯一事实来源，power probability、radiance eta scale、importance eta scale 分离且互逆；surface furnace 测试要求 bare dielectric、oblique reverse interface、dielectric thin-film 和 TIR 的 unpolarized `R+T=1`，slab reciprocity 测试要求两次真实 `scatter()` 后 air→glass→air 的 eta scale 在 path weight 上相消。针对性门禁：`gpu_test_polarization` 126/0，构建日志 warning/error scan 为空。
- Phase E.5 C4 已收束：wavelength PDF 不再只是队列字段；`sampled_spectrum_to_xyz()` 是 lane contribution 的唯一 sampled XYZ estimator，普通 packet 继续走 quadrature fallback；ShadowQueue direct light 也携带 spectral mode、active channel 和 wavelength pdf。packet→lane deterministic split 写出 `pdf=1/N` 并将 active lane throughput 预乘 pdf，sampled estimator 除以 pdf 后恢复每个 bin 的 quadrature contribution。针对性门禁：`gpu_test_spectral` 569/0、`gpu_test_spectral_soa` 633/0、`gpu_test_render` 281/0，全部构建日志 warning/error scan 为空。
- Phase E.5 C5 已收束：代码级静态审计对 `kNumWavelengths`、`.values.x/y/z/w`、`.wavelengths.x/y/z/w`、`.to_rgb()`、`from_rgb(`、`spd_from_rgb(`、旧 thin-film helper 和 dielectric transmission clamp 均无命中。CPU `SampledSpectrum::from_rgb()` / `to_rgb()` 已删除；旧 scene factory 已在 Phase M 删除。针对性门禁：`test_asset_pipeline` 48/0、`test_gltf_frontend` 185/0、`gpu_test_spectral` 569/0，构建日志 warning/error scan 为空。

## 预估总工期

| Phase | 工期 | 依赖 | 性质 |
|-------|:----:|------|------|
| Phase 0 | 3 天 | 无 | 纯新增 |
| Phase F | 5 天 | Phase 0 | 搬文件+CMake |
| Phase P | 8 天 | Phase F | 运行时数据管线 |
| Phase G | 6 天 | Phase F | 与 H/P 可并行 (含审计修复 + 测试) |
| Phase H | 4 天 | Phase F | 与 G/P 可并行 |
| Phase Dx | 6.5 天 | Phase F+H | 诊断系统，与 G/H/I/P 可并行；仅限光学模块 |
| Phase I | 4 天 | Phase G+H | 与 P 可并行 |
| Phase A | 5 天 | Phase P+0 | SoA 队列 |
| Phase B | 4 天 | Phase A | 多 GPU |
| Phase C | 3 天 | 无 | 契约,可插队 |
| Phase D | 3 天 | Phase C | 分布式 |
| Phase E | 15 天 | Phase A+G+N | N 通道光谱（架构重构 + 物理校正 + SPD + 色散/Mueller） |

---

## 与独特高级特性的兼容性

| 特性 | 设计中位置 | 冲突风险 |
|------|-----------|---------|
| N 通道光谱 (运行时指定) | RenderConfig.num_wavelengths → Phase E | ✅ SceneIR material RGB fallback 已在 compiler 按 runtime-N bin 重建；`GpuMaterialData` upload 不再运行时重解释旧 RGB slots；RGB/N-channel texture carrier 已接入，advanced texture semantics 属 post-E 资产/性能扩展 |
| CIE 解析函数 | CPU/GPU 均使用 CIE 1931 2-degree table 插值，GPU 端以 device constexpr table 避免 `std::array` 进入 device code | ✅ Phase E 已统一为官方表插值；equal-energy E 与 D65 SPD whitepoint gates 已通过 |
| SoA 队列 | Phase A, ure_core 内部 | ✅ 已兼容 N 通道 |
| Wavefront 渲染 | ure_core 核心算法 | 需批量修改 `.values.x/y/z/w` 为循环访问 |
| 嵌套介质 IOR | 已在 kernel.cu → ure_core | 需接入 N 通道透射计算 |
| Mueller 矩阵 | Phase E Step E.5 | ✅ conductor/dielectric/thin-film Mueller 输入由 per-channel boundary complex amplitudes 派生，packet 输出按 channel 写回 |
| SPD 输入 | Phase H + Phase E.4 | ✅ glTF `URE_spectral_material` 的 SPD 路径已解析并按 `RenderConfig::num_wavelengths` 重采样到 material SoA；`test_gltf_frontend` 覆盖 compiler + render pass |
| 统一诊断日志系统 | Phase Dx, ure_diag | ✅ 零冲突 |
| RGB→高斯上采样 | gpu_spectrum_utils.cuh → ure_core | 作为 SPD fallback 保留，非默认路径 |
| `GpuSpectrum::to_rgb()` / `from_rgb()` | 物理错误根源 | ❌ **删除**，替换为显式 spectrum_to_xyz/xyz_to_rgb |
| 物理/声学 | ure_physics 独立库 + ISpatialQuery | ✅ 零冲突 |
| ECS/World 数据模型 | Phase P, ure_types | ✅ 零冲突 |
| GPU Transform 热更新 | Phase P, ure_core | ✅ 零冲突 |

---

## 依赖图

```
Phase 0 (硬件检测)
    │ 无依赖,先写代码
    ▼
Phase F (目录树+CMake)
    │ F.1-F.5: 已完成 (2026-06-09)
    ├──────────┬──────────┬──────────┬──────────┐
    ▼          ▼          ▼          ▼          ▼
Phase P    Phase G    Phase H    Phase Dx   Phase I
(管线)     (glTF)     (资产管线)  (诊断系统)  (配置)
    │          │          │          │
    ├──────────┴──────────┘          │
    ▼                               │
Phase A (SoA 队列) ──────────────────┘
    │
    ├─────────────────────────────────────────┐
    │                                          │
    ▼                                          ▼
Phase B (多GPU)  Phase C (分布式)   Phase E (N通道光谱)
    │                 │               │
    │                 │               ├── E.0 前置测试
    ├────────┬────────┘               ├── E.1 核心类型重构
    ▼        ▼                        ├── E.2 物理精确转换
Phase D (分布式集成)                  ├── E.3 运行时 N
    │                                 ├── E.4 SPD 输入
    │                                 └── E.5 色散+Mueller
    └──── Phase E 完成后 ───→ 远期 Phase (S/M/U/X/K)
```

并行建议:
- Phase G / H / I 可同步进行，无交叉依赖
- Phase P 与 Phase G / H / I 无依赖关系
- Phase Dx 与 Phase G / H / I / P 均可并行，是对现有诊断输出的增量式替换，不影响渲染管线
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
| 场景描述 | glTF/GLB only | glTF 2.0 + URE 扩展 | USD + Hydra 委托 |
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
| S.1 | 定义 `RenderSession` 接口 + 实现 | ✅ `session.hpp`, `session.cpp`; 支持 create/load_scene/render_pass/start/pause/resume/cancel/reset/update_camera/progress/framebuffer；`test_session` 覆盖状态机 |
| S.2 | 定义 `SceneDiff` 增量变更描述（增/删/改 entity，改 transform，改材质） | ✅ 当前 Session 边界已完成：`scene_diff.hpp` 建立高层 diff contract，支持 full SceneIR replacement、camera mutation、reset accumulation、instance transform mutation、SceneIR material-table mutation、SceneIR instance add/remove、SceneIR sphere add/remove；transform/material 参数 mutation 继续走 `IRenderEngine::update_transforms()` / `update_materials()` 热更新；拓扑 mutation 与 texture/resource material mutation 先更新 retained SceneIR，再通过 `IRenderEngine::reload_scene_ir()` 显式 full GPU reload，避免误用 transform-only path；Phase M 起旧 Scene mutation API 与底层 Scene bridge 已移除 |
| S.3 | C API：`ure_session_create()`, `ure_session_render()`, `ure_session_get_frame()` 等 | ✅ `ure_session_t` handle、create/config/destroy、load_scene_file、start/render_pass/pause/resume/cancel/reset/progress/framebuffer 已接入 C ABI；`ure_set_min_log_level()` 暴露 runtime log gate；新增高层 mutation helper：camera、instance transform、material 参数、material texture；空指针/未加载场景/非法 texture 参数错误码路径由 `test_session` 覆盖 |
| S.4 | Python binding：发布 `pyure` 包 | ✅ `pyure/` 通过 ctypes 直接绑定 C ABI 和 `pyure_native.dll`，不包装 CLI；提供 Session lifecycle、load_scene_file、start/render_pass/pause/resume/cancel/reset、progress、framebuffer_size、framebuffer/AOV copy、runtime log level，以及 `update_camera()` / `update_instance_transform()` / `update_material()` / `update_material_texture()` 高层 mutation helper；该路线保留稳定 ABI 边界，比 pybind11 直接暴露 C++ 对象更适合当前 Session API；`test_pyure_smoke` 覆盖 channel count、no-scene error、真实 8x8 scene background progressive render、pause/resume/cancel、framebuffer/AOV copy、camera/material/texture mutation 后 reset+继续渲染 |
| S.5 | 渐进式渲染：`render_pass()` 循环 + 交互相机回调 | ✅ `RenderSession` 已支持后台 progressive worker：`start_render(true)` 启动 worker 连续 render_pass，pause/resume/cancel 控制状态并停止/恢复 SPP 增长；所有 engine 访问由 Session mutex 串行化，scene mutation/camera/framebuffer/AOV copy 会先停 worker 或在锁下执行；`start_render(false)` 保留同步单 pass 行为 |
| S.6 | AOV 系统：法线/深度/反照率/UV/运动矢量 独立输出 | ✅ 已完成当前引擎边界：typed `AovType::{Beauty,Normal,Albedo,Depth,Uv,MotionVector}`，C++ Session、C ABI、Python channel count 可访问；UV 由 first-hit `HitQueue::uv` 写入真实 GPU AOV buffer；MotionVector 输出 current-minus-previous screen-space delta，静态物体使用 current/previous `GpuCamera`，instance object motion 使用 previous/current instance transform 将同一 surface point 经 local space 重建后再投影；非 instance 的拓扑变形/材质动画仍需后续动态队列，不属于当前 SceneDiff 能力 |

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

2026-06-13 S batch 进展：新增 `RenderSession` 作为 Phase S 的会话边界，生产路径通过 `RenderEngineFactory::create_gpu_renderer(config)` 创建，测试路径允许注入 `IRenderEngine`，因此 Session 状态机可在 host test 中验证而不启动 CUDA。新增 `SceneDiff` 高层 contract，支持 full SceneIR replacement、camera mutation、reset-only mutation、instance transform mutation、SceneIR material-table mutation、SceneIR instance add/remove 和 SceneIR sphere add/remove；Phase M 起旧 Scene mutation API、retained `Scene` state、`RenderSession::load_scene(Scene)`、`IRenderEngine` Scene overload、`SceneIR -> Scene` compiler、procedural SceneBuilder CLI fallback 与旧文本场景 parser 均已移除。Transform diff 使用源场景索引，验证目标必须是可渲染 mesh instance，然后调用 `GpuSceneCompiler::build_instance_transform()` 编译完整 transform/AABB buffer 并走 `IRenderEngine::update_transforms()`，没有把 `GpuInstanceTransform` 暴露到 public Session API。非 texture material diff 修改 retained SceneIR material table 后重新编译 scene-owned material append table，并通过 `IRenderEngine::update_materials()` / `update_materials_gpu()` 更新 GPU material header 与 6 组 SoA 光谱数组。Topology diff 和 texture/resource material diff 不伪装成 hot-update：变更先更新 retained SceneIR，再显式 `reload_scene_ir()` full GPU reload；`replace_scene()` 也强制 full reload，避免旧 `load_scene_ir()` initialized 分支只更新 transform 的错误语义。SceneIR texture rebinding 复用已有 image/texture resource cache。Session progressive scheduler 已接入后台 worker：`start_render(true)` 连续 render pass，pause 停止 SPP 增长，resume 继续，cancel/destructor/mutation 会停止 worker；所有 engine 调用通过 Session mutex 串行化，避免 Python 主线程和 worker 并发访问同一 GPU context。`ure_session_t` C ABI 覆盖 create/config/destroy、load_scene_file、start/render_pass、pause/resume/cancel/reset、progress、framebuffer_size、framebuffer pointer、typed AOV pointer、runtime log level，以及 camera/instance/material/texture mutation helper；Python `pyure` 通过 ctypes 直接调用 `pyure_native.dll`，提供 background progressive workflow、framebuffer/AOV copy、runtime log level 和高层 mutation API，不包装 CLI。AOV 已通过 GPU kernel 写出 first-hit normal/albedo/depth/uv/motion-vector，Beauty 走 framebuffer。MotionVector 使用 first-hit world position 输出 current-minus-previous screen-space delta；静态物体由 current/previous `GpuCamera` 投影得到 camera motion，instance object motion 由 `GpuContext` 保存 previous/current instance transform，kernel 将当前 hit point经 current inverse transform 回到 local，再经 previous transform 重建 previous world point 后投影。针对性门禁：`test_session`、`gpu_test_render`、`gpu_test_instance`、`test_pyure_smoke` 已覆盖后台 scheduler、texture/resource full reload、真实 GPU reload smoke、C ABI mutation failure gates 与 Python workflow；完整 Release build + `ctest -C Release` 17/17 通过，warning/error scan 为空。Phase S 当前边界已完成；非 instance 的拓扑变形仍需专用 geometry/update queue，不能用当前 world hit point 或零值假装完成。

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
| M.1 | 设计节点图 IR（不依赖 OSL，自定义格式） | ✅ `scene_ir::MaterialGraph` / `MaterialGraphNode` / `MaterialGraphInput` 已进入公共 SceneIR；首批节点覆盖 ConstantColor/ConstantFloat/BSDF/OutputSurface，Texture/Add/Mix 等复杂节点保留为显式 unsupported |
| M.2 | GPU 编译：节点图 → GpuMaterial + 内核参数 | 进行中：`GpuSceneCompiler` 已支持单 OutputSurface → 单 BSDF 编译到现有 `GpuMaterialData` + spectral SoA，并支持 ConstantColor/ConstantFloat、Texture2D、单纹理 Multiply；graph 存在时 graph 输出为 authoritative，scalar material texture fields 不参与 GPU 编译；glTF PBR baseColor/roughness/emissive 已自动生成等价 MaterialGraph；旧文本场景、`scene_io::load_scene()`、`SceneParser::parse_file()`、`SceneIR -> Scene` compiler、procedural CLI fallback、`IRenderEngine` Scene overload 与旧视觉 smoke 资产均已移除，C/Python file loading 进入 SceneIR；raw in-memory texture mutation 直接失败，后续资源变更必须走 graph/resource 节点；Mix/BSDF layering/procedural material nodes 仍为 fail-loud |
| M.3 | MaterialX 导入（`mtlx` → 节点图 IR） | M.1 |
| M.4 | MaterialX 导出（节点图 IR → `mtlx`） | M.1 |
| M.5 | 材质预设库（金属/玻璃/皮肤/织物/汽车漆） | M.2 |

**前置条件**: Batch 4 BSDF 测试 (OT2, OT3) 必须在此步骤前通过，确保现有 BSDF 行为基线锁定。

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
| K.6 | Spectral MIS（波长采样的多重重要性采样） | Phase E 后的高级采样优化 |

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
