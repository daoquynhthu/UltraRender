# UltraRender 升级路线图 (PLAN.md)

最后更新: 2026-07-28 (V.3 closure and V.4 cursor)

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
远期 Phase M:   材质系统                                             已完成
远期 Phase L:   百万级光谱域 / packet-resolution 解耦                已完成 (L.0-L.12)
远期 Phase Q:   URE 原生场景系统 / 程序化工业格式                    已完成
远期 Phase R:   工业级/科研级积分器升级                              已完成
远期 Phase T:   可移植 GPU 运行时 / 多后端执行                        已完成
远期 Phase V:   GPU 几何加速结构 / BVH / OptiX / Clustered Geometry   进行中
远期 Phase W:   波动光学求解器 / 相干场输运                          进行中
```

---

## 权威执行顺序（唯一施工队列）

本节是所有阶段状态、依赖和施工顺序的唯一执行口径。后文各 Phase 章节描述范围与内部步骤，不得据此绕过本节并行启动新的施工阶段。除明确列出的长期门禁外，同一时间只允许一个主阶段处于施工状态；已经提前完成的其他阶段成果保留，但其后续工作冻结到队列游标到达。

```
R-P2 closure [done]
   │
   ▼
Phase M complete [done]
   │
   ▼
R-P6 Mie / volume resources [done]
   │
   ▼
Phase Q complete [done]
   │
   ▼
R-P4 specular manifold + BDPT/VCM [done]
   │
   ▼
Phase T complete [done]
   │
   ▼
当前游标: V.4
   │
   ▼
Phase V complete
   │
   ▼
Phase W complete
   │
   ▼
Phase U complete
   │
   ▼
Phase X complete
```

### 执行约束

- **R-P2 已闭环**: multi-GPU guide delta merge/broadcast、device-derived 或显式 memory budget，以及 Cornell/multi-light/complex-material/volume 的 variance、MSE、time-to-error 曲线均已进入生产与验证路径。
- **Phase M 已闭环**: MaterialGraph、BSDF layering、procedural nodes、MaterialX import/export 和 presets 已稳定材质语义；skin 采用 participating dielectric medium preset，不做 Lambert fallback。
- **R-P6 已闭环**: deterministic Lorenz-Mie generator、严格 table adapter、不可变 SceneIR resource、GPU spectral eval/pdf/sample、NEE/continuation、scalar-depolarizing Stokes、Session rebuild 和端到端生命周期均已进入生产与验证路径。
- **Phase Q 已闭环**: URE native schema、serialization、programmatic graph、feature declaration、tooling/adapter、compiled cache/farm 与 validation suite 已冻结为后续阶段的权威 authoring contract。
- **R-P4 已闭环**: GPU specular-manifold、BDPT/VCM、独立 anchored-delta technique AOV、精确 estimator support partition，以及 glass/SDS/small-emitter/mixed-specular bias/variance/time-to-error suite 已进入生产与验证路径。
- **Phase T 已闭环**: T.0-T.11 已冻结 coupling、backend identity、共享 Slang 工具链、SDK-free runtime/resource/execution/acceleration/scheduling contract、CUDA production lowering、Vulkan compute/acceleration foundation、Windows optional D3D12/DXR backend、heterogeneous sample-shard negotiation 和 cross-backend validation/performance suite。
- **当前唯一施工项 — Phase V**: V.0-V.3 已闭环；当前游标为 V.4 SAH/SBVH 与 compact BVH4/BVH8 quality preset。Phase W 现有 reference/oracle 成果保留，新增 W production work 继续冻结。
- **Phase U/X**: 只在核心 scene/runtime/acceleration/wave contracts 稳定后暴露外部生态和插件 ABI。
- **Phase K**: 不作为并行主阶段；只在对应主阶段完成时运行该阶段指定的性能测量、Nsight 和长期回归门禁。
- 文档中的“进行中”表示已有未闭环成果，不等于允许并行施工。发生冲突时，以本节当前游标为准。

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
- **Phase Q** (URE 原生场景系统 / 程序化工业格式) → 依赖 Phase S 的 retained SceneIR/session 边界、Phase M/L 的 material/resource graph 基础，并为 Phase R/V/W/U/X 提供权威 authoring contract。Phase Q 的职责是定义 `.ure` / `.urescene` / `.urepkg` 原生格式、schema/versioning、程序化描述、能力声明、validation/compiler/cache 和外部格式 adapter 边界。glTF/USD/MaterialX 只能作为可选导入/导出适配层，不能定义 UltraRender 的核心能力边界。
- **Phase R** (工业级/科研级积分器升级) → 依赖 Phase E 的 spectral PDF/transport closure、Phase L 的 domain/packet 解耦、Phase S 的 session/progressive API，并为 Phase W 的 wave solver 提供不被 radiometric 调度瓶颈拖累的 baseline。Phase R 负责 radiometric integrator 的调度、采样、MIS、light transport algorithm 和 benchmark contract；Phase W 负责相干/衍射/局部全波求解，两者不能混淆。
- **Phase T** (可移植 GPU 运行时 / 多后端执行) → 依赖 Phase S 的稳定 session 边界、Phase L 的 resource contract 和 Phase R 已稳定的 estimator/validation contract。CUDA 是当前唯一经过验证的生产工作后端，但不得继续定义公共类型、SceneIR、MaterialIR、IntegratorIR、WaveIR、资源语义或调度合同。Phase T 先迁移现有 CUDA backend 保持零物理回归，再建立 Vulkan compute/RT 跨厂商后端；D3D12/DXR 是 Windows 可选后端。Phase T 不引入 CPU production integrator，也不以最低能力后端限制高级功能。
- **Phase V** (GPU 几何加速结构 / BVH / OptiX / Clustered Geometry) → 依赖 Phase P/S 的 retained scene/session 边界、Phase M/L 的 resource/material graph、Phase R 的 validation suite 和 Phase T 的 backend/acceleration-provider contract。Phase V 处理 GPU traversal/build/refit/compaction/TLAS/BLAS/clustered geometry，以及 CUDA BVH、OptiX、Vulkan RT、DXR provider；它不改变 radiometric estimator，也不替代 Phase W 的 wave solver。UltraRender 不引入独立 host traversal backend，host 侧只负责构建、调度、资源上传和验证。
- **Phase W** (波动光学求解器) → 依赖 Phase E 的 spectral/polarization 基线与 Phase L 的 high-resolution spectral domain/resource contract。W.1/W.2/W.3 可在 Phase M 完成前推进；W.4 diffractive material operators 依赖 Phase M 的 MaterialGraph/MaterialX 语义稳定。现有 CUDA reference backend 可继续用于物理闭环，但新增 GPU operator 必须消费 Phase T 的 portable runtime contract。Phase W 不允许把相干/衍射能力隐藏在现有 radiometric path tracer 中，必须通过显式 feature switch opt-in，并在 unsupported film/merge/API/material path 上 fail-loud。

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
    src/image_loader.cpp
    src/image_saver.cpp
    src/spd_loader.cpp
    src/scene_ir.cpp
    src/camera.cpp
    src/mesh.cpp
    src/sphere.cpp
    src/triangle.cpp
    src/obj_loader.cpp
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
- 纹理不是 display RGB，而是光谱数据传递：`HostTexture` → `GpuTexture` RGB texture object 或 explicit spectral source-sample resource → kernel `sample_texture()` → spectral reconstruction / wavelength interpolation → Stokes×Mueller→volume→accum
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
      channels == 3: RGB → cudaArray<float4> texObj + per-wavelength reconstruction
      channels != 3: explicit spectral source-sample resource, not packet/domain-expanded texture
  → kernel: sample_texture(wavelengths, num_spec)
      RGB texObj path uses hardware filtering + runtime-N reconstruction
      spectral data path uses UV bilinear + source-sample wavelength interpolation
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
| 3.6 | `host_api.cu:433` 纹理上传改为 N 通道 | `host_api.cu`, `host_texture.hpp`, `image_loader.cpp`, `gpu_structs.hpp`, `wavefront.cuh`, `tests/gpu/test_spectral_pipeline_soa.cu` | ✅ E.3 已完成；L.8 已将过渡期 N 通道 packet buffer 替换为 source-sample texture resource，RGB 保留 `texObj`，explicit spectral texture 按 source sample count + wavelength interpolation 采样；`gpu_test_spectral_soa` 725 checks 通过 |
| 3.7 | CPU 端 `resample_uniform` 默认参数改为 `RenderConfig::num_wavelengths` | `spd_loader.hpp`, `scene_io.hpp`, `scene_io_api.cpp`, `tests/host/test_asset_pipeline.cpp` | ✅ 已完成；`resample_uniform` 不再有隐藏 4 通道默认值，`load_spd(path, int)` / `load_spd(path, RenderConfig)` 接受 runtime N；新增 `test_scene_io_load_spd_runtime_n`，`test_asset_pipeline` 7 tests / 48 checks 通过 |

### Step E.4 — SPD 输入

**目标**: 接入完整 SPD→SpectralResource/SpectralPacket cache 管线。

**子步骤**:

| # | 内容 | 文件 |
|---|------|------|
| 4.1 | glTF `URE_spectral_material` 的 `albedoSPD`/`emissionSPD` 解析为相对 glTF 文件目录的规范路径 | `gltf_scene_frontend.cpp` | ✅ 已完成 |
| 4.2 | `GpuSceneCompiler::compile(scene_ir, RenderConfig)` 按 runtime N 的光谱 bin 中心重采样 SPD，填充 `GpuMaterialData::{albedo,emission}` packet cache；Phase L.5 后同步保留原始 sampled table resource | `gpu_scene_compiler.hpp/.cpp` | ✅ 已完成 |
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
远期 Phase M: ████████████ 已完成 (MaterialGraph / GPU expression / BSDF mix-layer / MaterialX adapter / presets)

旧目录清理: ████████████ 已完成 (include/ + src/ + tests/{unit,integration} 删除; CMakeLists.txt 遗留构建块移除)
```

Phase E/L closure record and current follow-up boundaries（2026-06-15 校准）:
- Release build warning 债已清理：`C4100` 使用参数修正；`C4324` 仅在有意对齐的 GPU ABI 声明/实现范围内局部屏蔽；GPU test `LNK4098` 通过 `CUDA_RUNTIME_LIBRARY None` + `CUDA::cudart` shared runtime 链接消除；CLI `C4819` 通过 `/utf-8` 消除。当前 `build_modular_x64` Release 构建门禁无编译警告。
- Phase E.3 的 `GpuSpectrum` 32 通道过渡层已在 Phase L 清理：旧 `GpuSpectrum` 类型删除并更名/收敛为 packet-width `SpectralPacket`，资源域通过 `domain_bins` / sampled wavelength / resource descriptor 表达，不再把 packet 宽度误当全局光谱分辨率。Raygen 当前固定使用 bin center wavelength，保证和 host 编译的 material/medium/texture SoA 语义一致；未来若恢复 bin jitter，必须先把材质/介质/SPD 改成按 ray wavelength 动态采样或可插值表示。
- Phase E.3 显式光谱纹理的过渡期 N-channel carrier 已在 Phase L.8 替换为 `GpuTexture::spectral_source_values` source-sample resource；后续性能重点不再是 layered texture object，而是 basis/tile cache、miss 诊断和资源预算策略。
- Phase E.4 发现并修复配置链断点：`RenderEngineFactory::create_gpu_renderer()` 过去无法接收 `RenderConfig`，CLI `cfg.spectral.bands` 不会进入 renderer。已新增 config 重载并在 CLI 中传递 `spectral.bands` / queue capacity / max depth。后续配置审计应继续检查 `scene_ir.width/height` 与 CLI override 的合流点。
- Phase E.5 并行审查确认的数学债已在 C1-C6 和后续 blocker pass 内收束：packet 色散不再由 hero wavelength 单独决定整包方向；旧 dielectric transmission clamp 已删除；radiance transport 方向锁定为 `(eta_i / eta_t)^2`；Stokes convention 固定为 `Q = I_s - I_p`；boundary Mueller 从 complex amplitude 派生；lane contribution 和 ShadowQueue direct lighting 已使用 explicit wavelength PDF estimator。剩余不是 Phase E 未完成项，而是 K/M/W 边界：specular manifold NEE、更多 furnace/reference scenes、高阶 wavelength importance sampling、volume proposal 方差优化。
- 2026-06-13 物理第一性审查重新打开的 correctness blockers 已收束：lane active-channel、dielectric tint 语义、sphere-light MIS PDF、rough metal exact Smith GGX 一致性、rough dielectric GGX microfacet reflection/transmission、rough thin-film/dispersive dielectric BSDF/PDF/MIS 已进入同一 microfacet 路径。完整 furnace/reference scenes 与 specular manifold 仍是后续验证/功能入口。历史真实 CLI 视觉 smoke 已随旧文本场景 cutoff 删除；后续视觉 smoke 必须使用 glTF/GLB。
- 2026-06-13 用户级可靠性收敛：Release 默认配置已实跑通过；runtime spectral channel contract 显式收紧为 `[8, 32]`，核心默认 N=8，CLI 默认 N=32，4 通道会在 CLI/compiler/GPU init 层拒绝；CLI 输出新增 Radiance HDR (`--format hdr` / `.hdr`)；SceneFrontend 只分派 `.gltf/.glb`，未知扩展均 fail-loud，direct GltfSceneFrontend 也拒绝非 glTF 输入。
- Host interactive API 的旧自定义 `spheres/materials` 路径已随 legacy `Scene`/procedural frontend 清理退出 engine 主路径；当前 C/Python file loading 进入 SceneIR/glTF。后续新增 in-memory authoring API 时需要重新定义 material index scope，不应复用旧 offset 语义。
- Phase E.5.1 已将 `RayQueue` 从 scalar `StokesVector*` 迁移到 channel-major `stokes_i/q/u/v` SoA，并新增 `spectral_modes` / `active_channels` / `wavelength_pdfs`。packet scatter 的输入 Stokes 现在使用 channel-average，输出通过 `store_packet_scattered_stokes()` 按通道写回 metal/dielectric boundary Mueller；真正的 dispersive lane child ray 生成在 E.5.3。
- Phase E.5.2 已新增 `path_tracer_boundary.cuh`，集中 dielectric/conductor/thin-film boundary 计算；`DielectricSurfaceBoundary` 统一裸 dielectric 与 dielectric thin-film 的复振幅、power R/T、eta Jacobian 和 radiance/importance transport scale，`scatter()`、spectral lane split 和 transparent shadow visibility 均消费同一 surface result。旧 `conductor_fresnel_reflectance()`、`get_dielectric_thin_film_reflectance()` 和 `get_thin_film_interference()` 已删除；`apply_mueller_reflection_conductor()` 和 dielectric thin-film reflection 现在直接从 boundary 复振幅派生 Mueller 项；metal thin-film scatter 现走 `eval_thin_film_conductor_boundary()`。无 measured n/k 的 albedo-F0 fallback 是明确 preview/fallback 语义，不再作为 Phase E 技术债表述。
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
| 场景描述 | glTF/GLB only | glTF 2.0 + URE 扩展 | URE native scene package；USD/Hydra 只是外部生态 adapter |
| API 可集成性 | CLI only | C++ 公共 API | Python API + 脚本 |
| 材质系统 | 硬编码 BSDF | glTF PBR + URE 光谱 | URE native MaterialGraph；MaterialX 只是 adapter |
| 交互性 | 离线帧 → BMP | 渐进式渲染 | 视口交互 + 热重载 |
| 分发 | 单机 CUDA | 单机 + 多卡 | 分布式 (网络/云) |

### 新增远期 Phase

```
Phase E ──→ Phase S ──→ Phase M ──→ Phase L ──→ Phase Q ──┬──→ Phase R ──→ Phase T ──→ Phase V ──→ Phase W ──→ Phase U ──→ Phase K (持续)
                                                           └──→ Phase W foundation oracles
               │
               ├──→ Phase X (可并行)
               │
               └──→ Phase C/D (分布式, 可并行)
```

与中短期的关系：Phase S 和 Phase X 的部分工作可在 Phase E 完成后立即开始；Phase M 依赖 Phase G 的材质扩展 + Phase E 的光谱引擎；Phase L 是 README “百万级波长通道”承诺的真正架构阶段，必须在 MaterialX/USD/插件把材质语义固化到 `GpuMaterialData + GpuSpectrum[32]` 之前完成；Phase Q 是 UltraRender 自己的原生 authoring/package 格式阶段，必须在 Phase R/T/V/W/U/X 把高级求解器、执行后端、加速结构、波动光学、物理/声学和插件语义扩散到外部生态前确定权威 schema、程序化描述、能力声明、版本迁移和 fail-loud validation contract；Phase R 是 radiometric spectral/polarimetric integrator 的工业级/科研级升级阶段，负责调度、采样、MIS、light/path guiding 和高级路径空间算法，当前可继续以 CUDA 作为工作后端；Phase T 随后把已验证算法迁入 backend-neutral runtime contract，并以 CUDA parity 锁定行为，再增加 Vulkan 和 D3D12/DXR；Phase V 必须消费 Phase T 的 acceleration-provider contract，在 Phase W/U/K 继续放大场景复杂度前解决 mesh-local BVH、TLAS/BLAS、dynamic/refit、OptiX/Vulkan RT/DXR 和 clustered geometry；Phase W 必须消费 Phase L/R/T/V/Q 的 spectral domain/resource, integrator, execution backend, scene authoring, and acceleration contracts，并在 coherent film/distributed merge/API 语义稳定后再让 Phase U/USD 暴露相干与衍射能力；Phase U 依赖 Phase S/Q 的稳定 API 和原生场景 schema，并应消费 Phase L/R/T/V/W/Q 的 resource, integrator, backend, acceleration, wave-optics, and scene-package contracts。

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
| S.5 | 渐进式渲染：`render_pass()` 循环 + 交互相机回调 | ✅ `RenderSession` 已支持后台 progressive worker：`start_render(true)` 启动 worker 连续 render_pass，pause/resume/cancel 控制状态并停止/恢复 SPP 增长；状态访问与 engine 访问已拆为 state mutex / engine mutex，worker 不再在状态锁下执行 GPU pass；scene mutation/camera 会先停 worker，framebuffer/AOV 读取由 engine mutex 串行化；`start_render(false)` 保留同步单 pass 行为 |
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

2026-06-13 S batch 进展：新增 `RenderSession` 作为 Phase S 的会话边界，生产路径通过 `RenderEngineFactory::create_gpu_renderer(config)` 创建，测试路径允许注入 `IRenderEngine`，因此 Session 状态机可在 host test 中验证而不启动 CUDA。新增 `SceneDiff` 高层 contract，支持 full SceneIR replacement、camera mutation、reset-only mutation、instance transform mutation、SceneIR material-table mutation、SceneIR instance add/remove 和 SceneIR sphere add/remove；Phase M 起旧 Scene mutation API、retained `Scene` state、`RenderSession::load_scene(Scene)`、`IRenderEngine` Scene overload、`SceneIR -> Scene` compiler、procedural SceneBuilder CLI fallback 与旧文本场景 parser 均已移除。Transform diff 使用源场景索引，验证目标必须是可渲染 mesh instance，然后调用 `GpuSceneCompiler::build_instance_transform()` 编译完整 transform/AABB buffer并走 `IRenderEngine::update_transforms()`，没有把 `GpuInstanceTransform` 暴露到 public Session API。非 texture material diff 修改 retained SceneIR material table 后重新编译 scene-owned material append table，并通过 `IRenderEngine::update_materials()` / `update_materials_gpu()` 更新 GPU material header 与 6 组 SoA 光谱数组。Topology diff 和 texture/resource material diff 不伪装成 hot-update：变更先更新 retained SceneIR，再显式 `reload_scene_ir()` full GPU reload；`replace_scene()` 也强制 full reload，避免旧 `load_scene_ir()` initialized 分支只更新 transform 的错误语义。SceneIR texture rebinding 复用已有 image/texture resource cache。Session progressive scheduler 已接入后台 worker：`start_render(true)` 连续 render pass，pause 停止 SPP 增长，resume 继续，cancel/destructor/mutation 会停止 worker；当前实现使用 state mutex 管状态/stop/异常，engine mutex 管 GPU context 访问，worker 不在状态锁下执行 `render_pass()`，避免长 GPU pass 阻塞 `pause()`/`get_progress()`。`ure_session_t` C ABI 覆盖 create/config/destroy、load_scene_file、start/render_pass、pause/resume/cancel/reset、progress、framebuffer_size、framebuffer pointer、typed AOV pointer、runtime log level，以及 camera/instance/material/texture mutation helper；Python `pyure` 通过 ctypes 直接调用 `pyure_native.dll`，提供 background progressive workflow、framebuffer/AOV copy、runtime log level 和高层 mutation API，不包装 CLI。AOV 已通过 GPU kernel 写出 first-hit normal/albedo/depth/uv/motion-vector，Beauty 走 framebuffer。MotionVector 使用 first-hit world position 输出 current-minus-previous screen-space delta；静态物体由 current/previous `GpuCamera` 投影得到 camera motion，instance object motion 由 `GpuContext` 保存 previous/current instance transform，kernel 将当前 hit point经 current inverse transform 回到 local，再经 previous transform 重建 previous world point 后投影。针对性门禁：`test_session`、`gpu_test_render`、`gpu_test_instance`、`test_pyure_smoke` 已覆盖后台 scheduler、texture/resource full reload、真实 GPU reload smoke、C ABI mutation failure gates 与 Python workflow；完整 Release build + `ctest -C Release` 17/17 通过，warning/error scan 为空。Phase S 当前边界已完成；非 instance 的拓扑变形仍需专用 geometry/update queue，不能用当前 world hit point 或零值假装完成。

