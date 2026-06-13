# UltraRender 现状文档 (STATUS.md)

最后更新: 2026-06-13

> 过期说明（2026-06-13）: 本文件描述的是模块化重构前后的早期快照，包含旧 `include/`/`src` 路径和 `render_frame_gpu()` 入口等已删除内容。当前工程状态以 `PLAN.md`、`AGENTS.md` 和 `docs/Phase_E_Spectral_Architecture.md` 为准。不要把本文件作为当前架构事实来源；保留它仅用于历史追溯。Phase E.5 物理审查、Phase S background progressive scheduler、AOV/Python progressive API、C ABI + pyure mutation helpers、SceneDiff material texture/resource full reload、SceneDiff instance/entity/sphere topology full reload、SceneDiff instance transform/material hot-update、MotionVector camera/instance object-motion 进展和当前门禁均已移入当前文档。

本文档记录代码库的实际状态、已知问题和与设计文档的差距。所有信息来自对源码的直接审查。

---

## 一、架构总览

```
UltraRender
├── GPU 路径 (主开发目标)
│   ├── include/gpu/         — 数据结构 + 设备工具函数 (8 个文件)
│   └── src/gpu/             — CUDA 实现 (6 个 .cu 文件)
├── CPU 路径 (src/integrators/) — 已标记 OBSOLETE，冻结不改
├── API 层 (src/api/)        — 场景加载 + 渲染引擎实现
└── 测试 (tests/)            — 仅 CPU 单元测试
```

### 渲染流程

```
render_frame_gpu()                   ← gpu_driver.cu (host 端)
  └─ wavefront 循环 (per-sample)
       ├─ generate_rays_kernel       ← path_tracer_kernel.cu (定义在 kernel 内部)
       ├─ extend_kernel              ← path_tracer_kernel.cu (定义在 kernel 内部)
        ├─ shade_kernel               ← path_tracer_kernel.cu
        │    ├─ 散射 scatter() ①  ← kernel.cu:1807 → material.cu scatter() via #include
        │    └─ 散射 scatter() ②  ← kernel.cu:2719 → material.cu scatter() via #include
       ├─ extend_shadow_kernel       ← path_tracer_kernel.cu
       └─ resolve_framebuffer_kernel ← path_tracer_post.cu
```

---

## 二、各文件状态

### ✅ 正常工作（无需改动）

| 文件 | 行数 | 功能 | 备注 |
|------|------|------|------|
| `include/gpu/gpu_structs.hpp` | 404 | GpuVec3, GpuSpectrum, StokesVector, RayQueue, GpuMaterial 等 | 整洁，设计合理 |
| `include/gpu/gpu_spectrum_utils.cuh` | 133 | CIE 匹配函数, spectrum↔RGB 转换 | 三类语义上映射齐全 |
| `include/gpu/gpu_math_functions.cuh` | ~35 | ggx_D, smith_G1, schlick, power_heuristic | Phase 3 从 kernel.cu 提取 |
| `include/gpu/gpu_driver.hpp` | 77 | Host API 声明 | 清晰，有 Interactive API |
| `include/gpu/gpu_scene_loader.hpp` | - | Device 端场景结构 | 正常 |
| `include/gpu/bvh_builder.hpp` | - | BVH 构建 | 正常 |
| `include/gpu/path_tracer_sampling.cuh` | - | 设备端采样/RNG | 正常 |
| `include/gpu/material_library.hpp` | - | 材质参数预设 | 正常 |
| `src/gpu/gpu_driver.cu` | 54 | Host 端管理 (init, render_pass, reset, copy) | 整洁 |
| `src/gpu/path_tracer_denoise.cu` | 158 | A-Trous 小波降噪 + 暗点抑制 | 逻辑完整 |
| `src/gpu/path_tracer_post.cu` | 242 | resolve_framebuffer + FXAA | 逻辑完整 |
| `src/api/gpu_engine_impl.cpp` | 142 | GpuRenderEngine 实现 | 已重构，使用 CompiledGpuScene |
| `src/api/gpu_scene_compiler.cpp` | ~400 | Scene → CompiledGpuScene 编译器 | 支持 SceneIR/legacy 双路径 |
| `src/api/scene_parser.cpp` | 14 | 场景文件解析 | 正常 |
| `CMakeLists.txt` | 125 | 构建配置 | CUDA 正确，all-major 架构 |
| `build_gpu.bat` | 27 | 构建脚本 | 正常 |

### ⚠️ 死代码或有问题的文件

| 文件 | 行数 | 问题 |
|------|------|------|
| **`src/gpu/path_tracer_raygen.cu`** | 9 | **空壳** — 只有 `namespace ure::gpu { }`，无任何代码 |

---

## 三、已知 Bug 清单

### Bug #1 (严重) — Dielectric 透射能量不守恒

**位置**: `src/gpu/path_tracer_kernel.cu:1355-1359`
**当前状态**: 注释掉
```cpp
// float radiance_scale = eta_ratio * eta_ratio;
// attenuation = attenuation * radiance_scale;
```
**影响**: 高 IOR 材质（钻石 2.4、玻璃 1.5）透射亮度不正确
**material.cu 中已有修复**: `src/gpu/path_tracer_material.cu:812-816` 包含 `radiance_scale` 并加了 1.5x 钳位
**修复状态**: ❌ 未修复

