# UltraRender Preview 产品集成与端到端闭环路线图

最后更新: 2026-08-13（PRV.1 客户端主干保留；新增 PRV.1R 修复阻塞性运行时语义并重建可信产品 E2E 基线）

本文档是 UltraRender 当前唯一的全局施工权威。它将项目重心从继续扩展高阶研究能力，切换为已有非研究能力的产品总装、端到端闭环，以及训练无关重建/降噪的生产接入。

此前两份根计划均已归档为只读参照：

- [`docs/archive/Legacy_Construction_PLAN_2026-08-01.md`](docs/archive/Legacy_Construction_PLAN_2026-08-01.md)：Q/R/T/V/W/U 等主体建设历史；
- [`docs/archive/High_Order_Public_Boundary_PLAN_2026-08-11.md`](docs/archive/High_Order_Public_Boundary_PLAN_2026-08-11.md)：HO/HT/HR/HW/HD 与 PB 建设历史。

归档计划中的完成标记只证明其声明边界内的合同、组件或证据已经闭环，不自动等于产品端到端能力。归档文件不得重新定义当前游标或授权继续冻结的研究路线。

架构权威为 [`docs/UltraRender_Preview_Architecture.md`](docs/UltraRender_Preview_Architecture.md)。若本计划与该架构冲突，必须先修正文档再施工。Core ABI 1.0、Worker Protocol 1.0 及其支持策略继续由公共边界规范独立约束。

---

## 0. 权威状态

当前游标: PRV.1R — Product Runtime 阻塞性修复与可信 E2E 基线

### 0.1 唯一施工队列

```text
Legacy construction Q/R/T/V/W/U                    [archived, read-only]
High-order HO/HT/HR + Public Boundary PB           [archived, read-only]
                  |
                  v
PRV.0 product truth baseline and closure ledger    [done]
                  |
                  v
PRV.1 one product runtime and client spine         [done]
                  |
                  v
PRV.1R runtime correctness and trusted E2E         [current]
                  |
                  v
PRV.2 complete scene realization and packages      [blocked by PRV.1R]
                  |
                  v
PRV.3 material, asset and bounded wave composition
                  |
                  v
PRV.4 production MeasurementBundle and output
                  |
                  v
PRV.5 training-free reconstruction and denoising
                  |
                  v
PRV.6 automatic integration productization
                  |
                  v
PRV.7 stateful session, bounded simulation and checkpoint
                  |
                  v
PRV.8 complete-scene portable backends and acceleration
                  |
                  v
PRV.9 multi-device, cache and farm execution
                  |
                  v
PRV.10 public clients and adapter convergence
                  |
                  v
PRV.11 UltraRender_preview validation and declaration gate
```

只有一个生产游标。满足依赖的调查、基准采集和设计验证可以先行，但不得把未来 PRV 阶段的生产改动混入当前切片，也不得借归档计划继续 HR.3、HW 或 HD。

### 0.2 Preview 的产品定义

`UltraRender_preview` 是一个产品里程碑，不是 UltraRender 1.0，也不是 Core ABI 2.0。它要求用户通过统一客户端服务完成以下闭环：

1. 提交 native/package/glTF/USD/MaterialX-derived 输入；
2. 得到完整场景实现、资源冻结和能力判定；
3. 由 automatic transport plan 选择适用积分器、backend、provider 与预算；
4. 产生真实 MeasurementBundle、原始图像和 AOV；
5. 执行训练无关重建/降噪并保留不确定性和拒绝原因；
6. 经 CLI、direct Core extension 或 Worker 获得一致结果；
7. 保存多层图像、checkpoint 和 provenance；
8. 在支持范围内完成多设备、farm、恢复和 backend 选择。

Preview 不要求每个 backend 执行每个高级积分器，但要求每个宣称可用的组合具有真实完整场景路径。能力差异必须通过 capability/applicability 处理，不能静默切换到 CUDA、wavefront、RGB 或无重建输出。

### 0.3 全局约束

- 当前完整场景物理参考后端仍是 CUDA；CPU 不发展为 production integrator。
- Core ABI 1.0 和 Worker Protocol 1.0 的冻结前缀、ID、语义和 Windows x64 支持承诺不得被破坏。
- Preview 新能力使用独立 0.x extension/schema；未经单独稳定性审计不得创建 StableExtension。
- CLI、Python、Hydra 和未来编辑器不得拥有第二套 renderer、scene-realizer、autopilot、reconstruction 或 output 实现。
- Worker 是隔离宿主和协议桥，不是第二个渲染后端。
- 每项语义必须是 `Executed`、`Rejected`、`PreservedForTooling` 或 `FrozenResearch`；禁止 accepted-but-ignored。
- 研究成熟度、合同稳定性、运行时状态和产品闭环等级保持独立。
- 默认继续单 Agent 工作；本项目不使用 subagent。
- 不读取、维护、迁移或测试废弃的 `gui/`。
- 不恢复 Phase X 通用插件系统，不引入 ambient provider/model/script discovery。
- OpenEXR 产品输出采用官方 OpenEXR 3.4.12 作为首选基线，固定源码、许可证与构建身份；依赖门禁失败时不得以自制不完整 EXR writer 替代。
- 所有产品阶段使用 Windows Release 完整门禁；CUDA-off Linux GCC/Clang 与 Windows MSVC CI 继续验证可移植 host/package 边界。
- `ProductE2E` 证据必须走维护中的外部产品调用链；直接调用 `ure_core`、`RenderSession`、内部 helper 或临时诊断程序只能作为组件/定位证据。
- 例行正确性、产品质量与压力/尺度证据分层执行；QHD/UHD 500+ spp 属于压力/尺度验证，不进入普通提交门禁。
- 正向产品证据不得为了适应本机而关闭产品声明中的光谱、积分器、重建或输出语义；降级配置只能作为明确标注的诊断对照。

### 0.4 冻结路线

| 路线 | Preview 期间状态 | 允许行为 |
|---|---|---|
| HR.3 learned proposal / neural control variate | 冻结 | 保留合同和历史证据，不训练、不接入默认路径 |
| neural denoiser / model runtime | 冻结 | 不引入模型格式、推理 ABI 或权重包 |
| HW unified physical world | 冻结 | 只接通现有有界 deterministic subset；不扩展统一状态研究 |
| HD differentiation / inverse control | 冻结 | 不施工 |
| 新积分器与 proposal 研究 | 冻结 | 只产品化现有 estimator |
| general coherent scene renderer | 冻结 | 保留现有参考合同和 fail-loud 边界 |
| 实时增强 / frame generation | 冻结 | 不施工 |
| Phase X plugin ecosystem | 冻结 | 不施工 |
| repository GUI | 永久废弃 | 禁止考察 |

### 0.5 PRV.0 路线起点（历史基线）

以下事实固定 PRV.0 启动时的能力状态，而不是当前实现声明；现状由阶段完成记录、`STATUS.md` 和现役账本更新：

- Core ABI 1.0 / Worker Protocol 1.0 已声明，但当前稳定 Session 的多项 Objective 语义和 sample accounting 需要产品一致性复核；
- CLI render、Hydra 与 legacy pyure 当时均绕过统一产品服务；CLI render 已由 PRV.1 收敛，后两者仍待 PRV.10；
- Native archive 能验证 procedural/resource/solver/simulation block，但现有 CLI/runtime 主要只消费 `archive.scene`；
- `ure_transport` 已有 Technique Graph、support/measure、pilot、portfolio 与 automatic plan，当前 simplified automatic renderer 未完整消费这些合同；
- CUDA 拥有完整场景路径；Vulkan、D3D12/DXR、OptiX 当前主要是 runtime、acceleration 和 parity fixture；
- MaterialGraph GPU lowering 可执行，MaterialX/preset 与产品 authoring/client 路径仍未统一；
- MultiGpuContext、distributed files、farm schedule 和 `.urecache` 主要是内部组件/测试入口；
- `ure_reconstruction`、MeasurementBundle 和统计/样本级 reconstruction 尚未成为完整场景 renderer 的正式输出链；
- CLI 官方输出仍以单 Beauty HDR/BMP/PPM 为主，公共 Core Frame 当前是单 RGBA plane；
- 路线启动时的维护构建快照为 Windows Release 101 个注册 CTest；数量不是 Preview 成熟度指标。

---

## 1. 施工与证据原则

### 1.1 产品闭环等级

所有旧能力在 `contracts/product_closure_ledger.json` 中记录最高闭环等级：

