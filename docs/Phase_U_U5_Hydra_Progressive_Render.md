# Phase U.5 Hydra Progressive Render

Last reviewed: 2026-08-01

U.5 makes the optional GPU-enabled Hydra plugin render-capable. Actual OpenUSD camera, render-buffer and render-pass objects bridge the retained U.3/U.4 state to the existing native SceneIR and CUDA `RenderSession`; OpenUSD remains an adapter and does not define renderer or scene semantics.

## Execution model

Each Hydra render-pass execute performs one synchronous `RenderSession::render_pass()`. Hydra therefore owns scheduling and redraw cadence, while the session exclusively owns the CUDA context and accumulation state. This avoids a background-session/framebuffer race and does not create an OpenGL, Vulkan, D3D12 or window-system context.

The render pass rebuilds its session when the retained scene revision, collection or render-buffer dimensions change. Camera-only changes use `RenderSession::update_camera()` and reset accumulation without rebuilding scene resources. `ure:samplesPerPass` controls work per execute and `ure:maxSpp` defines convergence.

## Native snapshot boundary

The retained snapshot filters Hydra collection root and exclude paths, omits invisible meshes and rejects material-tag partitions that do not yet have an exact native mapping. Polygonal geometry and immutable MaterialGraph nodes are copied into a SceneIR snapshot before session upload.

Hydra affine transforms are baked into copied mesh data so shear is not weakened into an approximate TRS decomposition. Positions use the full affine transform, normals use the inverse transpose, tangents are transformed and re-orthogonalized, and negative determinants reverse triangle winding. Singular, projective and non-finite inputs remain fail-loud. A single-sided authored mesh produces an explicit loss entry because the current native traversal evaluates both triangle sides.

## Camera, AOV and readiness contract

The implemented camera boundary is perspective projection with centered aperture. Focal length and aperture determine vertical field of view; focus distance and f-stop map to the existing native depth-of-field camera. Orthographic projection, aperture offsets and invalid lens or transform data reject instead of being approximated.

`HdURERenderBuffer` accepts depth-one, non-multisampled Float32 scalar/vector formats. The pass writes:

- color/Beauty;
- normal;
- depth and cameraDepth;
- albedo;
- UV;
- motionVector.

Channel expansion is explicit, including alpha one for RGB-to-RGBA output. Writes while mapped reject. Render statistics expose SPP, convergence, snapshot loss count and the last execution error.

The plugin returns supported only in a CUDA-enabled Hydra build and only when Hydra permits GPU rendering. CUDA-off builds retain the U.1-U.4 adapter and validation surface but remain non-ready. Unsupported backend settings reject; there is no silent fallback to Vulkan, D3D12 or a CPU integrator.

## Current limits

U.5 does not add subdivision, Hydra instancing, authored light SPrims, orthographic cameras, arbitrary USDShade nodes, stage/file ingestion or time-sampled stage orchestration. The current environment illumination is the existing native renderer environment. These limits are not represented as general DCC viewport support.

## Verification

Run the actual OpenUSD and CUDA gate:

```powershell
.\scripts\run_phase_u5_hydra_render_gate.ps1 `
  -OpenUsdRoot "C:\Program Files\Side Effects Software\Houdini 21.0.671"

.\scripts\check_phase_u5_static.ps1
```

The dedicated gate builds the plugin with the memory-aware CUDA compile pool, runs six Hydra tests, performs a real 4×4 progressive GPU render, checks camera accumulation reset and finite nonzero output, installs the module and rediscovers it from the installed plugin layout. The closure baseline uses OpenUSD 25.05 from Houdini 21.0.671, Visual Studio 2026 18.8.2, MSVC 19.51, CUDA 13.3, Windows SDK 10.0.28000 and native `sm_120` code.
