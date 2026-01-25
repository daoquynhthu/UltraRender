# 前端编辑器架构设计文档 (Editor Architecture Specification)

## 1. 概述 (Overview)
**UltraStudio** 是 UltraRender 引擎的集成开发环境（IDE）前端。它旨在提供一个高效、直观的交互界面，支持场景编辑、程序化建模、物理脚本编写及实时渲染预览。
本架构采用 **Docking 布局** 模式，将渲染视口、逻辑编辑与参数控制深度集成，目标是成为一个生产级的数字内容创作（DCC）工具。

## 2. 核心架构设计 (Core Architecture)

### 2.1 交互模式 (Interaction Model)
界面采用 **ImGui Docking** 系统，划分为四大核心区域：

1.  **3D Viewport (渲染视口)**
    *   **功能**：实时显示渲染结果（光栅化预览或路径追踪结果）。
    *   **交互**：集成 ImGuizmo，支持对物体的平移、旋转、缩放操作；支持鼠标框选与射线拾取。
    *   **多模式显示**：支持切换 Wireframe, Albedo, Normal, Final Render 等不同视图模式。

2.  **Node Graph / Script Editor (逻辑核心)**
    *   **Node Graph**: 基于 **ImNodes** 的可视化节点编辑器。用于构建程序化建模流程（如生成分形地形、排列植被）。
    *   **Script Editor**: 集成 Python/Lua 脚本编辑器，支持语法高亮。允许用户编写物理更新逻辑（`on_update(dt)`）和场景构建脚本。

3.  **Properties Panel (属性面板)**
    *   **功能**：基于反射机制（Reflection）自动生成 UI。
    *   **机制**：当选中场景对象时，面板自动列出其所有成员变量（颜色、粗糙度、位置等），并提供对应的控件（Slider, Color Picker）。

4.  **Console & Timeline (控制台与时间轴)**
    *   **Console**: 命令行接口，支持输入指令控制引擎参数（如 `-spp 5000`），并显示系统日志。
    *   **Timeline**: 动画与物理模拟的时间轴控制（Play, Pause, Reset, Keyframe）。

### 2.2 技术栈 (Tech Stack)
*   **Window System**: **GLFW** (轻量级窗口管理与输入处理)
*   **UI Framework**: **Dear ImGui (Docking Branch)** (即时模式 GUI)
*   **Node Editor**: **ImNodes** (节点图绘制)
*   **Scripting**: **Python (via pybind11)** (脚本与指令逻辑)
*   **Rendering API**: **OpenGL 4.5** (UI 绘制与光栅化预览)

---

## 3. 后端升级规划：平行光栅化管道 (Parallel Rasterization Pipeline)

为了支持高帧率的实时预览与交互，我们将构建一个独立于现有路径追踪（Path Tracing）管道的**光栅化渲染管道**。

### 3.1 设计原则
*   **平行独立 (Parallel & Independent)**：
    *   光栅化管道与路径追踪管道共享同一套**场景数据（Scene Data）**，但拥有独立的**渲染后端（Render Backend）**。
    *   修改光栅化管道的代码绝不影响离线渲染的物理正确性。
*   **互不干扰 (Non-Intrusive)**：
    *   路径追踪器继续使用 CUDA/OptiX。
    *   光栅化器使用现代 OpenGL (4.5+) 或 Vulkan。
    *   两者通过抽象的 `IRenderer` 接口共存，用户可在编辑器中实时切换。

### 3.2 架构图
```mermaid
graph TD
    SceneData[Scene Data (CPU)] --> |Sync| PT_Backend[Path Tracer (CUDA)]
    SceneData --> |Sync| Rast_Backend[Rasterizer (OpenGL)]
    
    subgraph Path Tracing Pipeline
        PT_Backend --> PT_BVH[BVH Build]
        PT_Backend --> PT_Kernel[Monte Carlo Kernel]
        PT_Kernel --> PT_Buffer[Frame Buffer]
    end
    
    subgraph Rasterization Pipeline
        Rast_Backend --> Rast_VBO[VBO/VAO Upload]
        Rast_Backend --> Rast_Shader[PBR Shader]
        Rast_Shader --> Rast_FBO[G-Buffer / Color]
    end
    
    PT_Buffer --> |Texture Map| Viewport[Editor Viewport]
    Rast_FBO --> |Texture Map| Viewport
```

### 3.3 实施步骤 (Implementation Steps)

1.  **抽象层构建**：定义 `IRenderer` 基类，将现有的 `GpuEngine` 重构为 `PathTracer` 子类。
2.  **光栅化器实现**：新建 `Rasterizer` 类，实现基础的 PBR 光栅化渲染（Albedo + Simple Lighting）。
3.  **数据同步机制**：建立 `SceneProxy`，负责将 CPU 端的 `Scene` 对象高效同步到 OpenGL 的 VBO/UBO 中。
4.  **视口集成**：在 ImGui 视口中根据用户选择，绑定 `PathTracer` 或 `Rasterizer` 的输出纹理。

---

## 4. 数据流与反射 (Data Flow & Reflection)

为了实现属性面板的自动化显示，需要构建一套轻量级的 C++ 反射系统。

*   **Type Descriptor**: 为 `GpuMaterial`, `GpuSphere` 等结构体注册元数据（字段名、类型、偏移量）。
*   **UI Generator**: 编写通用函数 `DrawUI(void* obj, TypeDescriptor* type)`，根据元数据自动调用 `ImGui::SliderFloat`, `ImGui::ColorEdit3` 等。

## 5. 脚本系统集成 (Scripting Integration)

*   **Python Embedding**: 在编辑器启动时初始化 Python 解释器。
*   **Binding**: 使用 pybind11 将 C++ 的 `Scene`, `Object`, `Renderer` 接口暴露给 Python。
*   **Execution**:
    *   **Console**: 用户输入 `scene.add_sphere(...)` -> Python 执行 -> C++ Scene 更新 -> 视口刷新。
    *   **Scripts**: 每帧调用 `update()` 函数，允许脚本修改物体变换矩阵，实现物理模拟驱动。