| 等级 | 判定 |
|---|---|
| Contract | schema/type/validation/rejection 存在 |
| ComponentExecutable | 独立组件或 fixture 可实际执行 |
| RendererIntegrated | 完整场景 renderer 消费，且无 test-only bypass |
| ClientReachable | canonical runtime 可由维护中的客户端调用 |
| ProductE2E | 外部工作流产生真实 artifact，并验证生命周期、错误和恢复 |

任何阶段不得仅凭类、enum、config、schema、host oracle、shader prototype 或拒绝测试把能力提升到 RendererIntegrated 以上。

### 1.2 切片标准

每个 PRV 子步骤必须形成一个可独立拒绝的工程切片，包含：

- 明确输入、输出和 owner；
- 正向真实执行证据；
- 不适用、版本、预算、损坏和生命周期负向证据；
- direct/Worker 或 backend 间适用的语义一致性；
- 机器可读报告或 ledger 更新；
- 相关现役文档更新。

不做仅证明“尚未创建的文件不存在”的仪式化红测。涉及已冻结合同、静默忽略、错误 accounting、身份或 fallback 的测试必须先暴露真实错误行为，再修复。

### 1.3 产品验证优先级

验证从强到弱排序：

1. 独立外部客户端真实 E2E artifact；
2. public/direct/Worker 语义 parity；
3. 完整场景 renderer 行为与统计/物理证据；
4. backend/provider 实机执行；
5. host/component contract；
6. static/source-shape audit。

低层证据不能替代高层证据。

### 1.4 产品 E2E、质量与尺度证据

产品 E2E 的权威调用链是 `ure_client` 或 generated SDK → 显式 Direct/Worker transport → runtime Product extension → `ure_product` → 正式 artifact 获取。CLI 是该客户端模型的人类适配器，不是第三套实现。mock、内部 renderer API、专用测试 renderer、临时 harness 和仅验证 schema/hash 的 fixture 都不能单独建立 ProductE2E。

验证分四层，分辨率名称使用精确尺寸而不使用含混的“2K/4K”：

| 层级 | 建议基线 | 目的与门禁位置 |
|---|---|---|
| Contract smoke | 32-128 px，1-4 spp | 生命周期、ABI/protocol、错误与快速回归；可进入每次提交 |
| Product functional | 854×480，16-64 spp | 真实产品路径、非平凡 artifact、资源与调用方式；进入维护分支门禁 |
| Product quality | 1280×720 / 1920×1080，128-512+ spp | 收敛、材质/光谱/AOV/重建和视觉质量；进入定时、里程碑及发布候选门禁 |
| Stress/scale | 2560×1440 / 3840×2160，500+ spp | VRAM、吞吐、长时取消与稳定性；定时或人工实机执行，不作为普通提交门禁 |

精确样本数由场景和统计设计决定，上表不是用低成本 smoke 替代质量证据的许可。产品质量和尺度层必须使用产品默认或明确请求的 production profile；任何关闭能力的运行仅作为诊断对照，必须记录差异且不得用于能力毕业。

所有真实图像门禁保留 renderer 的权威 raw artifact，并通过一个确定性的派生查看流程生成 PNG，固定方向、色彩空间、曝光和 tone-map identity。自动检查至少覆盖维度、finite、非零/非平凡、能量/统计范围、收敛或 reference metric；质量与发布层同时保留人工视觉审阅记录。文件存在、header 可读、hash 相同或 Direct/Worker 字节一致本身不证明图像正确。

调用矩阵按风险和 pairwise 设计，不要求无意义的全笛卡尔积，但每个维护中的调用方式至少完成一条真实渲染与 artifact 获取，代表性场景必须跨 Direct、Worker、CLI 和外部 SDK 客户端复用。证据绑定 runtime/build、GPU、driver、VRAM、production profile、分辨率、样本域和耗时；同机同配置用相对回归判定，跨硬件使用声明的 applicability/class 约束。

当内存、设备或预计工期不适用时，产品必须在执行前结构化拒绝或选择已声明且语义等价的计划；不得通过隐式能力降级、失控 paging 或超时后伪装成功来通过门禁。

### 1.5 持续诊断成熟度

错误和诊断不是一次性基础设施切片，而是每个 Preview 阶段的持续交付物。PRV.1R 建立统一诊断信封、关联身份和最低质量门槛；PRV.2-PRV.10 分别扩展本领域的版本化 detail schema、恢复建议和真实负向 E2E；PRV.11 只做全局一致性与包内目录封口，不能首次补写前序阶段缺失的错误语义。

每个非成功结果至少保留稳定 result、domain、版本化 detail code/schema、人类可读消息、operation/session/job、scene/snapshot/objective/plan、runtime/build、transport/backend/device 和 cause/retryability 中适用的字段。调用链必须携带 correlation identity，使 CLI、SDK、Worker、runtime、product service 和 backend 日志能关联同一次请求；不得只返回 `Internal`、裸 vendor code 或“无 Error object”字符串。

Error 对象分配失败是唯一允许没有 retained Error handle 的资源极限边界，此时仍返回原始稳定 result，并通过预分配/无分配的 emergency diagnostic 保留最小 domain/detail/correlation。普通失败、跨进程失败和 operation terminal failure 必须可检查。Core result 中的 `Timeout`、`Incomplete`、`BudgetExhausted`、`Canceled`、`DeviceLost`、`CapabilityUnavailable` 和 `Internal` 不得混用；`NotApplicable` 等产品状态作为版本化detail表达，不伪造新的Core result。

诊断 detail 可以随 0.x schema 演进，但稳定 result/domain 和 Core 1.0 行为不得原地改义。消息不得泄漏绝对私有路径、内存地址、密钥或未清理的 vendor 文本；事件、cause chain、structured payload 和日志必须受大小、深度、速率和保留预算约束。

每个阶段的完成门禁必须包含本领域的正向诊断、失败注入、Direct/Worker parity、CLI/SDK 可读性和文档化枚举/恢复建议。新增 fail-loud 路径若没有结构化诊断、可复现负向测试和 owner，不得视为完成。

### 1.6 提交与游标

每个 PRV 子步骤按 `PLAN -> IMPLEMENT -> VERIFY -> REVIEW -> REPORT -> COMMIT` 执行。当前文档切换提交已获用户明确授权；后续生产施工仍按 AGENTS.md 的授权和报告规则执行。只有完整 phase gate 通过后才能将根游标推进到下一阶段。

---

## 2. PRV.0 — 产品真相基线与闭环账本

**状态**: 已完成（2026-08-11）。44 项 product closure ledger、25 项 semantic/no-op audit、12 个 retained E2E 场景和 `ure.preview.baseline.v1` 已由正向、确定性与负向门禁固定；Windows Release 103/103 CTest 通过，最近 hosted non-GPU main gate 为绿色。

**目标**: 把“旧 Phase 完成”重新映射为可执行产品闭环等级，冻结 accepted-but-ignored、重复执行权威和测试专用入口，建立 Preview 的机器可验证起点。

**依赖**: 已归档主体计划、HO.0、PB.8、当前源码与 fresh build/test inventory。

### PRV.0.1 — Product Interaction and Execution Ledger

- 扩展现有 Public Interaction Surface Ledger，新增产品执行 owner、当前调用链、closure level、bypass、ignored-semantics risk、Preview disposition 与迁移阶段；
- 覆盖 CLI render/tooling、Core runtime、Worker、legacy C/Python、Hydra、native/package、MaterialX、integrators、automatic bridge、MeasurementBundle、output、multi-GPU、distributed/farm/cache、portable runtime/provider、bounded simulation；
- 修正已经过期的 PB.8 terminal migration 描述：PB.8 冻结公共 grammar，不证明 CLI/Hydra/pyure 已经收敛到产品服务；
- ledger gate 拒绝未知 owner、重复 execution authority、已过期 terminal gate、无 ProductE2E 证据的 ProductE2E 标记和禁止目录 anchor。

### PRV.0.2 — No-op and accepted-but-ignored audit

- 审计 public Objective、CLI/JSON config、Native feature declaration、solver/simulation/resource/procedural block、backend/provider/cluster selection、output/reconstruction request；
- 每项记录 code owner、当前消费点、行为、影响和目标 disposition；
- 优先锁定 Core sample budget/accounting、output semantics、determinism、usage、latency，以及 CLI `device_ids`、RR、tone map、SPD search、MLT advanced fields等已知风险；
- static gate 阻止新增已解析但无消费/拒绝路径的维护字段。

### PRV.0.3 — Product E2E scenario manifest

