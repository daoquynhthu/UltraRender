# UltraRender 高阶能力研究与实施路线图

最后更新: 2026-08-01（高阶能力路线切换）

本文档是 UltraRender 当前唯一的行动纲领。2026-08-01 以前的主体建设路线已归档为
[`docs/archive/Legacy_Construction_PLAN_2026-08-01.md`](docs/archive/Legacy_Construction_PLAN_2026-08-01.md)，
用于追溯已经完成的 Q/R/T/V/W/U 等阶段，不再定义当前施工顺序。原 Phase X 插件系统延期，
其 ABI 只能在本路线冻结新的 world、transport、measurement 和 solver 边界后重新设计。

本路线服务于一个长期目标：把 UltraRender 从“拥有多种高级渲染技术的离线渲染器”推进为
“共享同一物理世界、可组合估计、可重建观测、可求导并可反向影响世界状态的深度仿真与渲染系统”。
这是研究路线，也是工程路线；工程门禁用于判断成果何时可以进入默认路径，不得用来禁止尚无现成答案的研究。

---

## 0. 权威状态

当前游标: HT.3 — 在线 portfolio 调度

### 0.1 唯一生产施工队列

```text
Legacy construction plan Q/R/T/V/W/U                    [done]
    │
    ▼
HO.0 capability debt and research baseline              [done]
    │
    ▼
HO.1 unified semantics and architectural contracts      [done]
    │
    ▼
HO.2 executable research substrate                      [done]
    │
    ├──────────────┬────────────────┬────────────────┐
    ▼              ▼                ▼                ▼
HT transport     HR reconstruction HW physical world HD differentiation
[HT.3 current]   [HR.0 done]
    │              │                │                │
    └──────────────┴────────┬───────┴────────────────┘
                            ▼
                     HO.C integrated closures
```

队列游标只表示默认/生产路径的唯一施工阶段。HO.1 之后，满足依赖的研究实验可以并行存在，
但不得借“研究中”宣称生产完成，也不得在没有适用域、证据和回退语义时进入默认执行路径。

### 0.2 当前基础

以下能力作为本路线的输入，不在新计划中重新建设：

- 原生场景、MaterialGraph、资源与分布式文件合同已经建立；
- CUDA 是当前完整场景生产/参考后端，Vulkan、D3D12/DXR、OptiX 已有不同范围的 runtime 与 acceleration 基础；
- wavefront、path guiding、ReSTIR DI/PT、SMS、BDPT、VCM、PSSMLT 已有独立实现和统计证据，但仍由单一模式选择与组合禁区约束；
- 光谱、Stokes/Mueller、Mie、荧光、衍射材料、部分相干和局部全波耦合已有不等成熟度的实现或参考合同；
- `RenderSession` 当前主要输出 RGB framebuffer 和有限 AOV，尚不足以支撑高阶重建、技术组合和稳健求导；
- `World`、native simulation contract 与 `ure_physics` 提供了 ECS、solver 描述、简单刚体/流体/声学起点，但尚未构成统一动态物理世界；
- 当前 Release 构建注册 57 个 CTest。该数字只是切换路线时的快照，不是长期固定指标。

### 0.3 延期路线

| 项目 | 当前决定 | 原因 |
|---|---|---|
| 原 Phase X 通用插件 ABI | 冻结，不施工 | 过早冻结 ABI 会把旧的单积分器、静态 World、RGB/AOV 和 solver 边界固化为长期债务 |
| 通用交互式 viewport | 非当前目标 | 先建立时间、状态、重建和可中断执行语义；GUI 不能反向定义核心物理模型 |
| CPU production integrator | 继续不做 | CPU 保留 oracle、编译、调度、研究验证和小规模参考角色 |
| 单一“万能物理解算器” | 明确不做 | 统一的是状态、时间、单位、耦合和观测，不是用一个数值方法替代所有专用求解器 |
| 先做神经降噪再补数据合同 | 明确不做 | 缺失波长、PDF、技术来源、矩和置信信息会形成不可逆的数据债务 |

---

## 1. 总体架构

### 1.1 闭环物理世界

```text
Authoring / Native Scene
          │
          ▼
Canonical World Definition ───── parameters / controls / constraints
          │
          ▼
Time-varying Physical State ───── specialized solver-private states
          │                         │
          │                         ▼
          └────────────── Coupling / Transfer Graph
                                  │
                                  ▼
                     Immutable Observation Snapshot
                                  │
                ┌─────────────────┴─────────────────┐
                ▼                                   ▼
       Transport Technique Graph              Physical Sensors
                │                                   │
                └─────────────────┬─────────────────┘
                                  ▼
                         Measurement Bundle
                                  │
                    ┌─────────────┴─────────────┐
                    ▼                           ▼
            Reconstruction                Estimation / Loss
                    │                           │
                    └─────────────┬─────────────┘
                                  ▼
                     Gradients / Inverse / Control
                                  │
                                  └──────────────► World parameters
```

“统一物理世界”在本项目中的严格含义是：所有渲染、传感、仿真、求导和控制操作都引用相同的
世界身份、时间语义、单位系统、属性字段和不可变观测快照。每个求解器可以保留最适合自己的网格、
粒子、模态或稀疏结构，但不能私自维护无法追溯到 canonical world 的第二份真相。

### 1.2 目标模块边界

这些是路线目标，不表示本次文档切换已经创建对应库：

| 模块 | 责任 | 禁止承担的责任 |
|---|---|---|
| `ure_types` | 稳定值类型、标识、单位和不可变描述 | solver 状态、GPU SDK 类型、训练 runtime |
| `ure_runtime` | backend-neutral resource、execution graph、同步与预算 | 积分器物理策略、世界 authoring |
| `ure_transport` | observable、path measure、Technique Graph、组合与采样合同 | backend native allocation、图像美化 |
| `ure_reconstruction` | MeasurementBundle、统计/学习型重建、置信与有效性投影 | 改写无偏累计器或伪造缺失物理信息 |
| `ure_world` | 时间、canonical dynamic state、property field、snapshot、coupling graph | 把所有专用 solver 合并为一个实现 |
| `ure_physics` | 刚体、连续体、流体、声学、热等 solver/provider 实现 | 成为另一份 authoring schema |
| `ure_inverse` | 参数化、replay、梯度、优化、实验编排 | 隐藏不可导边界或把有限差分冒充解析梯度 |
| `ure_core` | GPU transport/reconstruction lowering、CUDA 参考实现、session orchestration | 重新暴露 backend-private native handle |

