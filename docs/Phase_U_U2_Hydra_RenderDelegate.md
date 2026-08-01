# Phase U.2 Hydra RenderDelegate Foundation

Last reviewed: 2026-07-29

U.2 adds a real OpenUSD Hydra plugin boundary without claiming a functioning renderer before the RPrim and session phases exist. The implementation compiles against OpenUSD 25.05 from Houdini 21.0.671, is dynamically discoverable through `plugInfo.json`, and keeps all OpenUSD SDK types inside the optional `ure_hydra` component.

## Architecture

`HdURE` derives directly from `HdRenderDelegate`. It owns:

- an `HdResourceRegistry`;
- a stable `HdRenderParam`;
- namespaced `ure:*` render settings;
- native-schema and delegate identity statistics;
- a monotonic resource commit epoch.

The plugin factory derives from `HdRendererPlugin` and constructs `HdURE` with or without initial settings. `TF_REGISTRY_FUNCTION(TfType)` and the generated plugin resource make the module discoverable by `HdRendererPluginRegistry`.

The optional install layout keeps `ure_hydra` and its `resources/plugInfo.json` together under `lib/UltraRender/hydra/ure_hydra`; adding that resources directory to `PXR_PLUGINPATH_NAME` is sufficient for discovery.

The adapter is disabled by default. Enabling it requires both `UR_ENABLE_HYDRA=ON` and an explicit `UR_OPENUSD_ROOT`; the build never searches DCC installations or mutates process-global SDK paths. The current verified provider is the Houdini HDK CMake package. At U.2 the module was SDK-only; U.5 optionally links the CUDA core for actual rendering while still requiring no graphics or window-system context.

## Fail-loud readiness boundary

At U.2 closure the delegate advertised no prim types and returned `IsSupported(false)`. U.3 and U.4 added mesh and material prims. U.5 subsequently adds camera/render-buffer/render-pass execution and advertises readiness only in a CUDA-enabled build; CUDA-off adapter builds remain non-ready. Instancing remains rejected until it has a dedicated native mapping.

`GetRenderStats()` reports:

- `ure.hydra.render-delegate/1.0`;
- the U.1 `ure.adapter.usd-schema/1.0` native mapping boundary;
- phase-appropriate `renderReady`, progressive SPP/convergence and execution diagnostics;
- the current resource commit epoch.

This keeps USD/Hydra subordinate to the Phase Q native schema and prevents a discoverable skeleton from becoming a silent black-frame path.

## Windows plugin registration

OpenUSD registration uses the `.pxrctor` section. MSVC's Release `/Zc:inline` optimization can remove the apparently unreferenced registration record, so the plugin translation unit is compiled with `/Zc:inline-`. The discovery test loads the built module through `HdRendererPluginRegistry`; a direct delegate-only unit test would not catch this packaging failure.

## Verification

Run:

```powershell
.\scripts\run_phase_u2_hydra_gate.ps1 `
  -OpenUsdRoot "C:\Program Files\Side Effects Software\Houdini 21.0.671"
```

The isolated SDK build verifies:

- `HdURE` inheritance, settings, resource ownership, statistics and commit lifecycle;
- the U.2 phase-bounded prim capability and CUDA-off non-ready state;
- generated plugin metadata, dynamic module discovery and plugin factory creation;
- discovery from both the build tree and an isolated installed plugin layout;
- actual OpenUSD/Hydra linkage without a graphics context.

`check_phase_u2_static.ps1` freezes the explicit SDK opt-in, U.1 native boundary, plugin registration and original fail-loud readiness contract. U.3 owns mesh RPrims, U.4 owns USD material translation, and the current U.5 execution contract is documented separately.
