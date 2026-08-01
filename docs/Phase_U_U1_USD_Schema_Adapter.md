# Phase U.1 USD Schema Adapter

Last reviewed: 2026-07-29

Phase U.1 defines the loss-aware boundary from authored USD semantics to UltraRender's Phase Q native scene archive. The URE native schema, resource catalog, MaterialGraph and validation pipeline remain authoritative. USD is an ecosystem adapter and does not become an alternate core scene model.

## Boundary

`ure::usd::UsdStageSnapshot` is an SDK-free normalized stage snapshot. A private OpenUSD/Hydra bridge may populate it from `UsdStage` objects, but OpenUSD types, handles and headers do not enter the installed `ure_sceneio` interface. The adapter does not invoke `usdcat`, parse USDA text itself or discover a DCC installation through ambient process state.

The U.1 path is:

```
OpenUSD bridge snapshot
    -> strict USD semantic validation
    -> URE native SceneDocument + SceneIR + resource catalog
    -> Phase Q archive validation
```

Direct `.usd`, `.usda` and `.usdc` file decoding is therefore owned by the future private OpenUSD integration, not by a second parser in UltraRender.

## Supported mapping

| USD-authored concept | Authoritative URE result |
|---|---|
| Stage `metersPerUnit` and Y/Z up axis | Meter-based SceneIR positions, radii, camera parameters and basis-converted transforms |
| Static triangle mesh with per-point normals/UVs | Native mesh resource and instance; normals are generated when absent |
| Bound analytic sphere | Native sphere |
| Selected camera | Native SceneIR camera |
| Preview-style Lambertian, metal, dielectric or light parameters | Validated URE MaterialGraph |
| `UREPhysicsAPI` mesh rigid-body values | Native instance rigid-body contract |
| `URESpectralMaterialAPI` resource binding | Phase Q resource catalog plus material-resource dependency graph |
| USD prim path | Deterministic SHA-256-derived native source identity |

Input order is normalized by prim path before archive construction. The semantic archive hash is consequently stable for equivalent authored snapshots.

## Spectral schema

The spectral adapter contract uses explicit domain and resource semantics:

```usda
over "Glass" (
    prepend apiSchemas = ["URESpectralMaterialAPI"]
)
{
    uniform int64 ure:spectral:domainBins = 1000000
    uniform int ure:spectral:packetLanes = 16
    uniform asset ure:spectral:resourceUri = @spds/glass.urespd@
    uniform string ure:spectral:contentHash = "..."
    uniform int ure:spectral:basisCount = 0
    uniform int64 ure:spectral:tileBins = 4096
}
```

`domainBins` describes the resource domain; `packetLanes` is only the transport packet hint and remains bounded by the runtime contract. URI, content digest, representation, wavelength range, sample/basis/tile counts, value bounds and memory estimates enter `NativeResourceCatalog`. The adapter never reduces this information to a legacy `bands` field or a fixed-size ray payload.

## Fail-loud boundary

U.1 rejects:

- unsupported required API schemas;
- animated stages that need time-sampled stage orchestration beyond the U.5 retained-session bridge;
- non-triangle topology, invalid indices, degenerate faces and non-finite geometry;
- xform stacks that cannot be represented as affine translation/rotation/scale;
- missing material bindings and duplicate or invalid prim paths;
- unsupported material models or analytic-sphere rigid bodies;
- invalid, weak or over-budget spectral resources;
- archives that fail Phase Q resource or SceneIR validation.

Unknown optional schemas produce a standardized adapter warning and keep the result exportable. Multiple cameras preserve only the selected static camera and report the loss. U.4 subsequently added bounded USDShade conversion, U.5 added retained Hydra progressive execution, and U.6 added the strict native-to-USDA projection. Time-sampled stage orchestration remains unavailable.

## Verification

- `test_usd_schema_adapter` covers unit/up-axis conversion, deterministic identities, native binary roundtrip, material/resource mapping and negative boundaries.
- `check_phase_u1_static.ps1` freezes the SDK-free public surface, strong spectral attribute set, Phase Q validation path and fail-loud cases.
- `test_public_surface_sdk_free` compiles the installed adapter header without OpenUSD, CUDA, Vulkan or D3D12 SDK headers.
