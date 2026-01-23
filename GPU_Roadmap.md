# UltraRender GPU 架构演进路线图

本文档详细规划了将 UltraRender 从当前 CPU 架构迁移至高性能 GPU (CUDA + OptiX) 架构的技术路径。

## 1. 总体架构愿景

目标是构建一个基于 **NVIDIA OptiX 7+** 和 **CUDA** 的波前路径追踪器 (Wavefront Path Tracer) 或 巨型内核路径追踪器 (Megakernel Path Tracer)。

- **光线求交 (Intersection)**: 委托给硬件加速的 RT Cores (通过 OptiX API)。
- **着色与采样 (Shading & Sampling)**: 使用 CUDA Core 并行计算。
- **光谱逻辑**: 保持现有的高精度光谱算法，利用 GPU 的 SIMT 特性加速波长积分。

---

## 2. 演进阶段规划

### 第一阶段：环境搭建与混合管线 (Hybrid Pipeline)
**目标**: 打通 C++ 与 CUDA 的编译链路，实现显存与内存的数据互通。

1.  **构建系统升级**:
    - 修改 `CMakeLists.txt`，引入 `FindCUDA` 或新版 CMake 的 `enable_language(CUDA)`。
    - 确保项目能同时编译 `.cpp` (Host) 和 `.cu` (Device) 代码。

2.  **Hello World Kernel**:
    - 编写第一个 CUDA Kernel，不进行光线追踪，仅根据线程 ID 输出 UV 颜色图。
    - 实现 `CUDABuffer` 封装类，管理 `cudaMalloc`, `cudaMemcpy`。
    - 将 GPU 计算结果拷回 CPU 并通过现有的 `ImageSaver` 保存。

### 第二阶段：数据结构扁平化 (Data-Oriented Design)
**目标**: 消除所有虚函数和指针跳转，适应 GPU 内存模型。这是最困难的一步。

1.  **几何体数据**:
    - **Mesh**: 将所有 `TriangleMesh` 的顶点 (`Vertex`) 和索引 (`Index`) 合并到两个巨大的全局 `UnifiedBuffer` 中。
    - **BVH**: 废弃 CPU 端的 BVH，准备使用 OptiX 构建的 GAS (Geometry Acceleration Structure)。

2.  **材质系统重构 (Uber-Shader)**:
    - 移除 `BSDF` 继承体系。
    - 定义统一的 `MaterialParams` 结构体 (POD类型):
      ```cpp
      struct MaterialParams {
          int type; // 0=Lambert, 1=Metal, 2=Dielectric
          float3 albedo;
          float roughness;
          float ior;
          // ... 
      };
      ```
    - 在 CUDA Kernel 中使用 `switch(type)` 处理不同材质逻辑。

3.  **光谱数据**:
    - 将 CIE 1931 表格 (`cie_data.hpp`) 移入 CUDA `__constant__` 内存或 `Texture Memory`，实现极速查找。

### 第三阶段：OptiX 集成 (Ray Generation & Traversal)
**目标**: 利用 RTX 硬件加速光线求交。

1.  **OptiX 初始化**:
    - 设置 OptiX Pipeline, Ray Generation Program, Miss Program。
    - 将网格数据传递给 OptiX 构建加速结构 (AS)。

2.  **光线生成 (RayGen)**:
    - 移植 `Camera::generate_ray` 到 CUDA。
    - 在 RayGen Shader 中调用 `optixTrace`。

3.  **最近面求交 (Closest Hit)**:
    - 即使只是返回三角形索引和重心坐标，暂不进行着色。

### 第四阶段：完整的 GPU 路径追踪
**目标**: 移植核心渲染循环。

1.  **随机数生成**:
    - 引入 `cuRAND` 或实现轻量级的 `PCG Hash` 随机数生成器 (每个线程独立状态)。
    - 这一点至关重要，GPU 并行需要数百万个独立的随机序列。

2.  **着色 Kernel**:
    - 移植 `PathTracer::trace` 逻辑。
    - 由于 GPU 栈空间有限，必须将递归逻辑重写为**迭代循环 (While Loop)**。

3.  **波长并行**:
    - 每个 CUDA 线程携带 4 个波长的能量包 (`Spectrum` 类移植为 `__device__` 类)。

---

## 3. 关键技术挑战与对策

| 挑战 | 解决方案 |
| :--- | :--- |
| **虚函数开销** | 使用 Material ID + Switch Case (Uber-Shader) 或 CUDA Callable Programs (OptiX 特性) |
| **显存限制** | 对于超大场景，需实现纹理流式传输 (Texture Streaming) 或外芯渲染 (Out-of-Core) |
| **寄存器压力** | 光谱计算涉及大量浮点数，需优化 `Spectrum` 类，避免过多的局部变量 |
| **随机数质量** | 使用高质量的 Hash 函数 (如 PCG32) 替代简单的线性同余 |

## 4. 预期收益

- **性能**: 预计渲染速度提升 **50x - 100x** (对比目前的 CPU 单核/多核)。
- **交互性**: 低 SPP 下可实现实时 30fps 漫游。
- **精度**: 更快地收敛，允许我们在合理时间内使用更高的 SPP (如 4096+)。
