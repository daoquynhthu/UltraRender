# Phase U.6 USDA Export Adapter

Last reviewed: 2026-08-01

U.6 adds a bounded, deterministic adapter from a validated URE native scene archive to ASCII USD. The native `.ure`, `.urescene` or `.urepkg` source remains authoritative; the emitted `.usda` is an interoperability projection and never becomes a replacement schema.

## Input and selection contract

The C++ API accepts a validated `NativeSceneArchive` directly. The tooling and `ure_cli export` accept `.ure` and `.urescene` files, or a `.urepkg`. A single-scene package is selected automatically. A multi-scene package requires `--scene-id`; unknown scene IDs and scene IDs applied to non-package inputs fail loudly.

```powershell
ure_cli export scene.urescene -o scene.usda

ure_cli export shots.urepkg -o shot_010.usda `
  --scene-id shot_010
```

The serializer is SDK-free. OpenUSD is used only by the dedicated validation executable to parse and resolve the generated layer.

## Exported projection

The lossless subset contains:

- deterministic root metadata with native document identity and semantic hash;
- native materials projected to a bounded `UsdPreviewSurface` parameter set;
- triangle meshes stored once under indexed prototypes and reused through internal instance references;
- per-instance transform, visibility, material binding and rigid-body metadata;
- analytic spheres with material bindings;
- a perspective camera with transform, field of view, focus distance and depth-of-field conversion;
- render dimensions, SPP and background metadata.

Names from the source are retained as metadata. USD prim identifiers are canonical index-based names so arbitrary native names cannot produce invalid paths or collisions. Numeric output uses locale-independent round-trip formatting, and repeated export of the same archive is byte-identical.

## Loss policy

`Strict` is the default. It emits no USDA when any adapter warning or error exists. `AllowDocumentedLoss` permits warnings only and requires an explicit JSON loss-report path before a file can be published. Adapter errors remain non-overridable.

Documented warning cases include the bounded Preview Surface reduction, unused image/texture resources, background lighting semantics and quad lights represented as emissive meshes. Native procedural graphs, solver or simulation contracts, resource catalogs, opaque chunks, participating media and other semantics without an exact projection remain hard errors.

The writer publishes files through same-directory temporary files and atomic replacement. For a lossy export, the structured report is published before the USDA artifact, preventing creation of a newly undocumented lossy output.

## Deliberate limits

U.6 does not add OpenUSD stage/file ingestion, arbitrary USDShade graph export, animation/time samples, Hydra subdivision or Hydra instancing. Mesh reuse in the exported USDA is an offline adapter feature and does not imply Hydra instancer support in the render delegate. Spectral resources, native MaterialGraph semantics, volume resources, solver state and simulation state remain authoritative only in the native source unless an exact USD mapping is defined later.

## Verification

Run the actual OpenUSD export gate and static audit:

```powershell
.\scripts\run_phase_u6_usda_export_gate.ps1 `
  -OpenUsdRoot "C:\Program Files\Side Effects Software\Houdini 21.0.671"

.\scripts\check_phase_u6_static.ps1
```

The host gate covers deterministic output, atomic file publication, strict and documented-loss policies, direct tooling, real CLI execution, single- and multi-scene package selection, and unsupported-feature rejection. The actual OpenUSD test imports the generated layer and resolves shared mesh instances, material bindings, Preview Surface parameters, transforms, an analytic sphere and the camera.

The unified closure gate is:

```powershell
.\scripts\run_phase_u_validation_suite.ps1 `
  -OpenUsdRoot "C:\Program Files\Side Effects Software\Houdini 21.0.671"
```