---

### Phase L — 百万级光谱域 / Large Spectral Domain

**目标**: 兑现 README 中“支持百万级波长通道的并行计算”的长期承诺，同时保持低端硬件可运行、高端单机/多卡/农场可扩展。Phase L 不表示一条 ray 携带 1,000,000 个 lane；它表示 renderer 的**光谱资源域**可达到百万级离散采样，而 GPU path state 通过小 packet / sampled wavelength / MIS 在该高维域上积分。

**核心结论**: 当前 Phase E 的 `[8, 32]` 是 runtime packet renderer，不是百万级光谱域。`RenderConfig::num_wavelengths` 同时承担了“全局光谱分辨率”和“ray packet lane 数”两个语义，这是最大架构债。Phase L 必须把两者拆开：

```cpp
struct SpectralConfig {
    uint64_t domain_bins;      // target spectral-domain resolution, can be 1,000,000+
    int packet_lanes;          // lanes carried by one GPU ray, typically 1/4/8/16/32
    int max_resident_bins;     // bins allowed resident/cacheable on this device
    SpectralSamplingMode mode; // Uniform, Stratified, Importance, Hero, FarmShard
};
```

校准说明（2026-06-15）：下面 L.1-L.12 的逐日记录保留历史上下文，其中较早条目的“未完成边界”已由后续 L 子步骤或 `docs/Phase_L_Completion_Audit.md` 收束为 non-blocker / 后续性能阶段范围，不表示 Phase L 当前未完成。

#### 当前硬阻塞与不兼容点

| 类别 | 当前状态 | 为什么阻塞百万级 |
|------|----------|------------------|
| `SpectralPacket` | packet-width `values[kMaxPacketLanes]` + `wavelengths[kMaxPacketLanes]`，当前上限 32 | 每个 device 局部对象随 packet lanes 线性膨胀；百万级不可能放进寄存器/局部内存 |
| 临时数组 | `path_tracer_material.cu` / `path_tracer_wavefront.cuh` 中 `float tmp[kMaxPacketLanes]` | 依赖固定 packet cap，不能表达动态百万域 |
| `RenderConfig::num_wavelengths` | 同时被 CLI、compiler、GPU init 当作 material SoA 宽度和 ray packet 宽度 | 低端硬件上限与目标光谱分辨率混淆；超过 32 直接拒绝 |
| material SoA | `num_spectral_channels × material_count` 全量驻留 | 对 1M bins × 6 spectra × material_count 显存不可控 |
| `GpuMaterialData` | host bridge 仍含 6 个 `SpectralPacket` cache，并在 L.5 后新增 sampled/analytic resource fields | C ABI、Session mutation、material graph compiler 仍可能把材质固化到 packet cache，必须继续迁移到 resource graph |
| spectral texture | L.8 后 explicit texture 是 source-sample resource descriptor，RGB texture 仍走 CUDA texture object | 已切断 texel × packet/domain 展开；后续仍需 basis/tile cache、miss 诊断和资源 graph |
| SPD pipeline | L.5 后材质 SPD 保留 sampled table resource，L.8 后 texture 也具备 source-sample resource；global medium 仍未 resource 化 | 高分辨率资源还缺统一 resource graph、cache、oracle 和分片契约 |
| CIE / photometry | GPU table 是固定可见域积分 helper，现有测试只覆盖 N=32 | 百万域下需要 domain-aware integration / sampled estimator / reference oracle |
| Session / Python / C ABI | `num_wavelengths` 单参数 | 外部 API 无法表达 domain resolution、packet lanes、硬件预算和采样策略 |
| Multi-GPU / Distributed | L.10 后 distributed contract/file backend 已携带 spectral-domain shard、wavelength PDF integral 与 frame shard metadata | 农场顶级性能仍需要高层调度/资源缓存把 sample shard × wavelength shard × frame shard 映射到实际 worker |
| Phase M | MaterialGraph 目前编译到 `GpuMaterialData` | 若继续扩展 MaterialX，会把节点图锁死到 32-lane GPU carrier |
| Phase U / X | USD/Hydra/插件计划使用现有 material/session 边界 | 若先实现，会把 `bands=64` 之类弱语义扩散到外部生态，后续破坏性更大 |

#### 最终架构

```
SceneIR / MaterialGraph / MaterialX
        │
        ▼
SpectralResourceGraph
        ├── analytic node: eval(lambda)
        ├── dense table: N bins, CPU/GPU sampled
        ├── compressed basis: coeffs × basis(lambda)
        ├── tiled spectral texture: texel tile × spectral tile
        └── fluorescence matrix: excitation lambda → emission distribution
        │
        ▼
SpectralDomain + SpectralSampler
        ├── low-end: sampled/hero wavelength, small resident cache
        ├── desktop: packet lanes 8-32, dense/basis hybrid
        ├── high-end: larger cache, multi-stream spectral tiles
        └── farm: sample shard × wavelength shard × frame shard
        │
        ▼
GPU Path State
        ├── SpectralPacket(packet_lanes)
        ├── active lambda/bin/pdf
        ├── Stokes per packet lane or sampled lane
        └── no full-domain array in a ray
```

#### 硬件适配与拒绝策略

Phase L 的配置必须在 scene load 前解析成 `SpectralRuntimePlan`，并在低端硬件上 fail-loud：

| 设备/模式 | 允许策略 | 拒绝条件 |
|-----------|----------|----------|
| 低端 GPU / 小 VRAM | `domain_bins` 可大，但 `packet_lanes` 小；spectral resources 必须走 analytic/basis/tiled streaming；resident cache 按 VRAM 预算限制 | 用户要求 dense resident 1M bins 且预算超过 VRAM；packet lanes 超过寄存器/occupancy 预算 |
| 当前 RTX 5060 级别 | packet lanes 8-32；1M domain 通过 sampled wavelength + resource cache；不承诺每 pass 全域遍历 | 用户把 `packet_lanes` 当成 1M；texture/material 要求全量展开到 `GpuSpectrum[]` |
| 高端单机 | 更大 resource cache、多 CUDA stream、多 GPU spectral tiles；仍保持 ray packet 小宽度 | scene 的 spectral working set 超过设备+spill 策略能力 |
| 多卡/农场 | sample-range partition + spectral-domain partition；每 worker 只持有 domain shard/resource shard；merge 时按 wavelength PDF/XYZ estimator 合并 | worker sampler seed/domain mapping 与 file metadata 不一致；资源缓存/调度层不能满足 shard working set |

硬上限计算必须基于：
- VRAM: framebuffer + AOV + queues + BVH + material/resource cache + staging buffer
- SM/register pressure: `packet_lanes` 与 kernel occupancy 的函数，不由 VRAM 单独决定
- scene resource working set: material count、texture texel count、spectral tiles、fluorescence matrix density
- requested quality: spp、domain_bins、sampler mode、farm shard count

#### 子步骤

| Step | 内容 | 文件/模块 | 完成判据 |
|------|------|-----------|----------|
| L.0 | README/PLAN 口径闭合：明确“百万级通道”= spectral domain/resource resolution，不是单 ray million lanes | `README.md`, `PLAN.md`, `docs/Phase_E_Spectral_Architecture.md` | 文档不再暗示 `GpuSpectrum` 可承载百万 lane；当前 `[8,32]` 被标为 Phase E packet cap |
| L.1 | 配置拆分：`num_wavelengths` 退役为 legacy alias；新增 `spectral.domain_bins`, `spectral.packet_lanes`, `spectral.max_resident_mb`, `spectral.sampling_mode` | `render_config.hpp`, `ure_config`, CLI, C ABI, pyure | 低端硬件超预算会明确错误；旧配置迁移有 warning 或兼容 alias |
| L.2 | 硬件预算器：`SpectralRuntimePlan` 根据 GPU/VRAM/SM/queue/scene resource 计算 packet lanes、cache budget、domain shard | `gpu_auto_config.hpp`, `gpu_hardware`, `ure_config` | 覆盖 4GB/8GB/24GB/80GB/farm profiles；超过上限 fail-loud |
| L.3 | 类型拆分：`GpuSpectrum` 拆成 `SpectralPacket` 与 `SpectralSample`; 禁止 scene/resource API 暴露 fixed 32 array | `gpu_structs.hpp`, `gpu_spectrum_utils.cuh`, tests | code search 对 resource path 中 `GpuSpectrum` 清零；packet cap 改名为 `kMaxPacketLanes` |
| L.4 | Ray/Shadow queue 语义迁移：队列只保存 packet lanes、lambda/bin/pdf；支持 sampled/hero/lane/packet 模式统一 | `RayQueue`, `ShadowQueue`, raygen, wavefront | N=1 sampled、N=8 packet、dispersive lane split 都走同一 estimator |
| L.5 | SpectralResource 抽象：material/medium/light/texture 不再 eager 展开到 material SoA；改为 `eval(lambda)` / sampled table / basis / tiled resource | `SceneIR`, `GpuSceneCompiler`, `path_tracer_*` | SPD、n/k、medium sigma、emission 均可按任意 lambda 查询 |
| L.6 | Million-bin host oracle：实现 CPU 端 reference integration，可生成 1M-bin domain 的 D65/equal-energy/narrowband/metamer fixtures | `tests/host`, `docs` | 不依赖 GPU 即可验证 high-res spectral resource 与 sampled estimator 偏差 |
| L.7 | GPU sampled estimator：以 sampled wavelength / packet subset 在 1M domain 上积分，XYZ 和 RR 全部显式使用 pdf | `gpu_spectrum_utils.cuh`, wavefront, render tests | 1M domain smoke 不展开 1M lane；D65/equal-energy 与 oracle 误差在阈值内 |
| L.8 | Spectral texture 重构：显式光谱纹理由 packet-width resident buffer 改为 source-sample resource descriptor；RGB texture 仍走硬件 filtering + reconstruction | `HostTexture`, `GpuTexture`, image/asset pipeline | ✅ 已完成 source-sample resource；大纹理不会按 texel×1M/domain 全量驻留；basis/tile cache miss 诊断归 L.11 |
| L.9 | MaterialGraph 对接：节点输出从 `GpuMaterialData` 改为 spectral expression/resource graph；Phase M 的 Add/Mix/Texture/MaterialX 不再被 32-lane carrier 限制 | `scene_ir.hpp`, `gpu_scene_compiler.cpp`, material graph tests | ✅ 已完成首版 MaterialGraph expression graph；texture/Add/Mix 可表达为 resource graph，而不是 fail-loud；BSDF layering/IOR expression slot 仍归后续 Phase M |
| L.10 | Multi-GPU / farm 分片：新增 spectral-domain shard metadata，支持 sample shard、wavelength shard、frame shard 组合 merge | distributed contract, file backend, multi GPU driver | ✅ 已完成 contract/file backend：不同 worker 的 wavelength PDF/domain shard 可复现合并；错误 shard metadata 会拒绝。实际 farm scheduler/cache preset 归 L.11 |
| L.11 | 性能路径：针对低端/高端分别提供 sampler preset、cache preset、CUDA stream preset；建立 benchmark scenes | `gpu_auto_config.hpp`, GPU init, tools/benchmarks, scenes/benchmarks, tests, docs | ✅ 已完成 runtime preset + resident budget gate + glTF benchmark smoke；低端显式超预算会在 GPU init 前拒绝；完整 perf suite 仍可后续扩展 |
| L.12 | 静态审计门禁：禁止新增 `kMaxSpectralChannels`、`GpuSpectrum[]` resource、`num_wavelengths` 作为 domain resolution、eager million-bin SoA | scripts/check_* | ✅ 已完成 Phase L static audit 扩展，本地脚本可阻断架构回退 |

#### 2026-06-14 L.0-L.2 first batch progress

- L.0 文档口径已开始闭合：README 和 Phase E 文档明确“百万级”指 spectral domain/resource bins，不是单条 ray 或 `GpuSpectrum` 承载百万 lane；Phase E 的 `[8, 32]` 被定义为 packet cap。
- L.1 配置拆分已进入公共配置链：`RenderConfig`、JSON/CLI、C ABI 和 pyure 已新增 `domain_bins`、`packet_lanes`、`max_resident_mb`、`sampling_mode`。`num_wavelengths` 暂保留为 legacy alias，当前渲染 kernel 仍消费 packet lanes。
- L.2 已建立第一版 `SpectralRuntimePlan`：硬件预算器按 VRAM/SM/material count 推导 packet lanes、resident spectral bins、sampled-domain/streaming 标志；1M domain smoke 通过小 packet 路径，不展开 million-lane GPU state。

#### 2026-06-14 L.3 progress

- L.3 已完成第一层类型拆分：旧 `GpuSpectrum` 代码类型删除，packet carrier 更名为 `SpectralPacket`，packet cap 更名为 `kMinPacketLanes`/`kMaxPacketLanes`；新增 `SpectralSample` 作为后续 sampled wavelength / domain-bin 状态的单样本载体。
- `GpuTexture` 不再暴露 fixed 32-array packet resource，旧 `SpectralPacket* data` 在 L.3 先改为显式 float carrier；该过渡 carrier 已在 L.8 被 source-sample resource descriptor 取代。
- 新增 `scripts/check_phase_l_static.ps1`，阻断旧 `GpuSpectrum`、旧 spectral channel cap、`GpuTexture` packet-data 字段和 texture upload 里 `sizeof(SpectralPacket)` 的回归。
- 历史边界：当时 `GpuMaterialData` host-side packet bridge、resource graph、million-bin oracle、sampled estimator、texture resource 与 spectral shard contract 尚未完成；后续 L.5-L.10 已收束核心 contract，basis/tile cache 留作 Phase L non-blocker。

#### 2026-06-14 L.4 progress

- RayQueue 已新增 `initial_spectral_mode`，raygen 不再假定所有 primary ray 都是 packet mode；`SpectralRayModeSampled` 与既有 deterministic split lane 通过 `spectral_mode_is_sampled()` 共用 active-channel/pdf estimator。
- `packet_lanes` 合法域改为 `1` 或 `[8, 32]`：`1` 表示 single sampled wavelength packet，不表示恢复 4 通道或把 domain bins 展开进 ray state；`2-7` 继续 fail-loud，避免半宽 packet 形成未经验证的寄存器/估计器状态。
- `RenderConfig::spectral_sampling_mode` 现在驱动初始 ray mode：`packet_uniform` 且 lanes>1 走 packet；importance/stratified/hero 或 lanes=1 走 sampled。direct lighting、medium proposal、Stokes load/store、metal/dielectric continuation 均按 sampled/lane 共用路径处理 active channel 和 wavelength pdf。
- 测试覆盖已扩展到 N=1 sampled session/pyure/raygen smoke，N=8 packet 与 dispersive lane split 保留同一估计器路径；`scripts/check_phase_l_static.ps1` 现在阻断 core 源码里重新直接比较 `SpectralRayModeLane` 的 estimator 分叉。
- 历史边界：L.4 当时只迁移 ray/queue 语义；任意 lambda resource eval、million-bin oracle、sampled estimator、texture descriptor 与 spectral shard contract 已在后续 L.5-L.10 建立，tiled/basis cache 留作 non-blocker。

#### 2026-06-14 L.5 progress

- `SpectralResourceKind` / `SpectralResource` / `HostSpectralResource` 已进入 GPU 边界：`GpuMaterialData` 现在只作为 host-side bridge 保存 resource 描述，真正传入 device 的是轻量 descriptor array；静态审计阻断把 `GpuMaterialData` 当 POD 直接 `cudaMemcpy`。
- `GpuSceneCompiler` 已将 RGB-derived albedo/emission/n/k/材质内介质 sigma 编译为 analytic spectral resources；`URE_spectral_material` 的 albedo/emission SPD 保留原始 sampled table，不再只在 scene load 时 eager 压扁到 packet bins。packet SoA 仍保留为 cache/fallback。
- wavefront shading 现在按 ray 当前 wavelengths resource-first 加载 material spectra、material medium、metal n/k 和 light emission；sampled wavelength 与 lane split 不再被 material packet cache 锁死到固定 bin center。
- material update 的低频资源边界已明确：全量 material update 可替换 sampled resource tables；局部 sampled resource update fail-loud，避免 partial update 后 device descriptor 指向旧 table 或缺 table。
- 测试覆盖新增 sampled table device interpolation、resource-overrides-SoA、glTF SPD raw table preservation；`build_x64.ps1` 支持逗号/分号分隔 target list，Phase L 静态审计补充 L.5 resource contract。
- 历史边界：million-bin host oracle、GPU sampled estimator、spectral texture resource、MaterialGraph resource graph 和 distributed spectral shard 已在 L.6-L.10 收束。全局 homogeneous medium 的更细资源化属于后续材质/体积优化，不阻塞 Phase L。

#### 2026-06-14 L.6 progress

- 新增 host-only `ure/spectral/spectral_oracle.hpp`，以现有 CIE 1931 2-degree 表为基准执行 center-sampled Riemann reference integration；默认 `SpectralDomain` 为 360-830nm / 1,000,000 bins，不依赖 CUDA 或 GPU resident packet。
- L.6 oracle 覆盖 `equal_energy`、标准 D65 5nm table interpolation、narrowband Gaussian、uniform high-res spectral table、uniform sampled estimator，以及通过四个窄带 basis 构造的 metamer pair。metamer pair 在 XYZ 上匹配但光谱曲线不同，用来防止后续测试只比较 display RGB。
- 新增 `test_spectral_oracle` host target，验证 equal-energy Y normalization、D65 chromaticity、narrowband spectral dominance、metamer XYZ equivalence、1M-bin high-res resource 与 4096-sample host estimator 偏差。`scripts/check_phase_l_static.ps1` 已要求 oracle header/test/1M fixture/D65/metamer/sampled estimator 存在。
- 历史边界：L.6 只建立 CPU reference/oracle；L.7 已让 GPU sampled wavelength / packet subset 以 explicit pdf 对齐该 oracle，distributed spectral shard merge 已在 L.10 收束，basis/tile cache 留作 non-blocker。

#### 2026-06-14 L.7 progress

- GPU sampled estimator 语义已拆清：`SpectralRayModeLane` 继续使用离散 lane probability 和 packet bin width，用于 deterministic packet lane split；`SpectralRayModeSampled` 改为连续 wavelength PDF density，XYZ estimator 使用 `value * CIE(lambda) / pdf_density / cie_y_integral`，不再错误乘 packet bin width。
- `generate_rays_kernel` 在 sampled mode 下写入连续 wavelength sample (`lambda_min + u * domain_width`) 和 `1/domain_width` PDF density；packet mode 仍写 packet bin centers，lane split 继续保留既有离散 pdf 语义。
- wavefront 的 shadow、miss/sky 和 emissive-hit resolve 路径已切到 mode-aware `spectral_sample_to_xyz()`，避免 sampled primary ray 与 lane split 共用错误 normalization。
- 新增 GPU oracle 对照：`gpu_test_spectral` 现在用 L.6 CPU oracle 验证 equal-energy、D65 和 high-res D65+narrowband resource 的 4096-sample GPU estimator 偏差；这证明 1M domain resource 不需要展开到 million lane 就可按 sampled wavelength 积分。
- Phase L 静态审计已阻断 sampled raygen 回退到 bin center、sampled estimator 回退到 bin-width normalization、wavefront 继续直接调用旧 `sampled_spectrum_to_xyz()`，并要求 L.7 三个 GPU/oracle tests 存在。
- 历史边界：L.7 解决 wavelength estimator 和 visible-domain XYZ reference 对齐；spectral texture resource、MaterialGraph resource graph、distributed spectral shard metadata/merge 和 runtime preset 已在 L.8-L.11 收束。

#### 2026-06-14 L.8 progress