模块拆分必须由 HO.1 的依赖审计确认后逐步进行。允许先在现有模块内建立合同，再迁移到独立库；
禁止为了目录美观进行一次性大搬迁。

### 1.3 共同身份

以下 identity 必须贯穿本路线，并进入缓存、分布式与证据记录：

- `world_definition_id`: authoring 与静态资源语义；
- `world_state_id`: 某一动态状态的内容身份；
- `time_sample_id`: 连续时间区间、离散 tick、曝光 shutter interval 与 solver substep 映射；
- `observation_snapshot_id`: 渲染/传感实际读取的冻结状态；
- `technique_graph_id`: 启用技术、support、measure、组合器和 lowering 的语义身份；
- `measurement_schema_id`: 输出字段、精度、累计与充分统计语义；
- `parameter_set_id`: 可微参数及其约束/变换身份；
- `solver_semantics_id`: solver 算法、离散化、边界和版本身份；
- `evidence_id`: 输入、seed、binary、配置、metric 与 artifact 的可复现记录。

---

## 2. 研究治理与成熟度

### 2.1 三种成熟度

成熟度与计划阶段正交。同一阶段可同时包含 Research、Experimental 和 Production 成果。

| 等级 | 允许 | 最低证据 | 不允许宣称 |
|---|---|---|---|
| Research | 临时数据结构、参考实现、局部 kernel、论文复现、失败实验 | hypothesis、输入/seed、baseline、metric、artifact、已知失效域 | 默认可用、ABI 稳定、跨后端、普遍优于基线 |
| Experimental | 受配置保护的完整路径、限定场景/设备研究 | 重复独立运行、统计区间、资源记录、明确适用域和退出条件 | production、无偏（除非已证明）、任意场景稳定 |
| Production | 默认或公开 API 路径 | 生命周期、预算、fail-loud、回归、跨层 identity、文档、完整相关门禁 | 超出已验证边界的能力 |

研究阶段不要求为了新增一个假设而先通过所有工程门禁。对 production 文件、公共合同或默认路径的修改，
仍必须通过受影响测试；研究成果升级为 Experimental/Production 时，再补齐相应的全量证据。

### 2.2 Research Capsule

每个非平凡研究实验必须保存一个可重放 capsule，至少包含：

1. 明确的问题和可证伪假设；
2. 输入场景/世界状态、参数、seed/range 和 source digest；
3. 对照方法与为何可比；
4. 误差、方差、偏差、时间、显存或物理残差中的适用指标；
5. executable/compiler/backend/driver identity；
6. 原始充分统计或可重新计算结论的最小 artifact；
7. 负结果和失效域，而非只保存成功图像；
8. maturity、owner phase 与下一次判定条件。

Research Capsule 不强制一种论文目录格式。HO.0 将冻结最小机器可读 schema 和便于人工审阅的索引。

### 2.3 能力边界不是同一种技术债

所有现有和新增 fail-loud 必须被归入以下类别之一：

| 类别 | 含义 | 处置 |
|---|---|---|
| Physical/Mathematical Boundary | 当前模型在物理或数学上不定义该组合 | 永久保留或由新模型显式替代，不能静默降级 |
| Missing Estimator Measure | 缺少 joint sample、shift、support、PDF 或 normalization | 归 HT/HD，解决前拒绝组合 |
| Missing Lowering/Backend | 合同存在但某 backend 未实现 | 归 backend owner，不把 capability 枚举当执行能力 |
| Resource/Budget Boundary | 内存、队列、栈、时间或精度不足 | 预检、分块或显式有损模式；禁止 OOM 后碰运气 |
| Insufficient Evidence | 路径存在但证据不足 | 保持 Experimental，不等于算法错误 |
| Explicit Lossy Projection | adapter/preview 主动丢失语义 | 必须有 loss report、可发现标记和非默认策略 |
| Schema/Identity Boundary | 输入版本、编码、observable 或 artifact identity 不相容 | 保留版本化拒绝或显式迁移；不能把未知数据按当前布局解释 |
| Accidental Debt | 无架构理由的互斥、重复状态、弱数据合同 | 排入修复队列并定义移除证据 |

路线目标不是机械减少异常数量，而是消灭未分类、不可追踪和本可组合却因旧结构造成的 accidental debt。

### 2.4 证据原则

- 对随机估计器使用独立 sample ranges/chains/replicates，禁止让参考图与候选共享导致偏乐观的随机前缀；
- 同时报误差、置信区间、时间和资源，禁止只报单张视觉结果；
- 对有偏重建器明确 bias class，不以低 MSE 偷换物理正确性；
- 对物理耦合报告守恒量、残差、稳定性和时间同步，不以“看起来会动”作为门禁；
- 对梯度同时使用解析/自动微分一致性、中心有限差分、小问题 oracle 和优化方向有效性；
- 负结果可以关闭研究分支，不需要用工程包装维持已经被证伪的方案。

---

## 3. HO Foundation — 共同基础

### HO.0 — 能力债务普查与研究基线

**状态**: 已完成（2026-08-01）。机器可读 ledger、八种积分器 inventory、十类 measurement gap、十一项 state ownership、Research Capsule v1 和七类 benchmark family 已由 `scripts/check_phase_ho0_baseline.ps1` 闭环；详见 `docs/HO_0_Capability_Baseline.md`。

**目标**: 在改变核心 API 前，把真实能力、组合禁区、缺失信息和研究证据统一为可查询的事实。

**主要工作**:

