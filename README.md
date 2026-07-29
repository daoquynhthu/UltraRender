# UltraRender

UltraRender 是一个处于持续开发阶段的 CUDA 离线渲染器。当前可执行路径以光谱辐射度、Stokes/Mueller 偏振和 wavefront path tracing 为基础；项目同时包含原生场景格式、材质图、体积相函数、会话 API、分布式文件契约，以及若干物理、声学和波动光学的参考实现或接口。

本项目尚未达到通用生产渲染器的成熟度。功能范围以 [PLAN.md](PLAN.md) 的权威施工队列和 [STATUS.md](STATUS.md) 的当前能力表为准；历史设计或阶段计划不代表现行能力。

## 当前状态

- Phase Q、Phase R、Phase T 与 Phase V 已完成。Phase W.2 的 diffraction camera、W.5 的 diffractive MaterialGraph operators 与 W.6 的 fluorescence material 已接入显式启用的 CUDA wavefront 路径；W.7 已建立部分相干的参考与统计合同，当前施工游标是 `W.9`。
- 默认完整场景渲染后端仍是 CUDA。Vulkan RT 与 DXR 已具备多 BLAS/TLAS build、compaction、transform refit/rebuild、scratch budget 和 telemetry；OptiX SDK 保持可选，存在时启用同一构建合同和实际 raygen/miss/closest-hit pipeline，缺失时不影响 CUDA self-compute、Vulkan 或 D3D12。一个由同一 SceneIR lower 的固定 fixture 已对齐四类 provider 的 shadow/closest hit、transform、material、UV/normal/tangent metadata 和小型 AOV；这不等同于任意 SceneIR 的完整 radiometric integrator 已迁移到 native provider。
- 后端选择、adapter identity、能力位、limits、显存预算及 driver/compiler identity 已贯穿 JSON、CLI、C ABI 和 pyure。Acceleration provider、build quality、update policy、cluster gate、stats gate 与 scratch budget 使用独立的向后兼容配置合同；CUDA `self_compute` 的质量预设、auto/static/refit/rebuild update、scratch-budget enforcement 与 versioned acceleration stats 已可执行。Native construction 与 traversal parity fixture 已完成。V.8-V.10 已加入 SDK-free clustered geometry resource、host/CUDA physical-error selector，以及 rigid/deforming/topology-change lifecycle planner；SceneDiff mesh mutation会校验并事务回滚。当前 CUDA 对 deformation/topology 采取正确但保守的完整 BLAS/TLAS rebuild，显式请求尚不可用的 BLAS refit 会失败。主 renderer 的 cluster flag 在完整 SceneIR traversal lowering 前继续明确失败。
- Slang 2026.14 已完成固定版本、多目标编译、反射、debug mapping、CUDA 占用率及数值执行验证。Vulkan SPIR-V 与 D3D12 DXIL 复用共享光谱/偏振及加速语义；D3D12 release DXIL 由固定 Windows SDK DXC 确定性生成，debug artifact 单独生成。现有 CUDA production kernels 仍是私有 `.cu` fast path，Slang RHI 未被引入。
- 纯 C++ `ure_runtime` 已定义 device、queue、timeline fence、event、buffer、image、sampler、module、pipeline、资源规划、dispatch DAG、execution graph、acceleration provider/selection/hit metadata、multi-backend scheduling 和 device-loss 合同；CUDA production backend 已实现这些合同的私有 lowering，并覆盖 path、wave、multi-GPU、PTX pipeline 与结构化错误路径。
- `ure_vulkan` 已实现 Vulkan 1.3 adapter、queue、timeline、buffer/image/sampler、SPIR-V module、typed descriptor、specialization、pipeline cache、validation/debug-utils、device-loss 映射，以及私有 BLAS/TLAS build 与 ray-query descriptor lowering；其公共头不暴露 Vulkan SDK 类型。
- `ure_d3d12` 已实现 Windows D3D12 adapter、buffer/image/sampler、DXIL pipeline、descriptor heap、queue/fence、DRED，以及私有 DXR BLAS/TLAS 与 inline ray-query lowering；其公共头不暴露 Windows、D3D12 或 DXGI 类型。
- SDK-free scheduler 会在执行前校验 worker feature、float precision、coherence mode、显存下限和共享 kernel semantics，以稳定整数权重划分 sample ranges；distributed file v5 保存 backend/adapter、driver/compiler、executable digest 和 resource-cache provenance。兼容 CUDA/Vulkan/D3D12 sample shards 可合并，不兼容或重叠分片会拒绝。
- `run_phase_t_validation_suite.ps1` 以机器可读报告统一检查物理 unit oracle、hit/framebuffer fixture、CUDA reference render、variance/MSE、device loss、budget、cache、cold/warm launch、VRAM 和 throughput。CUDA/Vulkan 必测，DXR 按实际 capability 执行；不同后端或工作负载的差异必须带阈值和原因分类。
- `run_phase_v_validation_suite.ps1` 输出稳定的 `ure.phase_v.validation.v1` 报告，聚合 dense build/trace/VRAM、async construction、provider parity、cluster LoD、dynamic update、distributed v5 resource/worker/cache provenance 和完整 CTest。farm worker 入口要求 clean tree 并记录 run/shard/sample coverage；这类证据不扩大 native renderer 或 clustered traversal 的已实现范围。
- 当前 Release 构建注册 54 个 CTest；测试数量只表示已登记门禁规模，不等同于功能覆盖率或发布成熟度。
- 当前 CUDA self-compute acceleration 使用每 mesh object-space BLAS 和独立 world-space instance TLAS。默认/`fast_build` 保留兼容的 median BVH2；`balanced` 使用 binned object SAH 与 72-byte quantized BVH4，`high_quality` 使用受引用预算约束的 spatial SAH/SBVH 与 116-byte quantized BVH8。transform hot update 可按 policy refit 或 rebuild TLAS；deforming/topology-changing mesh mutation 会重建 BLAS/TLAS并输出 timing/correctness telemetry。bounded async build、pinned-stream compact upload 和 scratch/device budget preflight 已执行。Vulkan RT、DXR 和可选 OptiX 已完成 native construction lifecycle，并通过同一 SceneIR fixture 的 cross-provider traversal/hit/AOV 门禁；通用 native integrator lowering与 clustered SceneIR traversal 仍未完成。
- 默认积分器是 spectral/polarimetric radiometric wavefront path tracer。
- 显式启用的 camera diffraction 支持圆孔或规则叶片 pupil、defocus phase、sensor-pixel integration 与 wavelength-binned PSF resolve。独立的材质开关支持 grating、sinusoidal phase mask、ideal zone plate、blazed DOE、有界 RCWA/FMM Jones table，以及有界 Stokes-shift excitation-emission fluorescence resource；它们都限制在各自校验过的普通 CUDA wavefront 组合。fluorescence 使用相机路径的 adjoint wavelength transition，并保留 detector wavelength 与 phosphorescence delay；当前 Beauty film 仍是 steady-state。partial coherence 目前仅提供 cross-spectral-density、Gaussian-Schell source、coherent realization、generalized ray、host/CUDA ensemble reduction 与正确平均顺序的参考合同，生产 session 仍明确拒绝；coherent scene transport 和局部全波求解尚未实现。
- production unbiased/spatial ReSTIR DI 与受限 ReSTIR PT suffix reuse 已完成验证。GPU specular-manifold、BDPT、VCM 和独立 PSSMLT 已通过各自统计门禁；MLT 与 bidirectional/VCM/manifold 的组合仍明确拒绝。
- 完整 CUDA 渲染基线为 Windows 11、Visual Studio 2022 Build Tools、CUDA 13.0 和 NVIDIA compute capability 12.0。Vulkan foundation 另有 Linux GCC/Ninja、Windows NVIDIA native ray-query 及 NVIDIA/Intel compute-BVH 证据；D3D12 foundation 有 Windows NVIDIA DXR 1.1、compute fallback、texture/descriptor 与 cross-queue fence 证据。这些门禁不等同于完整 Linux、D3D12 或非 NVIDIA 场景渲染支持。