- 建立 retained scenario manifest，按 scene/material/transport/backend/session/scale/output/client 维度分组；
- 每个场景绑定 source hash、required capability、expected disposition、artifact schema、指标和可接受失败类；
- 使用 pairwise/risk-based 组合而非不可维护的完整笛卡尔积；
- 固定 Preview 必测场景：基础 diffuse、textured PBR、glass/SDS、小光源、高遮挡、volume/Mie、large spectral resource、diffractive、fluorescent、procedural package、dynamic mutation、bounded simulation。

### PRV.0.4 — Baseline report

- 聚合 live CTest inventory、当前 CLI/direct/Worker image evidence、backend inventory、optional Hydra state和 closure ledger；
- 输出 `ure.preview.baseline.v1`，每个结论绑定 source commit 与 artifact digest；
- 文档明确当前没有 `UltraRender_preview` 发布。

**完成门禁**:

- 所有维护中的能力和外部入口均有 closure level、owner 与 Preview disposition；
- accepted-but-ignored/no-op 清单完整且有静态防扩散门禁；
- CLI、Hydra、legacy Python 和 direct C++ bypass 被诚实标记为未迁移；
- retained E2E manifest 和 baseline report 可在 clean tree 确定性重建；
- Windows Release 全量 CTest 与 hosted non-GPU CI 保持绿色。

---

## 3. PRV.1 — 唯一 Product Runtime 与客户端主干

**状态**: 架构主干已完成；其历史 ProductE2E/运行时正确性结论已由 PRV.1R 重新分类，不能作为后续阶段的可信产品基线。

**完成记录**: `ure_product`、ProductJob 0.1、`ure_client` Direct/Worker transport 与 CLI render 已收敛到同一产品执行权威。CLI 默认 Worker、显式 direct，渲染链接图不再包含 `ure_core`/`ure_sceneio`；原生场景工具暂由隔离的 `ultrarender_native_tool` 保持可用，语义权威在 PRV.2 迁入 runtime scene-tool extension。`docs/reports/phase_prv1_validation_v1.json` 保留了 64×64 smoke、transport parity 与结构性证据，但后续调查证明它没有覆盖真实 sample work、长时控制、资源根和产品质量，因此不再足以支持 ProductE2E 声明。

**目标**: 建立 `ure_product` 和 `ure_client`，让 CLI render 首先退出 renderer 实现，证明 direct 与 Worker 通过同一 runtime 服务执行当前基础场景。

**依赖**: PRV.0。

### PRV.1.1 — Internal product service

- 新增内部 `ure_product` 模块，拥有 ProductJob、ProductObjective、ProductOperation、ProductFrame/Artifact manifest 与服务生命周期；
- runtime adapter 只把 public Core/Preview requests 翻译到 `ure_product`，不在 adapter 中实现 estimator、scene lowering 或 output policy；
- 现有 `ure_core`、`ure_sceneio`、`ure_transport`、`ure_reconstruction` 作为 product service 的实现依赖，不反向依赖客户端；
- 建立 product plan identity、snapshot identity、objective identity 与 build identity 传播。

### PRV.1.2 — Preview Product extension

- 在 generated registry 中增加独立 0.x ProductJob extension UUID、operation/message/schema identity；
- Core ABI 1.0 table/structure frozen-prefix gate保持逐字节兼容；
- 当前 Core Objective 中无法兑现的非零/非默认语义必须明确拒绝，直到 Product extension 提供可执行定义；
- 修正 sample budget、completed work 与 Frame metadata accounting，使 reported samples 对应实际 accepted work domain。

### PRV.1.3 — Maintained client library

- 新增 `ure_client`，封装 runtime manifest/interface negotiation、handle lifetime、events、frames、errors 和 Product extension；
- 提供显式 `Direct` 与 `Worker` transport，返回相同 client-domain result；
- Worker transport 使用已有共享内存 lease 传递 frame/measurement payload；
- 禁止 Worker launch failure、registry mismatch 或 unsupported extension 静默回退 direct。

### PRV.1.4 — CLI render migration

- `ure_cli render` 默认通过 `ure_client` Worker transport；
- `--transport direct` 是显式选项；
- 从 CLI 移除 `RenderEngineFactory`、SceneIR、GPU backend SDK 和直接 image-save 所有权；
- CLI 只解析 human config、提交 ProductJob、显示事件/诊断和返回 artifact manifest；
- 基础 native scene 的 CLI/direct/Worker 输出内容、identity、错误和取消语义一致。

**完成门禁**:

- CLI render 链接图不再包含 `ure_core` 或 `ure_sceneio`；
- Worker 仍只通过两项 bootstrap export 加载 runtime；
- direct/Worker/CLI 三路至少完成 load/render/cancel/error/frame/artifact E2E；
- Core 1.0 retained client、PB.8 schema和全部 public-boundary tests 不回归；
- runtime/Worker crash 不带走 CLI，且没有隐式 transport 切换。

---

## 3A. PRV.1R — Product Runtime 阻塞性修复与可信 E2E 基线

**状态**: 当前游标；PRV.2-PRV.11 在本阶段闭环前不得继续生产施工。

**目标**: 修复已确认的 sample/work accounting、重复重建执行、Worker 长时控制、预算完成语义、资源根和显存适用性缺陷，并建立可以被后续所有 Preview 阶段复用的真实产品调用与图像证据基础设施。PRV.1 的单一执行权威和冻结 Core/Worker 1.0 前缀保持不变；本阶段不提前完成 PRV.6 的完整自动积分器产品化。

**已确认阻塞事实**:

- Product `sample_budget` 当前按外层 operation 次数驱动，而 automatic renderer 每次重新创建 renderer、重新加载场景并提高 technique target，导致实际目标工作呈几何增长；`sample_budget=50` 已对应约 163,840 target spp，而非 50 个规范样本；
- Stable Core Worker 存在固定 60 秒同步 wait，长任务可返回 `Timeout` 且没有 artifact；Worker 主循环和产品 render 调用缺乏可抢占的有界工作量；
- wall-time budget 仅中止外层循环后仍可能发布成功，Frame adapter 可无条件报告 complete；CLI 另有与 ProductObjective 无关的固定 deadline；
- package fixture 的相对 SPD 依赖当前工作目录，隔离目录/外部客户端不能自包含执行；
- 当前 PRV.1 证据主要是 64×64、1-2 spp 的 smoke 与字节 parity，未验证收敛、质量、尺度、完整调用矩阵或产品默认能力；
- QHD/UHD 500 spp 的临时 `ure_core` harness 证明 CUDA core 可产生结构一致的 Cornell 图像，但它绕过产品路径且使用过诊断降级配置，只能作为定位证据；默认配置下 UHD 1 spp 的本机显存/paging cliff 需要产品预检与 applicability 处理，不能混同为算法正确性失败。

### PRV.1R.0 — 证据重新分类与回归基线

- 将现役 closure ledger 中仅由 PRV.1 smoke 支撑的 `ProductE2E` 项降级到真实达到的 `ClientReachable` 或 `RendererIntegrated`；历史报告保持不可篡改，以 additive supersession record 说明证据撤回原因；
- 建立最小回归场景，记录 requested/accepted/completed work domain、scene realization 次数、executor 创建次数、实际 renderer samples、取消延迟和 artifact 状态；
- 固定问题复现：小样本结构计数、50/60 sample 长时路径、不同 CWD package、budget early-stop 和 cancel-in-flight；
- 禁止将临时诊断 harness、内部 core 调用或 capability-reduced 输出登记为 ProductE2E。

### PRV.1R.1 — 规范 Work Domain 与持久化基础执行

- ProductObjective 的 sample budget 映射为唯一、无重叠、可审计的 accepted work domain；pilot、production、technique、tile、wavelength 与 chain 工作分别计数；
- 当前 bounded automatic bridge 在一个 ProductJob 内持久保存 scene realization 与 candidate executor state，不得每次 progress step 重新 load scene、重建 renderer 或从零累计；
- 一个 public progress quantum 对应有界真实工作，而不是调用次数；completed work 只在实际完成后推进；
- 修复只建立 PRV.6 所需的正确增量地基，不替代后续 HT Technique Graph、qualification、portfolio 和 support/measure 产品化。

### PRV.1R.2 — Operation、预算、取消与完成状态