- Explicit spectral texture 已从 packet-width resident buffer 改为 source-sample resource descriptor：`GpuTexture` 现在保存 `SpectralTextureResourceKind::SourceSampleGrid`、`spectral_source_values`、`spectral_sample_count` 与 visible-domain lambda range；device `sample_texture()` 对 UV 做双线性，对当前 ray wavelength 做 source sample 线性插值。
- Host upload 不再为 RGB 或 explicit spectral texture 生成 `pixel_count × packet_lanes` buffer。RGB texture 只创建 `cudaArray<float4>` texture object 并在 kernel 端按当前 wavelengths 重建；非 RGB explicit spectral texture 上传原始 `width × height × source_sample_count` float table，和 `domain_bins`/`packet_lanes` 解耦。
- 新增 L.8 GPU 覆盖：`gpu_test_spectral_soa` 验证 source-sample resource 的 wavelength interpolation；`gpu_test_render` 验证 1,000,000 domain bins 下 uploaded descriptor 的 `spectral_sample_count` 仍等于 source texture channels，并验证 RGB path 保留硬件 filtering 且不创建 spectral source buffer。
- Phase L 静态审计已阻断旧 `spectral_values` 字段、texture upload 按 `ctx->num_spectral_channels` 展开、以及缺失 L.8 resource tests 的回归。
- 当前边界：L.8 不是完整 sparse virtual texture/cache 系统；basis compression、tile cache budget、cache miss/fallback 诊断留作后续性能工作。MaterialGraph resource graph 与 distributed spectral shard metadata/merge 已在 L.9-L.10 收束。

#### 2026-06-15 L.9 progress

- MaterialGraph 编译已新增 `SpectralExpressionNodeKind` / `HostSpectralExpressionNode`，graph 节点不再必须提前压扁成 `GpuMaterialData` packet cache；Texture2D、Add、Multiply、Mix 会编译为材质局部 post-order expression graph，device 端由 `eval_material_expression()` 按当前 ray wavelength 和 UV 评估。
- `GpuSceneCompiler` 的 graph 分支现在以 expression root 连接 albedo、roughness 和 emission；texture resolver 会缓存任意 graph texture resource，旧 scalar texture slots 只保留给非 graph material。`URE_spectral_material` SPD override 仍保持 authoritative，存在 SPD 时会清除对应 expression root。
- GPU upload 新增全局 material expression node buffer，并在 header 中记录每个 material 的 node range/root；expression graph material 的局部 `update_materials_gpu()` 现在 fail-loud，避免 header 更新后 device expression/resource table 留旧指针，低频 graph/resource 变更继续走 retained scene full reload。
- 测试覆盖新增 host `test_texture_add_and_mix_compile_to_expression_graph` 与 GPU `test_l9_material_expression_texture_add_mix_device_eval`，验证 texture/Add/Mix 不再以 Phase M.2 compiler fail-loud 退回 packet flatten；Phase L 静态审计要求 expression builder/evaluator/测试存在，并阻断旧 Add/Mix texture fail-loud 文案回归。
- 未完成边界：L.9 只解决 value/resource expression，不是完整 Phase M 材质系统。metal eta/k 与 dielectric ior 的 texture/expression 输入仍需要专用 spectral IOR/scalar expression slot；BSDF layering、procedural nodes、MaterialX import/export 的完整覆盖仍归 Phase M。

#### 2026-06-15 L.10 progress

- Distributed contract 新增 `DistributedSpectralDomainShard`、`DistributedFrameShard` 与 `DistributedShardMetadata`。sample range 和 framebuffer 现在都携带 spectral domain bins、domain shard start/count、wavelength PDF integral、frame index/count，避免 worker 只用 sample range 隐式假设全域光谱。
- `make_spectral_domain_shard()` 以整数分片覆盖百万级 `domain_bins`，`make_aggregate_spectral_domain()` 表达合并后的全域 accumulator；`merge_partial_framebuffer()` 允许不同 wavelength shard 合并，但要求 spectral domain/lambda range 和 frame shard metadata 一致，错误 metadata 会 fail-loud。
- Distributed file backend 升级到 v2，range/framebuffer 文件都会写入并读取 shard metadata；file merge 复用 contract 层兼容性检查，错误 spectral domain 或 frame shard 不会静默合并。
- 测试覆盖新增 spectral shard partition、spectral shard merge、metadata mismatch rejection、file metadata roundtrip 和 file merge rejection；`scripts/check_phase_l_static.ps1` 已要求 L.10 contract/file/test 关键符号存在。
- 未完成边界：L.10 是 contract/file backend，不是完整 farm scheduler。worker 调度、resource shard cache/preset、basis/tile cache 和 benchmark scenes 仍归 L.11。

#### 2026-06-15 L.11 progress

- `SpectralRuntimePlan` 已扩展为真正的 runtime preset 输出：根据 VRAM/SM、`SpectralSamplingMode::FarmShard` 和 scene resource stats 推导 sampler preset、cache preset、CUDA stream preset、resident budget bytes、estimated resident resource bytes、streaming/reject 信号。
- resource working set 估算已覆盖 material packet cache、sampled material/expression tables 和 explicit spectral texture source grids。`spectral_max_resident_mb` 不再只是配置字段；GPU 初始化会在任何 CUDA 分配前检查显式 resident budget，超预算直接报错。
- 测试覆盖新增低端 4GB 超预算 reject signal、高端 80GB multi-stream preset、farm shard preset，以及 oversized spectral texture resident upload fail-loud。新增 `scenes/benchmarks/phase_l_spectral_budget.gltf` 与 `tools/benchmarks/run_phase_l_spectral_smoke.ps1`，已用 CLI validate 和 1SPP HDR smoke 跑通 1M domain / sampled wavelength 路径。
- 未完成边界：L.11 现在提供了可执行预算/预设硬边界和 benchmark smoke 入口，但真实多 stream resource prefetch、basis compression、sparse/tiled cache runtime 和系统化 perf suite 仍是后续 perf/Phase K 或资源系统工作，不作为 Phase L 解耦完成的阻塞。

#### 2026-06-15 L.12 progress

- `scripts/check_phase_l_static.ps1` 已扩展为 Phase L 防回归门禁：阻断 `GpuSpectrum`、旧 spectral channel cap、texture packet carrier、`GpuMaterialData` POD cudaMemcpy、domain bins 回填 packet lanes/legacy `num_wavelengths`、以及 GPU init 中按 `spectral_domain_bins` 做 resident allocation。
- 静态审计同时要求 L.6-L.11 的 oracle、sampled estimator、source-sample texture、MaterialGraph expression、distributed spectral shard、runtime preset、budget gate、benchmark scene/script 关键符号存在。
- L.12 的边界是静态架构门禁，不替代运行时测试；完成报告必须仍以 `build_x64.ps1`、CTest、targeted benchmark smoke 和 `git diff --check` 为准。

#### 与未完成 Phase 的技术债约束

| Phase | 风险 | Phase L 前的约束 |
|-------|------|------------------|
| Phase M | MaterialGraph 若继续扩展 BSDF/MaterialX 但绕过 expression graph，会重新固化 packet carrier | M.2 已接入首版 expression graph；M.3 MaterialX 导入必须输出 spectral expression/resource graph，不允许回到 packet-only flatten |
| Phase U | USD `ure:spectral:bands = 64` 语义太弱 | U.1 schema 必须使用 `domainBins`、`packetLanes`、resource URI/basis/tile metadata，而不是单一 bands |
| Phase X | Shader 插件若直接返回 `GpuSpectrum` 会锁死 ABI | 插件 shader API 必须返回 spectral expression / sampled eval callback |
| Phase K | Spectral MIS 若只优化 N<=32 packet，会和 Phase L 采样器重复 | K.6 并入或依赖 L.7，不再单独设计一套 sampled wavelength estimator |
| Phase C/D | L.10 已把 distributed/file contract 扩展到 spectral-domain shard、wavelength PDF integral 与 frame shard metadata | 后续只剩 farm scheduler/resource cache，不再是 Phase L contract 阻塞 |
| Session API | `create_session(num_wavelengths)` 已公开 | C/Python API 已新增 spectral config object；旧参数保留为 packet-lane convenience wrapper，不能再代表 domain resolution |

#### 完成标准

- `README.md` 的百万级承诺有工程定义：1M+ spectral domain/resource bins，small packet path integration。
- 当前 32-lane packet cap 不再阻塞高分辨率 spectral resources。
- 低端硬件通过 budgeter 自动降级 sampler/cache/packet lanes，无法满足时在 scene load 前拒绝。
- 高端单机和农场支持 spectral-domain shard，merge 由 explicit wavelength PDF / domain metadata 保证无偏。
- `MaterialGraph`、MaterialX、USD、plugin API 不再暴露或依赖 `GpuMaterialData + GpuSpectrum[32]` 作为最终材质表达。
- 1M-domain host oracle + GPU sampled smoke + distributed shard merge test 全部通过。

---

### Phase R — 工业级/科研级积分器升级 / Research-Grade Radiometric Integrator

**状态**: ✅ 完成。R.0-R.12 local contract baseline 与 R-P1..R-P7 production closure 均已闭环；2026-07-23 clean-tree `Closure` 在 commit `56d1121` 上通过，权威执行游标进入 T.0。

**必须显式承认的能力边界**: Phase R 完成只覆盖各文档定义的 estimator support partition，不代表任意路径类型或算法组合均可用。ReSTIR PT 仍限于有界 diffuse/volume suffix；MLT 与 BDPT/VCM/manifold/adaptive reuse 的组合继续 fail-loud；unsupported spectral、material、volume 或 camera-delta 路径不会静默近似。

**目标**: 将当前 CUDA spectral/polarimetric wavefront path tracer 从“可用的物理路径追踪器”升级为工业级/科研级 radiometric light transport integrator。Phase R 不替代 Phase W：Phase R 处理默认非相干 radiance/Stokes transport 的调度、采样、MIS、路径空间算法和性能/收敛基准；Phase W 处理相干场、衍射、部分相干和局部全波求解。任何高级积分器都必须保持 Phase E/L 的 explicit wavelength PDF、spectral domain/resource contract、Stokes/Mueller 语义和 fail-loud wave feature policy。

**当前审计结论**: 现有有效积分器集中在 `libs/ure_core/src/path_tracer_host_api.cu::render_pass_gpu()` 与 `path_tracer_wavefront.cuh`。R.1 首批修复前，每个 sample 固定执行 `generate_rays -> (extend -> shade -> shadow) * max_trace_depth`，默认 `max_trace_depth = 50`，每个 depth 使用按 `max_rays` 计算的固定 launch blocks，并在 kernel 内以 `idx >= *queue.count` 早退；同时 `queue_capacity > width*height` 会把未初始化 queue slot 当 active ray 处理，`queue_capacity < width*height` 会让 primary ray generation 越界写。R.0-R.9 后，初始化会 fail-loud 拒绝小于 primary ray count 的 queue capacity，primary queue count 使用像素数，wavefront kernels 按 active ray/shadow ray count 发射并在空队列提前终止，surface/volume/RR 共享显式 path-dimension LDS 表，RayQueue/ShadowQueue overflow 汇总进 pass telemetry，sphere lights 已按 surface area × spectral emission power 建立 CDF/alias table，并在材质 emission 热更新后重建 light sampling distribution；`SpectralSamplingMode::Importance` 已不再等同 uniform sampled，raygen 可以消费 CIE-Y task-weighted proposal 或 scene/material spectral-power proposal table；scene proposal 存在时按 scene/CIE 50/50 mixture 抽样并把 balance mixture `p(λ)` 写入 path state；narrowband SPD oracle 用二阶矩证明 scene/CIE mixture proposal 相比 uniform wavelength sampling 降低 estimator 方差和权重尖峰；R.7 新增默认关闭的 progressive direct-light path guiding：可见 shadow contribution 按 spectral estimator 转亮度累积到 light-list guide weights，后续 NEE light selection 使用 base power/solid-angle sampler 与 guide distribution 的 explicit mixture PDF，保持 selection PDF 与实际 sampling 一致；R.8 新增默认关闭的 ReSTIR DI reservoir baseline：每像素保存可见 direct-light candidate 的 spectral radiance、wavelength PDF、light-list index、material/phase lobe PDF、history 和 visibility ray，下一轮 primary surface 可将该 reservoir 作为 temporal candidate 重新走 shadow visibility；当前实现明确是 biased temporal reuse，unbiased 和 spatial reuse 请求会 fail-loud，不会静默降级。R.9 新增 radiometric `SpecularManifoldConfig`、`integrator.specular_manifold` JSON/CLI 入口和 `ure::integrator` specular-interface oracle，锁定 Snell validity、TIR gate、Fresnel transmittance、solid-angle Jacobian 互逆、manifold PDF 与 radiance throughput scale；GPU production solver 在未实现真实 SDS/VCM 连接前会 fail-loud，当前 NEE specular dielectric blocker policy 保持不变，不恢复旧 straight-through transparent shadow。Henyey-Greenstein volume phase 现在有显式 `eval/pdf/sample` 接口，continuation ray 会记录 phase PDF 到 `last_pdf`，避免后续发光体命中 MIS 失去上一跳 sampling PDF；Rayleigh phase 已有 GPU closed-form eval/pdf/sample 与归一化 oracle；Mie phase 已有 unsupported selector gate，防止无参数/resource 时被静默当作 HG/Rayleigh；Lambertian/cloth/metal 已有 GPU white-furnace/reciprocity oracle，rough dielectric 已有 reflection/transmission PDF normalization 与 white-furnace energy bound oracle，thin-film 已有多波长/厚度/角度 s/p energy grid oracle。packet 模式仍存在 packet-average/hero-event 近似，这是 R.10+ reuse 与未来路径空间算法需要继续尊重的 estimator 边界，不是 R.0-R.9 未完成项。

2026-07-13 R-P6 校准：上段关于 Mie 仅有 unsupported selector 的描述是 R-P6 前基线。当前生产路径已由不可变 `MiePhaseResource` 驱动，支持 host Lorenz-Mie 生成、严格 JSON 导入、compiler revalidation/dedup、single/multi-GPU upload、nonuniform table interpolation、piecewise-linear CDF inversion、spectral cross section、volume NEE/continuation、`last_pdf` 和 scalar-depolarizing Stokes；缺资源或非法参数仍 fail-loud。

#### Phase R 边界

| 范围 | 属于 Phase R | 不属于 Phase R |
|------|--------------|----------------|
| 默认 radiometric path tracer | queue scheduling、active ray compaction/termination、surface/volume MIS、light sampling、path guiding、BDPT/MLT/ReSTIR radiance estimators | coherent film、Jones field transport、wave propagation operator |
| Spectral transport | wavelength PDF、多策略 wavelength MIS、packet/lane/sample estimator consistency、spectral guiding | 把百万 domain 重新塞回 per-ray lanes |
| Polarization | Stokes/Mueller 下的 radiometric estimator consistency 和 depolarization validation | Jones/complex coherent interference |
| Distributed/multi-GPU | sample-space/shard merge 对 integrator estimator 的无偏性和 determinism | coherent field frame merge，归 W.11 |
| Performance | occupancy/launch count/memory traffic/variance benchmark suite | 单纯 denoiser 替代物理收敛 |

#### 未生产化清单（当前核实结论）

| 能力 | 当前状态 | 证据/边界 |
|------|----------|-----------|
| Spatial-directional / BSDF-product path guiding | product-target 生产路径已接入，未完整生产化 | GPU resident spatial cell × light × direction-bin cache 已接入 reference-conditioned direct-light sampling/PDF；visible surface/volume shadow 学习 `Le × BSDF/phase × cosine × transmittance`，而非被旧 light PDF/path throughput/MIS 污染的 pixel contribution；sampled/lane target 消费 explicit wavelength PDF，并保留 representative wavelength/spectral mode/cache epoch；pass decay、reset、material/light rebuild、instance mutation epoch 语义已闭环；仍缺 multi-GPU merge、production memory budget 和四类场景收益曲线 |
| Unbiased / spatial ReSTIR DI | 未生产化 | `RestirDirectConfig` 有字段，但 spatial reuse 和 unbiased 请求在 GPU 初始化前 fail-loud |
| ReSTIR PT / path reuse | 未生产化 | 当前只保存 direct-light visible candidate reservoir；没有 path-space reservoir/reconnection/reuse |
| Specular manifold GPU solver | 未生产化 | 只有 config 和 specular-interface oracle；production solver 请求 fail-loud，glass direct-light 继续 blocker policy |
| BDPT / VCM | 未生产化 | 只有 Phase R 规划和 specular manifold 相关合同；没有 light subpath、connection、merging 或 MIS 权重生产路径 |
| MLT chain integrator | 未生产化 | 只有 primary-sample mutation oracle 和 config；GPU MLT integrator 请求 fail-loud |
| Light tree | baseline 已接入，未完整生产化 | GPU resident recursive light tree 已用于 base light sampling，节点携带 bounds，host build 按空间最长轴与能量平衡分群；device traversal/PDF 已 reference-point aware，per-light PMF 保留为全局 fallback/O(1) baseline；SceneDiff resource mutation 通过 retained SceneIR reload 保证 tree/cache 全量 rebuild；仍缺动态增量重建成本控制、生产级 clustering 和 farm long-run 收益曲线 |
| 非 sphere light 完整高级采样 | 部分完成 | Instance/direct mesh triangle、SceneIR analytic quad light、opt-in environment light、emission texture 和 MaterialGraph/resource-driven emissive mesh 已有 selection/PDF/eval baseline 与 targeted mixed-type PDF tests；本地 light sampling suite 覆盖 spectral emissive quad 与 multi-emissive-quad MSE/variance 曲线；生产级 tree clustering、更多 reference scene pack 和 farm long-run 仍未完成 |
| Mie resource | ✅ R-P6 已生产化 | 生成/导入统一为 validated immutable table；GPU spectral eval/pdf/sample、NEE/continuation、Session rebuild 与端到端生命周期已闭环；polarized Mie matrix scattering 不在本阶段范围 |
| 多场景 variance/MSE 收益曲线 | 本地 quick gate 已接入，未完整生产化 | `run_phase_r_light_sampling_suite.ps1` 输出两个 R-P1 场景的 HDR MSE-to-reference 与 radiance variance 曲线并接入 Phase R validation suite；Cornell/caustic/volume/dense spectral resource/multi-GPU shard 的完整 reference pack、farm long-run 和 Nsight dashboard 仍未完成 |

#### 子步骤

