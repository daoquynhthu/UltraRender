# Phase U.3 Hydra Mesh RPrim

Last reviewed: 2026-07-29

U.3 adds the first scene primitive to the optional OpenUSD component. `HdUREMesh` is an actual `HdMesh` RPrim and lowers Hydra polygonal topology and primvars into an immutable SceneIR `MeshResource`. USD/Hydra remains an adapter: the resulting native geometry is the retained boundary consumed by later material and session phases.

## Geometry mapping

The mesh RPrim uses OpenUSD `HdMeshUtil` for polygon triangulation. Each triangle corner becomes one native vertex so face-varying attributes and seams remain exact. The accepted inputs are:

- float or double point arrays;
- float or double normals;
- `st`, with `uv` as the secondary spelling;
- constant, uniform, vertex, varying and face-varying interpolation;
- indexed primvars resolved before triangulation;
- finite affine transforms, material paths, visibility and double-sided state.

Generated face normals are used only when no normal primvar exists. Native geometry revisions change after accepted geometry or instance-state synchronization. Transform-only changes retain the immutable geometry allocation.

## Rejection boundary

The adapter removes a rejected mesh from retained render state and records the current rejection count and diagnostic. It rejects:

- subdivision schemes until an exact tessellation contract exists;
- Hydra instancing until a dedicated native instance mapping exists;
- empty, incomplete, out-of-range or degenerate topology;
- unsupported primvar types or interpolation;
- non-finite geometry, attributes or transforms;
- projective transforms and meshes exceeding the native signed-index domain.

U.3 introduced `HdPrimTypeTokens->mesh`; U.4 subsequently added the material SPrim. At U.3 closure BPrims were absent and the plugin remained non-ready. U.5 now consumes this retained mesh state through a real progressive render path, while the U.3 subdivision and instancing rejection boundary remains unchanged.

## Update and lifetime model

`HdURERenderParam` owns a thread-safe map of retained mesh records. Geometry, transform, material binding, visibility and double-sided dirty bits update the corresponding native state. `Finalize()` removes the record. A repaired mesh clears its rejection entry, while unrelated valid updates do not erase diagnostics for other rejected meshes.

## Verification

Run the actual OpenUSD gate:

```powershell
.\scripts\run_phase_u3_hydra_mesh_gate.ps1 `
  -OpenUsdRoot "C:\Program Files\Side Effects Software\Houdini 21.0.671"
```

The isolated SDK build verifies plugin discovery in build and install layouts, actual `HdRenderIndex` RPrim creation, polygon triangulation, indexed face-varying UVs, normals, metadata retention, transform-only reuse, point updates, revision ordering and subdivision rejection.

Run the static boundary audit:

```powershell
.\scripts\check_phase_u3_static.ps1
```
