# Phase E — N 通道光谱架构设计

> 目标：物理精确的光谱渲染引擎，运行时 N 通道，消除所有 RGB 简化和两套光谱体系矛盾。
> 本文档定义 `GpuSpectrum` 重构后的内存布局、管线契约、以及逐条矛盾的解决策略。
> 状态：Phase E 已完成。剩余条目只记录 post-E 技术债或后续 Phase K/M 的功能边界，不再表示 E.0-E.5 未完成。

---

## 1. 核心矛盾清单与决策

### 矛盾 1: `GpuSpectrum::to_rgb()` 不是 CIE 积分

| 当前 | 改正 |
|------|------|
| `Vec3(values.x, values.y, values.z)` — 纯截断 | **删除 `to_rgb()` 成员函数**。`to_rgb()` 不是光谱内核运算，是显示输出操作。 |
| | 唯一 `to_rgb` 调用点：`resolve_framebuffer_kernel` 和调试路径。改用显式 `spectrum_to_xyz()` + `xyz_to_rgb()`。 |

**决议**: `GpuSpectrum` 不再提供 `to_rgb()`。渲染管线内所有 `to_rgb()` 调用改为各自独立的正确操作。只有一个例外——显示器输出端调用 `spectrum_to_xyz() → xyz_to_rgb()`。

### 矛盾 2: `GpuSpectrum::from_rgb()` 不做光谱重建

| 当前 | 改正 |
|------|------|
| 把 RGB 装进 `.values`，`wavelengths` 置 0 | **删除 `from_rgb()` 成员函数**。RGB→光谱重建是输入层职责，不在核心类型上。 |
| | 新增 `rgb_to_spectrum(float* out_vals, const GpuVec3& rgb, const float* lambdas, int N)` 显式命名空间函数。 |

**决议**: RGB→光谱的两种重建（校准高斯 / Smits 式 / SPD fallback）在 `gpu_spectrum_utils.cuh` 集中实现。`GpuSpectrum` 不携带 RGB 转换。

### 矛盾 3: 体积透射率的 RGB 往返

| 当前 | 改正 |
|------|------|
| `sigma_t.to_rgb() → exp(-σ·d) → rgb_coeff_to_spectrum()` | **每个通道独立计算**：`tr_vals[c] = expf(-sigma_t_values[c] * d)`。 |
|  | 这是 SoA 布局天然支持的；删除 `to_rgb()` 后不再有往返动机。 |

### 矛盾 4: `SampledSpectrum<N>` 和 `GpuSpectrum` 两套体系

| 当前 | 改正 |
|------|------|
| `ure::spectral::SampledSpectrum<4>` (CPU, CIE 积分正确) | **各自保留**但共享 CIE 1931 2-degree 官方数据源。两者定义同一物理量（N 个浮点数的光谱功率分布）。 |
| `ure::gpu::GpuSpectrum` (GPU, 截断式 to_rgb) | CIE 数据源统一：CPU `cie_data.hpp` 与 GPU device table 均由 CIE 018:2019 1nm CSV 重采样到 5nm。 |

**决议**: `SampledSpectrum<N>` 用于 CPU 渲染堆栈（ure_types 中的路径追踪器/BSDF 接口），`GpuSpectrum` 用于 GPU 渲染堆栈。两者 `spectrum_to_xyz()` 使用同一 `cie_table[]` 插值 + Riemann 和归一化。

### 矛盾 5: SceneIR `spectral_bands` / `albedo_spd` 解析了但永不用于渲染

| 当前 | 改正 |
|------|------|
| glTF 前端解析 SPD 文件路径 → `gpu_scene_compiler.cpp` 丢弃并调用 `from_rgb()` | **接入 SPD 加载路径**：`compile_material_node()` 中遇到 `albedo_spd` 非空时调用 SPD loader → 填充 N 通道，不走 RGB 重建。 |

**决议**: E.4 必须实现完整的 SPD → GpuSpectrum 管线。

---

## 2. SpectralPacket 内存布局

2026-06-11 过渡实现说明：当时代码中的 `GpuSpectrum` 已从旧 4 通道扩容为 `kMaxSpectralChannels=32` 的局部桥接类型，目的是让 runtime-N 迁移期间的 material/shadow/scatter 路径在 N=8 等配置下不再越界或截断。这个类型扩容不是最终架构；最终目标仍是本节定义的 SoA/数组访问模型，避免在高 N 下复制大对象并造成 register/local memory 压力。

2026-06-14 Phase L.3 进展：代码中的旧 `GpuSpectrum` 类型已删除并更名为 `SpectralPacket`，其语义限定为 GPU ray/material 计算中的 packet-width 临时载体；packet cap 已更名为 `kMinPacketLanes`/`kMaxPacketLanes`。新增 `SpectralSample` 用于后续 single sampled wavelength / domain-bin 状态。`GpuTexture` 已从 `SpectralPacket* data` 改为显式 float carrier，不再在 texture resource API 中暴露 fixed 32-array packet；该过渡 carrier 已在 L.8 被 source-sample resource descriptor 取代。

2026-06-14 Phase L.4 进展：RayQueue 已引入初始 spectral mode，primary ray 可以按 `spectral_sampling_mode` 进入 `SpectralRayModeSampled`。`packet_lanes=1` 现在是合法 sampled-wavelength carrier，`packet_lanes=2-7` 仍拒绝；`packet_lanes>=8` 继续表示 packet quadrature。lane split 与 sampled primary ray 通过同一 active-channel / wavelength-pdf estimator 处理 direct lighting、medium proposal 和 Stokes state，避免为 hero/sample/lane 分别维护不同公式。

2026-06-14 Phase L.5 进展：材质光谱输入已新增 `SpectralResource` device descriptor 与 `HostSpectralResource` host bridge。glTF SPD 的 albedo/emission 保留原始 sampled table，RGB-derived albedo/emission/n/k/材质内 medium sigma 编译为 analytic resource，wavefront shading 按 ray 当前 wavelength resource-first 评估；packet SoA 仍保留为 cache/fallback。全局 homogeneous medium 仍是 packet field，归后续 Phase L 子步骤处理；texture resource 化已在 L.8 建立 source-sample descriptor，完整 basis/tile cache 仍是后续性能路径。

2026-06-14 Phase L.6 进展：新增 CPU-only `spectral_oracle.hpp`，默认 360-830nm / 1,000,000 bins reference integration，用现有 CIE 1931 表验证 equal-energy、D65、narrowband、metamer pair 和 high-res resource sampled estimator 偏差。这个 oracle 是后续 L.7 GPU sampled wavelength estimator 的基准；它不表示 GPU 端 million-domain estimator 已完成。

2026-06-14 Phase L.7 进展：GPU sampled estimator 已按 mode 拆分。`SpectralRayModeSampled` 使用连续波长样本和 `1 / (830-360)` PDF density，XYZ estimator 为 `value * CIE(lambda) / pdf_density / cie_y_integral`；`SpectralRayModeLane` 仍使用离散 lane probability 与 packet bin width，保持 deterministic lane split 对 packet quadrature 的等价性。`gpu_test_spectral` 现在用 L.6 CPU oracle 对照 equal-energy、D65 和 high-res D65+narrowband resource，验证 sampled path 不展开 1M lane 也能收敛到 reference。

2026-06-14 Phase L.8 进展：explicit spectral texture 已从 packet-width resident buffer 改为 source-sample resource descriptor。`GpuTexture` 保存 source sample table、sample count 和 visible-domain lambda range；`sample_texture()` 先做 UV 双线性，再按当前 ray wavelength 在 source samples 上插值。RGB texture 不再同时上传 packet-width spectral buffer，只保留 CUDA `texObj` 并在 kernel 端按 wavelength 重建。L.8 解决 texel × packet/domain 展开问题；basis/tile cache、cache miss 诊断和分布式资源分片仍归后续 Phase L 子步骤。

2026-06-15 Phase L.9 进展：MaterialGraph value 节点已从 packet-only flatten 迁移到 spectral expression graph。Texture2D、Add、Multiply、Mix 会编译为材质局部 expression nodes，GPU shading 按当前 ray wavelength/UV 动态求值；albedo、roughness、emission 均可通过 expression root 覆盖 packet cache。`URE_spectral_material` SPD override 仍是 authoritative，存在 SPD 时清除对应 graph root。metal eta/k 与 dielectric ior 的 texture/expression 输入仍需要专用 IOR/scalar expression slot，不在 L.9 内伪装支持。