- 统一 Core session、ProductJob、Worker 与 CLI 的 requested/accepted/completed/canceled/failed 状态映射；
- wall-time、latency、sample、memory budget 必须进入执行计划；未完成请求不得发布成功 complete，允许的 partial result 必须由 objective 明确请求并在 artifact/progress 中标注；
- render kernel/executor 以测得的有界 work quantum 暴露取消点；取消、deadline 与 Worker disconnect 不等待一个无上界整帧调用结束；
- Worker wait timeout 是客户端观察窗口，不是作业预算或作业失败定义；移除固定 60 秒语义耦合和 CLI 独立 10 分钟产品 deadline；
- cancel/timeout/error 后的 frame lease、operation、session、shared memory 和临时 artifact 生命周期可重复验证。

### PRV.1R.3 — Worker 并发控制面与反压

- Worker 的长时 render 不阻塞 cancel、poll/event、heartbeat 与 shutdown 控制请求；
- 明确单 session 串行语义与跨 session 并发边界，禁止无界线程/队列；
- operation event/progress 在 Direct 与 Worker 单调且语义一致，slow consumer、disconnect、crash 与 backpressure 有结构化结果；
- 保持 Worker Protocol 1.0 冻结字段语义，通过内部调度或 additive negotiated extension 修复，不把 Worker 变成 renderer。

### PRV.1R.4 — 执行根、资源与最小自包含产品 fixture

- runtime 以明确的 scene/package execution root 或内容 URI 解析资源，不依赖进程 current directory；
- 修复 PB/PRV 维护场景的 SPD、纹理和其他相对资源，使外部客户端在任意隔离目录通过 Direct 与 Worker 渲染；
- fixture 打包并校验所有 required payload；测试不得通过临时切换 CWD 掩盖资源缺失；
- 完整 `.urepkg` 语义仍由 PRV.2 建设，本步骤只闭环已有维护产品场景和阻塞性 package truth。

### PRV.1R.5 — 显存/尺度适用性与预检

- 在 GPU allocation 前估算 framebuffer、spectral planes、queues、acceleration、executor state、scratch 与输出峰值，并记录预算分解；
- 对明显超过可用显存或触发不可接受 residency 风险的计划返回 Core `BudgetExhausted` 与结构化 `NotApplicable` detail，或选择 objective 明确允许且语义等价的计划；
- 禁止为适应设备静默减少 spectral lanes、积分器、AOV、重建或精度；
- 性能证据绑定 GPU/driver/VRAM/config，区分结构性复杂度回归、产品资源策略缺陷和本机硬件不适用。

### PRV.1R.6 — 诊断、设备与采样语义基础

- 在现有Core Error/Operation语义上增加versioned Product Diagnostic detail/catalog，而非建立平行错误体系：保留稳定result/domain，并补充cause、retryability、recovery hint、correlation identity与operation/session/job/snapshot/plan/build identity；
- runtime 普通失败必须产生 retained Error 或可查询 terminal error；Error 分配失败走有界 emergency diagnostic，不得把普通 `Internal` 降为 null error；
- 建立跨 Direct/Worker/CLI 的错误映射表，明确 `Timeout`、`Incomplete`、`BudgetExhausted`、`Canceled`、`DeviceLost`、`CapabilityUnavailable` 和 `Internal`；backend/vendor code只作为有类型的嵌套detail，`NotApplicable`保留为产品状态/detail；
- 通过独立 0.x Device/Execution extension 枚举 adapter/backend/provider、驱动、设备身份、总量/可用显存与能力，消费 objective 的设备约束，并在plan/artifact/diagnostic中报告实际选择；不扩张Core 1.0；
- 固定 ProductObjective sample budget、NativeScene `scene.spp`、simulation `spp_per_frame`、pilot、production和progressive accumulation的优先级/覆盖/重置表；禁止隐式相乘和重复累计，冲突输入在job compile阶段拒绝；
- 发布可机器读取的 result/domain/detail 目录和集成者恢复指南；对敏感字段清理、cause深度、payload大小、事件速率和日志保留设预算。

### PRV.1R.7 — 渐进反馈与外部集成基础包

- ProductJob 在有界work quantum后发布单调progress event；事件含accepted/completed domain、阶段、预算消耗、预计剩余范围和最新frame generation，不以每sample强制同步；
- Direct与Worker允许独立poll/wait event并获取最新immutable progressive frame；发布频率、合并、丢帧、backpressure和lease lifetime明确，慢客户端不能阻塞renderer；
- PRV.1R移除Worker和`ure_client`中的单plane snapshot/`planes.size()==1`假设，建立任意已发布Frame plane的有界descriptor/lease传输；PRV.4再以真实AOV/Measurement planes完成产品语义和质量门禁；
- SDK直接携带与registry/runtime manifest一致的预生成Worker/Frame/Scene/Product C++协议头、FlatBuffers runtime headers、生成器版本/命令/identity，不要求普通集成者本机运行`flatc`；schema仍同时分发供审计和其他语言生成；
- 将测试目录中的Worker client提炼为renderer-free exact-build参考库/源码包，提供`find_package(UltraRender)`目标、最小Direct/Worker示例和out-of-tree构建；它不得成为第二套客户端语义权威；
- 场景构建使用generated serialization/builder helper和PRV.2 scene-tool extension；不发布或冻结`ure_sceneio`、SceneIR或MaterialGraph的内部C++ ABI。

### PRV.1R.8 — 可信产品 E2E 矩阵与图像证据

- 建立共享 product scenario runner，经维护中的 `ure_client`/generated SDK 驱动 Direct、Worker 和 CLI；测试代码不链接 renderer-private 库；
- 所有维护调用方式至少一条真实 render→frame/artifact 流程，代表性 Cornell/PB 场景采用风险 pairwise 覆盖 transport、client、cancel/error 和隔离 CWD；
- 每次提交保留 contract smoke；Windows GPU 维护门禁至少包含 854×480、16-64 spp 的 production-profile product functional 场景；
- 定时/里程碑门禁包含 1280×720 与 1920×1080、最高 512 spp 的 Cornell/材质产品质量场景；QHD/UHD 500+ spp 仅进入有硬件身份的 stress/scale suite；
- 权威 raw artifact 经固定转换生成 PNG，自动验证 finite、非平凡、能量、空间结构、收敛/reference metric，并为 quality/stress 证据保留人工视觉审阅结果；
- Direct/Worker/CLI parity 比较相同 plan/work identity 下的统计或确定性输出，hash/字节一致只能作为附加传输证据。

**完成门禁**:

- 对固定场景，请求 N 个 production samples 实际只完成 N 个无重叠 work items；pilot 单独披露，scene realization 与 executor 创建次数不随 progress step 或 spp 几何增长；
- 1→2N 的结构计数保持线性，实测同机耗时没有由重复全量重渲染造成的超线性曲线；
- Direct/Worker/CLI/外部 C/C++ 客户端均通过 canonical product path 生成真实 artifact，Worker 长任务不再受固定 60 秒失败边界；
- budget、partial completion、cancel 与 failure 状态不再伪装为 complete，取消延迟受声明 work quantum 上界约束；
- Blender类外部客户端可在渲染期间取得单调进度、最新渐进帧并及时取消；设备选择和实际执行设备可查询且写入证据；
- `sample_budget`、`scene.spp`、`spp_per_frame`及progressive reset/accumulate组合均通过表驱动正负向门禁，无隐式乘法；
- SDK在没有`flatc`和renderer-private头/库的clean out-of-tree工程中构建并运行Direct/Worker真实渲染；
- 代表性runtime、Worker、resource、budget、cancel、device和malformed失败均返回可关联、可分类、可恢复的结构化诊断；普通失败不存在null Error退化；
- 维护场景从非作者 CWD 和隔离 package 目录成功运行，缺失/越界资源在 allocation 前拒绝；
- 480p functional 与 720p/1080p quality 证据在 production profile 下通过；QHD/UHD stress 结果按硬件适用性记录而不阻塞普通提交；
- closure ledger、baseline、STATUS、README 和架构文档不再包含由 smoke、mock、internal harness 或降级配置支持的虚假 ProductE2E 声明；
- Windows Release 维护门禁、Core ABI 1.0/Worker Protocol 1.0 compatibility 和 hosted non-GPU CI 保持绿色。

---

## 4. PRV.2 — 完整场景实现与自包含包

**状态**: 等待 PRV.1R 完成。

**目标**: 让一个产品作业消费完整 NativeSceneArchive，并让验证、实现和渲染对 required feature 得出同一结论。

**依赖**: PRV.1、PRV.1R。

### PRV.2.1 — Scene Realizer

