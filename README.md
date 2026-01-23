# UltraRender Engine - 世界顶尖物理光学渲染引擎

> **"Light is not RGB. Light is a Spectrum."**

## 1. 项目定位与愿景 (Vision)
UltraRender 不仅仅是一个图形渲染器，而是一个**物理光学模拟器**。
我们的目标是打造**世界最顶尖的物理光学渲染引擎**，不盲目追随工业界主流的 RGB 渲染管线，而是回归物理本质，从光的波粒二象性出发，构建一套完全自研的、具备科研级精度的渲染架构。

我们致力于解决传统渲染器无法正确模拟的光学现象：色散 (Dispersion)、同色异谱 (Metamerism)、薄膜干涉 (Thin-film Interference)、偏振 (Polarization) 以及荧光效应 (Fluorescence)。

## 2. 核心哲学 (Core Philosophy)
*   **光谱优先 (Spectral First)**: 拒绝 RGB 色彩空间的近似。所有计算在 360nm-830nm 可见光波段进行高精度采样。
*   **物理绝对正确 (Physically Rigorous)**: 材质模型必须通过能量守恒验证；相机模型基于真实光学透镜组而非简单的针孔。
*   **原创架构 (Original Architecture)**: 不照搬现有开源代码。我们针对光谱计算的特殊性，设计专有的 Wavefront GPU 架构。

## 3. 核心技术栈 (Advanced Tech Stack)
*   **语言标准**: C++23 (利用 `std::expected`, `std::format`, `constexpr` 等特性)
*   **计算架构**: **Spectral Wavefront Path Tracing** (自研光谱波前路径追踪)
    *   针对 GPU (CUDA/OptiX) 优化的 Data-Oriented Design
    *   支持百万级波长通道的并行计算
*   **色彩科学**: CIE 1931 Standard Observer (2-degree) + 高斯拟合光谱重建
*   **几何内核**: NVIDIA OptiX 7+ (硬件加速求交) + 自研 BVH (特殊几何体)

## 4. 创新路线图 (Innovation Roadmap)

我们不走寻常路，我们将探索渲染技术的无人区：

### 阶段一：光谱基石 (Spectral Foundation) [已完成]
- [x] 基于蒙特卡洛积分的全光谱传输管线
- [x] CIE 1931 标准观测者色彩空间转换
- [x] 物理正确的色散 (Dispersion) 与 柯西方程 (Cauchy Equation) 支持
- [x] 能量守恒的微表面模型 (Microfacet BSDF)

### 阶段二：GPU 算力解放 (GPU Evolution) [进行中]
- [x] **Material ID & Uber-Shader**: 已彻底抛弃虚函数，实现基于 Material Type 的分支调度。
- [x] **Megakernel -> Wavefront 重构**: 正在拆分光线生成、求交、着色逻辑。
- [x] **光谱并行化**: 已引入 `GpuSpectrum` 结构，准备进行 Wavelength Packet 矢量化计算。
- [ ] **显存外光谱纹理**: 实现 TB 级光谱数据的流式加载 (Streaming)。

### 阶段三：超越几何光学 (Beyond Ray Optics) [进行中]
- [x] **偏振光渲染 (Polarization)**: 引入 Stokes 矢量与 Mueller 矩阵，模拟天空偏振、全反射相位偏移。
- [x] **光谱金属材质**: 支持基于波长的复折射率 (n, k) 渲染（金、铜、铝等预设）。
- [/] **薄膜干涉 (Thin-film Interference)**: 已初步实现基于物理相位差的干涉模型，正在调试干涉色彩对比度。
- [ ] **波动光学接口 (Wave Optics)**: 探索在微观尺度引入波动方程，精确模拟昆虫翅膀的结构色 (Structural Color)。
- [ ] **荧光与磷光 (Fluorescence & Phosphorescence)**: 支持波长偏移 (Wavelength Shifting) 的材质路径追踪。

### 阶段四：AI 物理融合 (AI-Physics Hybrid) [远期]
- [ ] **神经光谱缓存 (Neural Spectral Cache)**: 训练神经网络预测光谱分布，而非简单的 RGB 降噪。
- [ ] **可微光谱渲染**: 支持从照片反推材质的化学成分。

## 5. 渲染优化与降噪路线图 (Optimization & Denoising Roadmap) [新增]

针对高采样场景下的彩色噪点与渲染时间问题，我们制定了专项优化计划：

### 优化一阶段：确定性与重要性采样 (Determinism & Importance Sampling)
1.  **修复色散通道选择算法**: 
    - 废弃随机波长选择，改为**三通道确定性加权积分**或全光谱追踪，确保每次采样都对 RGB 通道有贡献，消除因随机波长丢弃导致的色彩方差。
2.  **实现直接光照采样 (Direct Light Sampling / NEE)**:
    - 将直接照明与间接照明彻底解耦。
    - 对光源进行显式**重要性采样 (Importance Sampling)**，确保阴影射线总是指向光源，彻底消除阴影噪点。
3.  **增强重要性采样**:
    - 优化 BSDF 采样策略，根据材质粗糙度匹配光线分布，减少无效散射。

### 优化二阶段：采样序列与分层 (Sampling Strategy)
1.  **低差异序列 (Low-Discrepancy Sequences)**:
    - 使用 Sobol 或 Halton 序列替代伪随机数 (PRNG)，确保样本在空间分布更均匀，提升收敛速度（O(1/N) vs O(1/sqrt(N))）。
2.  **分层采样 (Stratified Sampling)**:
    - 对像素平面和透镜进行分层抖动采样，进一步减少聚集噪点。

## 6. 工程目录结构
```text
RenderEngine/
├── src/                # 源代码
│   ├── core/           # 核心基础库（数学、内存、日志）
│   ├── spectral/       # 光谱功率分布 (SPD) 与颜色科学
│   ├── integrators/    # 渲染积分器（路径追踪、BDPT 等）
│   ├── materials/      # 材质模型 (BSDF, BSSRDF)
│   ├── accelerators/   # 加速结构封装
│   └── scene/          # 场景加载与 USD 接口
├── include/            # 公共头文件
├── docs/               # 技术文档与指导书
├── tests/              # 单元测试 (Catch2/GTest)
├── external/           # 第三方库依赖
└── CMakeLists.txt      # 构建配置文件
```

## 7. 构建与运行
### 依赖准备
1.  CMake 3.25+
2.  支持 C++23 的编译器 (MSVC 19.34+, Clang 16+, GCC 13+)
3.  **CUDA Toolkit 11.8+** (推荐，用于开启极致性能)

### 编译步骤
```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

---
*UltraRender - Rendering Reality, One Wavelength at a Time.*
