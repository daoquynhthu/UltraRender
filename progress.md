# UltraRender Progress Log

## 2026-06-09 — Phase P 全部完成 + API 工业化 (P.9–P.13)

Phase P 全部 8 个子步骤完成（P.1–P.8），外加 API 工业化改造（P.9–P.13）。

### 完成项

| 子步骤 | 交付物 |
|--------|--------|
| **P.1** GPU Instance 分离 | `instance_desc.hpp` / `instance_transform.hpp` 独立，8 B / 152 B 布局，`d_instance_descs` 独立分配 |
| **P.2** 热更新 API | `load_scene_once()` + `update_transforms()` + `get_framebuffer()` + `create_gpu_renderer()` |
| **P.3** Transform RingBuffer | 文件名 `tranform`→`transform` 修正；`std::atomic<int>` SPSC 三缓冲，3 帧滞后 2 帧 |
| **P.4** World/ECS 组件池 | `World` + `TransformComponent/GeometryComponent/PhysicsComponent/AudioComponent` + `WorldSceneBuilder` 桥接 |
| **P.5** ISpatialQuery 抽象 | `PhysicsWorld : ISpatialQuery`，`AcousticRayTracer` 仅依赖接口 |
| **P.6** 类型统一 | `ure::Vec3` 移除，`core::Quat::from_euler_zyx/to_euler_zyx()`，main.cpp 欧拉角函数删除 |
| **P.7** 公共 API 契约 | `render.hpp` / `physics.hpp` / `scene_io.hpp` / `config.hpp` 四个库级头文件 |
| **P.8** 编排层瘦身 | `ure_cli` 使用公共 API，无本地业务能力；`load_scene_once` + `update_transforms` 热更新 |
| **P.9** render.hpp 去 GPU 污染 | 移除 `#include "ure/gpu_structs.hpp"`，前向声明 `gpu::GpuInstanceTransform`；外部应用不再拉入 CUDA 内核类型 |
| **P.10** scene_io facade | 移除内部实现头文件（`procedural.hpp`/`gltf_scene_frontend.hpp`/`image_loader.hpp`/`image_saver.hpp`/`scene_parser.hpp`），转化为纯声明 |
| **P.11** CMake install/export | `cmake/UltraRenderConfig.cmake.in` + `install(TARGETS ... EXPORT UltraRender_Targets)` + `configure_package_config_file` → `find_package(UltraRender)` 支持 |
| **P.12** engine->load_world() | `IRenderEngine::load_world(const World&)` + `update_world_transforms(const World&)` 非虚便利方法 |
| **P.13** C API | `ure_c_api.h` + `ure_c_api.cpp`：`ure_engine_create/destroy/load_scene_file/render_pass/get_framebuffer/save_bmp` |

### 修复的坑

- **CUDA 700**: `reinterpret_cast<GpuInstanceDesc*>(ctx->d_instances)` 步长 160→8 B
- **文件名拼写**: `tranform_ring_buffer.hpp` → `transform_ring_buffer.hpp`（含测试 _test_instance_hotupdate.cu_ 中的 include）
- **头文件冲突**: `ure_core/include/ure/render_config.hpp` 与 `ure_types` 版本同路径但不同 namespace（已删除多余副本）

### 测试结果

```
[GPU Instance Hot-Update]   PASS (66 assertions)
[GPU Basic Render Test]      PASS (37 assertions)
[GPU Math Functions]         PASS (27 assertions)
[GPU Spectral Pipeline]      PASS (30 assertions)
[GPU Device Test]            PASS (6 assertions)
[GPU Hardware Config Test]   PASS (17 assertions)
[World/ECS Test]             PASS (39 assertions, host)
Total: 183 GPU + 39 host = 222 assertions, 0 failures
```

## 2026-06-09 — Phase 0 收尾（硬件检测 + 自动配置）

### 交付物

| 改动 | 文件 | 说明 |
|------|------|------|
| 修复 `query_hardware()` 总显存查询 | `libs/ure_core/src/gpu_hardware.cu` | `cudaMemGetInfo` 第二参数获取 total（而非误用 `cudaDevAttrTotalGlobalMemory`，该属性 CUDA 13.0 不存在） |
| 新增 `auto_configure()` 接口 | `libs/ure_core/include/ure/gpu_auto_config.hpp` | 从旧 `include/gpu/render_config.hpp` 移植：`auto_select_wavelengths/auto_select_queue_capacity/auto_select_wg_size/auto_configure/print_render_config` |
| 测试迁移至模块路径 | `tests/gpu/test_hardware.cu` | include 改为 `ure/gpu_hardware.hpp` + `ure/gpu_auto_config.hpp`，调用 `ure::auto_configure()`（而非旧 `ure::gpu::auto_configure`） |

### 真实硬件检测结果

```
device:     NVIDIA GeForce RTX 5060 Laptop GPU
CC:         12.0
SMs:        26
VRAM:       8.0 GB
Bandwidth:  384.0 GB/s
Auto-Cfg:   N=64, queue=2073600
```

### 外部应用使用示例

```cmake
# CMakeLists.txt (external app)
find_package(UltraRender)
target_link_libraries(myapp PRIVATE UltraRender::ure_core UltraRender::ure_sceneio)
```

```cpp
#include <ure/render.hpp>
#include <ure/scene_io.hpp>
#include <ure/world_scene_builder.hpp>

auto engine = ure::RenderEngineFactory::create_gpu_renderer();
ure::World world;
// populate world...
engine->load_world(world);
engine->render_pass();
const auto& fb = engine->get_framebuffer();
```

```c
#include <ure/ure_c_api.h>
ure_engine_t* eng = ure_engine_create();
ure_engine_load_scene_file(eng, "scene.gltf");
ure_engine_render_pass(eng);
const float* fb = ure_engine_get_framebuffer(eng);
ure_engine_destroy(eng);
```

---

## Phase P.1 — GPU Instance Desc/Transform Split + Hot-Update Path

### 2026-06-09 (superseded by Phase P complete)

- [DONE] P.1.1–P.1.10: See full Phase P summary above

## Phase P.3 — Transform Ring Buffer

### 2026-06-09 (superseded by Phase P complete)

- [DONE] P.3.1–P.3.4: See full Phase P summary above

## Phase P.6 — 类型统一（Vec3/Quat 一致化）

### 2026-06-09 (superseded by Phase P complete)

- [DONE] P.6.1–P.6.3: See full Phase P summary above

## Phase P.5 — ISpatialQuery 抽象（声学 ↔ 物理 解耦）

### 2026-06-09 (superseded by Phase P complete)

- [DONE] P.5.1–P.5.6: See full Phase P summary above

## Phase P.4 — World/ECS 组件池

### 2026-06-09 (superseded by Phase P complete)

- [DONE] P.4.1–P.4.5: See full Phase P summary above

## Phase F — Directory Restructure + CMake Library Separation

### 2026-06-08

- [DONE] F.1–F.8: Migrated from single EXE to 6 libraries + 1 EXE

## Phase 4 — Code Modularization

- Raygen kernel extraction + accessors + section markers
- GPU test infrastructure + math function extraction
- Nested dielectric IOR fix