| Step | 内容 | 完成判据 |
|------|------|----------|
| R.0 | Integrator audit + benchmark harness：建立固定场景集、metric 和性能门禁，覆盖 Cornell/玻璃焦散/多光源/体积雾/百万 spectral resource/sampled wavelength/多 GPU shard | ✅ baseline 完成：`tools/benchmarks/run_phase_r_integrator_smoke.ps1` 建立本地 smoke 入口并输出 JSON metric；R.12 负责扩展完整固定场景集、variance/MSE、kernel launch curve、farm dashboard 和 spectral color error，不再作为 R.0 尾巴 |
| R.1 | Wavefront scheduling：active queue count 驱动 launch blocks、空队列提前终止、可选 depth-count polling 策略、overflow/termination telemetry | ✅ baseline 完成：primary queue count 使用 pixel count，queue capacity 小于 primary rays fail-loud，extend/shade/shadow 按 active count 发射，空队列提前终止并记录 pass-level telemetry；Nsight 长跑量化归 K.5/K.6/R.12 |
| R.2 | Queue compaction and path state layout：评估 persistent queues、stream compaction、SoA cache locality、shadow queue capacity、sampled wavelength lane state 的内存带宽 | ✅ baseline 完成：RayQueue overflow 汇总进 pass telemetry，ShadowQueue 有 `overflow_count` 和 `reserve_shadow_slot()` clamp，direct-light shadow contribution 不再静默丢失；`gpu_test_render` 覆盖 ray/shadow overflow 可见性。persistent queue、stream compaction 和 SoA locality 调优归 K.5，不阻塞 R.7 |
| R.3 | Unified sampling dimensions：surface、volume、RR、light picking、lens/camera、wavelength sampling 统一低差异维度分配；消除 volume path 对 xorshift RNG 的独立依赖 | ✅ baseline 完成：camera/wavelength/path-depth 维度表已进入 `path_tracer_sampling.cuh`，surface BSDF/NEE、volume distance/NEE/HG continuation 与 RR 已接入 `sample_path_dimension()`；`gpu_test_volume` 覆盖维度 stride 与 HG LDS sampling；完整 replay/variance dashboard 归 R.12 |
| R.4 | Light sampling upgrade：从均匀 sphere light picking 升级为 power/solid-angle aware sampler，建立 light distribution alias table/light tree 第一版，并同时覆盖 surface 和 volume NEE PDF | ✅ sphere-light baseline 完成：sphere lights 按 surface area × spectral emission power 建立 normalized CDF 和 O(1) alias table，surface/volume NEE 与命中发光体 MIS 使用同一 selection PDF；材质 emission 热更新会重建 light CDF/alias table；`gpu_test_render` 覆盖 CDF/alias 上传和材质更新重建，`gpu_test_spectral_soa` 覆盖 weighted alias selection/pdf helper。light tree 与非 sphere light sampling 归 R.8/R.12 |
| R.5 | Spectral MIS and wavelength guiding：在 Phase L sampled wavelength 基础上加入 wavelength proposal families、spectral power/CIE/task-weighted guiding、per-material/resource spectral importance | ✅ baseline 完成：CIE-Y task-weighted wavelength proposal 已进入 GPU raygen；Importance 模式优先消费 scene material emission/albedo spectral carriers 构建的 tabulated spectral-power proposal，缺少可分辨光谱结构时回退 CIE-Y；scene proposal 存在时 raygen 从 scene/CIE 50/50 mixture 抽样，并把 mixture `p(λ)` 写入 path state；材质 emission 热更新同步重建 wavelength proposal table；XYZ estimator 消费 wavelength PDF；`gpu_test_spectral` 覆盖 CIE-Y、tabulated proposal/PDF、scene/CIE mixture PDF 和 narrowband SPD proposal 二阶矩方差 oracle；`gpu_test_render` 覆盖 RenderConfig→GpuContext 策略映射、narrow emission SPD proposal 和 material update rebuild。per-texture/resource guiding 与 fluorescence 前置场景归 R.7/W.6 |
| R.6 | BSDF/phase sampling completeness：rough dielectric BTDF、measured conductor、thin-film、volume HG/Rayleigh/Mie candidate 的 sampling/PDF/eval 三元闭合 | ✅ baseline 完成：HG 和 Rayleigh volume phase 均有 `eval/pdf/sample` 三元接口与归一化/sampler-PDF tests；`VolumePhaseFunction` selector 接入默认 HG 生产路径并对 Mie 返回 unsupported/pdf=0；volume continuation `last_pdf` 使用真实 phase PDF；Lambertian/cloth/metal 有 GPU white-furnace/energy oracle、metal reciprocity oracle 和 Lambertian PDF oracle；rough dielectric reflection/transmission PDF 有数值积分归一化 oracle，white-furnace energy bound 按 radiance transport scale 分离验证；thin-film 有多波长/厚度/角度 s/p energy grid oracle。真实 Mie resource/parameterization 是后续体积 phase resource 功能，不作为 R.6 baseline 阻塞 |
| R.7 | Path guiding：建立 radiance/BSDF product guiding 或 spatial-directional guiding cache，支持 spectral/PDF metadata 和 progressive update | ✅ baseline + config contract 收口完成：新增 `PathGuidingConfig`，JSON/CLI/GPU `RenderConfig` 默认关闭；`enabled/light_mixture/learning_rate/min_weight` 已贯穿 app config、JSON、CLI 和 GPU config；enabled path guiding 的 `light_mixture`、`learning_rate`、`min_weight` 在 GPU 初始化前 fail-loud，避免静默 clamp 或静默禁用。GPU direct-light path guiding cache 记录每个 light-list 的可见 shadow contribution 亮度，sampling 使用 base light distribution 与 guide distribution 的 explicit mixture PDF；shadow resolve 使用 spectral mode / active channel / wavelength PDF 转换贡献后更新 guide weights；`gpu_test_render` 覆盖 guide cache 分配、invalid config gate、mixture PDF/sample 和可见 shadow progressive update，`test_config` 覆盖 JSON/CLI。更完整的 spatial-directional BSDF-product guiding、variance benchmark 和 farm dashboard 归 R.12/K.6 后续扩展 |
| R.8 | ReSTIR DI/PT：实现 reservoir direct-light reuse，再评估 path reuse；必须携带 wavelength PDF、Stokes-compatible throughput 和 material lobe PDF | ✅ biased temporal DI baseline 完成：新增 `RestirDirectConfig`，JSON/CLI/GPU `RenderConfig` 默认关闭；GPU per-pixel reservoir 保存 visible direct-light candidate 的 spectral radiance、wavelength PDF、light-list index、material/phase lobe PDF、history 和 visibility ray；primary surface 可 replay 上一轮 reservoir 并重新走 shadow visibility；`reset_accumulation_gpu()` 同时清空 ReSTIR reservoir 和 path-guiding weights，保证 Session progressive reset 不跨场景污染；spatial reuse 和 unbiased ReSTIR DI 请求 fail-loud；`gpu_test_render` 覆盖 reservoir allocation/reset、unsupported unbiased gate 和 visible candidate metadata，`test_config` 覆盖 JSON/CLI。真正 unbiased temporal/spatial ReSTIR、PT/path reuse、多光源收益 benchmark 和 farm dashboard 归 R.12/K.6 |
| R.9 | Bidirectional / specular manifold：为玻璃直接光、焦散和 SDS 路径建立 specular manifold 或 BDPT/VCM 连接，不恢复旧 straight-through dielectric shadow | ✅ contract/oracle baseline 完成：新增 radiometric `SpecularManifoldConfig` 和 `integrator.specular_manifold` JSON/CLI 入口，与 `wave_optics.specular_manifold` 语义分离；新增 `ure::integrator::SpecularInterfaceConnection` oracle，覆盖 Snell 有效性、TIR、Fresnel transmittance、solid-angle Jacobian 互逆、manifold PDF 与 radiance throughput scale；GPU renderer 对未实现 production solver fail-loud，因此当前 specular dielectric NEE blocker policy 仍正确，未恢复 straight-through shadow；`test_integrator` 覆盖 oracle 与 production gate，`test_config` 覆盖 JSON/CLI。真正 glass/caustic 收敛提升、SDS manifold Newton solve、BDPT/VCM 连接、Jacobian 场景级验证和 benchmark 收益归 R.12/K.6 后续实现 |
| R.10 | MLT/primary-sample-space integrator：为困难焦散/低概率路径提供可选科研级 integrator，不作为默认交互 preview | ✅ contract/oracle baseline 完成：新增默认关闭的 `MltIntegratorConfig` 和 `integrator.mlt` JSON/CLI 入口；新增 `ure::integrator::PrimarySampleMutation` oracle，覆盖 seed/dimension/mutation deterministic replay、small-step wrapped symmetric proposal PDF、large-step uniform proposal 和 Metropolis acceptance ratio；GPU renderer 对 enabled MLT production 请求 fail-loud，避免在未实现独立 chain scheduling、path replay 和 contribution normalization 前静默退回默认 wavefront tracer。真正 caustic/low-probability 收敛收益、reference scenes、chain normalization、spectral/path-state mutation replay 和统计 dashboard 归 R.12/K.6 后续生产化验证 |
| R.11 | Integrator API/config：`RenderConfig` / JSON / CLI / C ABI / Session / pyure 增加 integrator mode、sampler、guiding、reuse、quality preset；unsupported combination fail-loud | ✅ 完成：新增统一 `IntegratorRuntimeConfig`，`mode/sampler/quality_preset/allow_biased_reuse` 贯穿 JSON、CLI、GPU `RenderConfig`、C ABI 和 pyure；`path_guided/restir_di/specular_manifold/mlt` mode 会显式映射到对应 feature config，默认 wavefront 行为不变；primary-sample-space sampler、ReSTIR biased reuse、path-guided/specular/MLT feature gate 等非法组合在 GPU 初始化前 fail-loud；`test_config`、`test_integrator`、`test_session`、pyure smoke 和 Phase R static audit 覆盖配置 parity 与非法组合 |
| R.12 | Industrial validation suite：建立 correctness/performance/variance dashboard，固定每个 integrator mode 的允许误差、最小收益和不回归门槛 | ✅ local baseline 完成：新增 `tools/benchmarks/run_phase_r_validation_suite.ps1`，本地统一运行全量 build、Phase R static audit、R 相关 CTest 子集（config/integrator/session/pyure/render/spectral/volume/polarization）和 integrator smoke benchmark；输出 `output/benchmarks/phase_r_validation_suite.json`，包含 CTest 计数、benchmark `samples_per_second`/`spp_per_second`、阈值和分步耗时。`run_phase_r_integrator_smoke.ps1` 默认场景改为真实 benchmark glTF，不再依赖 `{}` placeholder。长 benchmark farm、Nsight dashboard、完整多场景收益曲线和性能不回归历史数据库归 K.5/K.6 后续生产化 |
| R.13 | Production path guiding：spatial-directional / BSDF-product guiding cache，支持 spectral/PDF metadata、progressive update、reset/scene mutation 语义和收益验证 | 默认关闭；开启后不是 per-light scalar guide；固定多场景 variance/MSE 收益曲线证明有效，否则不得标记完成 |
| R.14 | Production ReSTIR DI/PT：unbiased temporal/spatial ReSTIR DI 和 ReSTIR PT/path reuse，携带 wavelength PDF、Stokes-compatible throughput、material/phase lobe PDF 和 visibility metadata | biased preview 与 unbiased production mode 分离；spatial/unbiased 请求不再 fail-loud；有 reference scenes 和偏差/方差验证 |
| R.15 | Specular manifold / BDPT / VCM production：实现 SDS/specular manifold solver、light subpath connection、vertex merging、MIS 权重和焦散/玻璃直接光 production path | specular dielectric direct lighting 不再靠 blocker policy；BDPT/VCM/SM 有独立 integrator mode、场景级 Jacobian/PDF/energy gates |
| R.16 | MLT chain integrator production：实现 independent chains、primary-sample replay、large/small-step scheduling、normalization、burn-in/acceptance stats 和 spectral path-state mutation | MLT mode 不再 fail-loud；输出 chain stats；caustic/low-probability scenes 有收益曲线 |
| R.17 | Production light sampling：light tree、非 sphere light、mesh/env/area lights、volume NEE 统一 selection/PDF/MIS 合同 | sphere-only alias table 不再是高级采样上限；所有 light type 有 PDF parity tests |
| R.18 | Volume phase resources：真实 Mie parameter/resource/table，支持 spectral medium resource、sampling/PDF/eval 三元闭合和 energy gates | `Mie` 不再只是 unsupported selector；缺资源仍 fail-loud |
| R.19 | Full variance/MSE benchmark suite：固定 Cornell/multi-light/glass caustic/volume/dense spectral resource/multi-GPU shard scenes，输出 variance、MSE、spectral color error、time-to-error、VRAM 和 launch metrics | 本地 smoke、farm long-run 和 JSON schema 稳定；每个 production integrator mode 有适用范围和失败边界 |

#### Phase R 后续生产化阶段

R.13-R.19 是能力编号，不是线性施工顺序。实际执行按依赖拆成 R-P1..R-P7：R-P 阶段可以先完成某个编号靠后的基础能力，只要它是编号靠前能力的前置依赖。每个阶段结束时都要让对应高级 mode 从 fail-loud / baseline preview 进入可验证生产路径，或者明确保留 fail-loud 并说明尚未进入下一阶段。不能再用“API 已有”“数学 oracle 已有”“拒绝边界已验证”替代能力完成。

| 能力编号 | 能力目标 | 执行阶段 | 排序理由 |
|----------|----------|----------|----------|
| R.17 | Production light sampling / light tree / non-sphere lights | R-P1 | path guiding、ReSTIR、BDPT/VCM、MLT 都依赖统一 light selection/pdf/eval 合同，因此先执行 |
| R.13 | Spatial-directional / BSDF-product path guiding | R-P2 | 依赖 R-P1 的 light PDF 合同和 light identity；随后建立 guiding cache |
| R.14 | Unbiased/spatial ReSTIR DI + ReSTIR PT | R-P3 | 依赖 R-P1 的 light/reservoir target PDF；可与 R-P2 并行，但不能早于 R-P1 |
| R.18 | Mie / volume phase resources | R-P6 | 可与 R-P2/R-P3 并行；进入 R-P7 前必须完成 |
| R.15 | Specular manifold + BDPT/VCM | R-P4 | 依赖 R-P1 的 light endpoint/PDF，也会消费 R-P2/R-P3 的 benchmark scene pack |
| R.16 | MLT chain integrator | R-P5 | 依赖 R-P4 的困难路径 contribution evaluator 和 replay contract |
| R.19 | Full variance/MSE benchmark suite | R-P7 | 最终生产化门禁，汇总 R-P1..R-P6 的 correctness/performance 证据 |

| Stage | 目标 | 必须交付 | 验证门槛 |
|-------|------|----------|----------|
| R-P1 | Production light sampling foundation | 统一 light resource abstraction；sphere、mesh area、emissive triangle、environment、analytic area light 的 selection/pdf/eval 接口；light tree 或等价层级采样结构；surface/volume NEE 共享同一 PDF 合同；材质 emission/resource mutation 后 tree/alias 增量或全量 rebuild 语义。进度：typed `GpuLightRecord`、instance/direct mesh triangle light distribution、triangle solid-angle PDF、opt-in environment direct sampling、O(1) PMF PDF、带 bounds 的 GPU resident recursive light tree、空间最长轴/能量平衡 host tree build、reference-point aware device tree traversal/PDF、SceneIR `QuadLightNode` analytic area light、surface/volume NEE、emissive-hit MIS 与 environment-miss MIS 统一入口已落地；host light distribution power 已覆盖 scalar emission、SPD/resource emission、emission texture 和 MaterialGraph emission expression，非法发光纹理引用 fail-loud；SceneDiff texture/MaterialGraph/SPD resource mutation 已按 retained SceneIR full reload 触发 tree/alias/resource cache 全量 rebuild；`gpu_test_render` 已覆盖 instance triangle、direct mesh triangle、environment record/PDF、light-tree PMF/PDF/sampling、三光源 spatial split/subtree bounds、reference-point dependent sampling/PDF、混合 sphere/mesh/env PDF 归一、material emission update 后 PMF/tree/leaf rebuild、emission texture/expression power 和 invalid emissive texture gate；`test_session` 覆盖 texture/graph/SPD material mutation full reload；`test_gltf_frontend` 覆盖 SceneIR quad light compile 与 degenerate area fail-loud；`test_config`/C ABI/pyure smoke 覆盖 environment direct sampling 配置面；`run_phase_r_light_sampling_suite.ps1` 接入 Phase R validation suite 并输出 two-scene MSE/variance JSON。剩余生产级 tree clustering、增量 rebuild 成本控制、完整 reference scene pack/farm long-run 和 Nsight dashboard 未完成 | PDF parity tests 覆盖所有 light type；surface/volume NEE 和 hit-light MIS 消费同一 selection PDF；多光源场景 variance/MSE 不低于 sphere-only baseline；不支持的 light type 在 scene compile fail-loud |
| R-P2 | Spatial-directional / BSDF-product path guiding | ✅ 已完成：GPU resident `spatial cell × light × direction bin` cache、完整 scene bounds domain、reference-conditioned proposal/PDF、global/light-tree fallback、局部光谱 product target、wavelength PDF metadata、decay/epoch/reset/mutation、emissive transform reload boundary、multi-GPU baseline+delta merge/broadcast、checked device-derived/explicit memory budget，以及内建 Cornell/多光源/复杂材质/volume workload 的 variance/MSE/time-to-error suite | Guide sampling PDF 与实际 proposal 完全一致；四类场景曲线由 `run_phase_r_path_guiding_suite.ps1` 生成；错误 cache epoch、预算不足、非法参数和 unsupported material path fail-loud |
| R-P3 | ✅ Unbiased/spatial ReSTIR DI + ReSTIR PT | production DI 使用独立 ping-pong GRIS reservoir、current-point target/visibility reconstruction 和 unbiased metadata；PT 使用独立 mode、per-ray global sample identity、versioned dimension interval、bounded actual surface/volume suffix、forward/reverse PDF/Jacobian、Stokes/wavelength state、isolated candidate replay 和一次 normalized film commit；specular manifold suffix 明确交给 R-P4 | 37/37 CTest、Release 全构建与 Phase R static audit 全绿；默认 multi-light/occlusion/volume suite 记录 bias 95% bound、MSE、variance、time-to-error、samples/s 和 rejection counters；8 SPP relative mean bias 为 5.0%/0.8%/1.7%，95% bound 为 7.2%/1.9%/9.1% |
| R-P4 | Specular manifold solver + BDPT/VCM | GPU specular manifold Newton solve；SDS/specular chain connection；light subpath generation；camera/light subpath connection；vertex merging radius schedule；MIS 权重；spectral/polarimetric throughput 和 Jacobian 合同；glass direct-light blocker policy 替换为真实路径空间算法。进度：共享 host/GPU path vertex measure、technique MIS、dimensional VCM radius、BDPT/VCM config/ABI、checked context ownership、stable per-ray path identity、typed sphere/triangle light endpoint generation和实际 camera surface vertex capture已落地；bounded importance light subpath 已递归生成 diffuse、cloth、rough metal、rough dielectric 和 homogeneous volume vertex，使用共享逐波长 Mueller/Stokes/phase transport并记录 forward/reverse area/volume PDF；camera path 同步捕获 surface/volume vertex，surface↔volume mixed edge 使用目标 measure 的正确 Jacobian；平滑色散介质仍显式留给 manifold solver，粗糙介质无效 PDF 不再伪装为 delta；light endpoint position PDF 已与首条 edge area PDF 分离为不可变密度；host/device 对同一完整路径重建 `s=0..n` 全部 connection strategy absolute density，VCM merge 进一步重建 light prefix、实际 spectral BSDF/phase bridge 双向 PDF 与 camera suffix，connection 与 merge 两侧共享包含所有 connection techniques 和唯一 merge technique 的同一 power MIS 分母，Host/GPU partition oracle 严格为 1；connection kernel 现按 `connections_per_pixel` 有界枚举实际 `(s,t)` pair，逐端重算 spectral BSDF/phase、geometry 与 visibility；高级 mode 调度隔离旧 wavefront radiance，只将 connection/surface merge/volume merge techniques 一次提交 film，避免未加权 estimator 叠加；VCM surface/volume merge 已有按 policy 分配的预算内 context ownership、2D/3D progressive radius/reset、collision-safe spatial hash、surface leak gate、normalized disk/sphere kernel、overflow fail-loud 和独立 GPU E2E；MaterialGraph Composite 复用 resolved lobe selection/eval/mixture PDF，Layered 复用 finite-thickness coating/substrate scatter/eval/PDF，均已进入 light subpath、connection 与 surface merge；manifold 数值核心已有 host/GPU bounded 8-variable partial-pivot solve、determinant/singular gate 与 Newton `J Δx=-F` step，配置上界锁定 4 个 specular events/64 iterations；sphere angular/triangle barycentric parameterization、reflection/Snell tangent constraint、central-difference Jacobian、domain projection、damped line search、physical hemisphere/TIR gates 的 GPU single-event solver 已闭环；GPU multi-event solver 已对最多 4 个事件组装耦合 2N residual/central-difference Jacobian、全变量 pivot solve 与全链 line search，双界面折射 slab 从扰动初值收敛；scene primitive extraction 已从真实 sphere/direct mesh/instance triangle identity 构建世界空间 manifold primitive，并由命中位置恢复 angular/barycentric 初值；production solve pass 已拥有预算内 per-path solution artifact/telemetry，逐 path 从 trailing delta camera vertices 构建真实 scene chain，写出 anchor/light identity、world-space solution、parameters、determinant、residual、iterations、epoch 与 typed rejection reason，renderer-owned sphere E2E 已收敛；SDS contribution 与最终 benchmark仍在实施 | Glass caustic、SDS、small emitter、rough/specular mixed path 场景有 correctness oracle 和收益曲线；BDPT/VCM/specular-manifold 各自 mode 不再 fail-loud；Jacobian/PDF/energy gates 必须通过 |
| R-P5 | MLT chain integrator | Independent chain scheduler；primary-sample-space replay；large/small step proposal；burn-in、acceptance stats、normalization、chain seeding；spectral wavelength/path-state mutation；与 BDPT/VCM 或 default path tracer 的 contribution evaluator 边界 | MLT mode 不再 fail-loud；输出 chain diagnostics；低概率焦散/小光源/高遮挡场景 time-to-error 优于 default baseline；deterministic replay 和 distributed shard seed contract 通过 |
| R-P6 | Volume phase resources and Mie production | ✅ 已完成：deterministic Lorenz-Mie generator、严格 JSON adapter、immutable resource、compiler revalidation/dedup、single/multi-GPU ownership、spectral eval/pdf/sample、NEE/continuation、scalar-depolarizing Stokes、Session rebuild 和 generated/imported global+bounded lifecycle | `VolumePhaseFunction::Mie` 已是 resource-backed production selector；缺资源或非法参数 fail-loud；HG/Rayleigh 回归、Mie energy/normalization/sampling-PDF/offset/lifecycle tests 全覆盖 |
| R-P7 | Full industrial validation suite | 固定 benchmark scene pack；reference images/metrics；variance、MSE、spectral color error、time-to-error、samples/sec、VRAM、kernel launch、occupancy 指标；local quick gate + farm long-run gate + Nsight dashboard schema；每个 production mode 的适用范围和拒绝边界文档 | 本地 suite 可复现；farm/Nsight 输出稳定 JSON；每个高级 integrator mode 至少有一个正收益场景和一个边界失败场景；Phase R completion 只能在 R-P1..R-P7 全部闭环后声明 |

