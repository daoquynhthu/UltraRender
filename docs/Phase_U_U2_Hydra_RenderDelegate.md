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

The adapter is disabled by default. Enabling it requires both `UR_ENABLE_HYDRA=ON` and an explicit `UR_OPENUSD_ROOT`; the build never searches DCC installations or mutates process-global SDK paths. The current verified provider is the Houdini HDK CMake package. CUDA, Vulkan, D3D12 and a window-system context are not dependencies of the Hydra module.

## Fail-loud readiness boundary

U.2 deliberately advertises no RPrim, SPrim or BPrim types. RPrim/instancer creation rejects until U.3, material and related SPrim creation rejects until U.4, and render pass/BPrim execution rejects until U.5. The plugin returns `IsSupported(false)` so a DCC cannot select an empty render delegate as if it were render-capable.

`GetRenderStats()` reports:

- `ure.hydra.render-delegate/1.0`;
- the U.1 `ure.adapter.usd-schema/1.0` native mapping boundary;
- `renderReady = false`;
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
- empty advertised prim support and non-ready state;
- generated plugin metadata, dynamic module discovery and plugin factory creation;
- discovery from both the build tree and an isolated installed plugin layout;
- actual OpenUSD/Hydra linkage without a graphics context.

`check_phase_u2_static.ps1` freezes the explicit SDK opt-in, U.1 native boundary, plugin registration and fail-loud readiness contract. U.3 owns mesh RPrims, U.4 owns USD material translation, and U.5 owns interactive render execution.
