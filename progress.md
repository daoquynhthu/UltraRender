# UltraRender Progress Log

## Phase P.1 — GPU Instance Desc/Transform Split + Hot-Update Path

### 2026-06-09

- [DONE] P.1.1: Add `GpuInstanceDesc` (mesh_index + material_index) and `GpuInstanceTransform` (transform + inverse + min/max AABB) structs
- [DONE] P.1.2: Reorder `GpuInstance` layout so `mesh_index`/`material_index` come first → `reinterpret_cast<GpuInstanceDesc*>(d_instances)` works correctly
- [DONE] P.1.3: Add `d_instance_transforms` to `GpuContext` as separate hot-update target buffer
- [DONE] P.1.4: Fix transform upload in `init_gpu_renderer` — use field-by-field extraction (not raw memcpy from wrong offset)
- [DONE] P.1.5: Fix transform upload in `render_frame_gpu` — same field-by-field extraction
- [DONE] P.1.6: Add `update_instance_transforms_gpu()` with null/count/offset assertions
- [DONE] P.1.7: Add runtime assertions in `gpu_engine_impl.cpp` (null checks, size consistency)
- [DONE] P.1.8: Add `gpu_test_instance` target with 3 tests (layout, hot-update, transform readback)
- [DONE] P.1.9: Full GPU test suite passes — 6 executables, 132 assertions, 0 failures
- [DONE] P.1.10: Build verification with `Visual Studio 17 2022` + CUDA 13.0 + RTX 5060 (CC 12.0)

### Bugs Fixed During Testing

| Bug | Root Cause | Fix |
|-----|-----------|-----|
| `GpuInstanceDesc` cast returned wrong mesh_index | `GpuInstance` had `transform` before `mesh_index` (offset 128), but `reinterpret_cast<GpuInstanceDesc*>` assumed offset 0 | Reordered `GpuInstance` to put `mesh_index`/`material_index` first |
| CUDA illegal memory access in BVH traversal | Transform buffer `cudaMemcpy` from `instances.data()` with `sizeof(GpuInstanceTransform)` — copied from offset 0, getting `transform(64)+inverse(64)+garbled(24)` instead of `transform(64)+inverse(64)+min(12)+max(12)` | Field-by-field extraction for transform buffer upload |

## Phase P.3 — Transform Ring Buffer

### 2026-06-09

- [DONE] P.3.1: Create `tranform_ring_buffer.hpp` — triple-buffer (kNumFrames=3), resize/begin_write/end_write/advance/begin_read/end_read/init_from_instances
- [DONE] P.3.2: Integrate ring buffer into `gpu_engine_impl.cpp` — replace `cached_transforms_` vector with `tranform_ring_buffer_`
- [DONE] P.3.3: Write frame → advance write → read frame → upload to GPU → advance read; 1-frame lag guarantee via 3 frames
- [DONE] P.3.4: Add 3 ring buffer tests (basic read/write cycle, init_from_instances, wraparound)

### Architecture

```
PhysicsSystem → write frame → advance()
                               ↓
                         [Frame 0]  ← write_index
                         [Frame 1]
                         [Frame 2]  ← read_index  (lags by 2)
                               ↓
                   begin_read() → cudaMemcpy H2D → end_read()
```

### Test Results

```
[GPU Device Test]         PASS (6 assertions)
[GPU Math Functions]      PASS (27 assertions)
[GPU Spectral Pipeline]   PASS (30 assertions)
[Hardware Config Test]    PASS (17 assertions)
[GPU Basic Render Test]   PASS (37 assertions)
[GPU Instance Hot-Update] PASS (66 assertions, +3 ring buffer tests)
Total: 183 assertions, 0 failures
```

## Phase P.5 — ISpatialQuery 抽象（声学 ↔ 物理 解耦）

### 2026-06-09

- [DONE] P.5.1: Create `ure/physics/ispatial_query.hpp` — `ISpatialQuery` interface with pure virtual `ray_cast()`, `RayCastHit` struct
- [DONE] P.5.2: `PhysicsWorld : public ISpatialQuery` — inherit interface, remove local `RayCastHit` definition
- [DONE] P.5.3: `AcousticRayTracer` takes `ISpatialQuery*` instead of `PhysicsWorld*` — no longer includes `physics_world.hpp`
- [DONE] P.5.4: `AcousticSystem::set_spatial_query(ISpatialQuery*)` replaces `set_physics_world(PhysicsWorld*)`
- [DONE] P.5.5: Update `apps/ure_cli/src/main.cpp` callers
- [DONE] P.5.6: Full regression — 8 suites, 222 assertions, 0 failures

### Dependency Change

```
Before: AcousticRayTracer ──▶ ure::physics::PhysicsWorld  (concrete type)
After:  AcousticRayTracer ──▶ ure::physics::ISpatialQuery (interface)
          PhysicsWorld ──▶ ISpatialQuery  (implements)
```

## Phase P.4 — World/ECS 组件池

### 2026-06-09

- [DONE] P.4.1: Create `ure_types/include/ure/world.hpp` with EntityId + SoA component pools
- [DONE] P.4.2: Define `TransformComponent` (Vec3f+Quat+Vec3f → to_matrix()), `GeometryComponent` (mesh ptr + material index), `PhysicsComponent` (config_id handle), `AudioComponent` (material_id + modal_body_id)
- [DONE] P.4.3: O(1) entity creation + swap-remove compaction on `remove_entity`
- [DONE] P.4.4: Add `tests/host/test_world.cpp` (4 tests: create, remove, transform matrix, pool compaction → 39 assertions)
- [DONE] P.4.5: All 7 test suites pass: 222 assertions, 0 failures

### Design Notes

- SoA layout (separate vectors per component type) for cache-friendly iteration
- `entity_to_index` map for O(1) EntityId → pool index lookup
- Physics/Audio components use lightweight `int` handles; concrete configs live in `ure_physics`
- `Camera` stored as `std::unique_ptr<core::Camera>` (abstract base)

## Phase F — Directory Restructure + CMake Library Separation

### 2026-06-08

- [DONE] F.1: Create new directory structure (`libs/ure_core`, `libs/ure_types`, `libs/ure_sceneio`, `libs/ure_config`, `libs/ure_physics`, `apps/ure_cli`)
- [DONE] F.2: Create ure_types (header-only INTERFACE lib with GpuVec3, GpuMat4, GpuSpectrum, etc.)
- [DONE] F.3: Create ure_core with all GPU sources (path_tracer_kernel.cu et al.)
- [DONE] F.4: Create ure_sceneio with scene/IO sources
- [DONE] F.5: Create ure_config + ure_physics + ure_cli
- [DONE] F.6: Write all CMakeLists.txt files
- [DONE] F.7: Update tests/gpu/CMakeLists.txt to link ure_core
- [DONE] F.8: Build and verify all tests pass

## Phase 4 — Code Modularization

- Raygen kernel extraction + accessors + section markers
- GPU test infrastructure + math function extraction
- Nested dielectric IOR fix