2026-06-15 Phase L.10 进展：分布式契约已新增 spectral-domain/frame shard metadata。`DistributedSampleRange` 与 `DistributedFrameBuffer` 都携带 domain bins、domain start/count、lambda range、wavelength PDF integral 和 frame index/count；file backend 升级到 v2 并序列化同一 metadata。`merge_partial_framebuffer()` 允许不同 spectral shard 合并到 aggregate accumulator，但拒绝 domain/lambda/frame 不兼容的输入。这解决的是 farm contract 和文件交换层，不等于完整 scheduler/resource cache；worker 调度、basis/tile cache 和 cache preset 仍归 Phase L.11。

2026-06-15 Phase L.11 进展：`SpectralRuntimePlan` 已输出低端/桌面/高端/farm 的 sampler preset、cache preset 和 CUDA stream preset，并估算 material packet cache、sampled resource table 与 explicit spectral texture source grid 的 resident bytes。`spectral_max_resident_mb` 现在是 GPU init 前的硬预算门禁；显式超预算会 fail-loud，而不是试图分配后崩溃或假装已有 streaming fallback。新增 `phase_l_spectral_budget.gltf` 和 benchmark smoke 脚本，已跑通 1M-domain sampled HDR smoke。完整 basis/tile cache runtime、resource prefetch 和系统化 perf suite 仍是后续性能路径。

### 2.1 约束条件

- GPU 内核中寄存器能容纳 N=16（每像素 16 floats = 64 bytes，含波长 128 bytes）。N=64 时需 SoA 或 shared memory 中转。
- `GpuMaterial` 含 6 个光谱场（albedo, metal_eta, extinction, medium_scattering, medium_absorption, emission）。若每个材质存 6×N 浮点在全局内存，cache 效率随 N 降低。
- 但随机材质访问（由 BVH 遍历决定）使 SoA 分离与 AoS 连续布局各有优劣：SoA 让同一光谱通道的材质连续，AoS 让同一材质的所有通道连续。光线在场景中随机命中不同材质 → SoA 跨材质访问多个通道时 cache 行利用率更高（同一通道的所有材质在独立数组中）。
- 最终决定：**SoA 分离**——材质光谱场与标量场分离。

### 2.2 `GpuMaterial` 拆分

```cpp
// --- 全局内存中的布局 ---

// 标量头 (固定 48 字节，无对齐填充)
struct alignas(16) GpuMaterialHeader {
    MaterialType type;          // 4B
    int texture_index;          // 4B
    int roughness_texture_index;// 4B
    int emission_texture_index; // 4B
    float roughness;            // 4B
    float ior;                  // 4B
    float dispersion;           // 4B
    float thin_film_thickness;  // 4B
    float thin_film_ior;        // 4B
    float medium_density;       // 4B
    float medium_anisotropy;    // 4B
    float _pad0;                // 4B (reserved)
};

// 光谱体 (SoA, 通道主序)
// d_mat_albedo:    float*    [N × mat_count]   连续: mat0_c0, mat0_c1, ..., mat(N-1)_c0, ...
// d_mat_metal_eta: float*    [N × mat_count]
// d_mat_extinction: float*   [N × mat_count]
// d_mat_medium_scattering: float* [N × mat_count]
// d_mat_medium_absorption: float* [N × mat_count]
// d_mat_emission:  float*    [N × mat_count]

// --- 设备端访问器 ---

// 读取材质 albedo 的第 c 通道
float albedo_c = d_mat_albedo[mat_idx * num_spec + c];

// 读取 GpuMaterial 标量头（保持随机访问）
GpuMaterialHeader header = d_material_headers[mat_idx];
```

**理由**:
- SoA → 同一通道的所有材质值在内存中连续 → 光线命中不同材质时，所有光线对同一通道的访问命中同一 cache 行
- 分离标量头 → 材质无关光谱通道数的添加/删除不影响标量字段的内存布局
- 兼容运行时 N：`d_mat_albedo` 分配 `N * mat_count * sizeof(float)`

### 2.3 内核局部操作（寄存器）

对每个像素/光线在内核中处理光谱值时，不创建 `GpuSpectrum` 对象，而是用循环+N 维局部数组：

```cpp
// 计算体积透射率 —— 无 RGB 往返
float tr[16]; // N_max=16, 放寄存器或 local memory
for (int c = 0; c < num_spec; ++c) {
    tr[c] = expf(-sigma_t_per_channel[c] * t_hit);
}

// 合并 throughput
for (int c = 0; c < num_spec; ++c) {
    throughput[c] *= tr[c];
}
```

**N > 16 的处理**：Phase E 的 `N` 是 GPU path packet 宽度，不是全局光谱资源域分辨率。当前完成态把 packet quadrature lanes 约束在 `[8, 32]`，并在 Phase L.4 允许 `packet_lanes=1` 作为 single sampled wavelength 模式；`2-7` 仍拒绝，避免未经验证的半宽 packet。Phase L 将 `domain_bins` 与 `packet_lanes` 解耦，百万级 SPD/纹理/材质资源不能通过扩大 `SpectralPacket` 或 `num_wavelengths` 来实现。

### 2.4 已 SoA 的队列

| 队列 | 当前 | 状态 |
|------|------|------|
| `RayQueue::throughput_vals` | `float*` (ch×capacity) | ✅ 无需改动，只需移除 `#pragma unroll 4` |
| `RayQueue::throughput_wavelengths` | `float*` (ch×capacity) | ✅ 同上 |
| `ShadowQueue::radiance` | `GpuSpectrum*` (AoS) | ❌ **改为 `float* radiance_vals` (ch×capacity)** |

### 2.5 纹理（特殊处理）

Phase E 的纹理路径最初存储经 RGB 重建或 SPD 加载后的 packet-width spectrum。Phase L.8 后，RGB texture 与 explicit spectral texture 分开：RGB 输入保留 CUDA texture object；explicit spectral texture 作为 source-sample resource descriptor 驻留，source sample count 不再要求等于 packet lanes 或 domain bins。理由：

- `tex2D<float4>` 硬件只支持 1-4 通道，不支持 N>4
- `sample_texture()` 纹理采样用于材质属性（漫反射 albedo、粗糙度贴图），是连贯内存访问（相邻像素 → 相邻 texels），SoA 没有优势
- 对于 explicit spectral texture，当前实现使用 `GpuTexture::spectral_source_values` 存储 texel-major `pixel * source_sample_count + sample` float table，`sample_texture()` 手写 UV 双线性和 wavelength 插值；这已经切断 texel × packet/domain 展开，但还不是完整 sparse/tiled cache 系统。

**决议**:
- RGB 输入：`HostTexture::channels == 3`，上传为 `cudaArray<float4>` `texObj`；kernel 端用硬件 filtering 后按当前 wavelengths/runtime packet 重建。
- Explicit spectral 输入：`HostTexture::channels != 3`，`channels` 表示 source spectral sample count，上传到 `GpuTexture::spectral_source_values`，不创建 `texObj`；kernel 端按当前 ray wavelengths 对 source samples 插值。
- 非 3 通道不再要求等于 `spectral_packet_lanes(config)`；这正是 L.8 解耦 texture resource 与 packet width 的核心。

---

## 3. 管线契约

### 3.1 数据类型命名

| 名称 | 位置 | 用途 |
|------|------|------|
| `gpu::SampledSpectrum` | `ure_core` (gpu_spectrum.cuh) | GPU 内核中固定栈数组 `float vals[N_max]` |
| `spectral::SampledSpectrum<N>` | `ure_types` | CPU 渲染堆栈 |
| `gpu::CIE` | `ure_core` (cie_table.cuh) | GPU 上的 CIE 1931 table + 插值 |

**注意**: `SampledSpectrum`（GPU 版）不是类，是一个概念（带 N 的 float 数组）。不再存在 `float4 values; float4 wavelengths` 对象。

### 3.2 各阶段 N 的传递

