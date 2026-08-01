# Phase U.4 Hydra Material Conversion

Last reviewed: 2026-08-01

U.4 adds an actual Hydra material SPrim and converts OpenUSD material networks into retained URE `MaterialGraph` resources. OpenUSD types remain private to `ure_hydra`; the resulting immutable `MaterialNode` is the native boundary used by later session integration.

## Network boundary

`HdUREMaterial` accepts both `HdMaterialNetwork2` and the legacy `HdMaterialNetworkMap`, normalizing the latter through OpenUSD before conversion. The delegate advertises only the material SPrim in addition to the U.3 mesh RPrim.

Two authored paths are supported:

- URE adapter nodes such as `URE_constant_color`, `URE_texture2d`, `URE_bsdf_lambert`, `URE_bsdf_metal`, `URE_bsdf_dielectric`, `URE_bsdf_mix`, `URE_bsdf_layer` and `URE_output_surface` map directly to existing MaterialGraph kinds;
- `UsdPreviewSurface`, `UsdUVTexture` and `UsdPrimvarReader_*` map through a bounded Preview Surface adapter.

Texture assets preserve resolved or authored URI, explicit linear/sRGB interpretation and `st`/`uv` set identity. USD `auto` color space is resolved to sRGB with a structured loss entry because the current native image resource has no deferred resolver. Material updates replace the immutable graph and advance a retained revision. Finalization removes the material record.

## Loss report and rejection

Every accepted or rejected material retains a structured report with severity, stable code, source path and message. The adapter never silently substitutes unknown nodes or connected inputs.

The current Preview Surface mapping reports its explicit physical boundary:

- non-metal surfaces use the native dielectric-layer over Lambert model;
- fully metallic surfaces use the native spectral metal model;
- mixed metallic surfaces use the native Lambert/metal mix and report the omitted separate dielectric-specular lobe;
- unsupported texture channel, scale, bias, wrap or fallback semantics are reported.

Opacity, combined emission, occlusion, normal binding, clearcoat, specular workflow, unsupported terminals, cycles, invalid parameter ranges and unknown reachable nodes reject the material. URE adapter graphs still pass native cycle/reference validation before retention.

## Readiness boundary

U.4 does not create a Hydra render pass. The plugin remains `IsSupported(false)`, BPrims remain unavailable and no material conversion is described as interactive rendering. U.5 owns progressive session execution and synchronization.

## Verification

Run:

```powershell
.\scripts\run_phase_u4_hydra_material_gate.ps1 `
  -OpenUsdRoot "C:\Program Files\Side Effects Software\Houdini 21.0.671"

.\scripts\check_phase_u4_static.ps1
```

The actual OpenUSD gate covers plugin discovery and install layout, mesh compatibility, native URE graphs, Preview Surface and UV texture conversion, legacy-network normalization, dynamic material revision, structured loss reporting, unsupported-node rejection and finalization.

The closure baseline uses Visual Studio 2026 18.8.2, MSVC 19.51 and Windows SDK 10.0.28000 for the four-test OpenUSD build. The independent main Release tree compiles native `sm_120` code with CUDA 13.3 and passes all 57 registered tests.
