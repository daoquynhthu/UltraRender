<!-- # UltraRender 渲染引擎项目交接技术文档

## 1. 项目概述
本引擎是一个基于物理（PBR）的**偏振光谱路径追踪渲染引擎**。它在整个渲染管线中使用光谱功率分布（SPD）和 Stokes 矢量来计算能量传输，能够真实模拟色散（Dispersion）、偏振（Polarization）以及薄膜干涉（Thin-film Interference）等复杂物理现象。

## 2. 核心架构说明

### 2.1 偏振与光谱系统
- **stokes.hpp / gpu_structs.hpp**: 定义了 `StokesVector` 和 `MuellerMatrix` 相关计算。
- **采样机制**: 采用波长包采样。在路径追踪过程中，通过 Mueller 矩阵描述光与材质表面的偏振交互（反射、折射、全反射相位移）。

### 2.2 材质系统 (GPU Kernel)
- **path_tracer_kernel.cu**: 核心逻辑已移植至 GPU。
- **Dielectric**: 支持偏振敏感的 Fresnel 反射/折射。
- **Conductor**: 支持基于物理 $n, k$ 参数的复数反射。
- **Thin-Film**: 嵌入在电介质和金属中的干涉调制层。

## 3. 当前已知问题与异常分析 (Critical)

### 3.1 薄膜干涉（Thin-Film）效果不佳
- **现象**: 在 `thin_film.scene` 中，圆环内的薄膜虽然不再是空白，但没有呈现出预期的彩色（彩虹色）干涉效果，看起来更像是普通的半透明层。
- **分析**: 
  - **相位累积**: 目前的干涉计算可能未充分考虑多次内反射的相干叠加。
  - **波长分辨率**: 4 通道光谱采样可能不足以捕捉高阶干涉条纹。
  - **模型简化**: 目前使用的是单层干涉简化模型，可能需要升级为完整的波动力学模型。

### 3.2 性能瓶颈
- **Wavefront 调度**: 随着材质分支增加，Wavefront 架构的线程分叉（Divergence）问题开始显现，需要进一步优化。

## 4. 后续开发建议 (Next Steps)

1.  **攻克薄膜干涉**: 
    - 验证 `get_thin_film_interference` 中的 `phase` 计算是否与观察角度和波长严格匹配。
    - 尝试引入更复杂的多层干涉算法（如 Transfer Matrix Method）。
2.  **完善偏振效果**: 
    - 增加天空偏振模型（Rayleigh Scattering）。
    - 增加双折射（Birefringence）材质支持。
3.  **渲染质量**: 
    - 引入更高级的采样器（如 Blue Noise）以减少低 SPP 下的噪声。

## 5. 构建与运行
- **构建**: 使用 CMake 构建，依赖支持 C++20 的编译器。
- **运行**: `./UltraRender.exe [scene_name]` (如 `test` 或 `quick`)。
- **输出**: 默认输出为 `output.ppm` 和 `output.bmp`。 -->
