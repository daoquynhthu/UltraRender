# UltraRender

UltraRender 是一个处于持续研发阶段的光谱/偏振离线渲染器。当前完整场景参考路径基于 CUDA；仓库还包含原生场景格式、材质图、估计器组合、测量与重建合同，以及有界的多后端、波动光学和物理基础。其中不少能力目前仍停留在合同、独立组件或测试入口，并未形成统一产品工作流。

本项目尚不是通用生产渲染器，也没有发布“UltraRender 1.0”。功能与成熟度以 [STATUS.md](STATUS.md) 为准，施工顺序以 [PLAN.md](PLAN.md) 为准。Research、Experimental、Production 是不同的证据等级；类型、配置项或拒绝测试的存在不代表对应能力已经可用。

`PRV.3 — 材质、资产与有界波动能力组合` 已完成；下一权威游标为尚未施工的 `PRV.4 — 生产 MeasurementBundle 与输出系统`。PRV.1R 建立了有界原生颜色路径的可信产品 E2E 基线，PRV.2 增加唯一 Scene Realizer、自包含包和统一场景工具扩展，PRV.3 将 canonical material program set、受支持资产与有界 radiometric wave 能力接入同一 ProductJob。项目继续冻结 learned/neural、新积分器、广义统一物理世界和可微路线；`UltraRender_preview` 仍是尚未达成的里程碑，不是现有发布版本。

## Preview 集成方向

Preview 路线要求 CLI、Python、Hydra 和后续编辑器通过同一个产品服务工作：客户端使用共享 `ure_client`，显式选择进程内 direct transport 或本地 Worker transport；两者最终调用同一 runtime/product implementation。Worker 只负责隔离、协议和共享内存传输，CLI 也不再拥有第二套场景加载、积分器选择、重建或输出实现。

PRV.1 已让 CLI render 退出 renderer 实现：共享 `ure_client` 提供显式 Direct/Worker transport，两路均使用同一个 `ure_product` 服务。PRV.1R 明确区分 requested/accepted/completed work，并提供单调进度与最新不可变帧；PRV.3 将 exact-build ProductJob 演进到 0.4，增加计划的 eligible/qualified/executed estimator 观测。CLI 默认 Worker，启动失败不会回退 direct。原有 64×64 smoke 只保留为合同/路由证据；480p functional 与 720p/1080p quality 证据建立了有界原生颜色工作流的 ProductE2E。

PRV.2 让 ProductJob 在 GPU 分配前通过一个 Scene Realizer 消费完整 `NativeSceneArchive`。确定性 procedural graph 被实际执行并重新验证；资源按内容身份解析并计入 stored、decompressed、resident、streamed、temporary 与 output 预算；受支持 solver contract 编译为执行约束，动态 simulation required 语义明确拒绝，optional tooling 语义保留而不静默忽略。`.urepkg` 现在嵌入 required local resources、场景、可删缓存和 provenance，删除作者目录后仍可 validate、realize 和 render。CLI 的场景操作均调用 exact-build runtime Scene Tool `UnstableExtension`；独立 native tool 仅保留 USDA adapter export。PRV.2 的 [854×480、16 spp 产品证据](docs/reports/phase_prv2_validation_v1.json)覆盖 `ure_client` 与 CLI 的 Direct/Worker 实际渲染，并保留[功能视觉审阅](docs/reports/phase_prv2_visual_review_v1.json)。

PRV.3 将 canonical material program set 固定为产品材质权威：MaterialGraph 提供拓扑，complete-scene backend 仍消费的完整材质状态也被纳入同一不可变身份。Scene Tool 0.2 提供 bounded glTF build、MaterialX import/export 和 preset realization，三类生成物均经 Direct/Worker ProductJob 实际出图；Hydra-derived 材质只证明 canonical artifact，Hydra viewport 本身仍待 PRV.10 迁移。纹理、SPD、Mie 与 medium 在 GPU 分配前严格预检；native normal map 明确保留给工具链，缺少产品 lowering 的 glTF normal/packed metallic-roughness 会拒绝，unsupported/lossy adapter、资源和 estimator/wave 组合也不会被 fallback material 掩盖。[功能报告](docs/reports/phase_prv3_functional_validation_v1.json)保留 854×480、16 spp 的五类主矩阵及补充 authoring 路径，[质量报告](docs/reports/phase_prv3_quality_validation_v1.json)与[视觉审阅](docs/reports/phase_prv3_visual_review_v1.json)保留五个 1280×720、500 spp 场景。可见的原始方差被如实记录；PRV.4/PRV.5 尚未提供完整 typed measurement、重建或降噪。[Preview 架构](docs/UltraRender_Preview_Architecture.md) 定义目标边界，[PLAN.md](PLAN.md) 从 PRV.4 继续产品总装。

