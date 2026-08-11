# UltraRender

UltraRender 是一个处于持续研发阶段的光谱/偏振离线渲染器。当前完整场景参考路径基于 CUDA；仓库还包含原生场景格式、材质图、估计器组合、测量与重建合同，以及有界的多后端、波动光学和物理基础。其中不少能力目前仍停留在合同、独立组件或测试入口，并未形成统一产品工作流。

本项目尚不是通用生产渲染器，也没有发布“UltraRender 1.0”。功能与成熟度以 [STATUS.md](STATUS.md) 为准，施工顺序以 [PLAN.md](PLAN.md) 为准。Research、Experimental、Production 是不同的证据等级；类型、配置项或拒绝测试的存在不代表对应能力已经可用。

当前权威实施游标为 `PRV.0 — 产品真相基线与闭环账本`。项目暂时冻结 learned/neural、新积分器、广义统一物理世界和可微路线，优先把已有能力接入一个可验证的 `UltraRender_preview` 产品闭环。该名称是尚未达成的里程碑，不是现有发布版本。

## Preview 集成方向

Preview 路线要求 CLI、Python、Hydra 和后续编辑器通过同一个产品服务工作：客户端使用共享 `ure_client`，显式选择进程内 direct transport 或本地 Worker transport；两者最终调用同一 runtime/product implementation。Worker 只负责隔离、协议和共享内存传输，CLI 也不再拥有第二套场景加载、积分器选择、重建或输出实现。

当前代码尚未达到这个目标：CLI render、Hydra 和 legacy pyure 仍有直接调用内部 renderer/session 的路径；自动渲染桥、原生高级块、MeasurementBundle、重建、可移植 backend、多设备/farm/cache 也尚未全部进入同一个完整场景工作流。新的 [Preview 架构](docs/UltraRender_Preview_Architecture.md) 定义目标边界，[PLAN.md](PLAN.md) 按 `PRV.0` 至 `PRV.11` 逐步完成迁移和真实图像门禁。

## 公共交互边界

项目现已声明以下两个独立版本化合同：

- **Core ABI 1.0**：Windows 11 x64 的 C11 动态加载接口；
- **Worker Protocol 1.0**：同一用户、本机 Named Pipe 与只读共享内存传输。

这是客户端交互合同的 1.0，不是 UltraRender 产品版本 1.0，也不表示仓库整体 API、算法或平台均已稳定。声明不等于公开分发；当前标签与仓库内构建用于固定声明证据，支持时钟仅在另行批准并公开分发软件包后开始。

稳定 Core 只覆盖运行时发现、句柄与生命周期、能力/错误、异步操作与事件、原生场景完整替换、通用渲染目标和不可变 frame lease。它不冻结积分器、MaterialGraph、SceneIR、RenderConfig、MeasurementBundle、WorldState、GPU 调度、模型格式、求解器或研究算法。初始 `StableExtension` 列表为空；UUID transaction 是独立的 `UnstableExtension`。现有 `ure_c_api.h`、`pyure_native.dll` 和 pyure ctypes 仍是 legacy experimental 接口。

公共边界的规范与使用说明：

- [架构规范](docs/Public_API_ABI_Architecture.md)
- [支持策略](docs/Public_API_Support_Policy.md)
- [集成指南](docs/Public_API_Integration.md)
- [PB.8 兼容性报告](docs/PB8_Stable_Compatibility_Report.md)

## 当前实现基础

- CUDA wavefront path tracing，运行时光谱域与最多 32 个 GPU wavelength packet lanes；
- Stokes/Mueller 偏振、MaterialGraph、glTF/MaterialX 适配、HG/Rayleigh/Mie 体积；
- `.ure`、`.urescene`、`.urepkg` 原生场景及验证、迁移、打包工具；
- ReSTIR DI、受限 ReSTIR PT、BDPT/VCM、specular manifold 与 PSSMLT 的独立已验证范围；
- SDK-free runtime、transport、research 与 reconstruction 合同及组件测试；
- Vulkan、D3D12/DXR 与可选 OptiX 的运行时/加速基础和固定 SceneIR parity 证据；
- 有界衍射、荧光、部分相干、各向异性介质和局部全波耦合参考合同。