R-P4 visibility closure：production manifold solve 对 anchor→specular chain→light 的每条 edge 使用正式 traversal 验证 visibility；预期 manifold/light endpoint 仅按 geometry identity 与端点容差豁免，其他提前命中统一以 typed `Occluded` reason 和 telemetry 拒绝。GPU E2E 同时覆盖无 blocker 收敛和真实 blocker 拒绝。SDS contribution evaluator 与最终 benchmark 仍未完成。

R-P4 differential geometry closure：最终解不再误用 Newton constraint determinant 作为 radiometric 权重；GPU 依据 specular-manifold differential geometry 重建 `|P₂ A⁻¹ B_L| × G(anchor, first)`，并分别持久化 constraint determinant、endpoint area Jacobian、ordinary geometry 与 generalized geometry。sphere 周期经度 seam 的中心差分已使用拓扑正确的 `2h` 分母；解析 planar mirror oracle、sphere oracle、生产 artifact 与 50 次重复稳定门禁均通过。SDS spectral/Stokes response、root-selection PDF/MIS 与最终 benchmark 仍未完成。

R-P4 spectral response progress：sphere/triangle/instance manifold surface 与 light endpoint 已携带真实 UV；smooth-delta eligibility 会拒绝粗糙 metal/dielectric，色散 packet 必须先拆为 wavelength lane，material expression/texture 与 Cauchy IOR 均按实际 UV/wavelength 求值。独立 GPU SDS response artifact 会重算 light emission、anchor BSDF、逐事件实际 `eta_i/eta_t` Mueller/Stokes radiance transport，并显式消费 generalized geometry、endpoint PDF、reciprocal-root 与 MIS 权重；resolved artifact 已按 anchor wavelength state 转换并进入唯一 bidirectional film commit。GPU oracle 覆盖正 radiance、Stokes physical bound、rough/spectral-split rejection、nested-medium IOR identity 和 nonzero film commit。最终多场景 bias/energy/benefit benchmark 仍未完成。

R-P4 reciprocal-root proposal closure：host 端按 bidirectional VRAM budget 持有覆盖全部非退化 sphere、direct mesh triangle 和 world-transformed instance triangle 的 GPU geometry catalog，保留稳定 identity/UV/material binding，且不因材质突变陈旧；非 smooth-specular proposal 是普通 Bernoulli miss。target/trial 共享 counter-based IID normalized proposal，联合采样 camera anchor、event count、catalog topology、uniform-area parameters 与 reflection/transmission branch。context-owned root state 以无固定上限的 8-trial bounded GPU pass 持续执行 geometric count，按完整 topology 与 converged world-position 匹配 target root，并在每个 render sample 提交前 drain 全部 pending。内部 delta-chain 对当前 connection/merge technique 是 disjoint support，显式 exclusive MIS=1；resolved spectral contribution 只进入一次 film commit。GPU gate 覆盖 proposal density、16-path pending drain、reciprocal weights、telemetry、VRAM lifecycle 与 nonzero commit；最终 statistical bias/variance/benefit benchmark 仍未完成。

R-P4 final closure：standalone SMS 只接管“non-delta area anchor → 1..4 smooth-delta events → finite emitter”的精确支持集；wavefront flag 独立携带 last-delta 与 preceding-area-anchor 状态，未覆盖的 camera-delta、environment、rough、volume-interrupted 路径继续由 wavefront 负责。dielectric Mueller/Stokes response 按 camera-subpath 的 anchor→light radiance transport 方向应用 eta scale。独立 wavefront technique AOV 在分区前记录同一支持集，四场景 suite 不再使用 total-image subtraction 或 SMS self-reference。2026-07-18 默认 Release suite 以 8192 SPP 基准和 rare-event adaptive reference budgets 通过 glass caustic、SDS、small emitter、mixed rough/specular；high-SPP relative mean bias 为 14.3%/7.6%/7.1%/1.3%，95% bound 为 26.5%/32.2%/27.0%/14.1%，2 个 workload 给出正 time-to-error。R-P4 已闭环，权威游标进入 R-P5。

R-P5 closure（2026-07-23 统计复核）：独立 GPU chain scheduler 已接入生产 wavefront contribution evaluator，queue-owned primary-sample replay 覆盖 camera/film/wavelength/surface/volume/light/lobe/RR dimensions；bootstrap weighted/stratified seeding、burn-in、wrapped symmetric Laplace large/small step、PSSMLT 双端沉积与归一化、显存预算、chain diagnostics、64-bit global chain identity、多 GPU disjoint shard、配置/API 传播和 deterministic GPU replay 均已实现。R-P7 复核发现旧 8x8 两场景结论的 wavefront curve 与 reference 共享 sample prefix，相关误差使其不能作为独立收益证据。新版 `ure.phase_r.mlt_suite.v2` 使用 4 个不相交 reference shard、4 个不相交 wavefront sample range、4 个不重叠 MLT chain identity interval，并把 reference uncertainty 纳入 full-image replicate bias interval；固定 normalized-MSE=5% 下，SDS small-light 由 MLT 在 64 SPP/约 0.157 秒达标，而 wavefront 在 256 SPP/约 0.294 秒达标，最终 95% relative-bias bound 约 1.7%；SDS、小光源、玻璃焦散及 high-occlusion 作为统计边界。更极端的 high-occlusion area-compensated small-light 仅保留 deterministic path-distribution contract，因当前预算下 high-sample confidence bound 不稳定而不进入默认统计矩阵。BDPT 审计同时修复 spectral accumulator wavelength 丢失与 camera reverse-PDF 方向错误；standalone energy regression 通过。MLT+BDPT 因 sampled-lane camera/light subpath 尚无共享 wavelength primary sample 而 fail-loud，VCM/manifold/adaptive reuse 继续拒绝。R-P5 实现保持闭环，证据按 R-P7 每个 mode 至少一正收益和一边界的标准重新加固。

R-P7 closure（2026-07-23）：`ure.phase_r.industrial_validation.v1` 在 clean commit `56d1121` 上通过 `Closure`，聚合 integrator smoke、light sampling、path guiding、ReSTIR PT、specular manifold、独立 BDPT/VCM、MLT 和 volume/Mie 八类 SHA-256 evidence。Release 全构建、37/37 CTest、Phase Q/L/R 静态审计与 physics-optics gate 全绿。最终 farm 使用两个不重叠 shard 覆盖 4,096 SPP；farm、Nsight 与当前 benchmark executable 的 SHA-256 均为 `7b32d2a64bc03dd412874075bf3b6df62f128d39319fdfcf69cb451abad7a95d`。Nsight 实测 14 次 launch、5 类 kernel 和 1,250,951,168-byte peak VRAM delta。BDPT/VCM、SMS 和 replicated MLT v2 均满足各自正收益、收敛、bias 与拒绝边界合同。Phase R 已闭环，权威游标进入 T.0。

#### Phase R 执行顺序

```
R.0-R.12 local baseline (done)
        │
        ▼
R-P1 production light sampling foundation
        │
        ├──► R-P2 spatial-directional / BSDF-product path guiding
        │
        ├──► R-P3 unbiased/spatial ReSTIR DI + ReSTIR PT
        │
        └──► R-P6 Mie / volume phase resources
        │
        ▼
R-P4 specular manifold + BDPT/VCM
        │
        ▼
R-P5 MLT chain integrator
        │
        ▼
R-P7 full variance/MSE + farm/Nsight validation
```

R-P1 必须先于 R-P2/R-P3/R-P4，因为 path guiding、ReSTIR、BDPT/VCM 都依赖统一 light selection/pdf contract。R-P6 可与 R-P2/R-P3 并行，但必须在体积 benchmark 被纳入 R-P7 前完成。R-P5 应在 R-P4 之后推进，否则 MLT 没有足够可靠的困难路径 contribution evaluator。Phase T 的 audit/contract 工作可以与 R-P2/R-P3 并行，但不得在 estimator 尚未稳定时复制 backend 实现；高级 integrator 继续在当前 CUDA traversal 上给出 correctness evidence。Phase V 的正式 GPU acceleration work 必须等待 Phase T acceleration-provider contract。

#### 完成标准

- 默认 radiometric renderer 的物理结果不回退，Phase E/L spectral PDF 和 Stokes/Mueller 语义保持有效。
- `render_pass_gpu()` 调度不再在 active queue 为空时固定跑满 depth；active queue launch policy 有性能证据。
- Surface、volume、light、RR、wavelength 采样维度统一可追踪，deterministic replay 稳定。
- 多光源和体积场景的 light sampling / MIS 有无偏 PDF 证明和 benchmark 收益。
- Spectral MIS/path guiding 不把 RGB luminance 当作真实光谱重要性。
- 玻璃/焦散直接光通过 specular manifold/BDPT/VCM 等显式路径空间算法解决，不恢复错误的 transparent shadow shortcut。
- 高级 integrator mode（path guiding、ReSTIR、BDPT/VCM、MLT）均有生产路径，不只是显式配置、数学 oracle 或 unsupported fail-loud；每个 mode 都有 reference scene、variance/MSE/performance 指标和文档化适用范围。
- Light tree、非 sphere light 高级采样、真实 Mie resource 和完整多场景 variance/MSE 收益曲线全部纳入 R-P1/R-P6/R-P7；任一缺失都意味着 Phase R 不能标记完成。

---

### Phase T — 可移植 GPU 运行时 / Portable Multi-Backend Execution

**状态**: 已完成，T.0-T.11 全部闭环。Phase V 的正式实现建立在本阶段冻结的 runtime、resource、execution、acceleration 和 validation 合同之上。

**目标**: 把 UltraRender 从“核心语义由 CUDA 实现细节定义”升级为“同一套物理、资源、调度和加速合同可由多个 GPU backend 执行”。CUDA 保留为当前生产后端、物理参考实现和 NVIDIA 性能路径，但不再拥有公共架构；Vulkan 是 Windows/Linux 跨厂商生产后端；D3D12/DXR 是 Windows 可选后端。各 backend 可以使用专有优化，不要求退化到最低公分母。

#### 不可破坏的架构边界

- SceneIR、MaterialIR、IntegratorIR、WaveIR、Session、distributed contract 和 URE native schema 不得包含 `cuda*`、OptiX、Vulkan 或 D3D12 handle。
- 公共资源使用 backend-neutral id、descriptor、usage、format、residency 和 synchronization contract；native handle 只能存在于 backend 私有实现。
- 光谱、Stokes/Mueller、BSDF/phase、MIS、path guiding、ReSTIR、wave operator 的数学语义只有一份权威定义；backend 只负责 lowering、资源绑定和执行。
- Acceleration structure 是可替换 provider，不拥有材质、光谱、偏振、介质或积分器语义。OptiX 只属于 CUDA/NVIDIA provider，Vulkan RT 与 DXR 分别属于对应 backend。
- 不引入 CPU production integrator。CPU 仅保留 oracle、编译、构建、调度和验证职责。
- `auto` backend 必须依据 feature/capability/budget 选择；不得静默关闭物理特性。显式请求不支持的能力必须在 scene compile 或 session create 前 fail-loud。

#### Backend 能力模型

| 层 | 统一合同 | Backend 私有实现 |
|----|----------|------------------|
| Device | adapter identity、feature set、memory heaps、subgroup/wave size、limits | CUDA device、VkPhysicalDevice/VkDevice、D3D12 device |
| Execution | queue、event/fence、command/dispatch graph、async copy、timeline dependency | CUDA stream/graph/event、Vulkan queue/command buffer/timeline semaphore、D3D12 queue/list/fence |
| Resource | buffer/image/sampler、typed view、usage、alignment、residency、sparse/tiled capability | cuda allocation/array/texture object、VkBuffer/Image、D3D12 resource/descriptor heap |
| Kernel | stable semantic entry id、specialization constants、layout reflection、required capabilities | CUDA module/kernel、SPIR-V compute/ray module、DXIL compute/ray module |
| Acceleration | BLAS/TLAS input、build/refit/compact、ray query/trace、hit metadata | self CUDA BVH、OptiX、Vulkan RT、DXR |
| Diagnostics | timestamp、memory budget、dispatch stats、device loss、validation message | Nsight/CUDA diagnostics、Vulkan validation/debug utils、D3D12 debug/DRED |

#### Kernel 与物理语义策略

Phase T 不允许简单维护三份独立 path tracer。T.2 必须用实际 spectral/polarization/queue kernel 原型决定 portable kernel toolchain：评估受限共享 C++ device subset、Slang/多目标编译或明确的 URE KernelIR + backend lowering。选择必须满足 CUDA、SPIR-V 和 DXIL 的布局反射、64-bit addressing、subgroup operation、atomics、specialization、debug/source mapping 和离线可复现编译；不能满足物理语义或性能基线的方案淘汰。工具链决定前，现有 CUDA kernel 是权威实现，但新公共接口不得继续暴露 CUDA 类型。

#### 子步骤

| Step | 内容 | 完成判据 |
|------|------|----------|
| T.0 | CUDA coupling audit：枚举 CMake、公共头、C ABI/pyure、CLI、GpuContext、texture/resource、queue、kernel launch、multi-GPU、wave optics 和 acceleration 中的 CUDA 泄漏 | 产出 `docs/Phase_T_Portable_GPU_Runtime.md`；每个耦合点有 contract owner、迁移批次和禁止回归的静态审计规则 |
| T.1 | Backend identity/capability contract：`BackendKind {Auto, Cuda, Vulkan, D3D12}`、adapter id、feature bitset、limits、memory budget、driver/compiler identity | RenderConfig/JSON/CLI/C ABI/pyure parity；显式 unsupported backend fail-loud；CUDA 仍为当前默认 production backend |
| T.2 | Portable kernel toolchain feasibility gate：以 spectral conversion、Mueller、queue compaction、BSDF sampling、wave propagation 和 traversal query 原型比较候选 source/IR/lowering | 决策文档含正确性、生成代码、寄存器/occupancy、debug、构建与依赖成本；至少 CUDA + SPIR-V + DXIL 编译验证后才能锁定工具链 |
| T.3 | Backend-neutral runtime API：device、queue、event/fence、buffer、image、sampler、module、pipeline、dispatch graph 和 error/device-loss contract | 公共头不包含 CUDA/Vulkan/D3D12 SDK 类型；mock contract host tests 覆盖 lifetime、alignment、overflow、同步和 device loss |
| T.4 | Resource/descriptor migration：替换公共 `cudaTextureObject_t`、CUDA allocation ownership 和 raw backend pointer；定义 typed descriptor、resource id、layout、residency、sparse/tiled 与 upload plan | SceneIR/MaterialIR/Session/distributed metadata 可在无 CUDA SDK 的纯 C++ target 编译；百万光谱资源预算语义不回退 |
| T.5 | Dispatch/queue IR：把 wavefront stage、active-count dependency、indirect dispatch、barrier、async transfer 和 pass/epoch boundary 表达成 backend-neutral execution graph | 当前 path tracer、guiding/ReSTIR 状态和 wave operator 可生成稳定 graph；后端不得自行改变 estimator 顺序或 PDF 语义 |
| T.6 | CUDA backend 迁移：现有 `.cu` kernel、CUDA texture、stream/graph、multi-GPU 和 diagnostics 接入新 runtime，不改 estimator | 全量现有测试与 reference render 通过；性能/VRAM 相对迁移前基线无未解释回退；CUDA 专有 fast path 可保留在私有目录 |
| T.7 | Vulkan compute production foundation：adapter/queue/resource/pipeline/cache、SPIR-V module、descriptor binding、timeline sync、device-loss 与 validation | Windows/Linux 构建；至少 raygen、spectral/polarization transport、wavefront queues、film/AOV 和 wave reference operator 通过跨厂商测试 |
| T.8 | Vulkan RT/acceleration bridge：把 Phase T acceleration-provider contract 接到 ray query/ray tracing pipeline，正式 SAH/wide/TLAS/cluster work仍归 Phase V | 与 self CUDA traversal 的 hit metadata、visibility、instance transform 和 framebuffer parity 达标；无 RT capability 时按配置使用 compute BVH 或拒绝 |
| T.9 | D3D12/DXR optional backend：复用 KernelIR/runtime/resource/acceleration contract，加入 DXIL、descriptor heap、queue/fence、DXR provider 和 DRED | Windows 可选构建；无 D3D12/DXR 环境不影响 CUDA/Vulkan；核心 parity fixtures 通过 |
| T.10 | Multi-backend scheduling：同构与异构 multi-GPU/farm capability negotiation、resource cache key、compiler/backend identity 和 merge compatibility | 不兼容 feature/precision/coherent mode 拒绝混合；允许兼容 sample shard 跨 backend 合并并保留可复现 metadata |
| T.11 | Cross-backend validation/performance suite：物理单元、hit metadata、reference renders、variance/MSE、device loss、budget、build cache、cold/warm launch、VRAM 和 throughput | `run_phase_t_validation_suite.ps1` 输出机器可读报告；CUDA/Vulkan 是必测生产后端，DXR 按 capability 可选；差异有阈值和原因分类 |

T.0 closure（2026-07-23）：`docs/Phase_T_Portable_GPU_Runtime.md` 已覆盖 build、device、public API、C ABI/pyure、GpuContext、resource/texture、queue/dispatch、kernel、multi-GPU、wave optics、acceleration、diagnostics、scene lowering 和 validation 14 类耦合；每类均指定 contract owner 与 T.1-T.11 迁移批次。`scripts/check_phase_t_static.ps1` 阻止 CUDA SDK/native handle 进入 backend-neutral modules、RenderConfig、SceneIR、C ABI 和 pyure，并冻结现有 4 个 public CUDA include debt allowlist，后续迁移只能缩减不能扩张。T.0 不声称第二后端存在；权威游标进入 T.1。

T.1 closure（2026-07-26）：backend-neutral `BackendKind`、stable adapter id、feature bitset、limits、memory capacity/budget 和 driver/compiler identity 已贯穿 RenderConfig、JSON、CLI、C ABI 与 pyure；`Auto` 和显式 `Cuda` 选择当前 CUDA production backend，UUID adapter identity、能力/预算验证和设备激活均 fail-loud。Vulkan/D3D12 仅保留身份值，显式请求不会静默回退。CLI device inventory 已移除直接 CUDA SDK 依赖；配置、GPU、C ABI-backed pyure 与静态审计门禁覆盖选择和拒绝边界。权威游标进入 T.2。

T.2 closure（2026-07-26）：固定 Slang 2026.14 及 release SHA-256，以共享 semantic module 和 spectral conversion、Mueller/Stokes、subgroup+atomic queue compaction、BSDF sampling、complex wave propagation、AABB traversal 六个 compute 原型直接生成 CUDA PTX/cubin、SPIR-V 与 DXIL。`run_phase_t2_kernel_toolchain_gate.ps1` 对全部 18 个目标产物执行 warnings-as-errors、双编译哈希一致、reflection/64-bit layout、subgroup/atomic、specialization、debug/source mapping 和 target validation；sm_120 cubin 通过 Driver API 数值执行，15-40 registers、零 spill、64-thread block 实测 occupancy 100%。受限共享 C++ 因无统一 SPIR-V/DXIL frontend/reflection/debug contract 淘汰；自研 shader KernelIR 因会引入三套 compiler backend 技术债淘汰。Slang RHI 未纳入，生产 CUDA executor 仍保持不变；权威游标进入 T.3。

T.3 closure（2026-07-26）：新增独立纯 C++ `ure_runtime` library，公共合同以 typed nonzero handles 和稳定 descriptor 表达 device、queue、timeline fence、event、buffer、image、sampler、module、compute pipeline、resource binding、submission 与 dispatch DAG；结构化 `ErrorCode`、`DeviceState` 和 retained `DeviceLossInfo` 明确 timeout/overflow/unsupported/device-loss 边界。公共验证器在 backend lowering 前拒绝非法 alignment/extent/mip/workgroup、整数 overflow、空 module identity、无效 handle、重复 binding、missing dependency 与 graph cycle。host mock contract test 覆盖 allocation budget、module/pipeline lifetime、use-after-destroy、copy/binding bounds、event ordering、monotonic timeline、timeout 和 injected device loss；`ure_runtime` 与测试均无 CUDA/Vulkan/D3D12 SDK 类型。T.3 不包装现有 `GpuContext`，生产 backend 迁移仍归 T.4/T.6；权威游标进入 T.4。

T.4 closure（2026-07-26）：新增稳定 128-bit semantic `ResourceId`、typed buffer/image/spectral layout、完整 mip/layer subresource pitch、resident/streamed/sparse-tiled policy、dependency DAG、deterministic upload plan 与 overflow/budget validation；百万 `domain_bins` 的 source-sample spectral grid 仍按 texel × source samples 计费，不按 domain 或 packet lanes 展开。公共 `render.hpp` mutation API 只接受 SceneIR，CUDA scene compiler、context、multi-GPU allocation state 和 texture view 均移入不安装的 backend-private detail boundary；`cudaTextureObject_t`、CUDA array/allocation ownership 与 cleanup 由 ResourceId-keyed RAII registry 独占，生产 RGB/spectral texture upload 先通过 neutral plan 再 lowering。distributed metadata/file v4 持久化 resource-set identity 与预算并拒绝不兼容 merge。纯 C++ resource test 同时包含 SceneIR/MaterialIR/Session/distributed headers，CUDA texture parity 与全量测试通过；权威游标进入 T.5。

