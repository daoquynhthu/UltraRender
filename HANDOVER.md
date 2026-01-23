# 渲染引擎开发交接文档 (Handover Documentation)

**日期**: 2026-01-23
**当前阶段**: 阶段三：超越几何光学 (Beyond Ray Optics) - 进行中

## 1. 项目现状概述
本项目已从基础的 GPU 路径追踪器演进为**偏振感知 (Polarization-aware)** 与 **光谱感知 (Spectral-aware)** 的物理渲染引擎。
目前已完成 Wavefront 架构的初步重构，并成功引入了 Stokes-Mueller 偏振体系和薄膜干涉模型。
**核心挑战**: 薄膜干涉（Thin-film Interference）虽已具备物理框架，但在渲染中观察到的色彩对比度尚未达到预期。

## 2. 近期关键变更 (Recent Changes)

### 2.1 偏振与波动光学 (Phase 3)
- **Stokes 矢量追踪**: 每一条光线现在携带一个 4 分量的 Stokes 矢量，用于描述光的偏振状态。
- **Mueller 矩阵集成**: 
  - 实现了电介质（Dielectric）的反射/折射 Mueller 矩阵，包含全反射（TIR）下的相位偏移。
  - 实现了金属（Conductor）的复数 Fresnel Mueller 矩阵，支持基于 $n, k$ 值的精确偏振渲染。
- **参考系旋转**: 引入了 `rotate_stokes` 和 `get_reference_frame`，确保光线在多次散射过程中的偏振坐标系保持一致。

### 2.2 光谱材质增强
- **金属预设**: 引入了金（Gold）、铜（Copper）、铝（Aluminum）的物理光谱参数（IOR 和 Extinction）。
- **波长采样**: 渲染内核现在支持在散射时根据光谱权重进行波长采样，实现了更真实的金属色泽。

### 2.3 薄膜干涉实现 (待完善)
- **物理模型**: 在 `path_tracer_kernel.cu` 中实现了 `get_thin_film_interference`，基于光程差和相位偏移计算反射率调制。
- **集成方式**: 调制后的反射率 $R_{tf}$ 已整合进重要性采样逻辑中。
- **当前遗留问题**: **薄膜干涉效果不明显**。虽然代码逻辑正确（支持 4 倍反射增强），但实际渲染图（如 `output/thin_film.bmp`）中观察到的彩虹色带对比度较低，表现为半透明感而非鲜艳的结构色。

### 2.4 几何与解析
- **圆环几何体**: 实现了 `mesh_torus` 过程几何生成。
- **解析器增强**: `SceneParser` 现在支持 `thin_film` 参数定义，并修复了可选参数导致的流状态错误。

## 3. 核心代码结构

| 文件路径 | 说明 |
| :--- | :--- |
| [`src/gpu/path_tracer_kernel.cu`](src/gpu/path_tracer_kernel.cu) | **核心渲染内核**。包含 Stokes 矢量操作、Mueller 矩阵计算以及薄膜干涉算法。 |
| [`src/api/scene_parser.cpp`](src/api/scene_parser.cpp) | **场景解析器**。支持新材质参数和圆环实体的加载。 |
| [`src/api/procedural.cpp`](src/api/procedural.cpp) | **过程几何生成**。包含新增的 Torus 生成逻辑。 |
| [`include/gpu/gpu_structs.hpp`](include/gpu/gpu_structs.hpp) | **GPU 数据结构**。新增 `StokesVector` 和 `GpuSpectrum` 扩展。 |

## 4. 下一步开发任务 (For the Successor)

### 4.1 核心攻关：薄膜干涉色彩对比度
- **现象**: 目前 `thin_film.scene` 渲染结果中彩虹色不明显。
- **排查建议**:
  1. **相位一致性**: 检查 `path_tracer_kernel.cu` 中的 `phase` 计算是否在多次反射间保持一致。
  2. **Air-Film-Medium 边界**: 目前简化了多层干涉，可能需要引入 Airy Summation（艾里公式）来处理完整的无限次反射叠加。
  3. **采样噪声**: 确认波长采样是否有足够的精度来捕捉窄带干涉。

### 4.2 架构深化
- **Wavefront 队列优化**: 目前虽有 Wavefront 框架，但负载均衡和队列压缩仍有提升空间。
- **支持更多薄膜配置**: 目前主要测试了“空气-薄膜-空气”模型，需扩展至“空气-薄膜-金属”等更复杂的层级材质。

## 5. 构建与运行
```powershell
# 构建
cmake --build build --config Release

# 运行薄膜测试场景 (256 SPP)
./build/bin/Release/UltraRender.exe scenes/thin_film.scene -s 256
```

祝好运！