这些条目均有明确适用域，不能从旧阶段的 “Done” 推导为产品 E2E。完整 SceneIR radiometric renderer 尚未迁移到 Vulkan/D3D12/OptiX；部分相干与一般全波路径不是生产场景渲染器；仓库不提供训练模型或 production neural inference ABI。

## 工程结构

```text
apps/                 CLI 与本地 worker
contracts/            公共 registry、schema、ABI baseline 与验证报告
libs/ure_public/      生成的 C11 公共头
libs/ure_contract/    Windows x64 Core ABI runtime adapter
libs/ure_core/        CUDA 渲染核心与 session
libs/ure_types/       后端无关类型与 SceneIR
libs/ure_runtime/     后端无关 GPU runtime 合同
libs/ure_transport/   估计器、measure、support 与组合合同
libs/ure_research/    可复现实验与证据合同
libs/ure_reconstruction/  MeasurementBundle 与重建合同
libs/ure_sceneio/     原生场景及资源 I/O
libs/ure_vulkan/      Vulkan runtime/acceleration foundation
libs/ure_d3d12/       D3D12/DXR runtime/acceleration foundation
tests/                Host、GPU、公共边界与 SDK-free 门禁
docs/                 现役文档与历史归档
```

`ure_client` 与 `ure_product` 是 Preview 路线中的批准目标模块，目前尚不能列为已实现目录。

仓库内 `gui/` 已废弃，不属于设计、维护或测试范围。未来编辑器应作为独立客户端使用公共 ABI/Worker 边界。

## 构建与验证

维护中的完整构建基线为 Windows 11、Visual Studio 2026/MSVC 19.52、Windows SDK 10.0.28000、CUDA 13.3、CMake 与 Ninja。

```powershell
.\scripts\build_x64.ps1 -BuildDir build_modular_x64 -Config Release
ctest --test-dir build_modular_x64 -C Release --output-on-failure
```

根工程的最终构建产物集中在 `build_modular_x64/artifacts/<Config>/`：可执行文件、运行时动态库及运行时着色器位于 `bin/`，静态库、导入库及 Unix 链接库位于 `lib/`，工具链生成的调试符号位于 `symbols/`，PB.8 的可交付目录位于 `pb8_packages/`。对象文件、生成源码、测试证据与非发布暂存目录仍保留在各自的 CMake 构建目录中。

Ninja 可并行构建普通目标；高内存 CUDA 编译由 `ur_cuda_heavy_compile` job pool 独立限流，不要求全局串行。PB 公共边界完整验证：

```powershell
.\scripts\run_phase_pb_validation_suite.ps1 -BuildDir build_modular_x64 -Config Release
```

GitHub Actions 另行在 Ubuntu 24.04 的 GCC 13/Clang 18 与 Windows 2025 的 MSVC 上执行 CUDA-off 根构建、32 项纯 host 测试、15 项 warnings-as-errors SDK-free 测试，以及安装后 `find_package()` 消费测试。该门禁验证非 GPU 源码和包边界的可移植性，不扩张完整场景渲染的平台承诺。矩阵、排除项和缓存策略见 [CI 说明](docs/CI.md)。

最新已冻结的 PB.8 证据为 Release 构建与 101/101 CTest，通过三种独立调用方式生成六幅实际 PFM 图像。它只证明公共边界声明门禁，不证明 Preview 产品闭环；测试数量也是快照。

## 已知边界

- 不提供 CPU production integrator。
- 不承诺完整 Linux、macOS、ARM64 或非 NVIDIA 场景渲染支持。
- 不提供 OSL 编译器；MaterialX 是适配层，URE MaterialGraph 是内部权威模型。
- 物理/声学模块仍是实验性基础，不是统一物理世界的完成实现。
- CLI、Hydra、legacy Python、完整场景重建与非 CUDA backend 尚未完成 Preview 收敛。
- 研究和不完整能力会 fail loudly；不会以静默降级伪装为已支持路径。

## 文档与许可

[文档索引](docs/README.md) 区分现役规范、阶段证据和历史归档。

UltraRender 项目代码采用 [Apache License 2.0](LICENSE)。第三方组件继续受其各自许可约束；相关许可文件随源码或分发包保留。