### ~~Bug #2 (严重) — 两个 `scatter()` 分裂~~ ✅ 已修复 (Phase 1+3)

旧版 `kernel.cu:887` scatter() 已删除，material.cu scatter() 通过 `#include "path_tracer_material.cu"` 接入 kernel.cu。
Phase 1 添加了 `out_pdf` 参数，Phase 3 通过提取 math 函数 + GPU 测试验证。
**修复提交**: `54233a6`

### Bug #3 (中等) — NEE + Dielectric BSDF 评估为 0

**位置**: `src/gpu/path_tracer_kernel.cu:880-882`
```cpp
} else if (mat.type == MaterialType::Dielectric) {
    // Delta distribution -> BSDF is Dirac Delta (cannot evaluate as function)
    return GpuSpectrum(0.0f);
}
```
**影响**: NEE 路径遇到 Dielectric 表面时，直接光照贡献被丢弃。可能导致透明材质内部看不到光源焦散。

### Bug #4 (中等) — 嵌套介质 IOR 不一致

- **scatter 调用①** (kernel.cu:1807): 传了 `ior_surrounding`（从介质栈获取）
- **scatter 调用②** (kernel.cu:2719): 没传 `ior_outside`，走默认值 1.0

次级反弹时，嵌套介质（玻璃中的水）IOR 计算错误。

---

## 四、缺失组件

| 组件 | 文档提及 | 代码 | 差距 |
|------|---------|------|------|
| GPU 测试 | GPU_Roadmap 隐含需要 | 4 个 .cu 测试文件 | ✅ Phase 3 完成 |
| 参考渲染对比 | - | 无 | ❌ 无基线 |
| 高通道光谱 (N≥32) | GPU_Roadmap §2.4 | 4 通道 | ⚠️ 精度限制 |
| BDPT 双向路径追踪 | Offline_Roadmap §2.1 | 无 | ❌ 未实现 |
| OptiX 加速 | GPU_Roadmap §3 | 无 | ❌ 未实现 |
| Jolt 物理引擎 | Offline_Roadmap §2.2 | 无 | ❌ 未实现 |
| HDR 输出 | Offline_Roadmap §2.3 | 无 | ❌ 未实现 |
| fluorecent 材质 | Offline_Roadmap | 无 | ❌ 未实现 |

> **注意**: 以上缺失组件中，仅 BDPT 需要当前修复阶段预埋接口（保留 `out_pdf`），其余组件独立于当前工作。

---

## 五、文档 - 代码一致性检查

| 文档 | 声称 | 实际代码 | 一致？ |
|------|------|----------|:------:|
| Spectral_Semantics_Guide | 3 类语义上映射 | 代码有 `rgb_to_spectrum` / `rgb_coeff_to_spectrum` / `emission_to_spectrum` | ✅ 一致 |
| HANDOVER_GUIDE | Dielectric 支持偏振 Fresnel | kernel.cu + material.cu 都实现了 Müller 矩阵 | ✅ 一致 |
| HANDOVER_GUIDE | Conductor 支持 n,k 参数 | `GpuMaterial` 有 `metal_eta` / `extinction` | ✅ 一致 |
| HANDOVER_GUIDE | Thin-film 干涉效果不佳 | 已实现 `get_dielectric_thin_film_reflectance`，质量待验证 | ⚠️ 已实现但不完美 |
| GPU_Roadmap §2.4 | 波长并行，每线程 4 波长 | `GpuSpectrum` 用 `float4` 存 4 样本 | ✅ 一致 |

---

## 六、测试覆盖现状

| 测试文件 | 类型 | 覆盖内容 | 状态 |
|---------|------|---------|:----:|
| `tests/unit/test_vector.cpp` | 单元 | GpuVec3 运算 | ✅ 通过 |
| `tests/unit/test_matrix.cpp` | 单元 | GpuMat4 运算 | ✅ 通过 |
| `tests/unit/test_aabb.cpp` | 单元 | AABB 碰撞 | ✅ 通过 |
| `tests/unit/test_ray.cpp` | 单元 | Ray 运算 | ✅ 通过 |
| `tests/unit/test_image_saver.cpp` | 单元 | 图片输出 | ✅ 通过 |
| `tests/integration/test_scene_factory.cpp` | 集成 | 场景构建 | ✅ 通过 |
| `tests/test_interactive_api.cpp` | 集成 | 交互 API | ✅ 通过 |
| `tests/gpu/test_device.cu` | GPU 集成 | CUDA 设备检测 | ✅ 通过 |
| `tests/gpu/test_math_functions.cu` | GPU 单元 | ggx_D, smith_G1, schlick, power_heuristic | ✅ 通过 |
| `tests/gpu/test_spectral_pipeline.cu` | GPU 集成 | RGB 光谱往返一致性 | ✅ 通过 |
| `tests/gpu/test_render_basic.cu` | GPU 集成 | ray-sphere intersection + emissive shade + NEE shadow | ✅ 通过 |

---

## 七、构建状态

- **CMake 配置**: 正确，CUDA `all-major` 架构
- **编译器**: MSVC 2022 + CUDA (NVCC)
- **OpenMP**: 可选，找到则启用
- **已知构建问题**: 无