PRV.0 的历史[产品真相基线](docs/PRV0_Product_Truth_Baseline.md)记录了当时的 44 项能力/入口、25 项维护语义和 12 个保留产品场景。PRV.1 后的 5 项 smoke-only ProductE2E 判断先由追加 supersession record 撤回；PRV.1R 随后用新的功能、质量和视觉证据把 Core/product service/Product extension/Worker/CLI/有界颜色输出 6 项重新提升到当前真实达到的 `ProductE2E`。这不表示十二个最终 Preview 产品场景已经闭环。维护语义中已无 accepted-but-ignored，仍有 2 项明确执行语义债务。

PRV.1 闭环了内部 `ure_product` 服务、generated ProductJob 0.1 `UnstableExtension`、共享 `ure_client`、Worker forwarding 与 CLI render 迁移。其[验证报告](docs/reports/phase_prv1_validation_v1.json)中的 64×64 PFM 与身份一致性仍只是路由/传输 smoke。PRV.1R 已将 exact-build ProductJob 演进到 0.3：production work 逐样本持久执行，requested/accepted/completed 与 pilot 计数分离，Worker wait、poll、cancel 可并发交错，跨 session 默认限制为一个并以 `Backpressure` 拒绝超额工作。固定 Worker 60 秒和 CLI 10 分钟作业语义已移除，预算提前结束不能伪装成功，执行根、资源边界和显存计划在 allocation 前校验。默认自动计划按显存选择最大的适用无偏候选子集，并把选择写入 plan identity；它不会静默降低光谱、精度、输出或重建语义。Direct/Worker 发布合并后的单调进度和最新不可变渐进帧，并能传递任意有界 plane 集；统一诊断信封、sample precedence 与 Device/Execution 0.1 覆盖设备枚举、约束和实际执行身份。分阶段 SDK 还提供无 `flatc` 的预生成协议头、`UltraRender::Client` exact-build 目标和真实 Direct/Worker 外部示例。

PRV.1R 的[功能证据](docs/reports/phase_prv1r_functional_validation_v1.json)覆盖 854×480、16 spp 的 `ure_client` Direct/Worker 与 CLI Direct/Worker；[质量证据](docs/reports/phase_prv1r_quality_validation_v1.json)覆盖 1280×720 Direct 和 1920×1080 Worker 的 128 spp production profile，并保留[视觉审阅](docs/reports/phase_prv1r_visual_review_v1.json)。raw PFM 是权威产物，PNG 使用固定线性 sRGB→Reinhard→sRGB8 查看变换；finite、能量、空间结构和嵌套样本收敛均由门禁检查。这只证明当前有界颜色路径；PRV.3 在独立证据中扩展了材质/资产/Mie/radiometric-wave 范围，但仍不宣称 MeasurementBundle、重建、降噪或 Preview 产品完成。

错误与诊断被视为贯穿 Preview 路线的产品能力，而不是 PRV.1R 的一次性补丁。PRV.1R 先建立 result/domain/detail、correlation、cause、operation terminal error、恢复建议和设备信息的公共基础；之后每个场景、材质、输出、重建、积分器、session、backend、farm 和客户端阶段都必须补齐自身结构化错误、负向 E2E 与文档目录。已知失败不得长期塌缩成无上下文 `Internal` 或普通 null Error。

## 公共交互边界

项目现已声明以下两个独立版本化合同：