| Step | 内容 | 完成证据 |
|---|---|---|
| HO.0.1 | 建立 Capability Boundary Ledger，逐项记录触发入口、类别、owner、依赖、是否 accidental、是否影响默认路径 | 静态扫描可定位源码/测试与 ledger 双向引用；未分类条目为零 |
| HO.0.2 | 审计全部积分器和 reuse 技术的 observable、sample space、support、PDF/weight、normalization、相关性、bias class、资源与组合限制 | 每个现有模式有完整 descriptor 草案，单一 `integrator.mode` 债务有迁移图 |
| HO.0.3 | 审计 framebuffer/AOV/sample record，列出重建、可微和统计调度所需但当前丢失的信息 | 形成 Measurement Gap Matrix，字段有生产者、消费者、成本和保留策略 |
| HO.0.4 | 审计 World、PhysicsWorld、native solver/simulation contracts、SceneDiff 与时间语义 | 形成 State Ownership Map，重复真相、隐含单位、离散时间和 snapshot 风险均有归属 |
| HO.0.5 | 冻结 Research Capsule v1 与结果索引，不搬运大型临时输出进入 Git | 一个正结果与一个负结果可从 capsule 重放并复核结论 |
| HO.0.6 | 建立高阶基准场景族：普通漫反射、SDS/焦散、参与介质、偏振、荧光/波动、动态耦合与逆问题小场景 | 每类包含 analytic/small oracle 或明确参考生成策略，资源 license/provenance 可追溯 |

**范围约束**:

- 本阶段不重写积分器、不引入训练框架、不扩展 PhysicsWorld；
- 可以为盘点增加只读工具、schema 和研究资产；
- 扫描出的 fail-loud 不得批量删除，必须先分类；
- 现有 57 个 CTest 和闭环证据保持可运行，文档-only 子步骤不要求重新编译 GPU。

**完成标准**:

- ledger、integrator inventory、measurement gap、state ownership 和 capsule schema 相互引用且无孤立条目；
- 每一项 accidental debt 都有后续 HO/HT/HR/HW/HD owner；
- 永久边界和暂时债务可以被机器区分；
- 权威游标推进到 HO.1。

### HO.1 — 统一语义与架构合同

**依赖**: HO.0。

**状态**: 已完成（2026-08-01）。新增 SDK-free `ure_transport` 与共享 `ure::semantic` value types，冻结 observable、measure/support、estimator、unit/time、provenance、uncertainty 和五态 compatibility algebra；独立 SDK-free 5/5、installed package consumer 与 Release 58/58 CTest 通过。详见 `docs/HO_1_Unified_Semantics.md`。

**目标**: 在实现自动组合和统一世界前冻结最小公共词汇，而不是冻结具体算法。

| Step | 内容 | 关键合同 |
|---|---|---|
| HO.1.1 | Observable 与 domain | radiance、Stokes、Jones/complex field、mutual intensity、transient、sensor response、loss functional 不可混型 |
| HO.1.2 | Measure 与 estimator semantics | path/sample measure、support predicate、PDF/weight、normalization、bias class、correlation group |
| HO.1.3 | Time 与 units | rational tick、continuous time、shutter interval、solver substep、SI/declared units、frame transforms |
| HO.1.4 | State 与 provenance | world/state/snapshot/resource/solver/technique/measurement/parameter identity |
| HO.1.5 | Uncertainty | moments、cross-moments、effective sample size、variance/covariance、confidence 与 OOD 标记 |
| HO.1.6 | Compatibility algebra | compatible、requires transform、independent aggregate、preview-only、mathematically undefined 五类结果 |

**设计约束**:

- 合同是 backend-neutral POD/value semantics，不暴露 CUDA/Vulkan/D3D12/OpenUSD 类型；
- 不把 RGB 当作所有 observable 的公共最低类型；
- 不假设所有 estimator 都能用普通 balance heuristic 相加；
- 不假设所有 world state 都能在一个固定帧率下推进；
- 未知 extension 只有在明确 optional 且可保留时才能 round-trip。

**完成标准**: host oracle 能拒绝量纲、measure、observable、identity 和时间不兼容组合；现有路径可无损映射到新合同。

### HO.2 — 可执行研究底座

**依赖**: HO.1。

**状态**: 已完成（2026-08-01）。新增 SDK-free `ure_research`，统一 deterministic manifest/sample-counter shard、前置目录与逐 chunk digest 的 measurement artifact、replicated comparison、maturity-aware capability negotiation、bounded host/small-GPU oracle hook 和机器化 promotion evidence；独立 SDK-free、installed package consumer 与 Release 59/59 CTest 通过。详见 `docs/HO_2_Executable_Research_Substrate.md`。

**目标**: 让新研究共享编译、执行、记录和比较基础，不强迫研究代码立即满足生产 ABI。

| Step | 内容 | 完成证据 |
|---|---|---|
| HO.2.1 | Research execution manifest 与 deterministic seed/range allocator | local/multi-GPU/farm 可生成不重叠身份 |
| HO.2.2 | Measurement artifact container | 支持 schema-versioned chunk、充分统计、压缩、partial read、digest 与预算 |
| HO.2.3 | Experiment registry 与 comparison runner | 同一 capsule 可运行 baseline/candidate/replicates 并输出置信区间 |
| HO.2.4 | Feature/capability negotiation | Research/Experimental path 不因 enum 存在而被误判可执行 |
| HO.2.5 | Lightweight reference backend hooks | host/small GPU oracle 可插入，不形成 CPU production renderer |
| HO.2.6 | Promotion checklist automation | Research→Experimental→Production 的证据缺口可机器报告 |

HO.2 完成后，各研究轨可以在满足依赖时推进；默认路径的唯一游标仍按本计划的 closure milestone 管理。

---

## 4. HT Transport — 自动积分与估计器组合

### 4.1 核心判断

目标不是“把所有积分器输出直接相加”，而是把现有和未来算法表达为 Technique Graph 中可分析的技术节点。
只有在 observable、measure、support 与 normalization 相容时，节点才可通过 MIS 或受证明的重采样规则组合。
MCMC 链、相关复用、biased preview 和不同 observable 必须使用各自的统计组合语义。

### 4.2 Technique Descriptor

每个 technique 必须声明：

