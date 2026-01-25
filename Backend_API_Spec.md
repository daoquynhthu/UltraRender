# UltraRender 后端 API 现代化改造文档 (Backend API Modernization Spec)

## 1. 概述 (Overview)
为了支持前端编辑器（UltraStudio）的实时交互需求，必须将 UltraRender 引擎从当前的“单次批处理模式”重构为“**交互式服务模式**”。
本核心目标是解耦渲染循环，提供细粒度的帧控制、状态管理和数据访问接口。

## 2. 核心接口设计 (Core Interface Design)

### 2.1 `RenderEngine` 接口升级
位于 `include/api/ure_api.hpp`。将原有的阻塞式 `render()` 方法拆解为细粒度控制方法。

```cpp
class RenderEngine {
public:
    virtual ~RenderEngine() = default;

    // --- 初始化与加载 ---
    // 加载场景数据（全量加载）
    virtual void load_scene(const Scene& scene) = 0;
    
    // 更新相机（轻量级更新，不重建 BVH）
    // 触发内部 reset_accumulation()
    virtual void update_camera(const Camera& camera) = 0;

    // --- 交互式渲染控制 ---
    // 执行一次渲染 Pass (例如 1 SPP)
    // 返回值：当前累积的总 SPP
    virtual int render_pass() = 0;

    // 重置累积缓冲区（清空画面，用于场景变动后）
    virtual void reset_accumulation() = 0;

    // 获取当前渲染状态
    virtual int get_current_spp() const = 0;

    // --- 数据访问 ---
    // 获取帧缓冲区指针 (用于前端显示/保存)
    // 返回 RGB float 数组 [r, g, b, r, g, b, ...]
    virtual const std::vector<float>& get_frame_buffer() const = 0;
    
    // 获取原始 AOV (Arbitrary Output Variable) - 可选，用于调试
    // virtual const std::vector<float>& get_aov_buffer(AOVType type) const = 0;

    // --- 传统兼容 ---
    // 保持原有阻塞接口，但在内部调用 render_pass()
    virtual void render(const RenderSettings& settings) = 0;
};
```

### 2.2 `GpuEngineImpl` 实现变更
位于 `src/api/gpu_engine_impl.cpp`。

*   **状态管理**：新增 `m_current_spp` 成员变量追踪当前进度。
*   **显存管理**：`get_frame_buffer()` 需要将 GPU 显存数据异步拷贝回 CPU（或者在未来支持 CUDA-GL Interop 直接映射）。当前阶段先实现 CPU 回读。
*   **Kernel 调用**：将 `path_trace` kernel 的调用逻辑从循环中提取出来，改为单次调用。

## 3. 新增辅助类 (New Helper Classes)

### 3.1 `RenderSession` (渲染会话)
位于 `include/api/render_session.hpp` (新增)。
作为“控制器”，管理 `Scene` 和 `RenderEngine` 的生命周期，处理高层逻辑。

```cpp
class RenderSession {
public:
    RenderSession();
    
    // 初始化引擎
    void initialize(BackendType type = BackendType::GPU);
    
    // 加载场景
    void load_scene(const std::string& filepath);
    void load_scene(const Scene& scene);
    
    // 交互操作
    void set_preview_mode(bool enabled); // 预览模式下可能降低分辨率或深度
    void move_camera(float dx, float dy, float dz);
    
    // 渲染循环步进
    void step(); // 调用 engine->render_pass()
    
    // 导出
    void save_snapshot(const std::string& filepath);
    
private:
    std::unique_ptr<RenderEngine> m_engine;
    Scene m_current_scene;
    RenderSettings m_settings;
};
```

## 4. 迁移计划 (Migration Plan)

1.  **Phase 1: 接口定义 (Interface Definition)**
    *   修改 `ure_api.hpp`，添加纯虚函数定义。
    *   确保不破坏现有 `main.cpp` 的编译（提供默认空实现或快速跟进实现）。

2.  **Phase 2: GPU 实现 (GPU Implementation)**
    *   重构 `GpuEngineImpl`。
    *   将 `gpu_driver.cu` 中的 `render_loop` 拆解为 `launch_kernel`。
    *   实现显存到主存的 `copy_to_host` 同步逻辑。

3.  **Phase 3: 验证 (Verification)**
    *   编写 `tests/interactive_test.cpp`，模拟“渲染5帧 -> 移动相机 -> 渲染5帧”的流程，验证图像是否正确重置和累积。

## 5. 风险控制

*   **性能损耗**：`render_pass()` 每次调用可能会引入 Kernel 启动开销。
    *   *对策*：在 `render_pass()` 内部可以一次性发射多个 batch（例如一次跑 5 SPP），减少 CPU-GPU 通信频率。
*   **线程安全**：前端 UI 线程和渲染线程可能冲突。
    *   *对策*：API 设计为非线程安全，由调用者（Session 层）保证单线程调用，或在 Session 层加锁。