## 已验证的能力

以下描述限定为当前仓库中已有实现和测试覆盖，不表示覆盖所有场景或达到商业渲染器的稳定性：

- 运行时光谱域与 GPU wavelength packet 分离；GPU packet 上限为 32 lanes，并支持单波长 sampled mode。
- CIE 1931 2° observer、显式 wavelength PDF、色散、导体复折射率和局部单层薄膜边界模型。
- CUDA wavefront diffraction camera：归一化的 360–830 nm PSF bank、仅对 PSF 插值分 bin 的精确波长 XYZ film、圆形/规则多叶片光阑、离焦相位和 2x2 sensor aperture integration；普通 material path 与关闭状态保持不变。
- CUDA diffractive material operators：按 wavelength lane 采样传播级次并传输完整 2×2 complex Jones response；RCWA/FMM table 受 4,096-entry 预算、完整采样网格和联合被动性校验约束。该路径是非相干 radiometric thin-sheet scattering，不等同于 coherent field propagation。
- 部分相干参考层：有界 Hermitian PSD cross-spectral density、Gaussian-Schell 扩展光源、确定性 coherent realizations、Jones/OPL generalized rays、OCT/interferometry coherence oracle、speckle 统计门禁，以及保持 coherent-before-incoherent 顺序的事务式 raw-field merge。CUDA 只执行 ensemble-to-CSD reference reduction，不表示主 path tracer 已支持部分相干场。
- Stokes 状态与若干 Mueller 边界变换。它们属于强度域偏振传输，不等同于跨路径相干场求解。
- Lambertian、metal、dielectric、cloth、有限厚度 dielectric layer 和受约束的 BSDF mix/material graph 路径。
- 均质体积、Henyey–Greenstein、Rayleigh，以及资源驱动的光谱 Mie `eval/pdf/sample`、NEE 和 continuation。
- glTF/GLB 导入、MaterialX 受支持子集适配、OBJ/图像/SPD 等资源输入。
- URE 原生 `.ure`、`.urescene`、`.urepkg` 和可重建 `.urecache` 契约，包含校验、迁移、打包和检查工具。
- `RenderSession`、C ABI、pyure、AOV、场景 mutation，以及 sample-range/file-backend 分布式契约。
- 同构/异构 GPU 与 farm worker 的 capability negotiation、sample-space 分区、resource-cache identity 和 framebuffer merge metadata。跨机器任务传输、worker 生命周期管理和完整场景 portable-backend 执行仍不属于已完成范围。
- production ReSTIR DI 的 temporal/spatial reuse，以及 ReSTIR PT 的有界、版本化 path-suffix replay；超出该有界契约的 suffix 会明确失败，不会静默近似。
- GPU BDPT/VCM 与最多四事件的 specular-manifold estimator；其适用范围、独立 wavefront technique-AOV 对照和统计门禁见 [Phase R-P4 文档](docs/Phase_R_P4_Specular_Manifold.md)。
- R-P5 已完成 primary-sample-space replay、独立 GPU chains、对称 Laplace mutation、stratified bootstrap seeding、归一化、诊断与多 GPU chain identity。R-P7 的独立 sample-range/chain-identity 复核否定了旧版相关参考图的两场景结论；当前只保留可复现的 SDS small-light 正收益，并将 SDS、小光源、玻璃焦散和高遮挡记录为统计边界，不以数量替代统计独立性。更极端的面积补偿小光源只保留路径分布契约，不冒充在当前预算下稳定的统计证据。MLT 与 BDPT/VCM/manifold 的组合在共享光谱主样本合同完成前明确拒绝。设计与证据边界见 [Phase R-P5 文档](docs/Phase_R_P5_MLT.md)。
- R-P7 已在 clean commit 上通过 `Closure`：版本化工业验证报告聚合八类带哈希证据及 MSE、方差、色差、time-to-error 和吞吐指标；BDPT/VCM 独立收益与边界矩阵、4,096-SPP 分片合并以及同一可执行文件的 Nsight/VRAM 实测证据均通过验证。Phase R 已闭环。见 [Phase R-P7 文档](docs/Phase_R_P7_Industrial_Validation.md)。