```
RenderConfig::spectral_packet_lanes  ← CLI/JSON/auto_config
RenderConfig::num_wavelengths        ← legacy alias during Phase L migration
    │
    ▼
init_gpu_renderer(config)
    │
    ├── alloc_ray_queue(..., num_spec)  ← 已做
    ├── alloc_shadow_queue_radiance(..., num_spec)  ← 新增
    │
    ▼
render_pass_gpu(num_spec)
    │
    └── GpuScene::num_spec   ← 传递给每个 kernel
         │
         ├── generate_rays_kernel(num_spec)
         ├── shade_kernel(num_spec)
         └── extend_shadow_kernel(num_spec)
```

`num_spec` 作为内核参数传递（非模板参数，真实的运行时变量）。所有内核用 `for (int c = 0; c < num_spec; ++c)` 循环访问光谱数据。

### 3.3 编译路径

- `GpuSpectrum`（旧 `float4` 类型）完全删除
- `kNumWavelengths = 4` 常量删除，替换为 `GpuScene::num_spec`（运行时 int）
- `render_frame_gpu`（旧 API）删除或强制改用 `RenderConfig`
- 编译器用 CUDA 20 标准 + `-maxrregcount` 控制寄存器压力

---

## 4. 逐条修正对照（🔴 物理错误 → 解决）

| # | 问题 | 解决 | 影响文件 |
|---|------|------|---------|
| 1 | `to_rgb()` 截断前 3 通道 | 删除 `to_rgb()`；显示输出走 `spectrum_to_xyz()`→`xyz_to_rgb()` | `gpu_structs.hpp`, `wavefront.cuh`, `host_api.cu` |
| 2 | `from_rgb()` 不做光谱重建 | 删除 `from_rgb()`；host 输入走 `rgb_to_spectrum()` 显式函数 | `gpu_structs.hpp`, `compiler.cpp`, `loader.cpp` |
| 3 | 体积透射 RGB 往返 | 每个通道独立 `exp(-σ[c] · d)` | `wavefront.cuh` |
| 4 | 消光检查 | 保留单通道独立检查；删除 `.w` 通道引用 | `wavefront.cuh` |
| 5 | thin-film 硬编码 3 波长 | 遍历 N 通道，统一调用 dielectric/conductor Airy boundary evaluator | `boundary.cuh`, `material.cu` |
| 6 | 色散通道索引 R/G/B | 遍历 N 通道，`channel` 索引直接用 `c` | `material.cu` |
| 7 | `dispersion_clamp` 伪参数 | 实现或删除参数 | `material.cu:13,223` |
| 8 | Stokes 标量非光谱 | 设计决策：每波长一个 Stokes（`float I[N], Q[N], U[N], V[N]`）或标量近似 + 文档 | `polarization.cuh` |

## 4.1 2026-06-11 光学物理并行审查结论

本轮审查确认，E.5 不能以“遍历 N 通道但共用旧 packet 几何/偏振状态”作为完成标准。N-channel layout 正确不等于光学估计器正确。

已修复并加测试的偏置:

| 问题 | 修正 | 测试 |
|------|------|------|
| SceneIR RGB fallback 和 legacy `GpuMaterialData` upload 把 RGB triple 写进 spectral slots，N>3 时波长错位 | `GpuSceneCompiler` 在 compile 阶段按 runtime-N bin center 重建 spectrum；host upload 对旧 RGB slot 形态执行同样的 runtime-N 重建；SPD/显式 spectral 数据保持原样 | `test_rgb_fallback_compiles_to_runtime_n_spectrum`, `test_runtime_n_upload_remaps_legacy_rgb_material` |
| Metal scatter 使用平均 `k` 和 scalar Fresnel，和 `eval_bsdf()` 的 per-channel conductor Fresnel 不一致 | scatter attenuation 改为 per-channel boundary evaluator；packet 输出 Stokes 按 channel 应用 conductor/thin-film Mueller；scatter 方向采样仍为 packet 共享样本 | `test_metal_scatter_uses_per_channel_conductor_fresnel`, `test_mueller_conductor_uses_boundary`, `test_mueller_thin_film_uses_boundary`, `test_packet_metal_stokes_are_channel_major` |
| 长波/红色 emission 只在后部 spectral bins 有能量时不会进入 light list | light-list 构建遍历 `RenderConfig::num_wavelengths` 全部通道 | `test_runtime_n_long_wavelength_light_list` |
| Volume no-scatter 分支用平均 transmittance 当 proposal probability | no-scatter 权重改为 `exp(-sigma_t[c] * t_hit) / exp(-sigma_t_avg * t_hit)` | `test_no_scatter_proposal_weight` |

### E.5 架构决策: hybrid packet + spectral lane split

E.5 最终方案不采用“packet 内选一个 hero wavelength 决定整包方向”的近似。该近似可作为过渡测试代码存在，但不能作为完成状态。

**决策**:

1. **非方向相关或弱波长相关事件保持 packet**
   Lambertian、rough metal、volume absorption/scattering、纯 attenuation、非色散 dielectric 等路径仍使用 N-channel packet，提高吞吐。

2. **波长决定几何方向的 delta 事件必须 split 成 spectral lanes**
   对 dispersion dielectric、临界角附近 TIR、未来 prism/caustic 路径，`shade_kernel` 不能给所有 wavelength 共用一个折射方向。遇到这类事件时，将一个 packet ray 拆成 per-channel lane ray。每个 lane 只携带一个 active wavelength/channel，方向、Fresnel、eta Jacobian、Mueller 都按该 channel 计算。

3. **specular dielectric 可确定性 split reflection/transmission**
   对 delta interface，不再用单次随机反射/透射决定整包。对每个 active channel 生成 reflected lane（权重 `R_c`）和 transmitted lane（权重 `T_c`，TIR 时为 0）。这会在一个事件产生最多 `2N` 个 child rays，但只在色散/临界角/薄膜强相关路径启用。队列容量不足必须作为诊断错误处理，不能静默丢 ray。

4. **Stokes 状态随 spectral lane/packet 一起 SoA 化**
   `RayQueue::stokes` 的 scalar `StokesVector*` 不是最终状态。E.5 需要改为 `stokes_i/q/u/v` 的 channel-major SoA。packet 模式下每个 channel 有独立 Stokes；lane 模式下只读写 active channel。scalar Stokes 只能作为过渡兼容层。

5. **Fresnel/thin-film/shadow 不能伪造 specular connection**
   `scatter()`、NEE visibility 不能各自实现一套 Schlick/Fresnel。E.5 新增 device boundary evaluator，`DielectricSurfaceBoundary` 返回每 channel 的 `R_s/R_p/T_s/T_p`、phase、eta Jacobian、transport scale、Mueller 所需复振幅。thin-film 走 complex Airy s/p，不再用 albedo 伪 interface coefficient。Shadow ray 遇到 specular dielectric 时不能直线透射；没有 specular manifold / refractive shadow path 前，NEE 应视为 blocked，由 BSDF 路径承担玻璃/焦散贡献。

6. **wavelength PDF 和 XYZ 积分权重显式进入路径状态**
   deterministic split lanes 的 `wavelength_pdf` 为当前 packet bin 的采样概率；未来 hero/importance wavelength sampling 也必须写入同一字段。`spectrum_to_xyz`、RR 和 spectral MIS 不再隐含“均匀 bin 中心且 pdf=常数”。

### E.5 可执行迁移顺序