- 估计的 observable 与积分域；
- path construction、sample space 与 support predicate；
- forward/reverse/joint PDF 或无偏 contribution weight；
- normalization 与零贡献条件；
- correlation group、replay/shift 能力和随机维度需求；
- bias class 与渐近性质；
- 每样本成本、scratch、persistent state 和 backend capability；
- spectral、polarization、volume、fluorescence、coherence、differentiation 兼容性；
- 与其他 technique 的互斥原因，且原因必须来自数学/资源/未实现分类之一。

### 4.3 现有技术的目标角色

| 现有技术 | 目标角色 | 不应承担的角色 |
|---|---|---|
| Wavefront PT | 防御性全支撑基线、pilot/reference 技术 | 永久由人工独占选择 |
| Path guiding | 可共享的方向/距离/介质 proposal service | 独立改变目标积分量 |
| ReSTIR DI/PT | 带 provenance 的候选复用与 GRIS 类重采样层 | 与任意 suffix/measure 无证明混用 |
| SMS | 对 delta/specular chain 提供 proposal/solver 节点 | 被当作覆盖所有路径的完整积分器 |
| BDPT/VCM | 显式 technique family 与连接/合并测度 | 用单个模式枚举隐藏 technique partition |
| PSSMLT | 独立 Markov-chain estimator family | 直接进入普通每样本 MIS 而忽略相关性和归一化 |
| Wave/coherent reference | 独立 observable 或局部 scattering provider | 复振幅在 RGB/辐射度累计器中相加 |

### HT.0 — 现有积分器描述化

**依赖**: HO.1。

**状态**: 已完成（2026-08-01）。`ure_transport` 新增 SDK-free Technique Descriptor、资源合同、Technique Graph 验证和 legacy preset compiler；wavefront、guiding、ReSTIR DI/PT、SMS、BDPT、VCM 与 PSSMLT 已被同义映射，现有数学/资源/未实现拒绝被结构化分类，`IntegratorMode` 决策面由静态 ledger 冻结。核心 estimator metadata 通过 preset route 读取同一语义，未引入自动组合或改变默认样本路径。独立 SDK-free 7/7、installed package consumer 与 Release 60/60 CTest 通过。详见 `docs/HT_0_Legacy_Technique_Graph.md`。

- 新增 SDK-free Technique Descriptor 与 graph validation；
- 将当前 `IntegratorMode` 与各独立 enable config 映射为 legacy preset，而非继续作为权威执行模型；
- 为 wavefront、guiding、ReSTIR、SMS、BDPT、VCM、PSSMLT 建立完整 descriptor；
- 保持当前默认结果与显式拒绝边界不变，先做到“同义表达”；
- 用静态审计阻止新增仅由 mode switch 决定的 estimator 语义。

**毕业证据**: legacy preset 与 Technique Graph 在固定场景上逐样本身份/统计等价；所有现有拒绝都有结构化原因。

### HT.1 — Support/Measure Graph 与组合器

**依赖**: HT.0。

**状态**: 已完成（2026-08-01）。SDK-free `ure_transport` 新增 bounded path-event grammar、epsilon-NFA 到 deterministic automaton 编译和 target/technique product partition；精确拒绝 support hole、target 外支持及状态/partition 超预算并保留 witness。组合计划显式转换 canonical measure/Jacobian，提供 balance/power MIS、GRIS provenance、独立 contribution、MCMC replicate aggregation 与 unbiased/asymptotic/consistent/preview/research 分层；preview 不得满足 unbiased coverage。解析积分、有限路径空间枚举与 CUDA packed-program E2E 通过，独立 SDK-free 9/9、installed package consumer 与 Release 63/63 CTest 通过。当前 legacy preset 仍由配置选择，HT.2 才开始 pilot 资格判定。详见 `docs/HT_1_Support_Measure_Composition.md`。

- 编译 path-event grammar、technique support 与 overlap partition；
- 对同测度技术提供 MIS family；对可转换测度显式提供 Jacobian/shift；
- 对 reservoir/reuse 提供 generalized resampled importance sampling 语义与 provenance；
- 对 MCMC 使用独立 replicate/chain normalization 和跨 estimator aggregation；
- biased preview 永远进入独立输出层，不污染无偏 production estimate；
- 对缺失 PDF、未知 support overlap、singular transform 和不兼容 observable 在 allocation 前拒绝。

**毕业证据**: analytic integral、finite path-space enumeration 与 GPU E2E 三层证明无重复计数和 support hole。

### HT.2 — Pilot 统计与自动资格判定

**依赖**: HT.1、HO.2。

**状态**: 已完成（2026-08-01）。SDK-free `ure_transport` 新增 content-bound pilot provenance、per-technique sufficient statistics、paired covariance 和自动资格报告；短 pass 统计成本、方差、tail risk、ESS 与 measured memory，并严格绑定 Technique Graph、world state、observation snapshot、support partition 和 observable。独立 holdout、cross-fitting 与 selection-probability correction 防止自适应选择偏差静默进入生产估计；scene/event/backend/budget/output-layer 资格判定不读取 `IntegratorMode`，expert override 默认关闭且不能绕过物理、后端或资源失败。实际 CUDA sample producer、host 统计/资格门禁、独立 SDK-free 10/10、installed package consumer 与 Release 65/65 CTest 通过。在线分配仍属于 HT.3。详见 `docs/HT_2_Pilot_Qualification.md`。

- 从短 pilot pass 估计每技术成本、方差、协方差、tail risk、有效样本量和内存压力；
- 资格判定基于 scene/world snapshot、observable、material/medium event 与 backend capability；
- 没有资格的技术不参与，不要求用户了解积分器名称；
- 保留 expert override 作为实验工具，但默认不依赖手工 mode；
- pilot 本身使用独立或可校正样本，不能把自适应选择偏差静默带入主估计。

### HT.3 — 在线 portfolio 调度

**依赖**: HT.2。

- 在预算约束下分配 technique、tile、wavelength、time、device 和 chain 资源；
- 使用 cost-aware variance/covariance 目标，明确 exploration floor 和 starvation 防护；
- 对非平稳 progressive/dynamic world 检测统计漂移并重新 pilot；
- scheduler 决策写入 measurement provenance，支持 deterministic replay；
- 分布式 worker 只能执行语义兼容的 graph shard，merge 时复核 technique coverage。

### HT.4 — 新积分器与 proposal 研究平台

