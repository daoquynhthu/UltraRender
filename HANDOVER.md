# 渲染引擎开发交接文档 (Handover Documentation)

**日期**: 2026-01-27
**当前阶段**: 阶段四：物理与流体模拟 (Physics & Fluid Simulation) - 验证与集成

## 1. 项目现状概述
本项目已从纯光学渲染引擎扩展为**物理-光学混合引擎**。
目前已集成基于 SPH (Smoothed Particle Hydrodynamics) 的流体模拟系统和 Marching Cubes 等值面提取算法，实现了流体与刚体的双向耦合。
**最新进展**: 
- **流体系统**: 实现了基于空间哈希 (Spatial Hashing) 的 SPH 流体求解器，支持数千粒子的实时模拟。
- **等值面生成**: 集成了 Marching Cubes 算法，将流体粒子转化为动态网格。
- **物理-声学接口**: 定义了物理事件监听接口，支持碰撞事件的外部响应（如声学反馈）。
- **演示场景**: 构建了 "Glass Cup" 演示，包含流体、刚体（球/盒）与静态容器的交互。

## 2. 近期关键变更 (Recent Changes)

### 2.1 流体与物理系统 (New Physics Core)
- **SPH 流体求解器**: 
  - 实现文件: [`src/physics/fluid_system.cpp`](src/physics/fluid_system.cpp)
  - 采用 Tait 方程计算压力，支持流体不可压缩性。
  - 使用空间哈希网格 (Spatial Hashing) 优化邻域搜索，从 $O(N^2)$ 降低至 $O(N)$。
- **Marching Cubes 网格化**:
  - 实现文件: [`src/physics/marching_cubes.cpp`](src/physics/marching_cubes.cpp)
  - **关键修复**: 修复了 `triTable` 处理逻辑，移除了错误的绕序反转 (Winding Inversion)，恢复了标准的 CCW 绕序，解决了流体几何反向/不可见的问题。
  - **稳定性**: 增加了对 `triTable` 的安全边界检查，防止了缓冲区溢出导致的崩溃。
- **刚体动力学**:
  - 实现了基础的刚体碰撞响应（冲量法），支持 Sphere-Sphere, Sphere-Box, Sphere-Plane 碰撞。
  - 集成了 `IPhysicsEventListener` 接口，用于将物理碰撞事件（冲量、材质ID）广播给声学模块。

### 2.2 渲染与可视化
- **动态网格更新**: 在 `main.cpp` 的渲染循环中，每帧调用 Marching Cubes 生成新网格并更新到场景实体（Entity Index 9）。
- **演示场景 (Glass Cup)**:
  - 场景包含：地板 (0)、静态玻璃杯壁 (1-5)、动态盒子 (6)、动态球体 (7-8)、流体 (9)。
  - 粒子数量：8788 个。

### 2.3 遗留问题与已知缺陷 (Known Issues - High Priority)
**1. 性能优化 (Performance)**:
- **Marching Cubes 分辨率**: 目前已提升至 **128**，流体表面更加平滑。但在高分辨率下生成网格的开销较大，影响帧率。建议考虑异步计算或 GPU 加速。

**2. 已修复问题 (Resolved)**:
- **流体几何异常/不可见**: 通过修正 Marching Cubes 的 `invertTriTable` 逻辑（移除错误的绕序反转）以及调整 Isolevel (200.0f) 和 Padding (0.2f)，流体现在可见且形状符合物理规律。

## 3. 核心代码结构更新

| 文件路径 | 说明 |
| :--- | :--- |
| [`src/physics/fluid_system.cpp`](src/physics/fluid_system.cpp) | **SPH 流体核心**。包含密度/压力计算、力累加、时间积分。 |
| [`src/physics/marching_cubes.cpp`](src/physics/marching_cubes.cpp) | **等值面生成**。包含 Paul Bourke 查找表和三角化逻辑。 |
| [`src/physics/physics_world.cpp`](src/physics/physics_world.cpp) | **物理世界主管**。管理刚体、流体步进及碰撞分发。 |
| [`src/main.cpp`](src/main.cpp) | **主程序**。包含 Glass Cup 演示场景构建和物理-渲染主循环。 |

## 4. 下一步开发任务 (For the Successor)

### 4.1 紧急修复
- **性能优化**: 目前 Marching Cubes 分辨率已达 128，需关注实时性能。建议优化网格生成的内存占用或采用 GPU 加速。
- **材质调优**: 验证流体材质 (Glass/Water) 在不同光照下的表现，确保视觉效果真实。

### 4.2 功能扩展
- **双向耦合优化**: 目前流体对刚体的浮力/阻力计算较为基础，需进一步完善。
- **声学集成**: Mock 声学模块已就位，需对接真实的声学传播/混响算法。

## 5. 构建与运行 (Build & Run)
**注意**: 必须使用 Visual Studio 2022 生成器。

```powershell
# 清理并重新构建
mkdir build_vs
cd build_vs
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release

# 运行物理演示 (Glass Cup Demo)
.\Release\UltraRender.exe --physics --width 800 --height 600
```
