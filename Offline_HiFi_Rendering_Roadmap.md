# 离线高保真动态渲染技术规划书 (Offline High-Fidelity Dynamic Rendering Roadmap)

## 1. 项目愿景与目标
本项目旨在构建一个**离线级高保真动态渲染引擎**，核心目标是生成符合物理规律的、电影级质量的动态影像。
引擎将融合**光谱路径追踪**、**偏振光模拟**与**高精度物理仿真**，支持程序化内容生成（PCG），并为未来引入AI辅助工作流奠定基础。

---

## 2. 技术架构规划

### 2.1 渲染核心架构 (Rendering Core)
构建分层、可扩展的混合渲染架构，解耦逻辑层与硬件层。

*   **渲染硬件接口 (RHI) 抽象层**：
    *   设计统一的 `IGpuDevice` 接口，屏蔽底层 API 差异。
    *   **后端支持**：
        *   **Primary**: Vulkan (跨平台，高性能，原生支持 Ray Tracing)。
        *   **Windows Optimized**: DirectX 12 Ultimate (深度集成 DXR)。
        *   **Legacy/Compute**: CUDA (保留当前核心作为 Reference Path Tracer)。
    *   **多后端策略**：采用插件式架构，允许在启动时动态加载不同的后端模块。

*   **离线与实时融合 (Hybrid Rendering)**：
    *   **实时预览 (Viewport)**：利用光栅化 (Rasterization) 或低采样率路径追踪 + AI 降噪 (DLSS/OptiX) 提供 >30fps 的交互帧率。
    *   **离线最终帧 (Final Frame)**：全特性的光谱/偏振路径追踪，支持复杂光路（如焦散、体积多重散射）。
    *   **切换机制**：基于 `RenderGraph` 的动态拓扑切换。编辑模式下启用简化 Pass，渲染模式下启用全精度 Pass。

### 2.2 物理引擎集成 (Physics Integration)
实现“仿真-渲染”双向耦合，确保动态效果的物理真实性。

*   **接口设计**：
    *   定义 `IPhysicsWorld` 与 `IRigidBody` / `ISoftBody` 抽象接口。
    *   **数据同步**：采用**双缓冲 (Double Buffering)** 机制。物理线程更新 `SimState`，渲染线程插值读取 `RenderState`，消除动态模糊中的时序抖动。
*   **交互机制**：
    *   **刚体/柔体**：支持基于粒子的流体与布料模拟（PBD/XPBD），直接映射到渲染网格。
    *   **碰撞反馈**：物理碰撞事件可触发渲染材质变化（如破碎、形变、发光）。

### 2.3 动态帧处理管线 (Dynamic Frame Pipeline)
专为离线动画设计的高精度数据流管线。

*   **G-Buffer 扩展 (Deep G-Buffer)**：
    *   除了基础的 Albedo/Normal/Depth，必须输出：
        *   **Motion Vectors** (3D世界空间 + 2D屏幕空间)：用于高质量运动模糊 (Motion Blur) 和时域抗锯齿 (TAA)。
        *   **Material ID & Object ID**：用于后期蒙版。
        *   **Spectral/Polarization Channels**：保存偏振态 (Stokes) 和光谱数据供后期合成。
*   **后处理合成**：
    *   **Compositor Graph**：节点式后期处理（Tone Mapping, Bloom, Lens Distortion）。
    *   **AI 增强**：集成超分辨率 (Super Resolution) 和帧插值 (Frame Generation) 接口，加速预览序列生成。

### 2.4 程序化建模与资产管线 (PCG & Asset Pipeline)
深化现有的程序化生成能力，构建“代码驱动内容”的生产流。

*   **PCG 核心模块**：
    *   **参数化模型**：扩展 `Procedural` 类，支持基于节点的几何体生成（L-System 植物、分形地形、建筑结构）。
    *   **运行时细分 (Runtime Tessellation)**：支持 Catmull-Clark 细分曲面，在渲染时根据视距动态生成微多边形。
*   **材质与纹理生成**：
    *   **过程纹理 (Procedural Texturing)**：基于噪声函数 (Perlin, Voronoi) 的 3D 纹理生成，无需 UV 拆分，彻底解决接缝和分辨率限制。
    *   **材质图 (Material Graph)**：支持多层材质混合 (Layered Materials)，自动处理光谱响应曲线。
*   **版本与依赖管理**：
    *   引入资产哈希 (Asset Hashing) 机制，确保程序化生成结果的确定性 (Determinism)。
    *   基于 JSON/YAML 的资产元数据描述，追踪 PCG 参数变更历史。