**依赖**: HT.1；可与 HT.2/HT.3 研究并行。

允许研究而不预先指定论文答案：

- multiplexed/path-space MCMC 与现有 BDPT/VCM/SMS 的 joint sample contract；
- manifold/path guiding、neural proposal、volume/fluorescence guiding；
- gradient-domain、control variate、multilevel/multifidelity estimator；
- transient、polarized、spectral packet 和 wave/radiance hybrid transport；
- 世界状态变化下的 reuse validity、transport maps 与 nonstationary estimators。

任何新技术先以 descriptor + capsule 进入 Research；只有证据显示其填补 support、降低 time-to-error 或解锁新 observable，
才进入 Experimental。计划不要求每个研究方向最终进入 production。

### HT.5 — 自动积分系统闭环

**依赖**: HT.3，并吸收有价值的 HT.4 成果。

完成条件：

- 默认用户描述质量/时间/内存目标，而不是选择积分器；
- 系统能自动排除不适用技术、组合可组合技术并解释决策；
- 每个输出可追溯到 technique coverage、weights、normalization 和 uncertainty；
- wavefront baseline 在未知域保留防御性 coverage；
- 固定多场景 suite 对 bias、variance、time-to-error、tail、VRAM 和 distributed merge 做独立重复验证；
- 手工 legacy mode 仅作为兼容 preset，且有明确移除或长期保留决定。

---

## 5. HR Reconstruction — 测量重建与降噪

### 5.1 定位

该子系统不只做 RGB 后处理。它从 Monte Carlo、波动、传感器或多物理观测的充分统计中恢复信号，
输出估计值、置信和适用性判断。它必须理解 spectral/Stokes/complex-field 边界，且不得破坏无偏累计器。

### HR.0 — MeasurementBundle / Feature Film

**依赖**: HO.1、HO.2；必须在大规模积分器改造前稳定第一版。

**状态**: 已完成（2026-08-01）。新增 SDK-free `ure_reconstruction`，冻结 typed plane、SI unit、observable、validity、provenance、retention/budget loss 与 schema identity；实现按 sample range/producer canonical ordering 的 additive/invariant/append/derived merge，使用 count/raw moments 重算 ESS、sample variance/covariance，并以 HO.2 `UREM` 提供自包含 checkpoint、前置索引、逐 plane authenticated partial read 和 bounded RLE。现有 CUDA framebuffer/AOV producer 保持不变，不虚报尚未接入的高阶 plane。独立 SDK-free 8/8、installed package consumer 与 Release 61/61 CTest 通过。详见 `docs/HR_0_Measurement_Bundle.md`。

MeasurementBundle 至少支持可预算选择的以下层次：

- spectral/Stokes contribution、detector/transport wavelength 与 joint PDF；
- technique ID、support class、MIS/GRIS/chain weight、sample/range identity；
- path-event signature、depth、time/OPL、material/medium/resource identity；
- normal、albedo、depth、UV、motion 等几何特征及其有效性 mask；
- first/second moments、cross-moments、effective sample count、variance/covariance；
- world/snapshot/graph/schema identity；
- bounded sample records 或 compressed sufficient statistics；
- complex/Jones/mutual-intensity 数据必须使用独立 typed planes，禁止压入 RGB。

**毕业证据**: 分块、累积、merge、checkpoint、跨设备和 partial read 不改变统计语义；超预算时显式选择降级层级并记录 loss。

### HR.1 — 统计重建基线

**依赖**: HR.0。

- 建立不依赖训练数据的 variance-guided spatial/temporal/filtering baseline；
- 引入 outlier/tail-aware statistics，区分 firefly、真实高能路径和错误样本；
- 对动态 snapshot 使用 motion/time confidence 和 disocclusion validity；
- 对 spectral/Stokes 先在物理域重建，再做显示变换；
- 输出 per-pixel uncertainty、filter support 和 rejection reason；
- 保留 raw estimate，所有质量比较可回到未重建数据。

### HR.2 — Sample-level 与光谱/偏振重建

**依赖**: HR.0、HR.1。

- 研究 sample-space splatting、kernel prediction、sample transformer/point-set 模型与混合架构；
- 使用 technique/path/spectral metadata，而非只消费 noisy RGB+normal+albedo；
- 对 Stokes 执行物理可实现性投影，对光谱执行非负/能量/观测一致性约束；
- 对 complex/Jones 数据保留 gauge/phase-reference 语义；
- 建立 OOD、置信校准、跨场景/材质/光谱域泛化和 adversarial physical fixture。

### HR.3 — Learned proposal 与 neural control variate

**依赖**: HT.1、HR.0。

- learned component 优先作为 proposal、control variate、residual 或 budget allocator；
- 若进入无偏 estimator，必须有严格 correction、独立 holdout 或 unbiased residual estimator；
- 禁止将同一训练/适配样本同时用于选择和无校正评估；
- 模型 artifact 绑定训练数据、代码、precision、backend 和 normalization identity；
- 模型缺失/OOD 时回退到已验证 estimator，不静默输出看似合理的图像。

### HR.4 — 时序、交互与远期实时增强

**依赖**: HR.1、HW.0。

- progressive 和动态世界的 history 必须绑定 snapshot/time/technique identity；
- 设计 latency-quality-memory 三维预算，而非单一 SPP；
- 研究 frame generation、reprojection、reservoir history 与 reconstruction history 的共同有效域；
- 实时目标属于远期增强，不得迫使 offline/reference 路径牺牲物理语义；
- 所有 temporal reuse 都有 reset、partial invalidate 和 confidence decay。

### HR.5 — 重建系统闭环

完成条件：raw/reconstructed/uncertainty/OOD/provenance 同时可取得；静态、动态、光谱、偏振、体积、焦散、
荧光和有界 wave 数据有独立验证；quality 提升以多指标和重复证据表达；任何 learned path 都有可用的非学习回退。

---

## 6. HW World — 统一物理世界

### 6.1 状态分层