| Step | 目标 | 代码范围 | 完成证据 |
|------|------|----------|----------|
| E.5.1 | Path spectral state: `RayQueue` 增加 `spectral_mode`, `active_channel`, `wavelength_pdf`，并将 Stokes 改为 `stokes_i/q/u/v` SoA | `gpu_structs.hpp`, `path_tracer_decl.cuh`, `path_tracer_raygen.cu`, `path_tracer_wavefront.cuh`, `path_tracer_host_api.cu` | N=8 packet roundtrip；lane mode 单通道 roundtrip；旧 scalar Stokes 无剩余 device 依赖 |
| E.5.2 | Spectral boundary evaluator: 统一 dielectric/conductor/thin-film 的 per-channel Fresnel、complex amplitude、Mueller 输入 | `path_tracer_polarization.cuh`, 新 `path_tracer_boundary.cuh`, `path_tracer_material.cu` | analytic normal-incidence conductor/dielectric；Brewster angle；TIR phase；thin-film Airy benchmark |
| E.5.3 | Dispersive dielectric lane split: 色散/临界角 delta interface 生成 per-channel reflected/transmitted lanes | `path_tracer_material.cu`, `path_tracer_wavefront.cuh`, queue append helpers | 两波长临界角测试：一个 channel 反射、另一个透射；N=8 split count <= 2N 且无静默 overflow |
| E.5.4 | Transport mode/Jacobian: 删除 dielectric transmission clamp，明确 radiance transport 下 `eta^2` 或 inverse transport 权重 | `path_tracer_material.cu`, boundary evaluator | 完成：eta scale 已进入 `DielectricSurfaceBoundary`，scatter/lane split 通过 `select_boundary_transport_scale()` 消费同一协议；slab scale 互易性、unpolarized transmission transport weight、两次真实 `scatter()` slab path 测试已补；bare dielectric / dielectric thin-film / TIR surface 的 `R+T=1` boundary furnace 门禁已补 |
| E.5.5 | Transparent shadow/NEE policy：不走 scalar Schlick，也不直线穿过 specular dielectric | `path_tracer_wavefront.cuh`, shadow queue | specular dielectric shadow blocker test；未来 specular manifold / refractive shadow path 设计 |
| E.5.6 | Spectral MIS/RR/XYZ: path state 使用 explicit wavelength PDF；RR importance 使用非负 spectral/Y 函数，不用 display RGB max | `gpu_spectrum_utils.cuh`, `path_tracer_wavefront.cuh`, resolve/accum path | 完成：spectral RR、官方 CIE 1931 2-degree table、N=32 white RGB roundtrip、equal-energy E XYZ/chromaticity、D65 SPD whitepoint、sampled/packet PDF 等价均已补；lane contribution 使用 explicit `bin_width / wavelength_pdf` estimator |

**完成判据**: E.5 只有在上述 6 个 step 全部通过 Release build、17/17 CTest、warning scan、diff check，并且 `rg` 确认无 scalar Schlick transparent shadow、无 dielectric transmission clamp、无 packet-wide dispersive refraction 后，才能标记完成。

### E.5 finite closure plan

E.5 不再继续拆成开放式小阶段。2026-06-12 起剩余工作固定为 6 个 closure steps，并已全部收束；后续新问题必须记录为 post-E 技术债或 Phase K/M 边界，只有推翻 hybrid packet + spectral lane split 架构的 P0 物理错误才允许修改路线图。

| Closure step | Scope | Exit condition |
|--------------|-------|----------------|
| C1 Medium / IOR transition closure | lane split 后的 medium state、enter/exit IOR、TIR 和普通非色散 dielectric path | ✅ `next_dielectric_medium_index()` 统一 ordinary scatter path 与 lane split；GPU tests 覆盖 enter/exit/nested/reflection state machine 和真实 lane split medium output；无 straight-through refractive shadow |
| C2 Optical material semantics closure | measured n/k、dielectric eta、thin-film、baseColor/F0 fallback 的统一材质语义 | ✅ `ConductorMaterialSemantics` 统一 BSDF/scatter/Stokes 判定；nonzero k 启用 measured conductor，zero-k 使用 albedo/F0 fallback；GPU + compiler tests 覆盖 |
| C3 Transport / reciprocity / white furnace closure | power、radiance transport、importance transport、eta Jacobian、furnace 行为 | ✅ boundary furnace `R+T=1` 覆盖 bare dielectric、dielectric thin-film、TIR 与 reverse interface；radiance scale 方向锁定为 `(eta_i / eta_t)^2`，air→glass attenuation、glass→air inverse；slab reciprocity 覆盖两次真实 `scatter()` 后 eta scale 相消；`gpu_test_polarization` 126/0 |
| C4 Spectral sampling / MIS / photometry closure | wavelength PDF、spectral MIS、D65/equal-energy/narrowband photometry 和 RR | ✅ `sampled_spectrum_to_xyz()` 将 lane estimator 固定为 `value * bin_width / wavelength_pdf`；ShadowQueue direct lighting carries spectral mode / active channel / wavelength pdf；deterministic split lane throughput 预乘 pdf，与 packet quadrature 等价；D65/equal-energy/RR/PDF tests 通过 |
| C5 Transitional API removal / static audit closure | 固定 4 通道、`.values.x/y/z/w`、scalar Stokes、过渡 helper/API 的最终审计 | ✅ code search 对固定 4 通道 API、RGB roundtrip API、旧 thin-film helper 和 dielectric clamp 清零；legacy RGB 示例材质只保留显式 `rgb_to_approximate_spd()` |
| C6 Final E.5 audit and documentation closure | 最终工程门禁、风险记录和状态更新 | ✅ Release build、17/17 CTest、warning/error scan、diff check、搜索门禁全绿；E.5 文档和 PLAN 已更新为完成 |

测试节奏：closure step 内以 targeted tests 为主，step 结束必须跑该 step 的相关测试和过滤日志；完整 Release/CTest 只在 C6 最终审计执行。

2026-06-11 E.5.1 进展：`RayQueue` 已新增 `spectral_modes`、`active_channels`、`wavelength_pdfs`，并将队列偏振状态从 scalar `StokesVector*` 迁移为 channel-major `stokes_i/q/u/v` SoA。`generate_rays_kernel` 初始化 packet mode、`active_channel=-1` 和均匀 wavelength PDF；`shade_kernel` 与 volume scatter propagation 会保留 spectral state。新增 `test_lane_spectral_state_roundtrip`，并扩展 `test_raygen_runtime_wavelength_count` 检查 packet state 和每通道 Stokes 初始化。该条为历史进展记录；后续 E.5.3 已完成 dispersive lane child ray 生成。

2026-06-12 E.5.1 进展：packet scatter 不再把 `active_channel=0` 的 Stokes 当作整包偏振状态复制。`load_packet_average_stokes()` 以 channel-average Stokes 作为 packet-level sampling 输入；`store_packet_scattered_stokes()` 在输出时按 channel 重新应用 metal/dielectric boundary Mueller，Lambertian/cloth 仍退偏振。新增 `test_packet_average_stokes_for_packet_sampling` 和 `test_packet_metal_stokes_are_channel_major`，覆盖 packet 输入平均和 metal packet 输出通道分化。当前仍不是完整 spectral-polarization 完成态：packet 仍共享单次方向样本，完整 per-channel sampling/MIS 和 white-furnace 仍归 E.5.6。

2026-06-11 E.5.2 进展：新增 `path_tracer_boundary.cuh`，集中实现 dielectric boundary、conductor complex reflection、nonabsorbing dielectric thin-film Airy boundary evaluator 和 real-film-on-complex-conductor Airy boundary evaluator。`scatter()` 的 dielectric per-channel R/T、hero-channel Mueller amplitudes、metal per-channel conductor Fresnel、metal thin-film Fresnel，`eval_bsdf()` 的 metal Fresnel，以及 `extend_shadow_kernel()` 的 transparent visibility 已接入该 evaluator；transparent shadow 不再使用 scalar Schlick。旧 `conductor_fresnel_reflectance()`、`get_dielectric_thin_film_reflectance()` 和 `get_thin_film_interference()` 已删除。新增/扩展 GPU 测试覆盖 normal-incidence dielectric/conductor、Brewster angle、TIR、quarter-wave thin-film、conductor thin-film 零厚度等价、conductor/thin-film Mueller-boundary 一致性、N=8 metal BSDF/scatter、runtime-N transparent shadow 调用面。该条为历史进展记录；transmission Mueller、transport mode、Stokes channel-major 输出和 material semantics 已在后续 C2/C3/C4 收束。

2026-06-11 E.5.3 进展：`shade_kernel` 现在会在旧 `scatter()` 前拦截 packet-mode dispersive/thin-film dielectric，并 deterministic split 为 per-channel reflected/transmitted spectral lanes。每个输出 lane 设置 `SpectralRayModeLane`、对应 `active_channel`，throughput 只保留该 channel，Stokes 只写 active channel，避免一个 hero wavelength 决定整包方向。新增 `test_dispersive_dielectric_splits_packet_to_lanes`，验证 4-channel packet 生成 8 条 lane 且每个 channel 正好一反一透；新增 `test_dispersive_dielectric_critical_angle_splits_n8`，验证 N=8 packet 在临界角附近同时存在 TIR-only channel 和 reflected+transmitted channel。`RayQueue` 新增 `overflow_count`，`test_ray_queue_overflow_count_visible` 覆盖 capacity=2 时第 3 次 reserve 会钳住 count 并递增 overflow。该条为历史进展记录；medium transition 专项测试已在 C1 补齐。

