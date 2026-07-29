# Backend and Session API Status

Document status: current interface summary

Last reviewed: 2026-07-28

This document describes the implemented API boundary. It is not a promise of long-term ABI stability. `PLAN.md` controls future API work.

## Interface layers

| Layer | Primary surface | Role |
|---|---|---|
| C++ engine | `ure/render.hpp` | Renderer creation, SceneIR loading, render passes and framebuffer access |
| C++ session | `ure/session.hpp` | Stateful progressive rendering, pause/resume/cancel, mutations and AOVs |
| C ABI | `ure/ure_c_api.h` | Opaque engine/session handles for language bindings |
| Python | `pyure/__init__.py` | ctypes wrapper for session configuration and rendering |
| CLI | `apps/ure_cli` | Offline render and native scene tooling |

## Current session lifecycle

```text
create session
  -> load validated SceneIR/native scene/package or supported adapter input
  -> start or render_pass
  -> pause/resume/reset as needed
  -> apply supported SceneDiff mutations
  -> read framebuffer/AOVs or save output
  -> destroy session
```

The session owns a renderer instance and retained SceneIR state. Scene replacement and mutation paths classify whether a hot update is safe or a full reload is required. Unsupported resource mutation must not be silently approximated.

## Implemented controls

- default, spectral, wave-optics and integrator-aware session creation;
- progressive and explicit render-pass execution;
- pause, resume, cancel and accumulation reset;
- camera, instance transform, material and supported texture mutation;
- Beauty, Normal, Albedo, Depth, UV and MotionVector AOV access;
- BMP/HDR output helpers;
- `.ure`, `.urescene`, `.urepkg`, glTF and GLB loading through native validation/adapter boundaries;
- pyure package guard through `RenderSession.load_package()`.
- backend and acceleration-aware creation through `RenderConfig`,
  `ure_engine_create_execution_config()`,
  `ure_session_create_execution_config()` and matching pyure arguments.

## Acceleration configuration

`AccelerationConfig` separates geometry acceleration policy from execution
backend selection. Its provider vocabulary is `auto`, `self_compute`, `optix`,
`vulkan_rt` and `dxr`; build quality is `auto`, `fast_build`, `balanced` or
`high_quality`; update policy is `auto`, `static`, `refit` or `rebuild`.
Clustered geometry, statistics collection and scratch-memory budgeting are
explicit requests.

Default selection resolves to CUDA `self_compute`.
Explicit `self_compute` accepts automatic, fast-build, balanced and high-quality
construction with auto/static/refit update. Automatic and fast-build retain the
compatible median BVH2; balanced selects binned SAH/BVH4 and high-quality
selects bounded spatial SAH/SBVH/BVH8. V.2 makes statistics executable, V.3
adds BLAS/TLAS hierarchy fields and V.4 adds BLAS build time, primitive
references, spatial splits, binary construction nodes and selected arity. C++
and pyure expose the complete current view. V.5 executes bounded asynchronous
host construction, compact-only pinned-stream upload and scratch-budget
enforcement, and adds wall/upload/temporary/compaction telemetry. The C ABI
retains all earlier layouts and adds `ure_acceleration_stats_v4_t` plus
versioned getters. V.6 adds an SDK-free multi-geometry native build contract
with compaction, transform refit/rebuild, scratch budgets and provider-owned
statistics for Vulkan RT, DXR and optional SDK-backed OptiX. Full renderer
selection remains fail-loud for arbitrary SceneIR on native providers. V.7
adds tangent/handedness to the SDK-free hit record and validates one shared
SceneIR closest/shadow, transform, material, interpolation and AOV contract
across CUDA self-compute, optional OptiX, Vulkan RT and DXR. Clustered geometry
V.8 adds an SDK-free derived resource with conservative bounds,
material/spectral/displacement/opacity/normal-field boundaries, page
residency, physical LoD error and canonical GPU upload ABI. It does not yet
make the renderer cluster flag executable; physical-error LoD selection and
production traversal remain V.9. Existing backend-only C entry points retain
their original layout and behavior; the execution-config entry points add
acceleration without requiring callers to pass a larger legacy structure.

## Threading and ownership

- `RenderSession` serializes state and engine access with its internal synchronization.
- Returned framebuffer and AOV pointers refer to internal storage and must not outlive or race the owning session operation.
- C handles are opaque and must be destroyed with the matching API.
- The Python wrapper is a thin ctypes binding; it does not add independent scheduling or memory ownership semantics.

## Error behavior

C functions generally return `0` on success and a negative value or null handle on failure. Unsupported wave/integrator modes, invalid backend/acceleration combinations, malformed resources and insufficient queue/budget configurations fail before rendering where possible. The C ABI does not yet expose a complete structured error object; callers should also route `ure_diag` logs.

## Stability boundary

The following are not promised:

- stable binary ABI across arbitrary commits;
- source compatibility for every configuration structure;
- concurrent mutation while a render operation is using the same session;
- non-CUDA backend parity;
- interactive viewport integration;
- production support for advanced integrators that currently fail loudly.

## Verification

Relevant registered tests include `test_session`, `test_pyure_smoke`, `gpu_render`, `gpu_instance`, `gpu_denoise`, `test_native_tooling`, `test_native_adapter` and `test_native_validation_suite`. Use the full CTest gate before relying on a changed API:

```powershell
ctest --test-dir build_modular_x64 -C Release --output-on-failure
```