T.5 closure（2026-07-26）：新增 SDK-free execution graph schema，以 stable region/queue/resource identity 表达 pass/sample/candidate/depth/bootstrap/mutation/manifold epoch、active-count 的 initial/iteration producer、3D direct/chunked/indirect dispatch、queue reset/swap、whole/ranged resource clear、ReSTIR/VCM/MLT/sample-count state transition、barrier、whole-resource/ranged async transfer、MLT bootstrap host normalization 和严格嵌套 boundary。Estimator contract 固定 spectral/scattering/medium/light/ReSTIR/support-partition/MLT-primary PDF semantic version，并以 dependency-closed ordered critical nodes 阻止 backend 重排；canonical validator 在 lowering 前拒绝非规范 id/order、缺失 producer、cycle/future dependency、非法 loop/chunk/indirect argument/clear/state transition、未闭合 epoch、dispatch overflow 与 estimator sequence 漂移，四 lane fingerprint 提供确定性 graph identity。当前 wavefront、path guiding、ReSTIR DI/PT、BDPT/VCM/manifold、PSSMLT 与 Fraunhofer operator 均生成并验证同一合同；CUDA path/wave 入口记录或验证 graph identity，但 kernel/resource/stream 的完整 runtime lowering 仍归 T.6。独立 C++-only gate 不配置 CUDA compiler，Release 全量 40/40 CTest 通过；权威游标进入 T.6。

T.6 closure（2026-07-26）：新增 backend-private `CudaRuntimeDevice : runtime::Device`，以真实 CUDA stream、timeline event、device/upload/readback buffer、mipmapped image、sampler/texture binding、PTX module、pipeline、DAG copy/dispatch/event 和结构化 device-loss error 实现 T.3 合同；preflight 在提交前验证 handle、usage、bounds、grid、timeline 与 event 顺序。T.5 execution graph 现在经 adapter limits lowering 为稳定 CUDA plan，path、Fraunhofer wave、multi-GPU 和静态 `.cu` fast path 都通过 runtime-owned queue/fence 完成提交；wave 资源与传输完全由 runtime device 管理，multi-GPU 拒绝不兼容 schema/node/dispatch contract。CUDA SDK、`USE_CUDA` 和 native structs/diagnostics 已从安装公共面移除，root `UR_ENABLE_CUDA=OFF` 可在不配置 CUDA compiler 的情况下构建、安装并由外部 CMake consumer 使用 `ure_runtime`、`ure_sceneio` 与 `ure_config`。生产 CUDA Device 的 PTX 数值执行、lifetime/budget/timeline/error test 加入门禁；Cornell reference hash 在 64×64×8 和 512×512×64 两档均逐位保持，最终闭环门禁中后者迁移前/后为 11.857/12.611 秒（+6.4%），处于 20% fail-loud 门限内；单次固定时点 VRAM delta 为 1753/1752 MiB，无回退。Release 全量 41/41 CTest、独立 SDK-free public-surface build 与安装包 consumer 通过；权威游标进入 T.7。

T.7 closure（2026-07-26）：新增 SDK-neutral public surface 与 private Vulkan 1.3 `VulkanRuntimeDevice : runtime::Device`，以动态加载的 adapter/device table 实现 compute queue、timeline semaphore、event、buffer/image/sampler、SPIR-V module、typed descriptor、specialization、pipeline cache、submission DAG 和 validation/debug-utils/device-loss 映射。完整提交 preflight 在创建 command pool 前拒绝非法 handle/usage/bounds/grid、重复 timeline、无 dependency 的 event wait 和 descriptor mismatch；native allocation、cache UUID/vendor/device identity、command/descriptor pool retirement 与异常清理均由 device 独占。Vulkan-Headers 1.4.352、Volk 固定 commit 和所有 vendored hash 进入 manifest；Slang 2026.14 从同一 `shaders/shared/portable_semantics.slang` 确定性生成五类 foundation operator。Windows MSVC CUDA-free build、Linux GCC warnings-as-errors build，以及 Windows NVIDIA/Intel 两厂商实际执行均通过 raygen、spectral Mueller/Stokes、wavefront queue compaction、film/AOV、wave reference、uniform/storage/image descriptor、specialization、timeline、cold/warm cache 和 lifetime/budget 门禁；Release 全量 42/42 CTest 通过。Vulkan 尚未宣称完整 scene renderer：adapter 不暴露 `SelfComputeTraversal`，显式完整渲染请求在 T.8 acceleration bridge 前继续 fail-loud；权威游标进入 T.8。

T.8 closure（2026-07-26）：新增 SDK-free acceleration capability/provider、automatic/compute/ray-query selection、明确 compute fallback/reject、indexed-triangle/instance validation、stable aligned ray/hit records、opaque acceleration handle 和 descriptor binding；CUDA 不虚报 native RT capability。Vulkan adapter inventory 按实际 extension/feature chain 记录 ray query/ray tracing pipeline 硬件能力，provider 只公布已经可执行的 compute BVH 与 ray query，compute-only adapter 仍可初始化；native bridge 以预算计费的私有 storage/scratch/input resource 构建单 indexed-triangle BLAS 与 instanced TLAS，覆盖 non-uniform transform、visibility mask、opaque double-sided semantics、input lifetime、build/query synchronization 和异常清理。固定 Slang 2026.14 从同一 T.8 source 确定性生成 real ray-query 与 bounded compute-BVH fallback SPIR-V；独立 CUDA production `world_hit`、Windows NVIDIA native ray query、NVIDIA/Intel compute fallback 和 Linux CUDA-free execution 对 hit distance/type、primitive/instance/material、UV/barycentric、normal、visibility、transform 和 4-pixel framebuffer 达成一致，显式禁止 fallback 时 fail-loud。全量门禁还修复了 CUDA timeline checkpoint 在两次 probe 之间完成时 `wait()` 误报 false 的竞争。正式 SAH/wide/TLAS construction policy、refit/compact/stats/clustered geometry 与 ray-tracing-pipeline production dispatch 仍归 Phase V；Vulkan 完整 SceneIR renderer 尚未 lowering，继续不暴露 `SelfComputeTraversal`。Release 全量 45/45 CTest、Windows/Linux CUDA-free build、确定性 shader hash 和 Phase T 静态审计通过；权威游标进入 T.9。

T.9 closure（2026-07-28）：新增 Windows-only、可独立关闭且公共头保持 SDK-neutral 的 `ure_d3d12`，实现 DXGI adapter/LUID 与 memory-budget inventory、D3D12 buffer/image/sampler resource、typed CBV/SRV/UAV/sampler descriptor heap、compute/copy queue、cross-queue fence/timeline、submission DAG、structured device-loss 和 DRED breadcrumb/page-fault retrieval。固定 Slang 2026.14 继续消费共享 portable semantics 与 T.8 acceleration source，经确定性 HLSL ABI normalization 后由 Windows SDK 10.0.26100.0 DXC 1.8 生成可复现 DXIL、reflection 和独立 debug artifact；root-signature/register lowering 与 descriptor heap 顺序由 runtime pipeline contract 校验。DXR 1.1 provider 构建 bounded triangle BLAS/TLAS 并执行 inline ray query，compute-BVH fallback 与显式 rejection 保留；NVIDIA closure machine 上 foundation spectral/polarization、storage/sampled image、cross-queue fence、compute/native hit metadata、visibility、instance transform 和 framebuffer parity 均通过，未实现的 ray-tracing-pipeline capability 不对外公布。`UR_ENABLE_D3D12=OFF` 且 CUDA disabled 的独立 Vulkan build/execution 证明 D3D12/DXR 缺失不影响其他 backend；完整 D3D12 SceneIR renderer、production acceleration construction/refit/compaction 与 DispatchRays 仍属于 Phase V，当前不作虚假宣称。Release 全量 46/46 CTest、确定性 shader gate、DXR-required gate、no-D3D12 isolation 和 Phase T 静态审计通过；权威游标进入 T.10。

T.10 closure（2026-07-28）：新增 SDK-free `multi_backend` scheduling contract，以 stable backend/adapter/vendor/device、driver/compiler、实际 executable artifact digest、共享 semantic digest、feature set、numeric precision、coherence mode、显存下限和整数 capacity weight 在执行前完成同构/异构 worker negotiation；canonical largest-remainder partition 与稳定 worker ordering 生成无重叠 sample ranges，输入 worker 顺序不影响结果。Backend-native resource cache key 纳入 resource content/layout、backend、vendor/device、driver/compiler 和 executable identity，同时排除同型号同后端 adapter 的实例 UUID，允许安全 cache reuse 而不跨不兼容 lowering。Distributed file v5 保存 compatibility contract、每个 sample/spectral/frame shard 的完整 worker provenance 和 cache key，按 canonical order 合并并拒绝 feature/precision/coherence/semantic/resource mismatch、重复或重叠 coverage、伪造 sample count 与 RGB coherent-field 输出；v4 文件保持只读兼容，legacy 与 versioned contribution 不会静默混合。CUDA private multi-GPU pass 已改用同一 scheduler 生成实际 sample-space assignment，并继续用 execution-graph contract 校验 estimator lowering；显式请求超过可用 adapter 时 fail-loud。closure machine 的实际 inventory 以 CUDA、NVIDIA/Intel Vulkan 和 NVIDIA/Intel D3D12 五个 worker 形成确定性异构 schedule，保留各自真实 driver/compiler/cache identity；独立 SDK-free MSVC `/W4 /WX` build 的 4/4 tests 和 Release 全量 48/48 CTest 通过。相干复振幅 framebuffer merge 仍归 Phase W；跨后端核心物理/统计/性能证据归 T.11，通用 portable SceneIR production renderer 依赖 Phase V 的正式 acceleration stack；权威游标进入 T.11。

T.11 closure（2026-07-28）：新增 `run_phase_t_validation_suite.ps1` 与 `ure.phase_t.validation.v1` 机器可读报告，把 analytic spectral/Stokes/Mueller/wave oracle、CUDA/Vulkan/D3D12 shared hit/visibility/transform/framebuffer fixture、CUDA production reference hash、独立 variance/MSE convergence、durable device-loss contract/native mapping、runtime/resource budget、incremental/pipeline cache、cold/warm launch、实际 adapter memory、VRAM 和 throughput 证据聚合为单一 fail-loud 门禁。每项差异均记录数值或 assertion upper bound、阈值与原因分类；CUDA/Vulkan 必测，Vulkan native ray query 与 optional DXR 只在 capability 已公布时强制执行，否则验证 compute fallback。closure machine 实际执行 CUDA、NVIDIA/Intel Vulkan 和 NVIDIA/Intel D3D12 五个 worker；CUDA 512×512×64 Cornell reference hash 保持 `ff81b8e...e935`，多次闭环测量为 12.5-13.3 秒、1.27-1.34M samples/s，处于 20% regression guard 内，VRAM delta 1752 MiB；统计 convergence、static audit、Release 全量 48/48 CTest 和文档一致性门禁通过。Phase T 冻结的是核心 radiometric spectral/polarimetric operator、resource/AOV/wave、acceleration fixture 与调度合同；通用 Vulkan/D3D12 SceneIR production renderer 仍依赖 Phase V 的正式 acceleration construction/provider，不在此虚报。权威游标进入 V.0。

#### 完成标准

- CUDA 是已验证的生产 backend，而不是公共架构或文件布局的定义者。
- 纯 C++ 的 `ure_types`、SceneIR、native schema、Session contract 和 distributed contract 在没有 CUDA SDK 时可编译和验证。
- CUDA 与 Vulkan 对核心 radiometric spectral/polarimetric path、资源、AOV 和至少一个 wave operator 给出物理/统计 parity；D3D12/DXR 有稳定可选入口。
- Backend capability、精度、subgroup、memory budget、acceleration、wave/coherent 和高级 integrator 支持度在执行前可查询；不支持时 fail-loud，不静默简化。
- 专有优化位于 backend 私有层；CUDA Graph、OptiX、Vulkan RT、DXR 等都不能反向污染 estimator、SceneIR 或 URE native schema。
- Phase V 只能扩展统一 acceleration-provider contract，不得重新建立 CUDA-only `AccelerationScene`；Phase W/K 的新增 GPU 工作必须通过 portable runtime 或明确记录为临时 reference backend。

---

### Phase V — GPU 几何加速结构 / BVH / OptiX / Clustered Geometry

**状态**: 进行中，V.0-V.3 已完成，当前游标 V.4。

**目标**: 将当前 mesh-local BVH 和线性 fallback traversal 升级为适配 UltraRender 光谱、偏振、材质图、动态场景和未来波动光学的 GPU 几何加速栈。Phase V 的核心不是复制外部引擎的可见性系统，而是在 Phase T 的 acceleration-provider contract 上建立自己的 `AccelerationScene`：同一份 SceneIR/resource graph 能选择自研 compute BVH、OptiX、Vulkan RT 或 DXR provider，并在能力不满足时 fail-loud。

**当前审计结论**: 现有生产路径集中在 `libs/ure_core/src/bvh_builder.cpp`、`path_tracer_intersect.cuh` 和 `path_tracer_wavefront.cuh`。mesh BVH 由 host 端中点/`nth_element` 二叉划分构建，leaf 固定少量 triangle，GPU 端使用 32B `GpuBvhNode`、固定 64 栈、per-mesh object-space traversal；没有正式 TLAS/BLAS、wide BVH、SAH/spatial split、compaction/refit policy、动态几何 rebuild budget、build quality preset、backend abstraction 或 OptiX pipeline。`gpu_accelerator.hpp` 里的 OptiX 类型只是 stub，不能作为真实 backend。旧的 `BVHAccelerator`/`SimpleAccelerator` 类属于过渡历史，不应扩展成第二套 host production traversal 系统。

#### Phase V 边界

| 范围 | 属于 Phase V | 不属于 Phase V |
|------|--------------|----------------|
| GPU traversal | CUDA BVH2/BVH4/BVH8 traversal、stack safety、ray sorting compatibility、any-hit/closest-hit parity | host-side production traversal 或第二套非 GPU 渲染路径 |
| Build/update | BLAS/TLAS、SAH/SBVH/LBVH/HLBVH builder、refit/rebuild policy、compaction、async upload、dynamic transform update | 改变 BSDF/phase estimator 或积分器语义 |
| Optional backend | OptiX、Vulkan RT、DXR 的 GAS/BLAS/TLAS backend、capability query、fallback policy、backend parity tests | 强制依赖单一厂商路径，或让任一 native RT API 成为唯一运行路径 |
| Dense geometry | cluster/meshlet resource format、streaming LoD、physical error budget、clustered acceleration backend | 把外部引擎可见性 pass 作为目标、屏幕空间 LOD 替代 path tracing visibility |
| Validation | traversal correctness、shadow/visibility parity、build time、trace throughput、VRAM budget、dynamic update benchmark | denoiser、sampling variance dashboard、wave optics solver |

#### 技术路线

| 层级 | 目标 |
|------|------|
| Acceleration contract | 在 Phase T provider API 上新增 `AccelerationConfig` / `AccelerationScene` / `GeometryBuildStats`，贯穿 `RenderConfig`、JSON、CLI、C ABI、Session/pyure；provider 由 runtime backend 和 capability 共同决定 |
| Self compute backend | 先保留算法可控性：实现 SAH baseline、wide-node layout、stack overflow fail-loud、node/triangle memory compaction、TLAS/BLAS 分离和 instance transform refit；首个实现为 CUDA，Vulkan compute 复用同一 provider 语义 |
| Native RT backend | 可选支持 OptiX、Vulkan RT、DXR build/compaction/refit；不支持 spectral/polarization/material graph 的路径必须 fail-loud 或回退兼容 compute provider；输出 traversal parity 和 build stats |
| Clustered geometry | 设计 UltraRender 自己的 cluster/meshlet resource：按 material/spectral resource/displacement/opacity/normal-field 边界切分，用 ray/path physical error 而不是单纯屏幕误差控制 LoD |
| Dynamic scenes | 对 rigid transform、deformation、topology change、resource streaming 分别定义 refit/rebuild/recluster 策略，并与 Phase P 的 retained scene 和 hot-update 管线对齐 |
| Benchmark contract | 固定小 mesh、大 mesh、高 instance、动态 transform、dense displacement proxy、shadow-heavy、reflection-heavy、spectral material 场景，记录 build ms、trace Mray/s、VRAM、stack spill、backend parity |

#### 子步骤

| Step | 内容 | 完成判据 |
|------|------|----------|
| V.0 | BVH/geometry acceleration audit：列出现有 mesh-local BVH、linear fallback、OptiX stub、fixed stack、无 TLAS/BLAS 等风险；新增 Phase V 文档和静态审计入口 | PLAN/README/docs 口径一致；`rg` 静态审计阻断 host traversal production path 扩展和 OptiX stub 被误称 production |
| V.1 | `AccelerationConfig` API：新增 provider (`auto`, `self_compute`, `optix`, `vulkan_rt`, `dxr`)、quality (`fast_build`, `balanced`, `high_quality`)、refit/rebuild policy、clustered geometry gate、stats gate | JSON/CLI/C ABI/pyure parity；provider/backend 组合不支持时 fail-loud；默认行为保持当前 CUDA self-compute path |
| V.2 | 自研 compute BVH baseline cleanup：以 CUDA reference 实现修复 builder 注释/索引语义、栈溢出处理、AABB/triangle robust tests、linear fallback policy；暴露 build/traversal stats | GPU tests 覆盖 mesh BVH/linear parity、shadow any-hit parity、stack overflow fail-loud；数据布局不含 CUDA handle |
| V.3 | TLAS/BLAS split：mesh BLAS 与 instance TLAS 分离，instance transform hot update 只 refit/update TLAS，不重复上传静态 mesh BVH | `test_instance_hotupdate` 增加 TLAS update gate；多实例场景 build time 和 VRAM stats 可见 |
| V.4 | SAH/SBVH/BVH4/BVH8 builder：实现质量 preset，支持 compact node layout 与 traversal benchmark | 固定大 mesh benchmark 比当前 median BVH 有明确 traversal/build 指标；结果与 reference traversal 一致 |
| V.5 | Async build/upload/compaction：把 build stats、temporary memory、compact memory、upload time 纳入 telemetry；大场景超预算 fail-loud | validation suite 记录 build/trace/VRAM；超出 `AccelerationConfig` budget 时拒绝而非 OOM |
| V.6 | Native RT provider productionization：在 Phase T bridge 上完成 OptiX、Vulkan RT、DXR 的 build/compaction/refit、scratch budget、update policy 与 compute fallback | 缺少任一 SDK/capability 不影响其他 backend 构建；显式不兼容 provider 请求失败；可用 provider build tests 通过 |
| V.7 | Cross-provider parity：shadow rays、closest-hit rays、instance transforms、material index、UV/normal/tangent interpolation 在 self-compute/OptiX/Vulkan RT/DXR 间对齐 | 同一 SceneIR 下可用 provider 的 framebuffer、AOV、hit metadata 在阈值内一致；报告 provider/compiler/driver identity |
| V.8 | Clustered geometry resource：定义 cluster/meshlet resource、cluster bounds、material/resource boundary、streaming residency 和 LoD error metric | host/gpu resource tests 覆盖 cluster residency、material boundary、invalid cluster fail-loud |
| V.9 | Physical error LoD：按 ray differential/path class/material/displacement/spectral resource 选择 cluster LoD，避免 shadow/reflection/caustic 使用错误低精代理 | shadow/reflection-heavy benchmark 证明 visibility 不被预览 LoD 静默破坏 |
| V.10 | Dynamic/deforming geometry：按 rigid/deforming/topology-change 分类执行 TLAS refit、BLAS refit/rebuild 或 recluster；与 SceneDiff 资源变更对齐 | dynamic benchmark 输出 update ms 与 correctness gate；unsupported topology path fail-loud |
| V.11 | Phase V validation suite：整合 build time、trace throughput、VRAM、backend parity、dynamic update、dense geometry 和 distributed shard metadata | `run_phase_v_validation_suite.ps1` 本地可跑；farm 长跑入口和 JSON schema 稳定 |