- 建立唯一 Scene Realizer：输入 native/package/adapted archive，输出 immutable ProductSnapshot；
- 实际执行 deterministic procedural graph，组合结果后重新验证并冻结 SceneIR；
- 消费 resource catalog，解析 content URI、domain、dependency、residency和预算；
- 编译 solver contract 到 product execution constraints；
- 编译现有支持的 simulation subset 到 bounded time plan；
- 未执行的 required semantics 在 GPU allocation 前拒绝；optional tooling semantics 进入 retained diagnostics。

### PRV.2.2 — Resource resolver

- 所有纹理、SPD、Mie、mesh、video/index和material resource 使用内容身份解析；
- ambient current directory、未声明 search path 和作者机器 absolute path 不参与 package execution；
- 资源预算覆盖 stored、decompressed、resident、streamed、temporary 和 output memory；
- missing、hash mismatch、cycle、domain mismatch、path traversal 和 decompression bomb 均产生结构化诊断。

### PRV.2.3 — Self-contained `.urepkg`

- package manifest 实际填充 resources、dependencies、caches、scenes 和 provenance；
- pack 收集所有 required local resource payload，deduplicate by content identity；
- unpack/build/migrate 保持 canonical scene/resource identity；
- 删除原作者资源目录后，package 仍能 validate、realize 和 render；
- cache 可以删除重建且不改变 source/package semantic identity。

### PRV.2.4 — Unified scene tooling extension

- Product runtime 提供 versioned validate/inspect/build/migrate/pack/unpack/realize operations；
- CLI scene tooling 调用该 extension，不直接拥有 native validation/migration policy；
- direct 与 Worker 对同一输入返回相同 diagnostics、feature disposition和snapshot identity；
- script build 继续显式 opt-in，runtime 不执行 ambient script。

### PRV.2.5 — Scene/package diagnostic maturity

- 为parse、schema、feature disposition、resource resolution、procedural realization、solver/simulation compile和package publication分配稳定domain与版本化detail；
- 诊断包含scene/package/resource identity、JSON/FlatBuffer字段路径或source span、required/optional disposition、预算分解与安全清理后的resolution trace；
- CLI、Direct和Worker返回同一cause graph与correlation identity，恢复建议区分修复输入、补资源、升级schema、提高预算和明确不支持。

**完成门禁**:

- retained Q.3-Q.12 advanced fixtures全部被 Executed、Rejected 或 PreservedForTooling，无 ignored block；
- procedural package实际生成几何/光源/光谱资源并渲染非平凡图像；
- solver/simulation/resource required declarations不能只通过 schema 后被丢弃；
- self-contained package在隔离临时目录由独立客户端完成 validate→realize→render；
- 该 render 必须复用 PRV.1R product scenario runner，以 production profile 生成至少 480p 非平凡 artifact；native tool 或直接 `ure_core` 调用不能替代；
- corrupt、ambiguous、oversized、missing-resource和unsupported-feature负向门禁通过。
- scene/package错误目录、真实负向fixture、Direct/Worker/CLI parity和redacted输出门禁通过；未知异常不得塌缩为无上下文`Internal`。

---

## 5. PRV.3 — 材质、资产与有界波动能力组合

**目标**: 使现有 MaterialGraph、glTF、MaterialX、preset、spectral resource、Mie、衍射和荧光能力通过同一 ProductSnapshot 与 renderer path 工作。

**依赖**: PRV.2。

### PRV.3.1 — Canonical material program set

- Native MaterialGraph 是唯一材质执行权威；
- glTF、MaterialX、Hydra material和preset全部先生成 validated MaterialGraph；
- material program identity绑定graph、resource、spectral domain、compiler和backend semantic；
- unsupported/lossy adapter semantics产生 structured loss或拒绝，不进入fallback material。

### PRV.3.2 — Product authoring paths

- scene tooling extension支持MaterialX import/export和preset material realization；
- package可携带MaterialX source provenance，但执行只消费canonical MaterialGraph；
- material/texture/resource transaction通过UUID和content identity更新，不使用index-only公共语义；
- 资源变化按hot update、partial rebuild或full snapshot replacement显式分类。

### PRV.3.3 — Cross-integrator material execution

- 建立Lambert/metal/dielectric/mix/layer/texture/medium组合矩阵；
- glass/SDS、volume/Mie和spectral texture至少由wavefront及适用高级estimator产生真实图像；
- 每个estimator报告MaterialGraph节点、medium和wave feature applicability；
- automatic不能因材质不适用而选择弱语义替代。

### PRV.3.4 — Bounded wave-material integration

- camera diffraction、radiometric diffractive MaterialGraph和fluorescence进入ProductJob、capability、measurement和output；
- non-radiometric/coherent/partial-coherent production session继续拒绝；
- reconstruction producer保留transport/detector wavelength、Stokes和lifetime/time identity；
- wave feature不能被tone-map或RGB-only intermediate提前压平。

### PRV.3.5 — Material/asset diagnostic maturity

- MaterialGraph compile、adapter loss、texture/SPD/Mie加载、spectral domain、medium、wave feature和estimator applicability具有版本化detail与node/resource path；
- adapter loss report与runtime rejection共享identity，明确unsupported、lossy、resource missing、domain mismatch和backend limitation，禁止fallback material掩盖错误。

**完成门禁**:

- native、glTF、MaterialX-derived、preset和Hydra-derived material都通过canonical graph生成artifact；
- retained material风险矩阵覆盖真实CUDA渲染、mutation、package和loss report；
- volume/Mie、diffractive和fluorescent场景有raw measurement和可查看图像；
- unsupported graph、resource、estimator和wave组合在计划阶段拒绝。
- 共享产品矩阵至少以480p functional覆盖主要材质组合，并以720p/1080p quality场景验证glass/caustic、volume/Mie、spectral和wave-material；不得用内部integrator fixture代替。
- 材质/资产错误在Direct、Worker、CLI及适用authoring adapter间保持分类与cause parity，并包含可操作恢复建议。

---

## 6. PRV.4 — 生产 MeasurementBundle 与输出系统

**目标**: 把renderer权威输出从临时RGB framebuffer提升为完整场景产生的typed measurement和artifact graph。

**依赖**: PRV.3。

### PRV.4.1 — Complete-scene measurement producer

- `ure_core`/product bridge生产raw estimate、sum、sum-of-squares、sample count、variance、ESS和tail evidence；
- Beauty、normal、albedo、depth、UV、motion以及请求的Spectrum/Stokes plane具有明确observable/unit/measure/time/uncertainty/provenance；
- 每个plane绑定snapshot、objective、automatic plan、technique、sample range、backend/provider和build identity；
- 缺失生产数据时请求拒绝，禁止伪造零plane或把wavefront AOV附到混合Beauty而不披露。

### PRV.4.2 — Measurement lifecycle

- MeasurementBundle支持增量append、immutable snapshot、canonical merge和budgeted retention；
- Frame lease能够发现和分块读取typed plane，而Core 1.0 color plane保持兼容fallback；
- direct/Worker shared-memory transfer验证extent、stride、digest、generation和lease lifetime；
- large spectral/Stokes plane支持partial read，不要求完整复制到客户端。

### PRV.4.3 — Output graph

- 引入固定OpenEXR 3.4.12依赖，使用官方库写multi-channel/multipart flat image；Preview不宣称deep EXR；
- EXR channel/part命名映射到registry semantic identity，保留scene/objective/plan/backend/provenance metadata；
- MeasurementBundle/checkpoint使用独立versioned container，不依赖EXR表达全部统计和sample记录；
- HDR/PPM/BMP是derived display products，tone-map请求被实际执行并记录；
- artifact publication使用temporary file、fsync/close、atomic replace和content manifest。

### PRV.4.4 — Output public extension

- versioned MeasurementFrame/OutputManifest extension支持plane discovery、partial copy、artifact request和publication status；
- CLI不再转换framebuffer或编码官方输出；
- external client可选择只读取raw planes，不强制runtime写文件；
- unsupported output semantic、layout、precision或budget在job compile阶段拒绝。

### PRV.4.5 — Plane transfer and output diagnostics

- Worker与`ure_client`透传runtime Frame的全部planes，不保留单plane snapshot或`planes.size()==1`假设；每个plane使用独立或明确分段的lease、extent、digest和lifetime；
- 输出诊断区分producer plane缺失、unsupported semantic、layout/precision、transfer corruption、backpressure、disk/permission、codec和atomic publication failure；
- 多plane partial acquisition失败不得损坏已发布的immutable frame，manifest标记complete/partial/failed及可重试范围。

**完成门禁**:

- 完整场景producer满足HR.0/HR.1训练无关重建所需最小plane集合；
- independent client取得raw/AOV/statistics并写出或请求官方multilayer EXR；
- EXR经官方reader重开，channels、metadata、finite值和content identity通过；
- direct/Worker measurement bytes与manifest一致；
- CLI tone-map、output semantic和SPD/resource路径中已知no-op被消除或拒绝。
- Direct、Worker、CLI和外部SDK读取同一正式artifact graph；raw权威与确定性PNG派生查看产物的方向、色彩、曝光和tone-map identity由门禁固定。
- Beauty、Z/depth、normal、albedo、motion、denoise inputs及适用Spectrum/Stokes planes经Worker完整传输；多plane错误注入、lease/backpressure和诊断parity通过。

---

## 7. PRV.5 — 训练无关重建与降噪

**目标**: 将HR.1统计重建和现有CUDA kernels接入产品执行图，提供raw/reconstructed/uncertainty/rejection共同输出，不引入神经模型。

**依赖**: PRV.4。

### PRV.5.1 — Applicability compiler

- 根据measurement schema、scene motion、time、technique、Spectrum/Stokes domain和history identity编译reconstruction plan；
- static、temporal、disocclusion、heavy-tail、insufficient-support和unsupported-observable有明确状态；
- 默认启用只发生在所有required plane可用且Production evidence覆盖的域；
- 不适用时保留raw并输出reason，不能静默采用RGB blur。

### PRV.5.2 — Production statistical reconstruction

- 接通variance/ESS/tail-aware temporal accumulation与à-trous spatial filtering；
- motion/disocclusion/history invalidation与camera、transform、material、resource、technique和backend identity绑定；
- Spectrum保持非负/观测一致性，Stokes投影到物理可实现域；
- raw、reconstructed、uncertainty、confidence/rejection和support mask同时发布。

### PRV.5.3 — Bounded sample-level option

- 现有analytic sample splatting只有在完整场景producer提供required sample records后进入Experimental Preview；
- external learned kernel/point-set/hybrid provider保持FrozenResearch；
- sample record预算、partial retention、OOD和calibration失败不能影响默认统计重建fallback；
- Experimental output与Production output使用独立part和maturity identity。

### PRV.5.4 — Reconstruction evidence suite

- 覆盖静态diffuse、细纹理、glass/caustic、volume/Mie、large spectral、Stokes、fluorescence、motion/disocclusion和heavy-tail；
- 使用多重复raw/reference，报告MSE、perceptual metric、uncertainty coverage、bias、tail、temporal stability和runtime/memory；
- 至少保留一组负结果/不适用场景，证明rejection不是测试空洞；
- quality提升不能通过裁剪高能样本、显示域压平或引用泄漏获得。

### PRV.5.5 — Reconstruction diagnostic maturity

- applicability、missing plane、insufficient support、heavy tail、history invalidation、OOD、calibration、physical projection和budget failure均使用版本化reason/detail；
- rejection明确raw结果仍是否有效、fallback是否允许、哪一历史或measurement identity失效，以及用户可采取的恢复动作。

**完成门禁**:

- CLI/direct/Worker均可请求raw-only或raw+reconstruction；
- official EXR包含raw、reconstructed、uncertainty和rejection；
- camera/material/transform变化触发正确history invalidation；
- Spectrum/Stokes物理约束和缺plane fallback通过；
- 无model、weight、inference runtime或neural ABI进入产品包。
- 训练无关重建质量门禁使用production-profile 720p/1080p、多重复与足够样本的完整产品路径；1-4 spp smoke只能验证生命周期，不能证明降噪质量。
- reconstruction rejection/partial结果在artifact、CLI、SDK和Worker中保持一致，不以一般`NotApplicable`掩盖具体原因。

---

## 8. PRV.6 — Automatic Integration 产品化

**目标**: 用HT.0-HT.5权威合同替换simplified automatic renderer，使已有积分器由场景、材质、backend、预算和输出目标自动资格判定与持续调度。

**依赖**: PRV.3、PRV.4、PRV.5。

### PRV.6.1 — Technique executor registry

- wavefront、path guiding、ReSTIR DI/PT、SMS、BDPT、VCM和PSSMLT注册execution adapter；
- descriptor绑定support/measure、bias/maturity、material/wave/backend requirements、state、measurement、memory和checkpoint能力；
- legacy mode config只作为reproduction preset，不能成为第二个automatic authority；
- registry与实际compiled executor、shader/kernel identity一致。

### PRV.6.2 — Pilot and qualification bridge

- 完整场景pilot产生per-tile/per-technique cost、variance、covariance、tail、ESS、memory和maximum-contribution evidence；
- pilot/production sample domain严格分离；
- scene/material/resource/backend/output变化触发局部或全局requalification；
- `allow_experimental`等objective字段实际进入policy或被拒绝。

### PRV.6.3 — Persistent portfolio execution

- 扩展PRV.1R已建立的规范work domain与持久executor；每个selected technique保留renderer/executor state，progressive pass不重新load scene或从零渲染；
- 使用HT.3 work domain分配tile/wavelength/time/device/sample/chain；
- support partition与normalization决定组合，不能只按整图均值variance混合完整frame；
- wavefront defensive coverage、exploration、starvation和drift policy可观测。

### PRV.6.4 — Measurement and objective closure

- automatic output直接生成PRV.4 MeasurementBundle；
- AOV/uncertainty/technique contribution与混合Beauty语义一致；
- quality、deadline、latency、memory、sample和determinism目标使用实际work accounting；
- report披露selected/rejected reason、allocation、coverage、budget、fallback和quality evidence。

### PRV.6.5 — Product automatic suite

- retained场景证明不同scene/material/backend会选择不同portfolio；
- all existing techniques至少有一条Applicable或结构化NotApplicable E2E；
- 多重复统计验证combined estimator、raw measurement和reconstruction；
- manual preset与automatic plan在显式reproduction场景保持可解释关系。

### PRV.6.6 — Automatic decision diagnostics

- 每个technique的selected/rejected/deferred原因绑定support、measure、maturity、scene/material/backend、pilot统计、memory和objective证据；
- schedule drift、qualification失败、defensive fallback、budget exhaustion和normalization拒绝使用机器可读decision trace，不暴露仅供内部的指针或vendor文本。

**完成门禁**:

- product runtime不再调用simplified candidate list/whole-frame pilot authority；
- SMS/VCM/MLT不再被无条件硬编码排除；其资格由evidence和applicability决定；
- progressive work不重复已接受sample range；
- public sample/progress/quality accounting与actual schedule一致；
- automatic成为CLI和ProductJob默认，无需用户选择积分器。
- PRV.1R的线性结构计数、长时取消、production-profile产品矩阵和显存预检不得因完整portfolio接入回归。
- external client可解释“为什么选择/排除某technique”及对应恢复边界，Direct/Worker decision trace identity一致。

---

## 9. PRV.7 — 状态化 Session、有界仿真与 Checkpoint

**目标**: 统一progressive lifecycle、scene transaction、automatic/reconstruction history、bounded simulation和跨进程恢复。

**依赖**: PRV.6。

### PRV.7.1 — Product session state machine

- ProductSession拥有snapshot、plan、executors、measurements、reconstruction history、artifacts和checkpoint epoch；
- start/pause/resume/cancel/reset/replace/transaction使用一个状态机；
- Operation completed work使用accepted work domain，不使用loop count替代sample count；
- frame/event/progress在direct与Worker保持单调和一致。

### PRV.7.2 — Mutation and invalidation

- camera、transform、material、texture/resource、geometry和topology mutation使用UUID transaction；
- Scene Realizer重建受影响的ProductSnapshot部分，并选择hot/partial/full strategy；
- automatic pilot/reservoir/guiding/MLT/reconstruction/acceleration/cache分别有精确invalidation；
- transaction失败原子rollback，不污染current frame、history或cache。

### PRV.7.3 — Bounded simulation slice

- 仅接入当前可验证的deterministic rigid transform/time subset；
- simulation tick产生versioned snapshot，shutter/time和motion AOV一致；
- simulation→transform update→acceleration update→automatic→reconstruction→frame形成真实动画序列；
- fluid/acoustic/coupling若不能完成同等级E2E，则按required/optional语义拒绝或PreservedForTooling。

### PRV.7.4 — Durable checkpoint/resume

