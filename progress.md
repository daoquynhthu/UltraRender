# UltraRender 进度记录

本文档按时间戳记录所有已完成的工作。每完成一个 Phase（或其子步骤）都追加一条记录。

---

## 2026-06-08

### [初始化] 创建项目治理文档

- **Phase**: Setup
- **状态**: ✅ 完成
- **变更**:
  - 创建 `AGENTS.md` — 项目约束、代码规范、工作流
  - 创建 `STATUS.md` — 实际代码现状记录
  - 创建 `PLAN.md` — 分阶段修复计划
  - 创建 `progress.md` — 本进度记录文件
  - 更新 `.gitignore` — 忽略构建目录和 GUI obj
- **验证**: 文档创建完毕，无需构建测试
- **Review**: 无需（纯文档）
- **备注**: 尚未开始 Phase 1 代码改动

### [提交] pre-fix snapshot

- **提交**: `bb9eea5` — pre-fix snapshot: baseline before Phase 1 repairs
- **包含**: 40 files changed, 6533 insertions, 583 deletions
- **备注**: 所有源代码 + 新增文件完整提交，打上"修复前基线"标记

### [Phase 1 Step 1.1] 确认 Bug 现状

- **Phase**: Phase 1 (能量守恒 + scatter 统一)
- **状态**: ✅ 完成
- **确认结果**:
  - `kernel.cu:1355-1359`: `radiance_scale` 确实被注释掉
  - `material.cu:812-816`: 修复代码确实存在且正确
  - `material.cu`: `scatter()` 标记为 `static`，无法被外部调用
  - 两处 `scatter()` 调用点均已确认（line 1807 和 2719）
- **下一步**: Step 1.2 — 给 material.cu 的 scatter 加回 `out_pdf`

### [Phase 1 Step 1.2] 给 material.cu 的 scatter 加 out_pdf

- **Phase**: Phase 1 (能量守恒 + scatter 统一)
- **状态**: ✅ 完成
- **变更**:
  - material.cu `scatter()` 增加 `float& out_pdf` 参数
  - Lambertian/Cloth: `out_pdf = cos/pi`
  - Metal: `out_pdf = VNDF PDF (D*G1/4NdotV)`
  - Dielectric: `out_pdf = 0.0f`（delta 分布约定）
  - 无效路径: `out_pdf = 0.0f`
- **验证**: 语法正确（当时未构建 kernel，material.cu 尚未被 include）
- **备注**: 所有 6 个 return 点均已设置 PDF，BDPT 路径未阻塞

### [Phase 1 Step 1.3] 删除旧 scatter + 连接 material.cu

- **Phase**: Phase 1 (能量守恒 + scatter 统一)
- **状态**: ✅ 完成
- **变更**:
  - 从 material.cu 删除 462 行重复辅助函数（reflect/refract/smith_G1/hit_*/world_hit/Mueller/thin-film 等）
  - material.cu 只保留 `scatter()` 函数体（420 行）
  - 给 material.cu 的 `scatter()` 增加 `ior_outside`/`ior_inside` 参数，接口匹配 kernel.cu 调用点
  - 修复 material.cu 中 eta_i/eta_t 使用硬编码 1.0f → 改用 `ior_outside`
  - 从 kernel.cu 删除约 539 行旧 `scatter()` 函数体
  - 在 kernel.cu 末尾添加 `#include "path_tracer_material.cu"`
  - 从 CMakeLists.txt 移除 material.cu 独立编译（改为被 kernel.cu include）
  - 在 kernel.cu 添加 `scatter()` 前向声明
- **验证**: 构建成功 UltraRender.exe 链接通过
- **Review**: 完整性检查通过 — 无悬空引用，所有 12 个被调函数在 include 前已定义
- **备注**: 两个 minor issue 已记录（ior_inside 未使用、第二个调用点使用默认 IOR 1.0）

### [提交] Phase 1 snapshot

- **提交**: `5327cdf` — `phase1: unify two diverged scatter() implementations`
- **包含**: 5 files, +108/-1019
- **备注**: AGENTS.md + material.cu + kernel.cu + CMakeLists.txt + progress.md

### [Phase 2 Step 2.1] 传递 ior_outside 给 shade_kernel 的 scatter 调用

- **Phase**: Phase 2 (嵌套 IOR + NEE Dielectric)
- **状态**: ✅ 完成
- **变更**:
  - kernel.cu line ~2196: scatter 调用前增加 `ior_outside` 计算
  - 使用 `current_medium_idx` 确定外部介质 IOR（进入时有效）
  - 增加 `front_face` 判断：仅在进入表面时使用 `current_medium_idx`
  - 退出时 `ior_outside = 1.0f`（vacuum），与介质跟踪的退出逻辑一致
- **验证**: 构建成功 UltraRender.exe 链接通过
- **Review**: reviewer 发现初始版本缺少 front_face 判断 → 退出时 eta_i=eta_t 折射不弯曲 → 已修复
- **备注**: 嵌套介质退出到非 vacuum 的场景仍需完整介质栈（不在本阶段范围）

### [Phase 2 Step 2.2] 评估 NEE Dielectric

- **Phase**: Phase 2 (嵌套 IOR + NEE Dielectric)
- **状态**: ✅ 完成（无需改代码）
- **评估结论**:
  - NEE 显式跳过 Dielectric（line 2071 条件中未包含 Dielectric）
  - `eval_bsdf()` 对 Dielectric 返回 0（delta BSDF 无法用 NEE 采样）
  - `pdf_bsdf()` 对 Dielectric 返回 0
  - 以上行为**正确** — 完美镜面 BSDF 不能通过随机方向重要性采样
  - Dielectric 的光照贡献来自 BSDF 采样链（散射路径命中光源）
  - 两处 NEE（path_trace + shade_kernel）对 Dielectric 处理一致
- **备注**: 若未来引入粗糙电介质 BSDF，NEE 需相应更新

### 当前进度

```
Phase 1: [████] 全部完成
Phase 2: [██··] Step 2.1-2.2 完成
Phase 3: [   ] 未开始
Phase 4: [   ] 未开始
```