V.0 closure（2026-07-28）：新增 `docs/Phase_V_GPU_Acceleration.md`，以十项审计台账冻结当前生产路径和迁移 owner：CUDA scene upload 为每个 mesh 构建 midpoint/median fallback binary BVH、4-triangle leaf 和 32-byte preorder node；active closest/shadow production traversal 均通过 `world_hit` 使用固定 `int stack[64]`，现有 push guard 在只剩一个 slot 时仍可能越界且深树会静默丢 work；一份未被 production 调用的重复 `any_hit` 省略了 instance list，容易造成审计歧义或未来误用；缺少 BVH 时会静默 O(N) triangle scan；transform hot update 没有 top-level refit。递归 host `BVHAccelerator`、`SimpleAccelerator`、Embree placeholder 和两份 installed `OptixAccelerator` miss/no-occlusion stub 均无 production consumer，并由 `check_phase_v_static.ps1` 的 consumer allowlist 与 file hash 冻结，禁止扩展为第二套 host traversal 或误称 production。Phase T SDK-free provider 是唯一前向边界；V.1-V.11 分别接管 config/stats、correctness、TLAS/BLAS、quality/wide build、async/compaction、native providers、parity、cluster/LoD/dynamic 和 validation。文档一致性、Phase T/V 静态审计和 Release 48/48 CTest 通过；权威游标进入 V.1。

V.1 closure（2026-07-28）：新增 backend-neutral `AccelerationConfig`，以独立 provider（`auto`/`self_compute`/`optix`/`vulkan_rt`/`dxr`）、quality（`auto`/`fast_build`/`balanced`/`high_quality`）、update policy（`auto`/`static`/`refit`/`rebuild`）、cluster gate、stats gate 与 scratch budget 表达完整 Phase V 施工词汇。JSON/CLI/C++ `RenderConfig` 已贯通；C ABI 使用新的 `ure_acceleration_config_t` 与 engine/session execution-config 入口，未扩展旧 backend struct，因此保留旧调用者布局安全；pyure 暴露对应 typed enums 和参数。当前默认和显式 CUDA `self_compute` 保持原路径，quality 仅 `auto`、update 仅 auto/static 可执行；尚未实现的 native provider、quality、refit/rebuild、cluster、stats 和 scratch request 均在创建 renderer/session 时 fail-loud，不会静默降级或把 Phase T bounded fixture 冒充 production provider。host JSON/CLI、CUDA selection、C ABI 与 pyure rejection/compatibility tests 已覆盖；Release build、48/48 CTest、Phase T/V 静态审计和文档一致性通过；权威游标进入 V.2。

V.2 closure（2026-07-28）：CUDA reference builder 现在验证 packed vertex/index shape、有限坐标、索引范围与 device triangle count，明确 leaf offset 是重排 index buffer 中的 triangle ordinal，并汇总 mesh/triangle/node/leaf/max-depth build stats；超过 traversal stack contract 的树在上传前拒绝。active closest 与 production shadow 统一使用 checked `world_hit`/`hit_bvh`，在解引用前验证 node/child/leaf range，在 push 前预留两个 stack slots，并以 typed `StackOverflow` 或 `InvalidAcceleration` 终止 pass；缺失 BVH 的 O(N) triangle fallback 与未使用的重复 `any_hit` 已删除。AABB zero-direction slab、invalid bounds、tiny valid/degenerate triangle、transformed instance closest/shadow parity、manual deep tree 与 missing acceleration 均有 GPU gate。`AccelerationStats` 已贯穿 C++、C ABI 与 pyure；node/triangle 原子计数只在显式开启 stats 时执行，overflow/invalid 检测始终启用。Cornell small/large reference hash 逐位保持，最终 closure large render 为 13,538.26 ms、VRAM delta 1752 MiB；Release 48/48 CTest、Phase T/V static、SDK-free package consumer 和 T.6 CUDA backend gate 通过。T.6 SDK-free configure 显式关闭 optional D3D12，避免 install gate 依赖未构建 target；权威游标进入 V.3。

V.3 closure（2026-07-28）：CUDA self-compute 现在为静态 mesh 保留 object-space BLAS，并以 `InstanceTlasBuilder` 对经过 finite affine matrix/inverse consistency 校验、由 referenced BLAS bounds 八角变换导出 conservative world bounds 的 instance 构建独立 world-space binary TLAS，caller bounds 不再能造成 false-negative culling；scene compiler 同时在生成 paired forward/inverse matrix 前规范化 authoring quaternion，修复 non-unit quaternion 导致的 inverse inconsistency。leaf 通过 stable instance-index permutation 保留 public instance identity。active closest 与 production shadow 共用 checked TLAS traversal 后才进入对应 mesh BLAS，旧的 instance linear scan 已删除。transform hot update 在 host construction path 保留 topology refit TLAS，仅上传 transform array 与 TLAS nodes；device BLAS allocation 和 TLAS allocation 均保持不变。`auto`/`refit` 执行该路径，`static` mutation 与尚未实现的 `rebuild` fail-loud。`AccelerationStats` 新增 BLAS/TLAS bytes、TLAS node/leaf/depth、build/update nanoseconds、update count 与 closest/shadow TLAS visits；C ABI 以 `ure_acceleration_stats_v2_t`/versioned getters 保留 V.2 output layout，pyure 消费 v2。GPU gates 覆盖 9-instance multi-level TLAS、transformed closest/shadow parity、stable instance identity、topology-preserving refit、derived root bounds、quaternion normalization、static/inconsistent-inverse rejection 和 BLAS pointer stability；multi-instance actual context 同时验证 build/update time 与 resident bytes。Cornell reference hash 逐位保持，最终 closure large render 为 13,503.49 ms、VRAM delta 1747 MiB；Release 48/48 CTest、Phase T/V static、documentation、SDK-free package consumer 和 T.6 CUDA backend gate 通过；权威游标进入 V.4。

#### 完成标准

- 当前兼容默认仍是 CUDA self-compute GPU path，不引入第二套 host production traversal backend。
- SceneIR 到 acceleration provider 有统一合同，self-compute/OptiX/Vulkan RT/DXR/clustered geometry 不分裂材质、光谱、偏振或 instance 语义。
- TLAS/BLAS、refit/rebuild、compaction、build quality preset 和 memory budget 都有显式配置与 fail-loud。
- OptiX、Vulkan RT、DXR 都是可选 native RT provider：无 SDK/无能力时其他 backend 照常构建，启用但能力不足时报错或按配置回退兼容 compute provider。
- Dense/clustered geometry 的 LoD 由物理路径误差和资源边界约束，不用屏幕可见性近似破坏 shadow/reflection/caustic/wave paths。
- Validation suite 固定输出 build ms、trace throughput、VRAM、stack/overflow、backend parity 和 dynamic update 指标。

---

### Phase W — 波动光学求解器 / Wave Optics Solver

**状态**: 进行中。

**目标**: 将 UltraRender 从“高级光谱偏振路径追踪器 + 局部边界波动效应”扩展为具备可选波动光学求解器的渲染系统。Phase W 不等价于全局 Maxwell/FDTD；宏观场景仍默认使用 radiometric spectral path tracing，波动能力通过显式 feature switch opt-in，并在 unsupported solver/film/merge/material/API path 上 fail-loud。

**核心审计结论**: 当前 `RayQueue` 传输的是 spectral throughput + wavelength PDF + Stokes (`I,Q,U,V`)。`path_tracer_boundary.cuh` 中的 `ComplexF` 只用于局部 Fresnel/thin-film amplitude，再折算为 power/Mueller 作用到 Stokes。系统没有 Jones/complex field path state、optical path length、coherence metadata、wave propagation operator、coherent film 或 coherent distributed merge contract。因此它能处理局部 Fresnel/TIR/thin-film phase 对功率和偏振的影响，但不能普遍处理双缝、多路径干涉、散斑、衍射图样、全息、波前重构或长距离相位积累。详见 `docs/Phase_W_Wave_Optics_Audit.md`。

**不能被 Phase W 掩盖的当前物理债**: 波动求解器不是现有 radiometric path 的遮羞布。W.0 已修复 rough dielectric direct-light MIS 使用与实际 BSDF 不一致 PDF 的问题：rough dielectric `eval_bsdf()`、scatter continuation PDF 和 direct-light MIS 现在共同消费 wavelength、UV effective thin-film thickness 与同一 dispersion clamp，并以 per-channel material PDF 计算 MIS。其他已记录边界包括 packet-average boundary event、single-layer scalar-IOR thin-film、empirical dielectric dispersion、metal artistic fallback 和 rough-surface coherent scattering 缺失。

#### Phase W 物理边界

| 模式 | 默认 | 输运对象 | 用途 |
|------|------|----------|------|
| Radiometric | ✅ | spectral radiance + Stokes | 当前默认路径；宏观非相干照明、普通材质、体积 radiative transfer |
| Camera diffraction | ❌ | exit-pupil complex field / wavelength PSF | 衍射极限相机、Airy disk、aperture blades、PSF/OTF/MTF、sensor aperture integration |
| Coherent field | ❌ | Jones/complex spectrum + optical path length | 激光、多路径干涉、相干薄膜、相位元件、coherent sensor |
| Partial coherence | ❌ | mutual intensity / cross-spectral density / Wigner or generalized ray | 扩展光源、部分相干成像、散斑、OCT/lidar/interferometry |
| Local full-wave coupling | ❌ | S-matrix / scattering operator tables | RCWA/FDTD/FEM/BEM/FMM 等局部求解器和宏观路径系统耦合 |

#### 开关与 fail-loud 合同

所有高级波动特性必须由未来 `WaveOpticsConfig` 控制，并一致暴露到 `ure::RenderConfig`、JSON、CLI、C ABI、Session API、`pyure` 和 distributed shard metadata。材质或场景可以声明需要某能力，但是否启用由 render config 决定。

| Switch | Default | Required behavior |
|--------|---------|-------------------|
| `wave_optics.mode` (`radiometric`, `camera_diffraction`, `coherent_field`, `partial_coherence`) | `radiometric` | 非默认模式必须在 scene load 或 session create 前验证 solver/film/API/merge 支持 |
| `wave_optics.camera_diffraction.enabled` | false | 关闭时相机只能使用 geometric pinhole/thin-lens；请求 diffraction camera 必须报错 |
| `wave_optics.coherent_field.enabled` | false | 关闭时依赖 Jones/complex field、OPL 或 coherent accumulation 的节点必须报错 |
| `wave_optics.partial_coherence.enabled` | false | 关闭时 mutual-intensity/Wigner/generalized-ray 特性必须报错 |
| `wave_optics.diffractive_materials.enabled` | false | 关闭时 grating/DOE/phase mask/RCWA table 节点必须报错 |
| `wave_optics.fluorescence.enabled` | false | 关闭时 fluorescence/phosphorescence excitation-to-emission matrix 必须 fail-loud，除非显式 preview degradation |
| `wave_optics.specular_manifold.enabled` | false | 关闭时不得恢复 straight-through dielectric shadow；glass direct lighting 继续 blocker policy |
| `wave_optics.local_fullwave.enabled` | false | 关闭时 FDTD/FEM/RCWA local solver node 必须报错 |
| `wave_optics.experimental_allow_preview_degradation` | false | 只有显式启用时，unsupported wave node 才允许降级为非物理预览；默认必须 fail-loud |

#### 子步骤

| Step | 内容 | 前置依赖 |
|------|------|----------|
| W.0 | 审计与口径收敛：记录当前局部边界波动效应、缺失相干场状态、缺失自由空间传播、缺失 coherent film/merge contract；更新 README/PLAN/docs，避免把 Phase L 的 million-domain spectral resources 误称为 wave optics；修复或显式阻断当前 radiometric correctness debt（rough dielectric spectral/UV thin-film PDF mismatch 等） | 已完成：`docs/Phase_W_Wave_Optics_Audit.md` 已建立审计；README/PLAN 已明确默认 renderer 不是通用波动传播器；rough dielectric spectral/UV thin-film PDF mismatch 已修复，`gpu_test_spectral_soa` 739/0 与 `gpu_test_render` 338/0 通过 |
| W.1 | 配置/API 骨架：新增 `WaveOpticsConfig`，贯穿 `ure::RenderConfig`、JSON、CLI、C ABI、Session API、pyure；默认全关；unsupported feature 在 scene load/session create 前 fail-loud | 已完成：JSON/CLI/C ABI/pyure 已覆盖；未知 mode fail-loud；非 radiometric solver/feature 在实现前拒绝创建或运行；`test_config` 29/0、`test_session` 163/0、pyure smoke 与 CLI invalid-mode gate 通过。Distributed coherent shard metadata 留到 W.11 |
| W.2 | 衍射相机 solver：建立 `WaveField` / pupil function / wavelength PSF / Fresnel or angular-spectrum propagation；输出 Airy disk、aperture blade diffraction、defocus phase、sensor aperture integration；不侵入普通 material path | 进行中：已新增 `ure::wave::WaveFieldGrid` host complex field carrier、`FraunhoferFieldGrid`、direct Fraunhofer/Fresnel/angular-spectrum CPU propagation oracle、`CircularAperture` Airy PSF host oracle、离散 `PsfKernel` reference、圆孔 diffraction-limited MTF oracle、`CircularPupil` defocus phase reference 和 `DiffractionCameraPlan` feature-gated plan boundary，覆盖中心归一化、第一暗环、sensor 半径、encircled energy、波长缩放、径向对称、kernel 归一化、cutoff frequency、MTF 单调性、pupil aperture mask、defocus edge phase、复场网格采样/总功率、Fraunhofer DFT 归一化、Fresnel 点场尺度、angular-spectrum z=0 重建/常量场强度守恒、默认关闭、半开拒绝和 invalid fail-closed；direct `GpuRenderEngine` scene load 也会在 GPU 初始化前拒绝未实现 camera diffraction film，避免绕过 CLI/Session gate 静默按 radiometric path 渲染；`test_wave_optics` 685/0 通过。GPU diffraction camera/film 接入仍待做 |
| W.3 | Coherent field state：定义 `ComplexSpectrum`、Jones field、OPL/phase accumulation、coherence group/source coherence metadata、coherent realization id；新增 `ComplexFieldFilm` 和 coherent accumulation order | 已完成基础合约闭环：`ComplexSpectrum` / `JonesSpectrum` / `CoherenceMetadata` / OPL phase helpers / `ComplexFieldFilm` 已进入 `ure::wave` host oracle 层；测试覆盖 OPL 相位推进、Jones 分量功率保持、同相两束 `|sum E|^2 = 4`、反相抵消为 0、incoherent `sum |E|^2 = 2` 与 out-of-range/invalid fail-closed。主 GPU path tracer 的 coherent field transport 和 distributed coherent merge 仍分别留给后续 W.7/W.11 |
| W.4 | Wave propagation operators：统一 `PropagationOperator` 接口，支持 Fraunhofer、Fresnel、angular spectrum、Rayleigh-Sommerfeld/Huygens-Fresnel 的 CPU oracle 与首个 GPU backend | 已完成本阶段闭环：已新增 `PropagationOperatorKind` / `PropagationConfig` / `PropagationResult` 统一 dispatch，Fraunhofer/Fresnel/angular-spectrum/Rayleigh-Sommerfeld/Huygens-Fresnel CPU oracle 通过同一接口返回 explicit field/far-field carrier；首个 CUDA backend 为 Fraunhofer direct DFT reference backend，`gpu_test_wave_optics` 覆盖 uniform aperture、CPU/GPU reference match 和 invalid fail-closed。后续性能型 FFT/tiling backend 进入 W.4 后续优化，不阻塞本阶段接口闭环 |
| W.5 | Diffractive material operators：MaterialGraph 新增 grating、phase mask、zone plate、DOE、RCWA/FMM scattering table 节点；返回 diffraction orders、complex amplitude、polarization response、propagating/evanescent classification | Phase M.2/M.3 + W.1 |
| W.6 | Fluorescence/phosphorescence：新增 excitation wavelength → emission distribution resource，路径状态处理 wavelength shift、energy conservation、PDF conversion 和 spectral budget；关闭时遇到 fluorescence node fail-loud | Phase L resource graph + W.1 |
| W.7 | Partial coherence/generalized transport：引入 coherence length、mutual intensity/cross-spectral density 或 Wigner/generalized-ray 表达；支持 extended coherent source、speckle、OCT/lidar/interferometry 的 coherent/incoherent averaging order | W.3 + distributed merge update |
| W.8 | Edge/aperture diffraction：先实现 knife-edge/slit/circular/rectangular aperture analytic references，再评估 GTD/UTD/Keller cone 或 generalized region transport | 已完成解析 reference 层：圆孔由 W.2 Airy/MTF oracle 覆盖；本阶段新增 knife-edge Fresnel half-plane、single-slit sinc^2、rectangular aperture separable sinc^2、finite grating order/grating equation + single-slit envelope references，并测试第一零点、对称性、矩形孔径乘积分离、传播/evanescent order 分类和非法几何 fail-closed。GTD/UTD/Keller cone 与生产 visibility/edge event 接入仍是后续集成，不作为本 reference 阶段闭环 |
| W.9 | Anisotropic/modal media：为 birefringence、optical activity、dichroism、liquid crystal、stress birefringence 建立 tensor/material modal transport，而不是继续扩展 scalar dielectric `ior` | W.3 |
| W.10 | Local full-wave coupling：定义 local solver plugin/cache contract，消费 RCWA/FDTD/FEM/BEM/FMM/DDA/S-matrix/scattering table，不做 scene-scale global Maxwell discretization | W.5 + Phase X |
| W.11 | Coherent distributed contract：扩展 distributed shard metadata，区分 radiance frame、complex field frame、mutual intensity frame、coherent realization merge；禁止把 coherent frame 当 RGB radiance 直接加和 | W.3 + Phase C/D |
| W.12 | 验证和静态审计：Airy first zero、slit/grating diffraction angles、two-beam interference、thin-film phase oracle、rough dielectric spectral/UV PDF consistency、Stokes/Jones conversion、fluorescence Stokes shift、energy/PDF conservation、coherent merge order、unsupported wave node fail-loud、config/API parity | W.1-W.11 |

#### 完成标准

- 默认 radiometric renderer 行为不变；所有 wave modes 都必须通过显式开关启用。
- Camera diffraction 有解析 Airy/PSF/MTF oracle 和可视化 smoke。
- Coherent field path 能在受控场景中复现两束干涉，并证明 `|sum E|^2` 与 `sum |E|^2` 的 accumulator 顺序差异。
- Diffractive material operator 能产生符合 grating equation 的 wavelength/order lane，并通过能量守恒和 PDF 门禁。
- Fluorescence path 能处理 excitation-to-emission wavelength shift，且关闭时 fail-loud。
- Distributed merge contract 区分 radiance、complex field、mutual intensity，不允许 coherent result 被普通 framebuffer merge 静默破坏。
- Static audit 阻断：wave switch 只在 CLI 生效、unsupported wave node 被 RGB/packet flatten、未启用 experimental path 却运行相干/衍射代码、coherent field 被直接累加到 RGB framebuffer。

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
| M.2 | GPU 编译：节点图 → GpuMaterial + 内核参数 | ✅ `GpuSceneCompiler` 已支持单 OutputSurface → 单 BSDF，并在 Phase L.9 接入 MaterialGraph expression graph；ConstantColor/ConstantFloat、Texture2D、Add、Multiply、Mix、Checker2D、Noise2D 可作为 typed resource expression 在 device 端按 wavelength/UV 评估，不再退回 32-lane packet flatten。Metal eta/k、dielectric IOR、layer absorption 已进入 OpticalConstant typed expression slot；Graph 输出为 authoritative，旧 scalar texture fields 不参与 graph material GPU 编译。`BsdfMix` 已收口为 unbiased opaque Lambert/Metal lobe mixture 并拒绝 dielectric；`BsdfLayer` 已新增 finite-thickness dielectric coating over opaque Lambert substrate，包含 top-interface Fresnel、Beer-Lambert absorption、substrate eval/pdf/sample 和 fail-loud unsupported boundary。旧文本场景、Scene parser shim、SceneIR→Scene compiler、procedural CLI fallback、IRenderEngine Scene overload 与旧视觉 smoke 资产均已移除；raw in-memory texture mutation 直接失败，后续资源变更必须走 graph/resource 节点 |
| M.3 | MaterialX 导入（`mtlx` → 节点图 IR） | ✅ `ure_sceneio` MaterialX adapter 已导入 URE custom MaterialGraph XML subset，并支持 `standard_surface`/`dielectric_bsdf` 基础映射；导入结果直接进入 `GpuSceneCompiler`，未知或不可保真的节点 fail-loud |
| M.4 | MaterialX 导出（节点图 IR → `mtlx`） | ✅ `export_materialx_graph()` 输出 URE custom MaterialGraph MaterialX XML，覆盖 Constant/Texture/value ops/BSDF/Mix/Layer/OutputSurface；MaterialX 是 adapter，不成为 UltraRender 权威 schema |
| M.5 | 材质预设库（金属/玻璃/皮肤/织物/汽车漆） | ✅ `ure::scene_ir::make_material_preset()` 已输出 MaterialGraph 级 gold/copper/aluminum、clear/diamond glass、woven fabric、automotive paint 和 skin participating dielectric medium preset，并通过 `GpuSceneCompiler` 覆盖；skin 不走 Lambert fallback，真实多层皮肤/BSSRDF 仍作为后续材质物理增强边界 |