- checkpoint保存snapshot/resource/plan/schedule/measurement/reconstruction/cache compatibility和completed domain；
- estimator-specific state按capability保存；不能checkpoint的executor只在atomic epoch使用或对durable job拒绝；
- runtime/Worker/CLI进程退出后可从checkpoint恢复，不重复或遗漏accepted work；
- corrupt、partial、stale、wrong-runtime、wrong-scene和overlap checkpoint拒绝。

### PRV.7.5 — Multi-frame and session diagnostics

- 明确普通序列渲染的frame/time/camera revision语义，与bounded simulation解耦；同一scene连续frame定义哪些state累积、复用或重置；
- camera、shutter、scene transaction和frame jump分别产生结构化invalidation/reset reason，frame identity绑定sequence/frame/time和snapshot；
- session state transition、illegal operation、stale frame、checkpoint incompatibility与恢复失败具有版本化detail和last-valid state。

**完成门禁**:

- progressive render经多次mutation仍能继续并输出正确reset reason；
- automatic和reconstruction history不跨不兼容snapshot泄漏；
- bounded rigid simulation生成连续非平凡图像与motion evidence；
- Worker kill/restart和CLI restart均能恢复checkpoint；
- unsupported simulation domain不再被场景loader接受后忽略。
- 长时session、pause/resume/cancel/checkpoint通过PRV.1R异步控制面执行，任何单次work quantum都不形成无界控制延迟。
- 同一scene的多帧序列、重复帧、跳帧、camera revision和transaction更新经CLI/SDK/Worker E2E验证累积/重置语义及诊断。

---

## 10. PRV.8 — 完整场景可移植 Backend 与加速 Provider

**目标**: 把Phase T/V的runtime、kernel和provider组件接入同一ProductSnapshot与MeasurementBundle，建立真实Portable Common Profile。

**依赖**: PRV.4、PRV.6、PRV.7。

### PRV.8.1 — Common Profile freeze

- 固定native/glTF static scene、common MaterialGraph、texture、spectral packet、wavefront、AOV、measurement、reconstruction input、lifecycle和error profile；
- profile使用semantic capability而不是backend name推断；
- CUDA Reference Profile继续包含当前更完整radiometric能力；
- explicit backend请求不适用时拒绝，Auto通过objective/capability选择并报告。

### PRV.8.2 — Vulkan complete-scene lowering

- ProductSnapshot资源、material program、wavefront queue、traversal、film/AOV和measurement lower到Vulkan runtime；
- Vulkan compute BVH与Vulkan RT都是可选择provider；
- Windows NVIDIA/Intel和Linux可用GPU执行真实scene artifact；
- device loss、memory budget、pipeline cache和shader identity进入ProductSession。

### PRV.8.3 — D3D12 complete-scene lowering

- 同一Common Profile lower到D3D12/DXIL；
- compute BVH与DXR provider可选择；
- descriptor/resource/queue/fence/DRED错误映射到统一runtime semantics；
- Windows实际GPU生成measurement与EXR artifact。

### PRV.8.4 — Native provider integration

- CUDA self-compute与OptiX接入CUDA Reference scene path；
- Vulkan RT、DXR、OptiX不再只由fixed ray fixture调用；
- provider保持material/instance/primitive/UV/normal/tangent/visibility/AOV一致；
- unavailable SDK/capability只影响相应provider，不破坏其他backend。

### PRV.8.5 — Clustered geometry traversal

- 将cluster resource、residency、physical LoD和dynamic lifecycle接入complete renderer；
- shadow/specular/caustic/wave路径在error policy要求时保持exact；
- nonresident、unsafe proxy、budget和recluster/refit缺失均fail-loud；
- dense scene artifact证明实际选择coarse LoD并保持物理可见性门禁。

### PRV.8.6 — Backend/device diagnostic maturity

- adapter/provider selection、feature/precision mismatch、shader/pipeline compile、allocation、residency、device loss、DRED/validation和driver failure映射到公共诊断目录；
- vendor payload作为有界、版本化、redacted nested detail保留，稳定result/domain不随CUDA/Vulkan/D3D12 vendor code改变；
- device inventory与实际选择、预算、driver/compiler和failure correlation写入plan/artifact/report。

**完成门禁**:

- CUDA、Vulkan、D3D12各自通过独立外部ProductJob生成Common Profile真实图像、AOV和measurement；
- automatic针对backend capability选择适用technique，不手工切换模式；
- self-compute/OptiX/Vulkan RT/DXR至少各有一条complete-scene provider E2E；
- clustered geometry由产品renderer消费，不再只是resource/selector fixture；
- cross-backend报告记录physical/statistical parity、性能、VRAM、driver/compiler和明确差异。
- 每个backend/provider先通过显存与capability预检再执行共享480p functional场景；720p/1080p质量及QHD/UHD压力结果按硬件适用性分层，不以静默降级或paging cliff冒充支持。
- backend/provider/device失败在CLI、Direct和Worker保持同一顶层分类与关联identity，并保留适用的native detail。

---

## 11. PRV.9 — 多设备、Cache 与 Farm 执行

**目标**: 将MultiGpuContext、multi-backend schedule、distributed formats、`.urecache`和farm shard接入ProductJob、automatic、measurement与checkpoint。

**依赖**: PRV.6、PRV.7、PRV.8。

### PRV.9.1 — Unified device scheduler

- CLI/ProductObjective device constraints进入统一adapter inventory和capability negotiation；
- `device_ids`、backend weights、memory budget和coherence mode实际消费；
- 单卡、多CUDA卡和heterogeneous worker使用同一work-domain partition；
- path guiding、reservoir、MLT chain、pilot和measurement merge保留各自合法状态语义。

### PRV.9.2 — Runtime compiled cache

- `.urecache`实际保存/加载scene realization、resource upload、material/backend program、acceleration和validation artifact；
- cache hit记录avoided work并在benchmark中可测；
- source/compiler/backend/driver/schema/semantic mismatch按policy重建或拒绝；
- package/farm传输按resource locality调度真实cache payload。

### PRV.9.3 — Farm job and worker

- ProductExecutionPlan生成versioned internal farm manifest和authenticated shard；
- farm worker运行同一product runtime，不使用benchmark-only renderer；
- shard携带scene/resource/plan/schedule/executable/backend/compiler/checkpoint identity；
- local Worker Protocol不被扩张为网络协议；farm transport实现与部署继续是内部边界。

### PRV.9.4 — Canonical merge and recovery

- radiance/measurement shard按canonical order合并sum、moments、ESS、tail、AOV和reconstruction inputs；
- overlap、gap、wrong provenance、incompatible estimator/backend/precision和corrupt payload拒绝；
- farm interruption从checkpoint恢复未完成domain；
- merged artifact与单机reference在声明统计阈值内一致。

### PRV.9.5 — Distributed/cache diagnostic maturity

- scheduler、cache miss/invalidation、worker admission、shard gap/overlap/provenance、network/deployment、merge和recovery分别使用版本化detail；
- 多worker cause graph保留worker/shard/device identity和可重试范围，聚合错误不得丢失首因或产生无界cause chain；
- cache miss是可观测状态而非错误，cache corruption、identity mismatch和untrusted payload明确区分。

**完成门禁**:

- 实际多设备ProductJob由CLI/Worker入口触发，不直接调用MultiGpuContext test API；
- cache cold/warm运行证明真实load-time或compile/upload收益；
- 至少两个实际worker完成disjoint shard→merge→reconstruction→EXR；
- kill/restart不重复sample/spectral/chain domain；
- single-device、multi-device和farm report共享plan/measurement identity体系。
- CLI/Worker触发的实际产品路径必须保留PRV.1R requested/accepted/completed work不重叠不遗漏的会计不变量，组件级merge fixture不能替代产品E2E。
- farm/cache/merge负向E2E证明诊断可定位到具体worker/shard/cache identity并给出安全恢复策略。

---

## 12. PRV.10 — 公共客户端与 Adapter 收敛

**目标**: 让所有维护中的渲染客户端使用`ure_client`与统一product service，清除Public Interaction Surface Ledger中的MustConverge执行绕过。

**依赖**: PRV.1-PRV.9。

### PRV.10.1 — Generated C/C++ SDK

- 独立SDK package包含Core 1.0、Preview extensions、Worker schemas、client library、fixtures、mock和examples；
- clean out-of-tree C11/C++23 clients只通过SDK/runtime packages工作；
- direct和Worker examples覆盖scene realize、automatic report、measurement、reconstruction、checkpoint和artifact；
- runtime package继续不暴露renderer-private headers或import-library dependency。

### PRV.10.2 — Python migration

