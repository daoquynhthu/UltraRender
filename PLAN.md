# UltraRender 修复计划书 (PLAN.md)

最后更新: 2026-06-08

本文档是唯一的行动纲领。所有开发工作必须严格按照此计划分阶段执行。不允许跳过阶段、合并阶段或擅自引入计划外改动。

---

## 总览

```
Phase 1: 能量守恒 + scatter 统一  (正确性核心)
    ↓ 依赖
Phase 2: 嵌套 IOR + NEE Dielectric  (边缘正确性)
    ↓ 依赖
Phase 3: GPU 测试基础设施  (验证能力)
    ↓ 依赖  
Phase 4: 代码模块化  (为升级铺路)
```

**各阶段互不重叠，前一阶段完成并通过 review 后才进入下一阶段。**

---

## Phase 1: 修复 Dielectric 能量守恒并统一 scatter

**目标**: 让 dielectric 透射能量正确，消除新旧两个 scatter 的分裂。

**影响文件**: `src/gpu/path_tracer_kernel.cu`, `src/gpu/path_tracer_material.cu`

### Step 1.1 — 实测确认 Bug

在动手改代码之前，先验证问题确实存在：
- 检查 `kernel.cu:1355-1359` 注释状态
- 检查 `material.cu:812-816` 修复代码状态
- 确认 `material.cu` 中 `scatter()` 的 `static` 属性导致它无法被外部调用

**产出**: 记录在 progress.md 中的确认结果

### Step 1.2 — 给 material.cu 的 scatter 加回 out_pdf

在 `path_tracer_material.cu` 的 `scatter()` 函数签名中加入 `float& out_pdf`：
```cpp
// 改前 (line 475-484):
static __device__ inline bool scatter(
    const GpuRay& r_in, const GpuMaterial& mat, ..., int& spectral_channel
)

// 改后:
static __device__ inline bool scatter(
    const GpuRay& r_in, const GpuMaterial& mat, ..., float& out_pdf, int& spectral_channel
)
```
**验证**: 函数内部所有 return 前赋值 `out_pdf = pdf`。

### Step 1.3 — kernel.cu include material.cu，删除旧 scatter

在 `path_tracer_kernel.cu` 文件末尾（或对应位置）加入：
```cpp
#include "path_tracer_material.cu"
```
然后删除 kernel.cu 中旧 `scatter()` 函数（~line 887-1459，约 572 行）。

**验证**: 编译必须通过。检查新 `scatter()` 签名变化：
- 新函数没有 `out_pdf` 以外的额外参数 → 所有调用点需调整参数
- 新函数没有 `ior_outside` 默认参数 → 需在调用点处理

### Step 1.4 — 更新 shade_kernel 中两处 scatter 调用

调用点 A (kernel.cu:1807): 移除 `ior_surrounding` 参数，调整为新签名
调用点 B (kernel.cu:2719): 调整为新签名，传递 `pdf_val`

**验证**: 编译通过后运行测试场景，确认 dielectric 透射亮度正确。

### Step 1.5 — Review + 报告

启动 reviewer subagent 审核 Phase 1 所有更改。

---

## Phase 2: 修复嵌套介质 IOR + NEE Dielectric

**目标**: 次级反弹时嵌套介质 IOR 正确，NEE 能看到 Dielectric。

### Step 2.1 — 传递 ior_outside 给新 scatter

在 shade_kernel 中，scatter 调用前从介质栈获取当前 ior_outside，若未设置则用 1.0f。

**验证**: 测试玻璃中装水的场景，次级弹射折射方向正确。

### Step 2.2 — 评估 NEE Dielectric 是否真正造成可见问题

查阅 scene 定义，分析是否有"Dielectric 内部被直接光照亮"的路径。若问题存在，在 eval_bsdf 中为 Dielectric 增加前向散射 PDF 评估。

**验证**: 渲染含点光源 + Dielectric 球体的测试场景。

### Step 2.3 — Review + 报告

---

## Phase 3: GPU 测试基础设施

**目标**: 不再"裸眼调色"，有自动化回归检测。

### Step 3.1 — 创建 tests/gpu/ 目录和 CMake 集成

```
tests/gpu/
├── CMakeLists.txt     — 独立的 CUDA 测试构建
├── test_render_basic.cu   — 渲染基础场景 + 像素验证
└── test_spectral.cu       — 光谱管线测试
```

### Step 3.2 — 实现 test_render_basic.cu

渲染一个 Lambertian 球体 + 白色环境光的场景，输出 4x4 像素块，硬编码对比预期值。

**验证**: `ctest --output-on-failure` 通过

### Step 3.3 — 实现 test_spectral.cu

测试 `rgb_to_spectrum` → `spectrum_to_xyz` → `xyz_to_rgb` 往返一致性。

### Step 3.4 — Review + 报告

---

## Phase 4: 代码模块化（为升级铺路）

**目标**: 为 N 通道光谱、OptiX 集成做架构准备。

### Step 4.1 — 填充 raygen.cu

将 `generate_rays_kernel` 从 `path_tracer_kernel.cu` 提取到 `path_tracer_raygen.cu`。

### Step 4.2 — 给 GpuSpectrum 加访问器抽象

增加 `sample(int i)` / `set_sample(int i, float v)` 内联方法，替代直接访问 `.values.x / .y / .z / .w`。当前不改内部布局，只加语法糖。

### Step 4.3 — 整理 kernel.cu 内部函数布局

将 shade_kernel 中的体积散射、NEE、表面散射等逻辑块标注清晰边界，便于未来进一步拆分。

### Step 4.4 — Review + 报告

---

## 依赖图

```
Phase 1 ─────────────────────────────────────────┐
   ├─ Step 1.1 (确认现状)                         │
   ├─ Step 1.2 (加 out_pdf) ──┐                  │
   ├─ Step 1.3 (include) ─────┤                  │
   ├─ Step 1.4 (更新调用点) ──┘                  │
   └─ Step 1.5 (review)                           │
                                                 ↓
Phase 2 ─────────────────────────────────────────┐
   ├─ Step 2.1 (嵌套 IOR) ──── 依赖 Phase 1     │
   ├─ Step 2.2 (NEE Dielectric)                  │
   └─ Step 2.3 (review)                           │
                                                 ↓
Phase 3 ─────────────────────────────────────────┐  Phase 4 ──────────────────────────────────┐
   ├─ Step 3.1 (test 目录)  独立于 Phase 1-2    │  ├─ Step 4.1 (raygen)  依赖 Phase 1       │
   ├─ Step 3.2 (basic test)                     │  ├─ Step 4.2 (访问器)                      │
   ├─ Step 3.3 (spectral test)                  │  ├─ Step 4.3 (整理)                        │
   └─ Step 3.4 (review)                         │  └─ Step 4.4 (review)                      │
```

---

## 预估工作量

| Phase | 改动文件数 | 预估改动行 | 难度 | 预计时间 |
|-------|:---------:|:---------:|:----:|:--------:|
| Phase 1 | 2 | ~+30 / -600 | ★★★ | 1-2 天 |
| Phase 2 | 1 | ~+20 | ★★ | 半天 |
| Phase 3 | 3-4 (新建) | ~+300 | ★★ | 1 天 |
| Phase 4 | 3 | ~+50 / -50 (搬家) | ★ | 半天 |

---

## 当前状态

```
Phase 1: [████]  已完成 (commit 5327cdf)
Phase 2: [████]  已完成 (commit 50b9a08)
Phase 3: [████]  已完成 (commit 6187cf1)
Phase 4: [████]  已完成
```
