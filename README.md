# UltraRender

UltraRender 是一个处于持续开发阶段的 CUDA 离线渲染器。当前可执行路径以光谱辐射度、Stokes/Mueller 偏振和 wavefront path tracing 为基础；项目同时包含原生场景格式、材质图、体积相函数、会话 API、分布式文件契约，以及若干物理、声学和波动光学的参考实现或接口。

本项目尚未达到通用生产渲染器的成熟度。功能范围以 [PLAN.md](PLAN.md) 的权威施工队列和 [STATUS.md](STATUS.md) 的当前能力表为准；历史设计或阶段计划不代表现行能力。

## 当前状态

- Phase Q 与 Phase R 已完成；Phase T 已建立 backend-neutral runtime、资源规划和 execution graph 合同，当前施工游标是 `T.6`（CUDA backend 迁移）。
- 默认生产执行后端是 CUDA。Vulkan、D3D12/DXR 和 OptiX 路径尚未完成。
- 后端选择、adapter identity、能力位、limits、显存预算及 driver/compiler identity 已贯穿 JSON、CLI、C ABI 和 pyure；显式请求尚未实现的后端会失败，不会静默回退。
- Slang 2026.14 已用六类真实计算原型完成固定版本、确定性多目标编译、反射、debug mapping、CUDA 占用率及数值执行验证；生产 CUDA kernels 尚未迁移，Slang RHI 也未被引入。
- 纯 C++ `ure_runtime` 已定义 device、queue、timeline fence、event、buffer、image、sampler、module、pipeline、资源规划、dispatch DAG、execution graph 和 device-loss 合同；当前 CUDA path/wave 入口会生成并验证 execution graph，但 kernel、resource、stream 和 multi-GPU 的完整 lowering 仍属于 T.6。
- 默认积分器是 spectral/polarimetric radiometric wavefront path tracer。
- coherent field、partial coherence、完整衍射相机和局部全波耦合仍属于 Phase W 后续工作；当前主渲染路径不会静默模拟这些能力。
- production unbiased/spatial ReSTIR DI 与受限 ReSTIR PT suffix reuse 已完成验证。GPU specular-manifold、BDPT、VCM 和独立 PSSMLT 已通过各自统计门禁；MLT 与 bidirectional/VCM/manifold 的组合仍明确拒绝。
- 当前验证基线为 Windows 11、Visual Studio 2022 Build Tools、CUDA 13.0 和 NVIDIA compute capability 12.0。其他平台与工具链尚未形成同等验证证据。

## 已验证的能力

以下描述限定为当前仓库中已有实现和测试覆盖，不表示覆盖所有场景或达到商业渲染器的稳定性：

- 运行时光谱域与 GPU wavelength packet 分离；GPU packet 上限为 32 lanes，并支持单波长 sampled mode。
- CIE 1931 2° observer、显式 wavelength PDF、色散、导体复折射率和局部单层薄膜边界模型。
- Stokes 状态与若干 Mueller 边界变换。它们属于强度域偏振传输，不等同于跨路径相干场求解。
- Lambertian、metal、dielectric、cloth、有限厚度 dielectric layer 和受约束的 BSDF mix/material graph 路径。
- 均质体积、Henyey–Greenstein、Rayleigh，以及资源驱动的光谱 Mie `eval/pdf/sample`、NEE 和 continuation。
- glTF/GLB 导入、MaterialX 受支持子集适配、OBJ/图像/SPD 等资源输入。
- URE 原生 `.ure`、`.urescene`、`.urepkg` 和可重建 `.urecache` 契约，包含校验、迁移、打包和检查工具。
- `RenderSession`、C ABI、pyure、AOV、场景 mutation，以及 sample-range/file-backend 分布式契约。
- 多 GPU sample-space 分区与 framebuffer 合并。完整渲染农场调度和跨机器运行时仍不属于已完成范围。
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
├── libs/ure_core/      # CUDA 渲染核心和 GPU scene compiler
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
`ur_cuda_heavy_compile` job pool 单独限流，默认同一时刻运行一个。不要为常规构建
全局指定 `--parallel 1`。只有在确认设备具备足够 host memory 后，才可在配置时通过
`-DUR_CUDA_HEAVY_COMPILE_JOBS=<N>` 调高该池容量。

运行完整注册测试：

```powershell
ctest --test-dir build_modular_x64 -C Release --output-on-failure
```

当前构建树注册 40 个 CTest。这个数字是当前快照，不应用作长期固定接口；以 `ctest -N` 的输出为准。

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