- **Core ABI 1.0**：Windows 11 x64 的 C11 动态加载接口；
- **Worker Protocol 1.0**：同一用户、本机 Named Pipe 与只读共享内存传输。

这是客户端交互合同的 1.0，不是 UltraRender 产品版本 1.0，也不表示仓库整体 API、算法或平台均已稳定。声明不等于公开分发；当前标签与仓库内构建用于固定声明证据，支持时钟仅在另行批准并公开分发软件包后开始。

稳定 Core 只覆盖运行时发现、句柄与生命周期、能力/错误、异步操作与事件、原生场景完整替换、通用渲染目标和不可变 frame lease。它不冻结积分器、MaterialGraph、SceneIR、RenderConfig、MeasurementBundle、WorldState、GPU 调度、模型格式、求解器或研究算法。初始 `StableExtension` 列表为空；UUID transaction、exact-build ProductJob 0.x 与 Device/Execution 0.x 均是独立的 `UnstableExtension`。现有 `ure_c_api.h`、`pyure_native.dll` 和 pyure ctypes 仍是 legacy experimental 接口。

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
libs/ure_client/      显式 Direct/Worker 的共享产品客户端
libs/ure_contract/    Windows x64 Core ABI runtime adapter
libs/ure_product/     内部 ProductJob 执行与身份/制品边界
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

`ure_product` 是内部产品作业、ProductSnapshot、身份/帧与制品清单执行边界，Core runtime adapter 与 Worker 均委托它执行；`ure_client` 是 CLI 当前唯一的产品客户端主干。原生 validate/inspect/build/migrate/pack/unpack/realize 语义由 runtime scene-tool extension 统一提供。`ultrarender_native_tool` 不再复制这些策略，只承载尚未迁入产品路径的 USDA adapter export。

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

GitHub Actions 另行在 Ubuntu 24.04 的 GCC 13/Clang 18 与 Windows 2025 的 MSVC 上执行 CUDA-off 根构建、33 项纯 host/contract 测试、15 项 warnings-as-errors SDK-free 测试，以及安装后 `find_package()` 消费测试。该门禁验证非 GPU 源码和包边界的可移植性，不扩张完整场景渲染的平台承诺。矩阵、排除项和缓存策略见 [CI 说明](docs/CI.md)。

最新已冻结的 PB.8 证据为 Release 构建与 101/101 CTest，通过三种独立调用方式生成六幅实际 PFM 图像。PRV.1R 将验证分为 32-128 px contract smoke、480p product functional、720p/1080p product quality 和单独调度的 QHD/UHD stress。当前维护门禁包含可重建的 Cornell 产品 fixture、480p 实际产品调用矩阵和留存质量/视觉证据；PRV.2 增加 procedural self-contained package 的 480p 矩阵；PRV.3 增加五类材质/资产/波动场景的 480p 功能矩阵、三条 authoring→ProductJob 路径，以及五个 720p、500 spp 质量场景。stress 仍按硬件适用性单独调度，不作为普通提交门禁。CTest 数量是构建快照，应以 `ctest -N` 的实时 inventory 为准。

## 已知边界

- 不提供 CPU production integrator。
- 不承诺完整 Linux、macOS、ARM64 或非 NVIDIA 场景渲染支持。
- 不提供 OSL 编译器；MaterialX 是适配层，URE MaterialGraph 提供规范拓扑，canonical material program set 才是产品执行与身份权威。
- 物理/声学模块仍是实验性基础，不是统一物理世界的完成实现。
- Hydra viewport、legacy Python、typed measurement、重建与非 CUDA complete-scene backend 尚未完成 Preview 收敛；CLI render、scene tooling 及 bounded glTF/MaterialX/preset authoring 已共享产品客户端边界，后续语义由 PRV.4-PRV.10 收敛。
- 研究和不完整能力会 fail loudly；不会以静默降级伪装为已支持路径。

## 文档与许可

[文档索引](docs/README.md) 区分现役规范、阶段证据和历史归档。

UltraRender 项目代码采用 [Apache License 2.0](LICENSE)。第三方组件继续受其各自许可约束；相关许可文件随源码或分发包保留。
