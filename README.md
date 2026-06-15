# UltraRender Engine - 世界顶尖物理光学渲染引擎

> **"Light is not RGB. Light is a Spectrum."**

## 1. 项目定位与愿景 (Vision)
UltraRender 不仅仅是一个图形渲染器，而是一个以**光谱与偏振物理**为核心的离线渲染引擎。
当前默认求解器是 spectral/polarimetric radiometric path tracer；Phase W 正在把相干场、衍射相机、部分相干输运和局部全波耦合提升为可选的波动光学求解器层。波动光学能力必须显式开启，未实现或不兼容路径必须 fail-loud，不能伪装成已支持。

我们致力于解决传统 RGB 渲染器难以正确模拟的光学现象：色散 (Dispersion)、同色异谱 (Metamerism)、薄膜干涉 (Thin-film Interference)、偏振 (Polarization)。荧光/磷光、衍射、散斑、全息和相干多路径干涉属于 Phase W 的可选波动求解器范围，尚未作为默认渲染路径完成。

## 2. 核心哲学 (Core Philosophy)
*   **光谱优先 (Spectral First)**: 拒绝 RGB 色彩空间的近似。所有计算在 360nm-830nm 可见光波段进行高精度采样。
*   **物理约束优先 (Physically Constrained)**: 材质模型必须通过能量守恒、PDF/MIS 一致性和 reference/oracle 验证；未实现的高级物理必须显式报错。
*   **原创架构 (Original Architecture)**: 不照搬现有开源代码。我们针对光谱计算的特殊性，设计专有的 Wavefront GPU 架构。

## 3. 核心技术栈 (Advanced Tech Stack)
*   **语言标准**: C++23 (利用 `std::expected`, `std::format`, `constexpr` 等特性)
*   **计算架构**: **Spectral Wavefront Path Tracing** (自研光谱波前路径追踪)
    *   针对 CUDA GPU 优化的 Data-Oriented Design
    *   支持百万级光谱资源域 (spectral domain/resource bins) 的采样积分；单条 GPU ray 携带小宽度 wavelength packet，而不是百万 lane
*   **色彩科学**: CIE 1931 Standard Observer (2-degree) + 高斯拟合光谱重建
*   **几何内核**: CUDA path tracing + 自研 BVH/scene compiler；OptiX 不是当前默认依赖。

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
- [x] **光谱并行化**: Phase E 已完成 runtime wavelength packet，当前 GPU packet quadrature lanes 为 8-32；Phase L 已将 packet carrier 命名为 `SpectralPacket`，并允许 `packet_lanes=1` 作为 sampled wavelength 模式，避免把 packet 宽度误认为全局光谱域。
- [x] **百万级光谱域**: Phase L 已将 `domain_bins` 与 `packet_lanes` 解耦；材质/SPD 已通过 `SpectralResource` 按 lambda 查询，已有 1M-bin CPU oracle 与 GPU sampled estimator 对照，explicit spectral texture 已改为 source-sample resource descriptor；MaterialGraph texture/Add/Mix 已接入 spectral expression graph；distributed contract/file backend 已携带 spectral-domain shard、wavelength PDF 与 frame shard metadata；runtime plan 现在会给出 sampler/cache/stream preset，并在显式 resident cache 超预算时于 GPU 初始化前拒绝。`tools/benchmarks/run_phase_l_spectral_smoke.ps1` 提供 1M-domain sampled HDR smoke 入口；basis/tile cache runtime 与系统化性能套件归后续性能阶段扩展。

### 阶段三：超越几何光学 (Beyond Ray Optics) [进行中]
- [x] **偏振光渲染 (Polarization)**: 引入 Stokes 矢量与 Mueller 矩阵，模拟天空偏振、全反射相位偏移。
- [x] **光谱金属材质**: 支持基于波长的复折射率 (n, k) 渲染（金、铜、铝等预设）。
- [x] **薄膜干涉 (Thin-film Interference)**: 已实现局部单层 Airy-style complex boundary evaluator，并接入 Stokes/Mueller。它不是通用多层 coating 或全路径相干传播。
- [x] **体积光与次表面散射 (Volume/SSS)**: 已完成均质介质 Beer-Lambert + Henyey-Greenstein radiative transfer；Mie/Rayleigh/相干体散射仍属于 Phase W 后续。
- [ ] **波动光学求解器 (Phase W)**: 统一配置/API/fail-loud 合同，逐步加入衍射相机、coherent field、partial coherence、diffractive materials、fluorescence 和 local full-wave coupling。
- [ ] **荧光与磷光 (Fluorescence & Phosphorescence)**: Phase W 计划支持 excitation-to-emission wavelength conversion、能量守恒和 PDF 转换。

### 阶段四：AI 物理融合 (AI-Physics Hybrid) [远期]
- [ ] **神经光谱缓存 (Neural Spectral Cache)**: 训练神经网络预测光谱分布，而非简单的 RGB 降噪。
- [ ] **可微光谱渲染**: 支持从照片反推材质的化学成分。