2026-06-11 E.5.4 进展：旧 `scatter()` dielectric transmission 的 `radiance_scale > 1.5` 任意 clamp 已删除。早期测试只验证“不再 clamp”，后来被审查确认方向不完整；最终协议见下一条，radiance transport 使用 `(eta_i / eta_t)^2`，air→glass 应衰减，glass→air 为 inverse scale。

2026-06-12 E.5.4 进展：`eval_boundary_transport_scale()` 已成为 dielectric transmission eta 权重的唯一 helper，并被封装进 `DielectricSurfaceBoundary`；`scatter()` 和 dispersive spectral lane split 不再各自手写 eta²，而是通过 `select_boundary_transport_scale()` 选择 radiance/importance 权重。新增 `eval_unpolarized_transmission_transport_weight()` 和 `test_boundary_transport_weight`，明确区分 power T、radiance-weighted T、importance-weighted T；`test_boundary_transport_scale` 覆盖 air→glass radiance attenuation、glass→air inverse scale、slab 往返 scale=1，以及 radiance/importance scale 互逆。新增 `test_dielectric_slab_scatter_transport_reciprocity`，通过两次真实 `scatter()` 穿过 air→glass→air slab，要求进入和出射界面的 eta scale 在 path weight 上相消。

2026-06-12 E.5.6 进展：`test_white_roundtrip` 从只检查 4-channel RGB 为正，扩展为对 N=32 runtime wavelength bin 的 RGB→spectrum→XYZ→sRGB 白点近中性断言。当前 4-channel roundtrip 只保留 smoke test，不作为白点正确性证据；低 N 下白点偏差是采样分辨率风险，不允许用来证明 E.5.6 完成。随后审计发现现有 5nm CIE Y/Z 表在长波段错误衰减，GPU 端还使用 Wyman Gaussian 近似；两者已统一为 CIE 018:2019 官方 1nm `CIE_xyz_1931_2deg.csv` 重采样 5nm 表，CPU/GPU normalization 改为从 CIE Y 积分派生。新增 `test_equal_energy_xyz_normalization`，要求 N=32 equal-energy E 输出 XYZ≈1 且 chromaticity≈1/3。后续 C4 已补 D65 SPD whitepoint 与 explicit wavelength PDF estimator。

2026-06-11 E.5.5 进展：曾新增 `test_sq_extend_transparent_spectral_visibility`，构造 ShadowQueue 穿过一个色散 dielectric sphere 的 visibility 路径，并用 device-side per-channel boundary evaluator reference 比较最终 RGB。2026-06-12 审计后确认该 straight-through shadow 会在 NEE 中伪造 specular refractive connection，因此不再作为完成方向保留。

2026-06-12 E.5.5 policy 修正：`extend_shadow_kernel` 遇到 specular dielectric 时直接阻断，不再按直线方向乘 Fresnel transmission。新增 `test_sq_extend_specular_dielectric_blocks` 和 `test_sq_extend_off_axis_specular_dielectric_blocks` 覆盖正入射与离轴 glass blocker。后续若要恢复玻璃后的直接光，必须实现 specular manifold / refractive shadow path，并显式携带目标端点、折射事件和 spectral lane 状态；不能在现有 ShadowQueue 上用局部折射方向修补。

2026-06-12 C1 收束：dielectric medium transition 现在由 `next_dielectric_medium_index()` 统一处理，ordinary scatter path 和 dispersive lane split 不再各自维护 enter/exit/nested replacement 状态机。`test_dielectric_medium_transition_helper` 覆盖 air→material、material→air、nested replacement 与 reflection no-crossing；`test_lane_split_medium_transition` 通过真实 `split_dispersive_dielectric_lanes()` 验证 reflected lane 保持原 medium，transmitted lane 进入 dielectric material medium。针对性验证为 `gpu_test_spectral_soa` 519/0，构建日志 warning/error scan 为空。该 closure 不改变 NEE policy：specular dielectric shadow 仍 blocked，未来玻璃直接光只能通过 specular manifold / refractive shadow path 恢复。

2026-06-12 C2 收束：conductor material semantics 现在由 `ConductorMaterialSemantics` 单点定义并被 `eval_bsdf()`、`scatter()`、packet Stokes 写回复用。规则为：`extinction/k` 非零才表示 measured conductor；measured conductor 优先使用 spectral `metal_eta`，若 eta 未提供则使用 material scalar `ior`；`k=0` 时必走 albedo/F0 fallback，避免 `metal_eta` 单独非零误触发 measured conductor。`test_conductor_material_semantics` 覆盖 fallback、measured-with-scalar-eta、measured-with-spectral-eta；`test_metal_coefficients_compile_as_physical_carriers` 覆盖 compiler 对 n/k coefficient carrier 和 zero-k fallback metal 的上传语义。针对性验证为 `gpu_test_spectral_soa` 534/0、`test_gltf_frontend` 185/0，构建日志 warning/error scan 为空。

2026-06-12 C3 收束：dielectric transport closure 现在由 boundary evaluator 的单一结果驱动，power probability、radiance eta scale、importance eta scale 不再混用。新增 `test_dielectric_surface_power_conservation`，覆盖 normal/oblique bare dielectric、reverse interface、normal/oblique dielectric thin-film 和 TIR 的 unpolarized `R+T=1`；既有 slab 测试继续覆盖两次真实 `scatter()` 穿 air→glass→air 后 eta scale 在 path weight 上相消。后续 blocker pass 纠正 radiance eta scale 方向为 `(eta_i / eta_t)^2`，并增加 air→glass attenuation / glass→air inverse 断言。针对性验证为 `gpu_test_polarization` 126/0，构建日志 warning/error scan 为空。

2026-06-12 C4 收束：wavelength PDF 现在进入实际 contribution estimator。`sampled_spectrum_to_xyz()` 对 active lane 使用 `value * bin_width / wavelength_pdf / CIE_Y_integral`，packet mode 则回退到完整 quadrature；packet→lane deterministic split 在写 active lane throughput 时预乘当前 pdf，因此所有 lane 求和与 packet quadrature 等价，不会多算 N 倍。新增 `test_sampled_spectrum_xyz_pdf_equivalence` 覆盖 sampled lanes 与 packet XYZ 一致，`test_lane_split_medium_transition` 同时检查 lane pdf carrier 与单通道 throughput carrier，`test_d65_spd_whitepoint` 使用 CIE D65 10nm 表验证 N=32 SPD 的 xy≈(0.3127,0.3290) 且线性 sRGB 近中性。后续 blocker pass 将 ShadowQueue direct lighting 的 spectral mode、active channel 和 wavelength pdf 也纳入 estimator，避免 lane direct-light 漏除 pdf。针对性验证为 `gpu_test_spectral` 569/0、`gpu_test_spectral_soa` 633/0、`gpu_test_render` 281/0，构建日志 warning/error scan 为空。

2026-06-12 C5 收束：静态审计对代码路径执行 `rg` 搜索，`kNumWavelengths`、`.values.x/y/z/w`、`.wavelengths.x/y/z/w`、`.to_rgb()`、`from_rgb(`、`spd_from_rgb(`、`get_thin_film_interference`、`dielectric_thin_film_reflectance` 和旧 dielectric clamp 均无命中。CPU `SampledSpectrum::from_rgb()` / `to_rgb()` 已删除；legacy scene factory 的演示材质保留 RGB 输入桥接，但名称改为 `rgb_to_approximate_spd()`，明确它是输入近似 SPD 构造，不是核心 RGB roundtrip。针对性验证为 `test_asset_pipeline` 48/0、`test_gltf_frontend` 185/0、`gpu_test_spectral` 569/0，构建日志 warning/error scan 为空。

2026-06-12 C6 收束：完整 Release build 通过，`build_modular/last_batch1_2_full_build.log` warning/error scan 为空；`ctest --test-dir build_modular -C Release --output-on-failure` 为 17/17 通过；`git diff --check` 通过；代码路径搜索门禁对固定 4 通道 API、RGB roundtrip API、旧 thin-film helper 和 dielectric clamp 均无命中。E.5 当前状态为完成。specular manifold / refractive shadow path、rough dielectric microfacet BTDF、RGB/photometry fallback 精度和 volume spectral proposal 方差是后续功能边界，不再作为 E.5 内用 straight-through shadow 或局部公式修补。