| 层 | 权威性 | 示例 |
|---|---|---|
| Authoring State | 用户意图的权威源 | native scene、材质、初始/边界条件、solver/coupling 声明 |
| Canonical Dynamic State | 跨系统共享的物理状态 | transform、velocity、temperature、stress、density、composition、phase |
| Solver-private State | 特定离散化内部状态 | FEM DOF、MPM grid/particles、SPH neighbors、modal coefficients |
| Coupling/Transfer State | 交换、插值、残差和守恒审计 | nonmatching mesh map、interface traction、heat flux、relaxation history |
| Observation Snapshot | 渲染和传感读取的不可变视图 | shutter interval 上的 geometry/material/field state |

solver-private state 可以缓存和 checkpoint，但不能被 adapter 或 renderer 当作 authoring truth。

### HW.0 — 时间、WorldState 与 ObservationSnapshot

**依赖**: HO.1。

- 连续时间、rational tick、substep、event 和 shutter interval 使用同一时间合同；
- `WorldState` 使用稳定实体/字段 identity 和 copy-on-write/versioned storage；
- solver 写入 staging state，经校验后原子发布新的 state identity；
- renderer 只读取 immutable ObservationSnapshot，不读取正在迭代的 solver buffer；
- SceneDiff 演进为带依赖和 invalidation 的 transaction，而非只处理少数热更新；
- distributed snapshot 绑定全部资源和 solver semantics，禁止 worker 拼接不同世界时刻。

### HW.1 — Property Field、单位与本构关系

**依赖**: HW.0。

- 建立 scalar/vector/tensor/complex/spectral field，支持 mesh、particle、voxel、analytic 与 sparse domain；
- 每个 field 声明 units、frame、domain、interpolation、conservation 与 validity interval；
- 材质从静态参数扩展为 state-dependent constitutive response；
- temperature、stress、strain、density、composition、phase、electric/magnetic field 可驱动 optical/acoustic/mechanical property；
- field transfer 必须记录误差、support、边界和是否守恒。

### HW.2 — Solver 与 Coupling Graph Runtime

**依赖**: HW.1、HO.2。

- solver descriptor 声明 input/output fields、domain、rates、latency、checkpoint、determinism、adjoint capability 和资源预算；
- coupling edge 声明 direction、transfer operator、conservation、unit/frame transform、lag 与 feedback；
- 支持 explicit、implicit、staggered、quasi-Newton/relaxation 等 coupling policy；
- 支持 multi-rate subcycling、event synchronization 和 rollback/checkpoint；
- nonmatching discretization 通过可验证 transfer operator 连接，不要求共享网格；
- divergence、nonconvergence、energy growth 和 stale state 有结构化诊断。

### HW.3 — 应力—双折射—偏振渲染纵切

**依赖**: HW.2、现有 W.9 reference、HT 基线。

这是第一个优先生产纵切：mechanical load → stress tensor → stress-optic constitutive model → spectral anisotropic field →
polarized observation。它验证 tensor field、单位、frame transform、snapshot、solver-to-render coupling 和可视化诊断。

完成证据包括 analytic plate/cylinder、小规模 FEM/参考解、主轴/retardance/Stokes 比较、能量/被动性和动态 load history。

### HW.4 — 辐射—热—光学反馈纵切

**依赖**: HW.2、HT/HR measurement 基础。

transport absorption → heat source → thermal solver → temperature field → emission/IOR/extinction/fluorescence/deformation →
next observation。必须区分 steady/transient、radiometric energy 与显示值，coupling iteration 报告能量平衡和残差。

### HW.5 — 流体/介质—光输运纵切

**依赖**: HW.2、现有 volume/Mie 基础。

density/temperature/composition/particle distribution → extinction/scattering/phase/emission fields；研究 SPH、MPM、grid/particle hybrid
在同一 world contract 下的适用性。首要目标是状态/输运一致和守恒，不以通用高性能流体产品为早期门禁。

### HW.6 — 连续时间、运动、Doppler 与相位

**依赖**: HW.0、HT/HR 的 time-aware measurement。

- geometry/material/field 在 shutter interval 内采样，不再只有 frame-start transform；
- motion blur、rolling/global shutter、transient time-of-flight 与 solver time 对齐；
- 对需要的 observable 研究 Doppler、moving medium 与 phase/OPL 演化；
- temporal reconstruction 与 reservoir history 使用同一 validity interval；
- 高速/不连续事件使用 event-aware snapshot，禁止线性插值掩盖拓扑或接触变化。

### HW.7 — 统一物理世界闭环

完成条件：至少 HW.3-HW.5 三个纵切共享同一 WorldState/Field/Coupling/Snapshot 合同；一个动态场景可 checkpoint、
分布式重放、渲染、重建并审计守恒；专用 solver 可以替换而不改变 authoring truth 和 observation identity。

---

## 7. HD Differentiation — 可微、逆问题与控制

### 7.1 横切要求

可微能力不是积分器完成后再包一层自动微分。HT/HR/HW 从第一版合同开始必须保留参数 identity、随机 replay、
技术来源、状态 snapshot、离散事件和充分统计，否则后续只能得到不可解释或内存不可承受的梯度。

### HD.0 — 参数与 Replay 合同

**依赖**: HO.1、HO.2、HR.0。

- 参数覆盖 geometry、material、spectrum、medium、light/sensor、solver initial/boundary/constitutive/coupling values；
- 每个参数声明 transform、bounds、units、sparsity、regularization 与 discontinuity class；
- counter-based RNG、sample-range、technique choice、reservoir provenance、MLT mutation 与 solver event 可重放；
- path/state record 支持 bounded checkpoint、suffix cache 和 recomputation policy；
- primal、tangent、adjoint 和 finite-difference evidence 使用同一 snapshot/measurement identity。

### HD.1 — 可微光输运核心

**依赖**: HD.0、HT.1。

- 研究 path replay backpropagation、radiative backpropagation、forward/reverse/hybrid mode 的成本边界；
- 对 smooth path events 建立局部 derivative 与 technique-aware score/pathwise terms；
- MIS/GRIS/guiding/adaptive portfolio 的导数必须包含选择和权重依赖，或显式 stop-gradient 并量化偏差；
- PSSMLT/相关链的梯度使用独立研究合同，禁止套用 iid 公式；
- checkpoint/recompute 策略以显存、时间和数值稳定性共同优化。