## 明确未完成或受限的能力

- 不提供 CPU production integrator。
- 不提供交互式 OpenGL/Vulkan viewport。
- 不提供 OSL 编译器；MaterialX 是适配层，URE MaterialGraph 是内部权威模型。
- USD/Hydra adapter 计划在 Phase U 实现；当前 USD 导出边界会显式失败。
- native procedural plugin ABI 计划在 Phase X 实现。
- 物理和声学模块属于可选、实验性子系统；不能把已有 SPH、碰撞或音频组件理解为经过系统验证的通用仿真器。
- “百万级光谱域”指资源域可使用很大的采样/索引空间，不表示每条光线携带百万个波长 lane，也不表示所有百万规模工作负载均具备实时或固定性能保证。

## 工程结构

```text
Render Engine/
├── apps/ure_cli/       # 离线命令行入口
├── libs/ure_types/     # backend-neutral 类型与 SceneIR
├── libs/ure_runtime/   # backend-neutral GPU runtime contracts
├── libs/ure_vulkan/    # Vulkan 1.3 compute and acceleration runtime
├── libs/ure_d3d12/     # Windows D3D12/DXR optional runtime
├── libs/ure_core/      # 渲染核心、会话 API 和私有 CUDA production backend
├── libs/ure_sceneio/   # 原生场景、glTF、MaterialX、图像和光谱资源 I/O
├── libs/ure_config/    # JSON/CLI 配置
├── libs/ure_diag/      # 日志与诊断
├── libs/ure_physics/   # 可选物理/声学实验模块
├── pyure/              # Python ctypes 封装
├── tests/              # host、GPU 和 Python 测试
├── docs/               # 当前技术文档与归档设计记录
├── scenes/             # 示例和验证场景
└── scripts/            # 构建、静态审计和验证脚本
```

## 构建要求

仓库当前维护的构建路径是 Windows x64：

- Windows 11
- Visual Studio 2022 Build Tools
- CMake 与 Ninja（构建脚本负责定位）
- CUDA Toolkit 13.0
- 支持 C++23 的 MSVC host compiler

配置并构建 Release：

```powershell
.\scripts\build_x64.ps1 -BuildDir build_modular_x64 -Config Release
```