### 2.5 资源与场景管理 (Resource & Scene Management)
应对海量数据的存储与流送。

*   **场景描述格式**：
    *   **USD (Universal Scene Description)**：作为核心场景交换格式，支持分层覆盖 (Layering) 和变体 (Variants)。
*   **流式加载 (Streaming)**：
    *   **Bindless Rendering**：利用现代 GPU 特性，解除纹理和缓冲区的绑定数量限制。
    *   **Virtual Texturing**：支持超大分辨率纹理的按需加载。
*   **加速结构管理**：
    *   两级 BVH (TLAS/BLAS) 动态更新策略：
        *   **静态物体**：高质量 SAH 构建，只构建一次。
        *   **动态物体**：快速重构 (Refit) 或低质量快速构建 (Fast Build)。

---

## 3. 关键技术选型与评估

### 3.1 核心组件选型

| 组件 | 候选方案 | 选型建议 | 理由 |
| :--- | :--- | :--- | :--- |
| **渲染 API** | Vulkan vs DX12 vs CUDA | **Vulkan + CUDA (Hybrid)** | Vulkan 提供最佳跨平台支持和光追控制；CUDA 保留用于现有的高精度光谱/偏振计算核心。 |
| **物理引擎** | PhysX 5 vs Jolt vs Bullet | **Jolt Physics** | Jolt 在多核 CPU 上性能卓越，架构现代，易于集成，且开源协议友好 (MIT)。 |
| **场景格式** | USD vs glTF vs Custom | **USD** | 影视工业标准，能够完美处理复杂的场景层级、动画缓存和协作流程。 |
| **数学库** | GLM vs Eigen vs Custom | **GLM (Gpu Compatible)** | 保持与 GLSL/HLSL 的内存布局一致性，减少 CPU-GPU 数据转换开销。 |
| **脚本/逻辑** | Lua vs Python vs C# | **Python** | 拥有最强的 PCG 生态 (NumPy, SciPy) 和 AI 库支持，适合离线管线。 |

### 3.2 风险评估与应对

#### 风险 1: 离线渲染与物理仿真的确定性 (Determinism)
*   **问题**：浮点数误差可能导致不同机器上的物理模拟结果不一致，破坏渲染农场的帧连续性。
*   **应对**：
    *   在物理步进中使用定点数数学库或严格的 IEEE 754 浮点控制模式。
    *   将物理烘焙 (Bake) 为 Alembic/USD 缓存文件，渲染时只读取缓存，不实时模拟。

#### 风险 2: 光谱/偏振计算的显存开销
*   **问题**：全光谱和偏振数据会使 G-Buffer 和纹理体积膨胀 4-8 倍。
*   **应对**：
    *   **Hero Wavelength Sampling**：每次路径追踪只采样 4-8 个波长，通过多帧累积实现全光谱。
    *   **纹理压缩**：对光谱反射率曲线进行 PCA 降维压缩存储。

#### 风险 3: 混合架构的维护复杂度
*   **问题**：同时维护 CUDA 和 Vulkan 两套后端极其困难。
*   **应对**：
    *   **阶段性演进**：短期内（1.0阶段）保持 CUDA 作为唯一后端，专注物理与 PCG 集成。
    *   中期引入 Slang 或 HLSL 作为统一着色器语言，自动编译到 PTX (CUDA) 和 SPIR-V (Vulkan)。

---

## 4. 实施路线图 (Milestones)

*   **Phase 0: 实时交互视口 (Immediate Priority)**
    *   集成 GLFW/SDL2 窗口系统。
    *   实现 CUDA-OpenGL 互操作，在窗口中实时显示渲染结果。
    *   添加基础的 FPS 相机控制（WASD + 鼠标）。

*   **Phase 1: 基础夯实 (Current - Q2)**
    *   完善光谱/偏振路径追踪核心 (已完成部分)。
    *   建立基于 Python 的场景加载与参数化接口。
    *   实现基础的 G-Buffer 输出 (Motion Vector)。

*   **Phase 2: 物理与动态化 (Q3 - Q4)**
    *   集成 Jolt Physics 物理引擎。
    *   实现刚体动力学与渲染场景的自动同步。
    *   开发 USD 导入/导出器。

*   **Phase 3: 生产力工具 (Next Year)**
    *   程序化节点编辑器 (Node Graph)。
    *   分布式渲染支持。
    *   AI 降噪与超分集成。