### 阶段五：物理与流体模拟 (Physics & Fluid) [新增/进行中]
- [x] **SPH 流体系统**: 实现了基于空间哈希的光滑粒子流体动力学 (SPH) 模拟。
- [x] **Marching Cubes**: 实现了流体粒子的实时等值面网格化，支持流体渲染。
- [x] **物理-声学接口**: 定义了碰撞事件监听系统，支持物理交互驱动声学反馈。
- [x] **Glass Cup 演示**: 集成了包含流体、刚体和静态容器的综合演示场景。

## 5. 最新更新 (Latest Updates - 2026-06-15)

### 5.1 Phase L: Large Spectral Domain
- `domain_bins` 与 `packet_lanes` 已解耦：百万级光谱资源域不再意味着单条 GPU ray 携带百万 lane。
- 显式光谱纹理使用 source-sample resource descriptor，RGB texture 保留硬件 filtering。
- MaterialGraph texture/Add/Mix 已接入 spectral expression graph，不再回退到 packet-only flatten。
- Distributed contract/file backend 已携带 spectral-domain shard、wavelength PDF 与 frame shard metadata。
- Runtime spectral plan 会根据资源工作集和 resident budget 在 GPU 初始化前拒绝超预算配置。

### 5.2 Phase W: Wave Optics Solver Track
- 新增 `docs/Phase_W_Wave_Optics_Audit.md`，明确当前 renderer 是 spectral/polarimetric radiometric path tracer，而不是通用波动传播器。
- W.0 已修复 rough dielectric direct-light MIS 与 BSDF/PDF 不一致：wavelength、UV effective thin-film thickness 与 dispersion clamp 现在进入同一 per-channel PDF 语义。
- W.1 已建立 `WaveOpticsConfig`，贯穿 `RenderConfig`、JSON、CLI、C ABI 和 pyure；非 radiometric 模式与未实现 wave feature 默认在运行前拒绝，未知 mode 不会静默回退。
- W.2 已启动衍射相机基准：新增 `WaveFieldGrid` host complex field carrier、`FraunhoferFieldGrid`、direct Fraunhofer/Fresnel/angular-spectrum CPU propagation oracle、圆孔 Airy PSF host oracle、离散 `PsfKernel` reference、diffraction-limited MTF oracle、`CircularPupil` defocus phase reference 和 feature-gated `DiffractionCameraPlan`，锁定第一暗环、encircled energy、波长缩放、sensor 半径、kernel 归一化、cutoff frequency、pupil 相位/遮罩、复场网格功率、传播算子归一化与半开配置拒绝；direct `GpuRenderEngine` scene load 也会在 GPU 初始化前拒绝未实现 camera diffraction film，后续 GPU diffraction camera 必须对齐该基准。
- W.3 已建立相干场基础合约：`ComplexSpectrum`、`JonesSpectrum`、`CoherenceMetadata`、OPL phase accumulation helpers 和 `ComplexFieldFilm` host accumulator 已能区分 coherent `|sum E|^2` 与 incoherent `sum |E|^2`；主 GPU path tracer 的相干输运和 distributed coherent merge 仍按 Phase W 后续步骤接入。
- W.4 已完成首个传播算子闭环：`PropagationOperatorKind` / `PropagationConfig` / `PropagationResult` 统一封装 Fraunhofer、Fresnel、angular-spectrum、Rayleigh-Sommerfeld 与 Huygens-Fresnel CPU oracle；首个 CUDA backend 是 Fraunhofer direct DFT reference backend，后续 FFT/tiling 属于性能升级。

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
Render Engine/
├── libs/
│   ├── ure_types/      # header-only core types, SceneIR, RenderConfig
│   ├── ure_core/       # CUDA renderer, BVH, GPU scene compiler
│   ├── ure_sceneio/    # glTF/GLB, image/SPD loading
│   ├── ure_config/     # JSON + CLI config
│   ├── ure_diag/       # logging/diagnostics
│   └── ure_physics/    # optional physics/acoustic modules
├── apps/ure_cli/       # offline CLI renderer
├── tests/              # host + GPU test executables
├── docs/               # technical docs and phase audits
├── scripts/            # build/check scripts
├── third_party/        # header-only dependencies
└── CMakeLists.txt      # 构建配置文件
```

## 7. 构建与运行
### 依赖准备
1.  CMake 3.25+
2.  支持 C++23 的编译器 (MSVC 19.34+, Clang 16+, GCC 13+)
3.  **CUDA Toolkit 13.0+**
4.  Visual Studio 2022 Build Tools on Windows

### 编译步骤
```powershell
.\scripts\build_x64.ps1 -BuildDir build_modular_x64 -Config Release
```

---
*UltraRender - Rendering Reality, One Wavelength at a Time.*
