# 渲染引擎开发交接文档 (Handover Documentation)

**日期**: 2026-01-23
**当前阶段**: 阶段三：超越几何光学 (Beyond Ray Optics) - 优化与稳定化

## 1. 项目现状概述
本项目已从基础的 GPU 路径追踪器演进为**偏振感知 (Polarization-aware)** 与 **光谱感知 (Spectral-aware)** 的物理渲染引擎。
目前已完成 Wavefront 架构的初步重构，并成功引入了 Stokes-Mueller 偏振体系和薄膜干涉模型。
**最新进展**: 成功解决了困扰已久的能量守恒异常（发光水体）、几何伪影（金属黑洞）和折射失真问题，引擎物理准确性大幅提升。

## 2. 近期关键变更 (Recent Changes)

### 2.1 物理核心修复 (Critical Physics Fixes)
- **辐射亮度缩放 (Radiance Scaling)**: 修正了折射过程中的亮度缩放公式。遵循 Veach/PBRT 标准，亮度随折射率平方比 $(\eta_i / \eta_t)^2$ 变化，彻底解决了水体/玻璃异常发亮的问题。
- **金属反射奇点 (Singularity Handling)**: 修复了视线与法线平行（垂直入射）时的数学奇点。通过增加对 `s_axis` 长度和 `fresnel_reflectance` 的鲁棒性检查，消除了金属球反射中心的黑色空洞。
- **能量钳制优化 (Clamping Strategy)**: 将 `max_radiance` 阈值从 20.0 提升至 1000.0，允许高动态范围的焦散光线通过，恢复了玻璃杯底部的真实集光效果。

### 2.2 几何与场景增强
- **高精度过程化网格**: 将 `glass_cup.scene` 中的杯子和水柱细分精度从 64 提升至 256，消除了折射下的多边形棱角伪影。
- **接触面优化**: 微调了水柱半径 (0.748 -> 0.752) 和底部位置，使其略微嵌入玻璃壁，消除了因微小空气隙导致的全内反射 (TIR) 伪影，模拟了真实的水-玻璃润湿界面。

### 2.3 偏振与波动光学 (Phase 3)
- **Stokes 矢量追踪**: 每一条光线现在携带一个 4 分量的 Stokes 矢量，用于描述光的偏振状态。
- **Mueller 矩阵集成**: 
  - 实现了电介质（Dielectric）的反射/折射 Mueller 矩阵，包含全反射（TIR）下的相位偏移。
  - 实现了金属（Conductor）的复数 Fresnel Mueller 矩阵，支持基于 $n, k$ 值的精确偏振渲染。
- **薄膜干涉 (Thin-film Interference)**: 
  - 实现了完整的 **Airy Summation** 公式，替代了简化的余弦模型，正确模拟多重反射干涉。
  - 增加了基于 UV 坐标的重力厚度调制，模拟真实的薄膜厚度不均匀性。

### 2.4 光谱材质增强
- **金属预设**: 引入了金（Gold）、铜（Copper）、铝（Aluminum）的物理光谱参数（IOR 和 Extinction）。
- **波长采样**: 渲染内核现在支持在散射时根据光谱权重进行波长采样，实现了更真实的金属色泽。

## 3. 核心代码结构

| 文件路径 | 说明 |
| :--- | :--- |
| [`src/gpu/path_tracer_kernel.cu`](src/gpu/path_tracer_kernel.cu) | **核心渲染内核**。包含 Stokes 矢量操作、Mueller 矩阵计算、薄膜干涉算法及修复后的物理逻辑。 |
| [`src/api/scene_parser.cpp`](src/api/scene_parser.cpp) | **场景解析器**。支持新材质参数和圆环实体的加载。 |
| [`src/api/procedural.cpp`](src/api/procedural.cpp) | **过程几何生成**。包含新增的 Torus 生成逻辑及高精度圆柱/杯子生成。 |
| [`include/gpu/gpu_structs.hpp`](include/gpu/gpu_structs.hpp) | **GPU 数据结构**。新增 `StokesVector` 和 `GpuSpectrum` 扩展。 |

## 4. 下一步开发任务 (For the Successor)

### 4.1 渲染质量攻坚
- **直接光照采样 (NEE)**: 目前主要依赖间接光采样，导致阴影噪点较高。需尽快实现 Next Event Estimation，对光源进行显式采样。
- **低差异序列 (LDS)**: 引入 Sobol 或 Halton 序列替代伪随机数，提升收敛速度。

### 4.2 功能扩展
- **模拟物理引擎 (Simulation Physics)**: 用户表达了对物理引擎的兴趣，可能需要探索刚体动力学或流体模拟与渲染的结合。
- **分层采样**: 对像素和透镜进行分层采样，减少抗锯齿噪点。

## 5. 构建与运行
```powershell
# 构建
cmake --build build --config Release

# 运行玻璃杯测试场景 (推荐)
./build/bin/Release/UltraRender.exe scenes/glass_cup.scene
```

祝好运！