---

## 5. 执行顺序（修正版）

```
Phase E.0 — 前置测试
    OT5: 光谱管道 N≠4 测试 (test_spectral_pipeline.cu 扩展)
    OT6: rgb_coeff_to_spectrum + emission_to_spectrum 测试

Phase E.1 — 核心类型重构  ★ 最关键
    1.1 gpu_structs.hpp: 删除 GpuSpectrum float4 类型，改为 load_throughput/store_throughput 为中心
    1.2 GpuMaterial 拆分为 header SoA
    1.3 ShadowQueue radiance → SoA float*
    1.4 删除 kNumWavelengths 常量

Phase E.2 — 物理精确转换
    2.1 spectrum_to_xyz: 4 点 Riemann → N 点 + 归一化 / N
    2.2 删除 to_rgb() 所有调用，替换为 spectrum_to_xyz→xyz_to_rgb
    2.3 删除 from_rgb() 所有调用，替换为显式 rgb_to_spectrum()
    2.4 体积透射率 RGB 往返 → 逐通道计算
    2.5 thin-film + 色散 → 逐通道计算

Phase E.3 — 运行时 N
    3.1 render_frame_gpu 删除/改用 RenderConfig  ✅ 2026-06-11 已删除旧 API 声明和实现
    3.2 #pragma unroll 4 → 运行时 loop
    3.3 generate_rays_kernel float4 波长 → N 通道  ✅ 2026-06-11 raygen 直接写 SoA 队列，N=8 测试覆盖
    3.4 RGB→光谱工具函数从 GpuSpectrum 返回值迁移到 float* + N  ✅ 2026-06-11 数组版接口与 N=8 测试覆盖
    3.5 material SoA / scatter / ShadowQueue runtime-N  ✅ 2026-06-11 N=8 `gpu_test_spectral_soa` 覆盖
    3.6 BSDF / sample_texture 调用面迁移到 pointer+N  ✅ 2026-06-11 N=8 `gpu_test_spectral_soa` 覆盖
    3.7 纹理上传适配 N 通道  ✅ 2026-06-11 HostTexture/GpuTexture channels + spectral data path，N=8 `gpu_test_spectral_soa` 覆盖

Phase E.4 — SPD 输入
    4.1 glTF albedoSPD/emissionSPD → resolve relative to glTF directory ✅ 2026-06-11
    4.2 SceneIR spectral extension → compiler-side SPD resample at runtime-N spectral bin centers → GpuMaterialData SoA fields ✅ 2026-06-11
    4.3 RenderEngineFactory(RenderConfig) + CLI spectral.bands → renderer config chain ✅ 2026-06-11
    4.4 HostTexture supports N-channel carrier ✅ E.3; SPD material path writes material SoA, not texture carrier
    4.5 compiler + renderer pass test ✅ `test_spectral_spd_compiles_runtime_n`

Phase E.5 — 色散 + Mueller 光谱化
    5.1 色散遍 N 通道
    5.2 thin-film Airy 和升级
    5.3 Stokes 光谱化决策落地
    5.4 K.6 光谱 MIS
```

---

## 6. 不改变的（N 无关的）

以下模块不受 Phase E 影响（零修改）：
- **BVH / mesh / sphere 几何管线**
- **TransformRingBuffer / 物理管线**
- **ECS World / 场景加载管线**（SceneIR 结构不变，SPD 路径新增）
- **Denoiser / FXAA / 后处理**（输入始终是 RGB float3 framebuffer）
- **CMake 库结构**（`ure_types` / `ure_core` / `ure_sceneio` 划分不变）
- **诊断系统 + 测试框架**

---

## 7. N 值选择策略

由 `gpu_auto_config.hpp` 基于以下输入决定：

| 输入 | 影响 |
|------|------|
| 可用 VRAM | `num_wavelengths * (队列数 × 容量 + 材质数 × 6 + 纹理数 × 像素) × sizeof(float)` |
| `CUDA_ARCHITECTURES` | Maxwell (SM 5.x) 寄存器压力大 → N ≤ 8；Turing+ (SM 7.x) → N ≤ 32；Ampere+ (SM 8.x) → N ≤ 64 |
| 场景请求 | 用户通过 `--spectral-bands 16` 或 glTF `spectralBands` 指定 |
| `RenderConfig::num_wavelengths` 默认值 | auto-select，上限由 VRAM 决定 |

**当前阶段默认值**: N = 8（在 RTX 5060 6GB 上平衡精度与性能）。

---

## 8. 与旧 API 的兼容性

`render_frame_gpu()`（旧 API）已在 2026-06-11 删除声明和实现。代码库当前只保留 `init_gpu_renderer()` + `render_pass_gpu()` + `copy_frame_buffer_gpu()` 的 RenderConfig 驱动路径。

验证记录：当前门禁为 Release build + 17/17 CTest；`build_modular/last_batch1_2_full_build.log` warning/error scan 为空。既有 `C4100`、`C4324`、`LNK4098`、`C4819` 构建噪声已清理。

2026-06-11 进展：`generate_rays_kernel` 已移除 `float4 ray_wavelengths`，按 `queue.num_spectral_channels` 直接写入 `RayQueue::throughput_vals` 和 `RayQueue::throughput_wavelengths` SoA 布局。新增 `test_raygen_runtime_wavelength_count` 使用 N=8 验证每个波长落入对应 stratified bin。该测试只证明 ray generation 的运行时 N 写入，不代表 shade/BSDF/texture 路径已支持 N>4。

2026-06-11 进展：`gpu_spectrum_utils.cuh` 已新增数组版 `spectrum_to_xyz(values, wavelengths, num_spec)`、`rgb_to_spectrum(out_values, out_wavelengths, rgb, wavelengths, num_spec)`、`rgb_coeff_to_spectrum(...)` 和 `emission_to_spectrum(...)`。新增 `test_array_spectrum_helpers_n8` 使用 N=8 验证值写入、波长透传、emission 与 RGB 上采样一致，以及数组版 CIE 输出有限非负。`GpuSpectrum` 返回函数仍作为过渡桥接保留给已迁移到 pointer+N 的调用面；后续最终 SoA 化时必须继续删除该桥接类型，不能把它视为最终架构。

2026-06-11 进展：`GpuSpectrum` 作为过渡桥接类型扩容到 32 通道；`load_mat_spectrum()` / `load_mat_spectra_6x()` 按 runtime N 读取 material SoA；`scatter()` 的 conductor、thin-film、dispersion、dielectric R/T 计算循环改为使用 `num_spec`；`extend_shadow_kernel()` 读取 `ShadowQueue::radiance_vals` / `radiance_wavelengths` 时改为遍历 `scene.num_spectral_channels`，并且 dielectric shadow 透过路径直接使用 material spectral SoA，不再把前三个通道当 RGB 重新上采样。`test_mat_soa_load_n8` 和升级后的 `test_sq_extend_nonuniform` 覆盖 N=8；`gpu_test_spectral_soa` 为 6 tests / 194 checks。

2026-06-11 进展：`eval_bsdf()` 和 `sample_texture()` 的波长输入从 `float4 wavelengths` 迁移为 `const float* wavelengths, int num_spec`；`shade_kernel` 中 sky、texture、emissive surface、volume direct light 和 NEE direct light 路径不再构造 `make_float4(throughput.wavelengths[0..3])`。新增 `test_sample_texture_invalid_n8` 覆盖 texture fallback 的 N=8 波长透传，新增 `test_eval_bsdf_metal_n8` 覆盖金属 Fresnel BSDF 对第 5-8 通道写入有限正值；`gpu_test_spectral_soa` 现为 8 tests / 257 checks。此时剩余固定 4 通道面主要是 host texture upload 的 `float4`/`tex2D<float4>` 存储路径；下一步应按本设计落地分层纹理或等价 N 通道纹理载体。

2026-06-11 进展：host texture N-channel carrier 已落地。`HostTexture::channels` 和 `GpuTexture::channels` 区分 RGB 输入与显式光谱输入；RGB 路径保留 `cudaArray<float4>` `texObj` 供硬件过滤，同时上传 packet-width float spectral buffer；显式光谱路径要求 `channels == RenderConfig::num_wavelengths`，上传到 `GpuTexture::spectral_values` 并由 `sample_texture()` 对 N 通道手写双线性插值。新增 `test_sample_texture_spectral_data_n8` 覆盖 2x2 spectral texture 的 N=8 插值和值/波长透传；`gpu_test_spectral_soa` 现为 9 tests / 283 checks。该段描述的是 Phase E 过渡状态，已被 2026-06-14 Phase L.8 的 source-sample resource descriptor 替代。