### HD.2 — 可见性与不连续性

**依赖**: HD.1。

- 对 silhouette、occlusion、shadow、topology/contact、delta chain 和 sampling support change 分类；
- 研究 edge sampling、boundary terms、reparameterization、manifold derivative 与 distributional methods；
- 每类不连续性有 small-scene oracle 和“当前不支持”边界；
- 禁止用平滑近似无标记替代真实几何导数；若使用 surrogate，必须声明目标函数已改变。

### HD.3 — 光谱、偏振、荧光与波动梯度

**依赖**: HD.1、现有 W contracts。

- spectral sampling、dispersion、complex IOR、Mie、fluorescence wavelength transition 的 joint PDF/response 可导；
- Stokes/Mueller 梯度保留物理可实现性；不能假设所有 Mueller matrix 可逆；
- 对 rank-deficient polarization transport 研究 cached suffix、hybrid replay 或专门 adjoint，而非机械套用传统 PRB；
- Jones/complex field 导数明确 complex convention、phase reference、gauge 和 Wirtinger/real representation；
- local full-wave provider 可选择导出 adjoint/sensitivity artifact，不要求所有外部 solver 立即支持。

### HD.4 — 可微物理与耦合

**依赖**: HW.2、HD.0。

- solver 声明 tangent/adjoint/replay/checkpoint 能力和不可导 event；
- differentiable rigid/continuum/fluid/thermal solver 各自保持离散化语义；
- coupling graph 反向传播包含 transfer、relaxation、multi-rate 和 fixed-point/implicit derivative；
- contact、fracture、phase change、remeshing 等事件使用明确 estimator/surrogate/stop-gradient policy；
- 梯度审计包含物理残差、有限差分和实际优化下降，而不只比较自动微分内部一致性。

### HD.5 — 逆问题、优化与闭环控制

**依赖**: HD.2-HD.4、HR uncertainty。

- loss 可以定义在 raw measurement、reconstructed estimate、sensor domain 或 physical field；
- 若对 denoised output 优化，必须传播/冻结重建器策略并防止 hallucination 驱动错误世界参数；
- 支持 deterministic optimizer baseline、stochastic optimizer、constraints、priors 和 uncertainty-aware stopping；
- 建立材质/光源/几何/介质反演、stress/temperature inference 和简单控制问题；
- 每个任务提供 identifiability、parameter correlation 和 posterior/uncertainty 诊断。

### HD.6 — 可微闭环

完成条件：至少一个静态光输运逆问题和一个多物理动态逆问题从 native authoring 到 measurement、loss、gradient、
optimization、updated world 全链路可重放；梯度误差、优化下降、资源和失效边界有独立证据。

---

## 8. HO.C — 跨轨集成闭环

### HO.C1 — Transport + Measurement Closure

**依赖**: HT.3、HR.1。

自动 portfolio 的全部贡献进入 MeasurementBundle；raw estimate、technique strata、variance/covariance、重建结果与置信可同步输出。
系统能证明重建不会污染无偏累计器，dynamic invalidation 与 distributed merge 保持身份完整。

### HO.C2 — World + Observation Closure

**依赖**: HW.3、HW.4、HW.5、HR.1。

三个物理纵切使用同一 snapshot/measurement contract，渲染与物理传感输出共享时间和单位语义；checkpoint/farm replay
产生相同 world/observation identity，差异受确定性或统计合同解释。

### HO.C3 — Inverse World Closure

**依赖**: HD.6、HO.C1、HO.C2。

正向世界、自动积分、重建、梯度和控制形成可审计闭环。对不可辨识、OOD、非收敛和物理不可实现状态明确拒绝或报告，
不以一张优化后的图像代替系统证据。

### HO.C4 — Unified Validation and Graduation

**依赖**: HO.C1-HO.C3。

统一报告至少覆盖：

1. observable/measure/support 正确性；
2. estimator bias/variance/covariance/time-to-error；
3. raw/reconstructed uncertainty 与 OOD；
4. world state/time/unit/coupling 守恒与残差；
5. gradient 与 optimization evidence；
6. memory/throughput/scaling/device-loss；
7. distributed identity、checkpoint 与 replay；
8. capability boundary ledger 的关闭、保留和新发现项；
9. public API、C ABI、pyure/native schema 的成熟度与迁移；
10. clean-tree source/binary/artifact provenance。

只有 HO.C4 完成后，才重新评估通用插件 ABI、实时产品化和更广泛 DCC/solver ecosystem。

---

## 9. 阶段依赖与研究通道

| 阶段 | 生产依赖 | 可提前研究 | 不得提前冻结 |
|---|---|---|---|
| HO.0 | legacy closure | 文献复现、ledger 工具 | 新公共 ABI |
| HO.1 | HO.0 | descriptor/field 原型 | module layout 与 serialization version |
| HO.2 | HO.1 | capsule runner 原型 | production farm protocol |
| HT.0-HT.1 | HO.1 | 新 proposal/组合 oracle | 默认自动 scheduler |
| HR.0 | HO.1/HO.2 | sample-level feature experiments | 只面向 RGB 的稳定接口 |
| HW.0-HW.2 | HO.1/HO.2 | solver/transfer 小问题 | 单一通用 discretization |
| HD.0-HD.2 | HO.1/HT.1/HR.0 | PRB/edge/reparameterization research | “所有路径可微”声明 |
| HT.2-HT.5 | HT.1/HO.2 | adaptive portfolio alternatives | 无证据默认策略 |
| HR.1-HR.5 | HR.0 | neural/control-variate models | 模型格式与训练栈长期 ABI |
| HW.3-HW.7 | HW.2 | 各 solver provider | 未经纵切验证的插件 ABI |
| HD.3-HD.6 | HD.1/HW.2 | polarized/wave/diff-physics | 统一梯度承诺 |
| HO.C | 各轨 closure | 大场景/实时探索 | 通用生态 ABI |

研究可以跨表格进行，但每个实验必须指出它假设了哪些尚未冻结的合同；合同改变时，旧 artifact 自动降为历史证据。

---

## 10. 施工原则

### 10.1 小切片但不做仪式化红测