- 新Python adapter绑定`ure_client`，不再以`pyure_native.dll` legacy C API为新功能入口；
- 提供async operation/event、scene/tooling、measurement partial read、reconstruction、checkpoint和artifact API；
- legacy pyure保持迁移兼容但feature-frozen；
- numpy/array view lifetime不能越过frame/lease generation。

### PRV.10.3 — Hydra convergence

- Hydra delegate通过`ure_client` ProductJob/Session服务，不直接创建C++ RenderSession；
- retained RPrim/SPrim/camera状态转为canonical scene transaction或replacement；
- Hydra render buffer消费同一measurement/output语义；
- OpenUSD SDK不可用不阻塞核心构建，但release evidence包含实际SDK/GPU E2E。

### PRV.10.4 — Interaction ledger closure

- CLI render/tooling、Hydra、Python、legacy C++/C、distributed/farm/provider和installed C++ surface更新最终disposition；
- externally supported rendering仅经Core/Preview/Worker transport；
- internal C++ APIs可以保留实现用途，但安装/文档不暗示稳定产品入口；
- 无duplicate authority、MustConverge bypass、expired migration gate或unclassified public-looking surface。

### PRV.10.5 — Integrator-facing diagnostics and SDK closure

- generated SDK直接分发预生成协议头、`ure_client`参考库/源码、CMake package、诊断目录/decoder与示例；普通consumer不依赖`flatc`或renderer-private C++ ABI；
- C/C++/Python/Hydra将同一结构化diagnostic映射到各自语言异常/结果模型而不丢失result/domain/detail/correlation/cause；
- 建立Blender类host集成fixture，覆盖进度、Esc取消、多帧、multi-plane、设备选择、错误恢复与日志关联；它是外部client evidence，不进入仓库GUI范围。

**完成门禁**:

- CLI、C11、C++23、Python、Worker和Hydra对共享场景取得兼容snapshot/plan/output identity；
- 上述每种维护调用方式至少直接完成一条真实render→artifact；mock只验证合同，不能满足产品E2E，跨调用方式采用共享场景的风险pairwise矩阵；
- repo client binaries的链接/import audit不包含绕过runtime的renderer入口；
- legacy API兼容测试保持绿色但不接收Preview-only semantics；
- generated SDK与runtime分包、安装、许可证和out-of-tree消费通过。
- SDK诊断目录与预生成协议头绑定registry/runtime identity，跨语言golden及无`flatc`clean consumer通过。

---

## 13. PRV.11 — `UltraRender_preview` 统一验证与声明门禁

**目标**: 冻结可重建的Preview证据，确认产品工作流闭环，并在单独用户批准后声明产品Preview。

**依赖**: PRV.0-PRV.10全部完成。

### PRV.11.1 — Workflow matrix

- native/text/binary/package、glTF、MaterialX-derived和USD/Hydra输入；
- diffuse/PBR/glass/layer/volume/Mie/spectral/diffractive/fluorescent材质；
- automatic多technique、manual reproduction和structured NotApplicable；
- raw/reconstructed/uncertainty/rejection/multilayer EXR；
- progressive mutation、bounded simulation、cancel、device loss、checkpoint/restart；
- CUDA Reference、Vulkan/D3D12 Common Profile和native provider；
- single/multi-device/farm/cache；
- CLI/direct/Worker/C/C++/Python/Hydra clients。

每个required cell绑定artifact、identity、metric、runtime、hardware、driver/compiler、repeat count和failure policy。可选环境缺失必须标记NotAvailable，不能用mock替代required release evidence。

矩阵按风险pairwise覆盖而不是机械构造全笛卡尔积，但每个维护client/transport、输入家族、输出家族、backend profile和恢复路径都有真实产品调用。PRV.11只汇总此前阶段持续运行的证据，不允许在声明门禁首次发现这些路径从未产生过产品artifact。

### PRV.11.2 — Quality, performance and reliability

- correctness覆盖reference、bias/variance、spectral/Stokes physical domain、AOV和reconstruction uncertainty coverage；
- performance覆盖cold/warm scene realization、render、reconstruction、output、VRAM/RAM、cache和Worker transfer；
- reliability覆盖100-run lifecycle、cancel race、backpressure、corrupt input、Worker crash、device loss、checkpoint和artifact atomicity；
- security保持same-user local Worker、no TCP/UDP listener、no firewall request、no ambient code/model/provider discovery。
- routine smoke、480p functional、720p/1080p quality与QHD/UHD 500+ spp stress分别报告；stress绑定适用硬件且不倒灌为普通提交门禁，质量层不得关闭production profile能力。
- raw measurement/artifact保留为权威，固定PNG派生、自动图像指标与人工视觉审阅共同进入release evidence；文件存在或跨transport hash相同不构成质量证明。

### PRV.11.3 — Unified diagnostic closure

- 聚合所有现役result/domain/detail schema、事件、operation terminal error、recovery hint与redaction policy，生成machine-readable catalog和human integration guide；
- 每个PRV领域至少一条真实失败注入贯穿CLI/SDK→Direct/Worker→runtime/product/backend并保持correlation/cause；未知异常、Error分配失败、cause truncation和diagnostic budget exhaustion具有安全应急路径；
- 运行fuzz、cross-version、message/redaction、bounded payload/rate、100-run failure lifecycle和跨语言golden，确认诊断本身不泄漏、不死锁、不遮蔽原始result；
- `Internal`占比和无detail fallback受release gate审计：除批准的emergency path外，不得存在普通失败无retained/terminal诊断。

### PRV.11.4 — Cross-platform build and execution evidence

- Windows x64 MSVC/CUDA/Vulkan/D3D12完整bundle构建；
- Linux GCC/Clang CUDA-off host/package保持绿色；
- Linux Vulkan Common Profile在实际适用GPU执行；
- Windows retained Core 1.0 client和Worker Protocol 1.0兼容矩阵继续通过；
- 不声明macOS、ARM64或未验证平台。

### PRV.11.5 — Product package

- package包含runtime、Worker、CLI、generated SDK、client library、预生成协议头、diagnostic catalog/guide/decoder、schemas、manifest、fixtures、licenses、examples和validation report；
- artifact目录、install layout、explicit runtime path和dependency manifest稳定；
- package在clean machine/isolated directory完成安装后E2E；
- 软件包仍标记Preview，extensions保持0.x，支持策略不偷换为UltraRender 1.0。

### PRV.11.6 — Declaration gate

- 输出`ure.preview.validation.v1`和human-readable closure report；
- root README、STATUS、docs index、architecture、support policy和AGENTS口径一致；
- product closure ledger为零accepted-but-ignored、duplicate authority、MustConverge bypass和false ProductE2E；
- diagnostic catalog覆盖全部现役失败域，普通失败为零无上下文`Internal`/null Error，所有调用方式保留可关联结构化错误；
- 完成REPORT后停下，必须获得用户对`UltraRender_preview`声明、tag、push和分发的分别批准。

**完成门禁**:

- 用户不需要选择积分器即可通过CLI/SDK/Worker完成复杂场景渲染；
- CLI、Worker和runtime不存在第二套产品实现；
- Native advanced semantics被真实执行、显式拒绝或保留为tooling；
- 现有积分器、材质、backend、provider、multi-device、farm、cache和session能力进入统一产品工作流；
- 训练无关重建提供raw/reconstructed/uncertainty/rejection共同结果；
- artifact可保存、验证、合并、checkpoint并跨进程恢复；
- Core ABI 1.0 / Worker Protocol 1.0兼容承诺未被削弱；
- 高阶研究路线保持冻结，没有用Preview名义引入新的研究债。

---

## 14. 文档与历史治理

- 根目录只保留`README.md`、`STATUS.md`、`PLAN.md`和`AGENTS.md`四份Markdown入口/治理文件；
- 本PLAN只记录PRV当前和未来施工，不复制归档计划的完整阶段历史；
- `docs/UltraRender_Preview_Architecture.md`定义产品架构，PLAN定义顺序和完成门禁；
- PB文档继续规范Core ABI 1.0 / Worker Protocol 1.0，不再拥有全局施工游标；
- HO/HT/HR文档是已完成合同和冻结研究参考，其中HT/HR已有成果可由PRV阶段消费；
- 阶段证据文档中的旧“next cursor”是历史快照，不得覆盖根PLAN；
- README与STATUS只陈述当前已验证能力和PRV游标，不预先把Preview目标写成现有功能；
- 每次PRV阶段推进同步更新PLAN状态、STATUS能力表、README必要摘要和docs index。
