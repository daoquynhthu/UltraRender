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
- [x] **显存外光谱纹理**: 实现 TB 级光谱数据的流式加载 (Streaming)。

### 阶段三：超越几何光学 (Beyond Ray Optics) [进行中]
- [x] **偏振光渲染 (Polarization)**: 引入 Stokes 矢量与 Mueller 矩阵，模拟天空偏振、全反射相位偏移。
- [x] **光谱金属材质**: 支持基于波长的复折射率 (n, k) 渲染（金、铜、铝等预设）。
- [x] **薄膜干涉 (Thin-film Interference)**: 实现了完整的 Airy Summation 公式，支持多重反射干涉，并增加了基于 UV 的重力厚度调制。
- [x] **体积光与次表面散射 (Volume/SSS)**: 已完成内核与场景解析联动，支持全局雾与材质内部 SSS (均质介质)。
- [] **波动光学接口 (Wave Optics)**: 探索在微观尺度引入波动方程，精确模拟昆虫翅膀的结构色 (Structural Color)。
- [ ] **荧光与磷光 (Fluorescence & Phosphorescence)**: 支持波长偏移 (Wavelength Shifting) 的材质路径追踪。

### 阶段四：AI 物理融合 (AI-Physics Hybrid) [远期]
- [ ] **神经光谱缓存 (Neural Spectral Cache)**: 训练神经网络预测光谱分布，而非简单的 RGB 降噪。
- [ ] **可微光谱渲染**: 支持从照片反推材质的化学成分。

### 阶段五：物理与流体模拟 (Physics & Fluid) [新增/进行中]
- [x] **SPH 流体系统**: 实现了基于空间哈希的光滑粒子流体动力学 (SPH) 模拟。
- [x] **Marching Cubes**: 实现了流体粒子的实时等值面网格化，支持流体渲染。
- [x] **物理-声学接口**: 定义了碰撞事件监听系统，支持物理交互驱动声学反馈。
- [x] **Glass Cup 演示**: 集成了包含流体、刚体和静态容器的综合演示场景。

## 5. 最新更新 (Latest Updates - 2026-01-27)

### 5.1 物理与流体系统集成
- **流体模拟**: 引入了完整的 SPH 流体系统，支持数千粒子的实时模拟与交互。
- **伪影修复**: 修复了 Marching Cubes 算法中的三角形绕序问题，消除了流体表面的放射状伪影。
- **稳定性增强**: 优化了 Marching Cubes 的内存安全检查，防止了缓冲区溢出崩溃。
- **已知问题**: 目前流体网格在最终渲染中可能存在可见性问题 (Invisible Fluid)，需进一步调试材质与光照设置。

### 5.2 核心修复与优化
- **体积渲染增强**: 
  - 实现了 Henyey-Greenstein 相位函数，支持各向异性散射（g因子），能够正确模拟丁达尔效应（光束感）。
  - 优化了场景解析器，支持在全局介质和 SSS 材质中定义 anisotropy 参数。
- **用户体验优化**:
  - 解决了分辨率设置冲突问题：明确了“场景文件优先于命令行”的规则，并增加了冲突警告。
  - 恢复了默认输出目录为根目录下的 `output/`。
- **体积渲染修复**: 修复了各向同性散射采样中的数学错误（从球内采样改为球面采样），解决了体积雾在高采样下变暗/消失的问题。
- **构建系统增强**: 
  - 修复了 Windows MSVC 环境下的 UTF-8 编码问题 (C4819/C2143)。
  - 修复了 CUDA 宿主编译器 (Host Compiler) 检测失败的问题。
  - 实现了 CPU/GPU 混合编译架构，支持在无 CUDA 环境下回退至 CPU 模式（但在 GPU 机器上优先使用 GPU）。
- **色调映射**: 实现了 ACES (Academy Color Encoding System) 色调映射，提升了高动态范围图像的视觉表现。

## 6. 渲染优化与降噪路线图 (Optimization & Denoising Roadmap) [新增]

针对高采样场景下的彩色噪点与渲染时间问题，我们制定了专项优化计划：

<!-- ### 已完成优化 (Completed Optimizations)
1.  **物理能量守恒修复**:
    - 修复了电介质折射时的**辐射亮度缩放 (Radiance Scaling)**，严格遵循 $(\eta_i / \eta_t)^2$ 物理定律，解决了水体异常发光问题。
    - 修正了光谱渲染中的通道能量增强逻辑，消除了多波次反射后的能量爆炸。
2.  **几何与伪影修复**:
    - **鲁棒性反射**: 修复了金属反射视线与法线平行时的数学奇点（Singularity），消除了“黑洞”伪影。
    - **可见法线采样**: 金属散射改为可见法线采样，缓解视角边缘欠采样导致的暗边问题。
    - **焦散可见性**: 放宽了辐射亮度截断 (Clamping) 阈值 (20 -> 1000)，恢复了玻璃杯底部的真实焦散细节。
    - **几何细分**: 提升了过程化几何体（杯子、水柱）的细分精度 (64 -> 256)，消除了折射中的多边形棱角。

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
    - 对像素平面和透镜进行分层抖动采样，进一步减少聚集噪点。 -->

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