- 每个工程切片必须产生可独立审阅的合同或纵向能力；
- 不为尚未创建的文件/类型运行“必然找不到”的测试；
- 优先先实现最小 coherent slice，再验证实际行为；
- 物理公式、统计 estimator 和状态转换必须有 oracle 或独立推导；
- 高内存 CUDA 编译继续使用专用 job pool，宿主和独立目标保持合理并行。

### 10.2 默认路径与研究路径隔离

- Research artifact 不进入 installed public headers；
- Experimental feature 使用显式 maturity/capability，不伪装成 production enum；
- 默认路径只能消费通过 graduation gate 的 schema/algorithm；
- 研究失败不得破坏已有 renderer，可删除的临时 artifact 不成为场景权威源；
- 对影响公共数据合同的实验优先 versioned extension，而不是无版本修改。

### 10.3 文档与游标

- 本文件只维护权威路线、阶段依赖、成熟度和完成标准；
- `STATUS.md` 只描述已经实现的能力和诚实边界；
- `README.md` 提供简洁入口，不展开路线细节；
- 阶段完成文档进入 `docs/`，过时计划进入 `docs/archive/` 并标记历史身份；
- 任何“当前游标”冲突都以本文件第 0 节为准；
- 不全量读取本文件恢复上下文，必须通过索引/搜索读取游标、活动阶段和直接依赖。

---

## 11. 研究依据

这些工作是路线起点，不是不可质疑的最终答案。新论文、反例和本项目实验可以修订具体技术选择，但必须保持前述语义边界。

### 光输运、组合与可微

- Eric Veach, *Robust Monte Carlo Methods for Light Transport Simulation*: path-space、MIS、BDPT 与 MLT 的理论基础 — <https://graphics.stanford.edu/papers/veach_thesis/>
- Hua et al., *Controlled Mixture Sampling*: 基于估计质量调节 mixture，而非固定手工权重 — <https://qingqin-hua.com/publication/2023-controlled-mixture-sampling/>
- Lin et al., *Generalized Resampled Importance Sampling*: 跨 proposal domain 的重采样与可复用权重 — <https://research.nvidia.com/labs/rtr/publication/lin2022generalized/>
- Hedstrom et al., *ReSTIR BDPT*: reservoir reuse 与 bidirectional technique 的结合方向 — <https://research.nvidia.com/labs/rtr/publication/hedstrom2025restir/>
- Vicini et al., *Path Replay Backpropagation*: 常数级 tape 思路与可微 path replay — <https://dvicini.github.io/path-replay-backpropagation/>
- Li et al., *Differentiable Monte Carlo Ray Tracing through Edge Sampling*: visibility discontinuity 的边界项 — <https://cseweb.ucsd.edu/~tzli/diffrt/>
- *Differentiable Polarized Path Tracing*: rank-deficient Mueller transport 对传统 replay 假设的挑战 — <https://arxiv.org/abs/2607.13265>

### 重建与降噪

- Rousselle et al., *Sample-based Denoising for Monte Carlo Ray Tracing*: sample/statistical denoising 基础 — <https://groups.csail.mit.edu/graphics/rendernet/data/mc_denoising.pdf>
- Bako et al., *Kernel-Predicting Convolutional Networks for Denoising Monte Carlo Renderings*: learned kernel 与 auxiliary feature 路线 — <https://jannovak.info/publications/KPCN/index.html>
- Schied et al., *Spatiotemporal Variance-Guided Filtering*: progressive/temporal variance-guided baseline — <https://research.nvidia.com/sites/default/files/pubs/2017-07_Spatiotemporal-Variance-Guided-Filtering%3A/svgf_preprint.pdf>
- Müller et al., *Neural Control Variates*: 学习模型作为可校正估计器组件 — <https://research.nvidia.com/publication/2020-11_neural-control-variates>
- Zhang et al., *Ensemble Denoising*: 不确定性和多模型组合研究方向 — <https://jameszfs.github.io/publication/2021-12-01-ensemble>

### 多物理、统一状态与可微仿真

- Chourdakis et al., *preCICE v2*: partitioned multiphysics coupling、nonmatching meshes 与加速收敛 — <https://pmc.ncbi.nlm.nih.gov/articles/PMC10446068/>
- Hu et al., *A Moving Least Squares Material Point Method*: particle/grid transfer 与多材料仿真基础 — <https://yuanming.taichi.graphics/publication/2018-mlsmpm/>
- Macklin et al., *Unified Particle Physics for Real-Time Applications*: 多种现象共享约束/粒子框架的能力与边界 — <https://doi.org/10.1145/2601097.2601152>
- Hu et al., *DiffTaichi*: 可微物理模拟、checkpoint/recompute 和逆问题 — <https://arxiv.org/abs/1910.00935>
- Zhong et al., *Differentiable Contact Simulation through Smoothing*: 接触不连续的可微处理研究 — <https://arxiv.org/abs/2207.05060>
- Wang et al., *Radiative Heat Transport for Dynamic Thermal Emission*: 辐射—热—外观耦合纵切参考 — <https://onlinelibrary.wiley.com/doi/10.1111/cgf.14957>

---

## 12. 最终完成定义

本路线不会因“所有 fail-loud 消失”而完成。它在以下条件同时成立时完成：

- 用户可以描述世界、观测、质量/资源目标和可选逆问题，而不必手工选择积分器；
- 估计器组合有明确 measure/support/weight/normalization/correlation 语义；
- 重建系统消费足够的测量信息并同时输出 raw、estimate、uncertainty、OOD 和 provenance；
- 光、热、力、流体/介质等至少三个纵切共享统一 WorldState/Field/Coupling/Snapshot；
- 可微路径覆盖一个静态光输运和一个动态多物理闭环，并诚实标记不连续/不可导边界；
- research 可以快速试错，production graduation 仍有严格、相关且可复现的证据；
- 分布式、缓存、backend 和 API 不会混合不同 world/technique/measurement/solver 语义；
- 保留下来的 fail-loud 都是已分类、可解释和可追踪的真实边界，而不是旧架构偶然造成的互斥。

达到这些条件后，才进入下一轮生态化、实时化和更大尺度物理求解路线设计。