2026-06-11 进展：legacy `float4` spectral helper overload 已删除。`tests/gpu/test_spectral_pipeline.cu` 的 roundtrip、`rgb_coeff_to_spectrum` 和 `emission_to_spectrum` 覆盖全部改为 `const float* wavelengths, int num_spec`，其中 emission 匹配测试提升到 N=8；`gpu_test_spectral` 现为 12 tests / 469 checks。`resample_uniform` 同步移除隐藏 4 通道默认值，public `scene_io::load_spd(path, int)` 与 `load_spd(path, RenderConfig)` 透传运行时 N；新增 `test_scene_io_load_spd_runtime_n`，`test_asset_pipeline` 现为 7 tests / 48 checks。

2026-06-11 进展：Phase E.4 SPD 材质输入已接入。`gltf_scene_frontend.cpp` 将 `URE_spectral_material` 的 `albedoSPD`/`emissionSPD` 相对路径解析到 glTF 文件目录；`GpuSceneCompiler::compile(scene_ir, RenderConfig)` 按 `RenderConfig::num_wavelengths` 的光谱 bin 中心重采样 SPD，并填充 `GpuMaterialData::albedo` / `emission`，随后由既有 material SoA 上传路径进入 GPU。新增 `RenderEngineFactory::create_gpu_renderer(RenderConfig)`，CLI 将 `cfg.spectral.bands`、queue capacity 和 max depth 传入 renderer，避免 runtime-N 配置停留在 CLI 层。新增 `test_spectral_spd_compiles_runtime_n` 覆盖 N=8 SPD compiler 值、wavelength 写入、非法 N 拒绝，以及 `load_scene_ir()` + `render_pass()` 连通性；后续完整门禁已推进到 Release build + 17/17 CTest。

2026-06-11 进展：E.5.2 boundary evaluator 骨架已接入。`path_tracer_boundary.cuh` 提供 dielectric、conductor 与 thin-film Airy evaluator；`scatter()`、`eval_bsdf()`、`extend_shadow_kernel()` 已共享该入口，transparent shadow 删除 scalar Schlick 并按通道计算 dielectric transmission。后续 blocker pass 已继续修正 transport scale、Stokes convention、ShadowQueue lane estimator 和 metal thin-film scatter/eval consistency；当前针对性验证为 `gpu_test_polarization` 126/0、`gpu_test_spectral_soa` 633/0、`gpu_test_render` 281/0。

2026-06-11 进展：E.5.3/E.5.4/E.5.5 首段已接入。`shade_kernel` 对 packet-mode dispersive/thin-film dielectric 执行 per-channel deterministic reflected/transmitted lane split；RayQueue append 新增 capacity guard 和 host-visible `overflow_count`。`test_dispersive_dielectric_splits_packet_to_lanes` 验证 4-channel packet 生成 8 lanes，`test_dispersive_dielectric_critical_angle_splits_n8` 验证 N=8 临界角 channel 分叉，`test_ray_queue_overflow_count_visible` 验证 overflow 诊断。旧 dielectric transmission clamp 已删除；最终 transport 测试锁定 air→glass attenuation、glass→air inverse 和 slab reciprocity。旧 transparent straight-through spectral visibility 已在 2026-06-12 被撤回并替换为 specular blocker policy。当前完整门禁为 Release build、17/17 CTest、warning/error scan 和 diff check 通过。

2026-06-12 进展：E.5.2 metal thin-film 已从旧 `get_thin_film_interference()` 迁移到 `eval_thin_film_conductor_boundary()`，旧 helper 删除。新增 conductor thin-film 零厚度回归，验证 film thickness=0 时与 bare conductor boundary reflectance 一致。`apply_mueller_reflection_conductor()` 和 dielectric thin-film reflection 已改为复用 boundary complex amplitude，并新增 conductor/thin-film Mueller-boundary 一致性回归。后续同日已继续扩展到 thin-film transmission、TIR phase 和 packet channel-major Stokes 输出，当前针对性验证为 `gpu_test_polarization`、`gpu_test_spectral_soa`、`gpu_test_render` 全部通过。剩余债务不再是 conductor 公式双写、thin-film reflection phase、旧 thin-film helper 或 packet Stokes 单通道复制，而是 transport API/render-level reciprocity、per-channel sampling/MIS、material eta/k 与 albedo-F0 fallback 语义统一。

2026-06-12 物理公式审查补充：raygen wavelength jitter 与 host-side material/medium/texture bin-center SoA 是系统性不一致。当前采取一致性优先方案：`generate_rays_kernel` 固定写入 bin center wavelength，测试断言 exact center。未来若恢复 bin jitter，必须将材质、介质、texture/SPD 改成按 ray wavelength 动态采样或可插值表示，不能只随机化 ray wavelength。

2026-06-12 物理公式审查补充：n/k metal scatter 过去在 complex Fresnel 后又乘 `albedo`，而 `eval_bsdf()` 不乘，导致 NEE 与 continuation 不同式并对测量金属二次染色。现在有 n/k 时 scatter continuation 直接使用 conductor/thin-film Fresnel，baseColor 只作为无 n/k 的 Schlick F0 fallback。`test_metal_scatter_uses_per_channel_conductor_fresnel` 使用非均匀 albedo 验证 `attenuation / Fresnel` 跨 channel 恒定。

2026-06-12 物理公式审查补充：Russian roulette survival 和 transparent shadow radiance early-out 不再使用 display RGB max。`shade_kernel` 改为调用 `spectral_survival_probability()`，以非负 spectral max 作为 survival/early-out proxy，避免窄谱/出 gamut 光谱被 sRGB 矩阵负值污染。新增 `test_spectral_survival_probability` 覆盖 430/700/820nm 窄谱与负通道混合输入。完整 spectral MIS、wavelength PDF 和更精细的 photometric/RR 策略仍属于 E.5.6。

2026-06-12 物理公式审查补充：thin-film dielectric boundary evaluator 现在返回 complex `r_s/r_p/t_s/t_p`、power `R_s/R_p/T_s/T_p` 和 transmission Jacobian。`DielectricSurfaceBoundary` 将裸 dielectric 与 dielectric thin-film 收敛为同一 surface result，`scatter()`、dispersive lane split 和 transparent shadow 都消费同一个 complex amplitude/power/transport 数据结构；reflection/transmission Mueller 均从 complex amplitude 派生。新增/扩展 `gpu_test_polarization` 覆盖 thin-film `R+T≈1`、quarter-wave AR 透射峰、surface boundary 与裸界面/薄膜等价、TIR complex phase 和 transmission Mueller-boundary 一致性。剩余问题转为完整 transport mode/reciprocity 和 shadow medium stack，而不是 thin-film 透射仍用裸界面。

2026-06-12 物理公式审查补充：dielectric TIR reflection 现在返回 complex Fresnel amplitude，不再用实数 `1` 占位；film-substrate TIR 因而保留 s/p 相位。`gpu_test_polarization` 增加 TIR unit reflectance、非零 imaginary phase 和 film-substrate TIR overall reflectance 断言。剩余 shadow 风险是 medium stack/出射 TIR 路径管理，不是 boundary amplitude 缺相位。

2026-06-12 物理公式审查补充：transparent shadow 的 straight-through approximation 已撤回。该近似即使维护 enter/exit 状态，也无法保证折射路径连接到 shadow ray 原本采样的光点，会在 NEE 中产生偏置。当前 policy 是 specular dielectric blocker；未来恢复玻璃直接光必须做 specular manifold / refractive shadow path，而不是继续扩展直线 visibility。

2026-06-12 物理公式审查补充：metal scatter 的 VNDF continuation weight 已与 `eval_bsdf()*cos/pdf` 对齐。visible-normal PDF 为 `G1(V)*D/(4*NdotV)`，`eval_bsdf()` 为 `F*D*G/(4*NdotV*NdotL)`，因此 continuation weight 应为 `F*G1(L)`。旧实现多乘 `VdotH/(NdotH*NdotV)`，现已删除。`test_metal_scatter_uses_per_channel_conductor_fresnel` 直接验证 scatter attenuation 等于 conductor Fresnel 乘 `smith_G1(NdotL)`。