**前置条件**: Batch 4 BSDF 测试 (OT2, OT3) 必须在此步骤前通过，确保现有 BSDF 行为基线锁定。

**关键决策**: 不实现 OSL 编译器。OSL 在 GPU 上的支持极其复杂（需要 LLVM/SPIR-V 编译），维护成本远高于收益。我们的自定义节点图覆盖所有所需 BSDF 组合，且可直接编译为 CUDA 内核参数。

---

### Phase Q — URE 原生场景系统 / Procedural Industrial Scene Format

**状态**: ✅ 完成。Q.0-Q.12 已闭环；权威施工游标已推进到 R-P3。

**目标**: 建立 UltraRender 自己的第一等场景格式和程序化 authoring contract。`.ure` / `.urescene` / `.urepkg` 是权威格式；SceneIR 是编译后的内部 IR；glTF、USD、MaterialX、EXR、SPD 等只能作为导入/导出 adapter 或资源交换格式，不能限制 UltraRender 的核心能力。Phase Q 必须覆盖当前和规划中的场景、光谱、材质、介质、体积、光源、积分器、几何加速、波动光学、物理、声学、视频流、分布式和脚本化能力，并为未来物理/声学模型改变保留 schema migration 与开放 extension slot。

#### 核心原则

- **URE native first**：外部格式兼容我们；不是我们削弱能力去兼容外部格式。
- **Authoring 与 runtime 分离**：`.urescene` / `.urepkg` 是可迁移、可索引的二进制 production source；`.ure` 是 lossless canonical text projection；SceneIR/RuntimeIR 负责编译后执行。
- **程序化但可复现**：procedural graph 是默认安全路径；脚本只能作为显式 build step 生成确定性 SceneIR/resource/cache，不允许在 GPU 核心路径动态解释。
- **能力声明先于执行**：场景必须声明 `requires` / `optional` feature set；renderer 在 scene compile 或 session create 前验证支持度，unsupported feature 必须 fail-loud。
- **版本化与迁移内建**：format version、schema version、physics/acoustic solver version、resource hash、migration policy 和 deprecation window 是格式的一部分。
- **接口开放但边界明确**：物理、声学、波动、积分器、插件、视频流和程序化系统保留 typed extension slot；未知 required extension 失败，未知 optional extension 保留 metadata 并警告。
- **二进制 cache 不是权威源**：compiled cache 可加速 load/farm 分发，但必须可由 source manifest 重建，并用 source hash / compiler hash 验证。

#### 格式定位

| 层 | 建议扩展名 | 职责 |
|----|------------|------|
| Project/package | `.urepkg` | indexed binary 可搬运工程包；包含 manifest、source、resources、cache、validation output 和 provenance |
| Scene source | `.urescene` | indexed binary production source；metadata 使用 versioned FlatBuffers，large payload 保持独立 chunk |
| Text projection | `.ure` | canonical UTF-8 JSON；用于 review、source control、migration diagnostics 和 exploded project manifest，不内联 large typed arrays/Base64 |
| Binary cache | `.urecache` | SceneIR/RuntimeIR、GPU upload plan、acceleration metadata、spectral/resource tile cache；非权威，可删除重建 |
| Resource bundle | package 内 typed resources | geometry、spectral tables、medium fields、Mie tables、complex-field assets、audio/physics/video resources |
| Adapter output | `.gltf/.usd/.mtlx/...` | 外部互通导出；能力不完整时必须记录 loss report 或直接拒绝 |

#### 原生 schema 域

| Domain | 必须覆盖的原生语义 |
|--------|-------------------|
| Scene graph | entity hierarchy、instance、transform animation、visibility/category mask、AOV tags、mutation scope |
| Geometry | mesh、curve、sphere/analytic primitives、volumes、clustered geometry、BLAS/TLAS hints、dynamic/refit policy、future deformation stream |
| Material | MaterialGraph 全量节点、BSDF layering、spectral IOR/eta/k、procedural nodes、texture/resource expression、preset provenance |
| Spectral resources | domain bins、packet lanes、sampled/basis/tiled resources、emission/albedo/n/k/sigma tables、budget/cache policy |
| Lights | sphere、mesh/area、environment、procedural/emissive field、light tree resource、selection/PDF metadata |
| Medium/volume | homogeneous/heterogeneous medium、HG/Rayleigh/Mie resources、phase eval/pdf/sample contract、spectral sigma resources |
| Integrator | mode、sampler、path guiding/ReSTIR/BDPT/VCM/MLT requirements、bias declaration、validation scene tags |
| Wave optics | camera diffraction、coherent/partial-coherent field、OPL/Jones/complex spectrum、apertures、gratings/DOE、local full-wave tables |
| Physics | rigid/soft/fluid placeholder schema、collision resources、time step, coupling channels、solver-versioned extension slot |
| Acoustic | materials、emitters/listeners、room/geometry coupling、modal/acoustic ray resources、solver-versioned extension slot |
| Animation/video | camera paths、transform tracks、spectral/video texture stream、time sampling、frame-rate and shutter contract |
| Procedural | typed procedural graph、scatter/instancing/generator nodes、parameter domains、deterministic seed, build cache key |
| Scripting | explicit opt-in build scripts, sandbox policy、inputs/outputs、version lock、cache/provenance hash |
| Distributed/farm | sample/shard policy、spectral shard metadata、coherent merge mode、resource locality、checkpoint/resume contract |
| Validation | required references、metric set、tolerance、benchmark tags、expected fail-loud boundaries |

#### 程序化描述模型

程序化能力分三层，默认只启用前两层：

| 层 | 默认 | 用途 | 约束 |
|----|------|------|------|
| Declarative graph | ✅ | scatter、instancing、spectrum generator、light rig、volume field、camera path、batch variation | typed node + typed output；必须可静态验证和 deterministic rebuild |
| Native procedural plugin | ❌ | 高性能专用 generator 或 solver-side resource producer | 通过 Phase X ABI 注册；必须声明 input/output schema、version 和 capability |
| Script build step | ❌ | Python/Lua/WASM 等生成 `.urescene` fragment、SceneIR 或 resource cache | 显式开启；sandbox；锁定解释器/依赖；输出必须 hash；运行时 GPU kernel 不解释脚本 |

#### Adapter 策略

| 外部格式 | 地位 | 规则 |
|----------|------|------|
| glTF/GLB | asset importer/exporter | 只承载通用 geometry/PBR/texture/animation；不能作为高级场景主语言；无法表达的 URE feature 导出时 loss report 或 fail-loud |
| USD/Hydra | DCC/viewport ecosystem adapter | U.1 schema 必须映射到 URE native schema；USD 不是权威 format；Hydra 委托消费 SceneIR/session |
| MaterialX | material graph adapter | MaterialX import/export 映射到 URE MaterialGraph；不能限制 URE 原生 BSDF/wave/material resource 节点 |
| EXR/HDR/SPD/table | resource exchange | 可作为资源 payload；resource semantics 由 URE schema 定义 |
| Legacy `.scene` | 不支持 | 不恢复、不兼容；需要转换时走一次性 migration tool 输出 URE native |

#### 子步骤

| Step | 内容 | 完成判据 |
|------|------|----------|
| Q.0 | ✅ Audit：`docs/Phase_Q_Native_Scene_Format.md` 已覆盖 SceneIR、RenderConfig、WaveOpticsConfig、IntegratorRuntimeConfig、MaterialGraph、Mie/spectral resources、SceneDiff 和 distributed metadata；每个语义均有 native typed owner 或 versioned extension slot | Phase Q static gate 锁定 owner/domain 覆盖与 backend-neutral header |
| Q.1 | ✅ 格式身份与 package layout：`.urescene` binary production scene、`.ure` canonical text projection、`.urepkg` binary package、`.urecache` non-authoritative cache；relative/content URI、SHA-256、128-byte header、checked 64-bit chunk directory 已实现 | empty、single-scene、shared-resource fixtures 可 validate；cache 删除不改变 package semantic hash |
| Q.2 | ✅ Core schema/versioning：scene/package ID、container/schema version、canonical conventions、required/optional/advisory feature、opaque extension、migration metadata、FlatBuffers 25.12.19 schema/baseline 和 structured diagnostics 已实现 | unknown required fail-loud；unknown optional bytes 保留；major migration gate、minor compatibility、path/hash/dependency/budget/overflow/alignment/overlap validation 已进入 host tests |
| Q.3 | ✅ Native SceneIR serialization：`URIG` graph + content-addressed `URMS` mesh / `URMI` Mie typed chunks，完整覆盖当前 SceneIR/MaterialGraph/image/texture/medium/light/camera/physics/instance 字段；稳定 source ID、deep freeze、canonical exploded `.ure`、原子文件 I/O、可选 chunk 保留和 fail-loud validation 已实现 | binary/text semantic hash 等价、bit-preserving roundtrip、完整 fixture 经 `GpuSceneCompiler` 验证；28/28 CTest 与 Q/L/R/physics-optics gates 通过，未借用 glTF 字段作为权威语义 |
| Q.4 | ✅ 程序化 graph：`URPG` typed DAG、显式 parameter domain、SHA-256 counter seed、source/cache/output identity、scatter/instancing、blackbody/Gaussian spectrum、ring/grid/three-point light rig、transactional fragment composition 已实现；保留 core chunk 16，使用 kind 17 | binary/text graph 可生成 validated SceneIR fragment；同 seed/source/evaluator fingerprint 输出稳定；retained fixture 通过 `GpuSceneCompiler`；29/29 CTest 与 Q/L/R/physics-optics gates 通过；非法 graph fail-loud |
| Q.5 | ✅ Script build hook：`URSB` typed manifest、显式 opt-in host-neutral coordinator、外部 attestable sandbox runner、exact runtime/dependency lock、virtual I/O、硬预算、provenance/output hash 和完整 cache invalidation 已实现；协调器不直接启动解释器/进程并把 runner 输出视为不可信 | 默认禁用且 disabled 路径不调用 runner；启用后只经 build API 执行；ambient filesystem/environment/network/subprocess/time/entropy policy fail-loud；输出重哈希、attestation/声明/大小完整复核；30/30 CTest 与 Q/L/R/physics-optics gates 通过 |
| Q.6 | ✅ 光谱/材质/介质资源：`URRC` backend-neutral catalog 与 core chunk 19 已纳入 `.urescene/.ure`；typed spectral semantic/representation/domain、RGB/source-spectral texture、MaterialGraph owner、medium sigma/phase/Mie、spectral video、residency/budget/dependency contract 均已实现；复用 `URIG` graph 与 `URMI` payload，不复制权威数据 | `domain_bins` 是 source identity，`packet_lanes_hint` 不进入 semantic hash；sampled/basis/tile/source-grid、百万 bin metadata、Mie kind/domain coverage、video index、cycle/budget/enum/feature fail-loud、binary/text roundtrip 与 retained scene archive 已覆盖；31/31 CTest 和 Q/L/R/physics-optics gates 通过 |
| Q.7 | ✅ 求解器/积分器 contract：`URSC` + core chunk 20 覆盖 wavefront/guiding/ReSTIR/specular-manifold/MLT 与 reserved BDPT/VCM、sampler/quality/bias、spectral runtime、wave optics、backend、acceleration、coherent merge 和 validation metric；required `ure.render.solver` feature 与 capability registry 先于 `RenderConfig` mapping | 当前 supported mode bit-preserving 进入 `RenderConfig`；BDPT/VCM、unsupported backend/acceleration/wave/metric、隐式 biased reuse、coherent→radiometric degradation 均 fail-loud；execution hints 不进入 semantic hash；binary/text archive roundtrip 与 32/32 CTest、Q/L/R/physics-optics gates 通过 |
| Q.8 | ✅ 物理/声学开放 schema：`URPC` + core chunk 21 覆盖 rigid/soft/fluid、modal/ray/wave acoustic 与 extension domain，solver identity/version、rational time sampling、single-owner resource、directed typed coupling、feedback consent 和 migration policy；required `ure.scene.simulation` feature | 当前 rigid/fluid subset 编译到 `PhysicsConfig`，acoustic/future optional domain 保留；unknown required solver/domain/coupling、无 owner resource、未授权 coupling cycle 和无效 migration fail-loud；binary/text archive roundtrip、33/33 CTest 与 Q/L/R/physics-optics gates 通过 |
| Q.9 | ✅ CLI/API/tooling：`ure_cli validate/build/pack/unpack/inspect/migrate` 支持 URE native，render/C/Python Session 统一加载 `.ure/.urescene/.urepkg`；package 内嵌 canonical `.urescene` payload 并同时验证 container、manifest、content hash 与 scene semantic hash | CLI validate 输出 schema、feature、resource、stored/resident budget 和 adapter-loss diagnostics；build/migrate canonical 投影、multi-scene pack/unpack、inspect inventory、pyure `load_package()` 已由 host/Python 契约覆盖；34/34 CTest 与 Q/Q3-Q9/L/R/physics-optics/schema gates 通过 |
| Q.10 | ✅ Adapter contract：glTF import 先生成 validated `NativeSceneArchive`，render/info/validate/C Session 均消费同一 native boundary；MaterialX expressible subset 通过显式 native wrapper roundtrip；USD 在 Phase U 前保留为标准化 fail-loud 边界；`ure.adapter.loss/1.0` 统一 code/severity/native-path/feature/remediation | advanced procedural/resource/solver/simulation/analytic geometry/material graph fixture 生成稳定 loss inventory；外部 adapter 不再绕过 native validation；35/35 CTest 与 Q/Q3-Q10/L/R/physics-optics gates 通过 |
| Q.11 | ✅ Compiled cache/farm package：`.urecache` 使用独立 `UREC` binary identity、versioned canonical payload 与 payload SHA-256，typed manifest 覆盖 source/compiler/SceneIR hash、GPU upload plan、spectral/resource artifacts、acceleration metadata 和 validation metrics；cache 始终非权威 | source/compiler mismatch 可显式选择 rebuild warning 或 reject error；invalid hash/version/budget/upload overlap/artifact/metric/corruption fail-loud；farm shard 在容量门禁后按 package resource content-hash locality 确定性调度并核算 transfer bytes；36/36 CTest 与 Q/Q3-Q11/L/R/schema gates 通过 |
| Q.12 | ✅ Native scene validation suite：`ure.validation.fixture-set/1.0` 索引 retained full/procedural scenes 与 solver/simulation/resource/adapter/cache fixtures，覆盖基础/程序化、spectral、Mie/volume、wave/integrator、physics/acoustic、video、adapter loss、package/cache/farm contract | `run_phase_q_validation_suite.ps1` 独立完成 selected build/CTest、binary validate、text build、migration、pack/validate/inspect/unpack、FlatBuffers conformance、Q.0-Q.11 static 与 diff gate；全量 Release build、37/37 CTest、Q.0-Q.12/L/R/physics-optics/schema gates 全绿 |

#### 完成标准

- `.ure/.urescene/.urepkg` 是文档化、可验证、可迁移的第一等格式；glTF/USD/MaterialX 不再定义 UltraRender 核心能力边界。
- 所有当前核心能力和已规划高级能力都有 native schema owner、feature declaration 或 typed open extension slot。
- 程序化描述支持 deterministic graph；脚本 build step 默认关闭、显式启用、可 sandbox、可 hash、可缓存。
- Scene package 能完整表达场景、光谱、材质、介质、光源、积分器、波动光学、几何加速、物理、声学、动画/视频流、分布式/farm 和验证 contract。
- 缺失能力、未知 required extension、版本不兼容、外部 adapter 能力损失必须 fail-loud 或输出标准化 loss report；不允许静默降级。
- Source manifest 是权威；compiled cache 可删除重建，且由 source hash、compiler hash 和 schema version 验证。
- Phase U/USD、Phase X/plugin、Phase R/V/W 高级模式后续只能消费或映射 URE native schema，不得重新把 glTF/USD/MaterialX 作为权威 schema。

---

### Phase U — USD/Hydra 集成

**目标**: 使 UltraRender 可作为 USD/Hydra 生态中的渲染后端，同时保持 URE native scene system 的权威地位。USD/Hydra 是 DCC/viewport adapter：它映射到 Phase Q 的原生 schema 和 SceneIR/session，不反向定义 UltraRender 的核心场景语义。

**架构**:

```
DCC (Maya/Houdini) → USD Stage → HdURE adapter
                                      │
                                      ▼
                         URE native schema / SceneIR
                                      │
                                      ▼
                           ure_session / ure_core
```

**子步骤**:

| Step | 内容 | 复杂度 |
|------|------|--------|
| U.1 | USD schema adapter：把 USD prim/material 属性映射到 Phase Q URE native schema；不能用 USD schema 取代原生 schema | 中 |
| U.2 | Hydra RenderDelegate 骨架：`HdURE` 继承 `HdRenderDelegate` | 高 |
| U.3 | RPrim 支持：`HdMesh` → URE native geometry / SceneIR | 高 |
| U.4 | 材质转换：USD `Material` + URE adapter schema → URE MaterialGraph；能力损失输出 loss report | 中 |
| U.5 | 交互渲染：Hydra 视口 → 渐进式渲染会话 | 高 |
| U.6 | 场景导出：`.ure/.urescene/.urepkg` 或 `SceneIR → .usda` adapter output；无法表达的能力必须 fail-loud 或 loss report | 低 |

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

**核心约束**: 不依赖 OpenGL/Vulkan 上下文。Hydra 委托需要 GPU 上下文时的交互由 `ure_session` 内部管理。USD/Hydra 只作为 Phase Q 之后的 adapter；任何 USD schema 不得绕过 URE native validation。

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

**目标**: 渲染内核、数据结构和系统层性能改进，不是单次 Phase 而是长期迭代。光传输算法、采样、MIS、path guiding、ReSTIR、BDPT/MLT 等积分器升级归 Phase R；Phase K 只处理不改变 estimator 语义的底层性能工作。

**子步骤**:

| Step | 内容 | 时机 |
|------|------|------|
| K.1 | Phase V 后的 traversal kernel occupancy、memory coalescing、ray sorting 与 Nsight-driven tuning | Phase V baseline 后 |
| K.2 | OptiX denoiser 集成（替代 current denoise.cu） | Phase E 后 |
| K.3 | Wavefront occupancy auto-tuning（基于 SM 计数） | Phase A 后 |
| K.4 | GPU memory allocator / async upload / resource streaming performance | Phase L/R 后 |
| K.5 | Kernel fusion/splitting、register pressure、occupancy tuning 和 Nsight-driven cleanup | Phase R/V benchmark harness 后 |
| K.6 | Production perf dashboard 与长跑 benchmark farm integration | Phase R.12 后 |

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
                                                  ▼              │
                                             Phase L ─────┬──────┘
                                                  │
                                                  ▼
                                             Phase Q
                                                  │
                                                  ▼
                                             Phase R
                                                  │
                                                  ▼
                                             Phase T
                                                  │
                                                  ▼
                                             Phase V
                                                  │
                                                  ▼
                                             Phase W
                                                  │
                                                  ▼
                                             Phase U
                                                  │
                                             ─────┴──────→ (K.1–K.6 持续)
```

### 差异化战略总结

| 不做 | 做 |
|------|----|
| CPU production 渲染后端 | 多后端 GPU 执行；CUDA 是当前生产/参考 backend，Vulkan 为跨厂商目标，D3D12/DXR 可选 |
| OSL 编译器 | URE native MaterialGraph；MaterialX 只是 adapter |
| 以图形 API 定义场景或物理语义 | URE native schema/IR 定义语义；Vulkan/D3D12 只作为执行 backend，输出仍走 CLI/Python/Hydra |
| DCC 插件（Maya/Houdini） | Hydra adapter 消费 URE native scene/session |
| 通用 RGB 渲染器 | **光谱渲染 + Mueller 偏振 + 物理声学** |
| 实时游戏渲染 | 交互渐进式 + 离线帧序列 |
| 把优化等同于降噪 | Phase R 中以无偏估计器、方差/性能 benchmark 和显式高级积分器提升收敛 |

### 当前 PLAN.md 中加入远期规划的意义

1. **Phase P/S.1 的 Session API 接口设计需要远期考虑**：今天写的 `update_instance_transforms()` 签名必须被 `RenderSession::mutate_scene()` 调用。接口预对齐，避免远期返工。
2. **Phase G 的 `URE_spectral_material` 只是过渡输入**：glTF 扩展不得继续承担高级场景语言职责；Phase Q 后高级语义必须迁入 URE native schema。
3. **Phase C/D 的分布式契约必须考虑 SceneDiff 与 URE native package 的网络序列化**。
4. **Phase T 必须先于 Phase V 固化加速栈**：当前 CUDA 代码是工作实现和物理基线，不得继续把 CUDA handle、资源模型或 launch 语义扩散到 SceneIR、Session、积分器、波动光学和原生格式。