Ninja 可并行构建宿主代码和独立目标；高内存 CUDA 编译由工程内的
`ur_cuda_heavy_compile` job pool 单独限流。当前 16 GiB Windows/CUDA 13 开发机的
实测稳定最优深度为 2：三个最重 CUDA translation units 的重编译关键路径由约
602 秒降至 362 秒，深度 3 未继续缩短关键路径。不要为常规构建全局指定
`--parallel 1`。低内存或不同工具链环境可通过
`.\scripts\build_x64.ps1 -CudaHeavyCompileJobs <N>` 显式覆盖并重新配置。

运行完整注册测试：

```powershell
ctest --test-dir build_modular_x64 -C Release --output-on-failure
```

当前构建树注册 54 个 CTest。这个数字是当前快照，不应用作长期固定接口；以 `ctest -N` 的输出为准。

仅构建不依赖 GPU SDK 的公共基础库时，可关闭 CUDA backend：

```powershell
cmake -S . -B build_sdk_free -DUR_ENABLE_CUDA=OFF -DUR_BUILD_CLI=OFF -DUR_BUILD_TESTS=OFF
cmake --build build_sdk_free --config Release --target ure_runtime ure_sceneio ure_config
```

Vulkan compute/acceleration foundation 可在不安装 CUDA 或 Vulkan SDK 的情况下单独构建；运行时仍需要系统 Vulkan loader：

```powershell
cmake -S . -B build_vulkan -DUR_ENABLE_CUDA=OFF -DUR_ENABLE_VULKAN=ON -DUR_BUILD_CLI=OFF
cmake --build build_vulkan --config Release --target ure_vulkan test_vulkan_runtime test_vulkan_acceleration
```

Windows D3D12/DXR foundation 使用系统 D3D12/DXGI 和固定 Windows SDK DXC，可独立关闭：

```powershell
cmake -S . -B build_d3d12 -DUR_ENABLE_CUDA=OFF -DUR_ENABLE_VULKAN=OFF -DUR_ENABLE_D3D12=ON -DUR_BUILD_CLI=OFF
cmake --build build_d3d12 --config Release --target ure_d3d12 test_d3d12_runtime
```

OptiX provider 只需要 NVIDIA 官方 SDK/`optix-dev` headers，不成为默认构建依赖。以下门禁同时要求本机 Vulkan RT、DXR，并在提供 headers 时执行真实 OptiX GAS/IAS lifecycle：

```powershell
.\scripts\run_phase_v6_native_provider_gate.ps1 -OptixRoot <optix-sdk-or-optix-dev-root>
```

同一 SceneIR fixture 的 CUDA self-compute、可选 OptiX、Vulkan RT 与 DXR traversal/hit/AOV 一致性门禁：

```powershell
.\scripts\run_phase_v7_cross_provider_parity.ps1
```

## 命令行工具

```powershell
# 查看命令和参数
.\build_modular_x64\apps\ure_cli\ure_cli.exe --help

# 校验原生场景或 package
.\build_modular_x64\apps\ure_cli\ure_cli.exe validate <scene.ure|scene.urescene|scene.urepkg>

# 渲染受支持的原生或 glTF 场景
.\build_modular_x64\apps\ure_cli\ure_cli.exe render <scene> [options]

# 原生格式工具
.\build_modular_x64\apps\ure_cli\ure_cli.exe build <input> --output <output>
.\build_modular_x64\apps\ure_cli\ure_cli.exe pack <inputs...> --output <package.urepkg>
.\build_modular_x64\apps\ure_cli\ure_cli.exe unpack <package.urepkg> --output <directory>
.\build_modular_x64\apps\ure_cli\ure_cli.exe inspect <input>
.\build_modular_x64\apps\ure_cli\ure_cli.exe migrate <input> --output <output>
```

Phase Q 原生格式的独立闭环验证入口：

```powershell
.\scripts\run_phase_q_validation_suite.ps1 -BuildDir build_modular_x64 -Config Release
```

## 文档口径

- [PLAN.md](PLAN.md)：唯一施工队列、阶段依赖和完成判据。
- [STATUS.md](STATUS.md)：面向使用者的当前能力与限制。
- [docs/README.md](docs/README.md)：专题文档索引及“当前/历史”分类。
- [AGENTS.md](AGENTS.md)：AI agent 的项目治理规则，不是用户功能说明。

专题审计文档记录特定阶段的证据和边界。文件中的旧测试数量、日期和“下一步”只描述当时快照；发生冲突时以 PLAN、当前源码、CMake/CTest 注册项和实际运行结果为准。

## 许可证与发布状态

仓库当前没有在本 README 中声明稳定发布版本、兼容性承诺或性能保证。集成前应固定具体 commit，并在目标硬件上重新运行所需测试和参考场景。