2026-06-12 物理公式审查补充：volume free-flight/no-scatter proposal 现在按 spectral mode 选择。packet mode 继续使用 `sigma_t_avg` proposal，并保留 per-channel transmittance/proposal 权重；lane mode 使用 active channel 的 `sigma_t`，避免单通道 spectral lane 仍被其他 wavelength 的介质吸收控制。新增 `test_lane_no_scatter_proposal_weight` 覆盖 active-channel proposal 与 packet proposal 分离。

---

## 9. Post-E 技术债与后续 Phase 边界

2026-06-13 物理第一性审查补充：当前实现不能声明“完整忠实实现”。新的公式级审查发现 4 个必须立即修复的 correctness blocker：lane-mode dielectric 后续界面仍会回落到 packet hero-channel 逻辑；dielectric interface reflection/transmission 被 baseColor/albedo 染色；rough metal 的 VNDF sampling、eval 和 pdf 参数化不完全一致；球光源 NEE 的实际 solid-angle sampling 与 BSDF-hit MIS 反向 PDF 不一致。另有 2 个明确后续边界：rough dielectric 仍是 normal jitter 近似而非 microfacet BSDF/BTDF；specular dielectric direct lighting 仍采用 blocker policy，完整玻璃直接光需要 specular manifold / refractive shadow path。

2026-06-13 第一批修复状态：lane-mode dielectric 已强制使用 active channel；dielectric interface Fresnel 不再乘 albedo；sphere-light MIS reverse PDF 已切到与 NEE 一致的 solid-angle PDF；rough metal 已统一为 `alpha = roughness^2`、VNDF/pdf/eval/scatter 共用 exact Smith GGX；rough dielectric continuation 已从 normal jitter 替换为 GGX visible microfacet reflection/transmission，使用 microfacet boundary frame、Fresnel branch、radiance eta scale 和 `G1(L)` continuation weight，并新增 targeted GPU regressions。后续补齐项已继续推进：rough dielectric reflection/transmission lobe 的 microfacet half-vector、visible-normal PDF 和 transmission Jacobian 已提升到 BSDF 层，`eval_bsdf()` / `pdf_bsdf()` / `scatter()` 不再只在 continuation 私有路径里处理 BTDF；direct lighting/MIS 现在能看见 rough dielectric BTDF，并且 shade direct-light gate 允许 rough dielectric 透射半球，shadow origin 会按出射侧偏移。rough thin-film / dispersive dielectric 也已从 smooth specular lane split 中移出，进入同一 rough microfacet BSDF 路径，per-channel lobe 会按 wavelength 重新计算 dispersive IOR、thin-film boundary 和 transmission Jacobian。specular manifold 仍是更大范围设计工作。

2026-06-13 视觉验证补充：新增 `scenes/physics_optics_visual.scene` 与 `scripts/render_physics_optics_visual.ps1`，覆盖光滑玻璃、裸 rough dielectric、rough metal、sphere light、地面和背景墙的真实 CLI 渲染路径。该门禁首先暴露并修复了 CLI 默认 `spectral.bands = 64` 超过当前 GPU packet cap 32 的配置错误；默认值现为 32，CLI 会对超限 band count 给出显式错误。32 SPP smoke 图像已人工查看：非空帧，无明显 NaN 爆点，无 dielectric baseColor 染色回归；噪声仍明显，只能作为 smoke/观感回归入口，不能替代 furnace、reciprocity、reference render 等高质量物理验证。

2026-06-13 架构级修复补充：rough dielectric 已从仅在 `scatter()` 内部可见的 continuation 分支提升到 BSDF 层。新增 `is_rough_dielectric_bsdf()` / dielectric unpolarized reflectance helper，`eval_bsdf()` 和 `pdf_bsdf()` 现在对 rough dielectric reflection/transmission lobe 返回 GGX microfacet 值、visible-normal PDF 和 transmission Jacobian，并且 `shade_kernel` 对 rough dielectric 开启直接光采样和 BSDF-hit MIS。smooth dielectric 仍保持 delta/specular 处理；这些路径需要 specular manifold / refractive shadow path，不能通过当前 direct-light gate 混入。

2026-06-13 可靠性收敛补充：默认 Release 渲染路径已修复为可直接运行。runtime spectral channel contract 现在显式为 `[8, 32]`，核心 `RenderConfig` 默认值从 4 改为 8，CLI 默认仍为 32；SceneIR compiler、GPU init 和 CLI 均拒绝 4 通道，避免“全绿但默认/低 N 不可用”的隐性阈值。输出层新增 Radiance HDR (`.hdr`/RGBE) writer，CLI 支持 `--format bmp|ppm|hdr` 并按格式生成默认扩展名；BMP 仍作为 quick preview，不再是唯一输出。Scene frontend 分派收敛为 `.gltf/.glb` → glTF、`.scene` → legacy compatibility；未知扩展和 direct glTF frontend 的非 glTF 输入均明确拒绝，不再隐式 fallback 到 legacy parser。

| 问题 | 跟踪 | Phase |
|------|------|-------|
| Lane-mode dielectric active channel | 单波长 lane 后续 dielectric bounce 必须只使用 active wavelength/channel 的 Fresnel、TIR、方向、Mueller 和 transport；不能再随机 hero channel | K |
| Dielectric interface tint semantics | 非吸收 dielectric 表面 Fresnel 不能乘 baseColor；有色玻璃应通过介质 absorption/Beer-Lambert 或未来明确 absorption material 输入表达 | M/K |
| Rough metal BSDF/PDF consistency | 已统一 VNDF sampling、`eval_bsdf()`、`pdf_bsdf()` 和 scatter continuation 的 roughness/alpha 与 exact Smith GGX masking；后续仍可补更完整 white-furnace/PDF normalization scenes | K |
| Sphere-light MIS consistency | BSDF-hit emissive MIS 的 reverse NEE pdf 必须匹配实际 light sampling strategy；当前 sphere light 使用 solid-angle sampling | K |
| Specular dielectric 直接光 | 当前 NEE 对 specular dielectric blocker 是保守无偏 policy；恢复玻璃后的直接光必须实现 specular manifold / refractive shadow path，不能在 ShadowQueue 上做直线折射修补 | K |
| Rough dielectric microfacet BTDF | rough dielectric path continuation 已替换 normal jitter；reflection/transmission lobe 已接入 `eval_bsdf()` / `pdf_bsdf()` / direct-light MIS，scatter 也返回非 delta PDF；rough thin-film/dispersive 组合进入同一 microfacet BSDF 路径；direct-light gate 已允许 BTDF 半球并按出射侧偏移 shadow origin；transmission continuation 已用 `eval_bsdf * abs(cos) / pdf_bsdf` 等式锁定同分布；后续还需补更完整 furnace/reference scenes | M/K |
| Output fidelity | CLI 已支持 Radiance HDR 输出；EXR 仍需后续引入 tinyexr/OpenEXR 或自有半浮点 writer，不能继续只依赖 8-bit BMP | K |
| Scene frontend split | `.scene` 保留为 legacy compatibility frontend，非 `.scene/.gltf/.glb` 不再自动 fallback；后续应提供 `.scene` 到 glTF/SceneIR 的迁移工具，而不是扩展 legacy grammar | G/M |
| RGB/photometry fallback 精度 | Phase E 已完成 explicit wavelength PDF baseline、D65/equal-energy/RR 门禁；更高阶 importance wavelength sampling 和更完整 white-furnace 场景进入后续优化 | K.6 |
| Volume spectral proposal 方差 | lane mode 已使用 active channel `sigma_t`；packet mode 保持平均 proposal，后续可优化 proposal/MIS 方差 | K |
| 荧光/磷光（波长间能量转移） | K.5 | K |
| 黑体辐射（Planck 分布） | K.3 | K |
| 测量材质数据库（MERL 100 / 真实世界 SPD） | M.1 | M |
| Advanced spectral MIS / wavelength importance sampling | Phase E 已完成 explicit wavelength PDF baseline estimator；后续是高级采样优化 | K.6 |
| 偏振 Mueller 的材质扩展 | Phase E 已完成 conductor/dielectric/thin-film Mueller 光谱化；未来只保留新增 BSDF/材质类型的扩展工作 | M/K |
