# Phase Q Native Scene Format Foundation

## Status and scope

This document is the Q.0 capability-ownership audit and the Q.1-Q.3 encoding contract. It records what UltraRender owns independently of glTF, USD, MaterialX, CUDA, and future backends.

Implemented in this foundation:

- backend-neutral identities, versions, conventions, feature and extension declarations;
- resource and package manifests;
- canonical `.ure` JSON projection;
- FlatBuffers 25.12.19 metadata for `.urescene` and `.urepkg`;
- checked binary header/chunk-directory I/O;
- SHA-256 physical identity and encoding-independent semantic identity;
- structured validation and the empty, single-scene, and shared-resource fixtures.
- complete current SceneIR source serialization with stable object identities;
- `URIG` typed source graphs plus independently content-addressed `URMS` mesh and `URMI` Mie resources;
- bit-preserving binary and canonical exploded text roundtrips, deep-freeze ownership, safe atomic file I/O, and a retained full-scene fixture;
- a loader boundary verified by the existing `GpuSceneCompiler` without linking `ure_sceneio` to `ure_core` or CUDA.

Not implemented here: procedural execution, script hooks, generalized Q.6 spectral resources, solver lowering, CLI pack/unpack, adapter conversion, production compression, compiled farm caches, signatures, or encryption. Those remain Q.4-Q.12.

## Authority and encoding

| Identity | Encoding | Authority |
|---|---|---|
| `.urescene` | Indexed little-endian binary container | Production scene source |
| `.ure` | Canonical UTF-8 JSON | Lossless semantic projection and exploded manifest |
| `.urepkg` | Indexed little-endian binary package | Portable multi-scene and shared-resource source package |
| `.urecache` | Binary runtime/compiler artifact | Non-authoritative; excluded from semantic package identity and safe to delete |

The URE semantic model is authoritative. Encoding is replaceable. SceneIR remains the compiled internal IR. External formats are adapters and may not define native feature limits.

Large geometry, spectra, Mie tables, volume fields, animation, audio, and video are typed resource payloads. `.ure` references them by stable ID and SHA-256; it does not Base64-encode them or expand them into large JSON arrays.

## Q.0 capability ownership audit

The classification column distinguishes authoritative source semantics, runtime requests, rebuildable cache data, and adapter-only metadata. `Foundation` means Q.0-Q.2 supplies the owner and descriptor. A later Q step owns the complete typed payload or execution behavior.

| Current type / semantic | Current fields or behavior | Native owner | Classification | Delivery |
|---|---|---|---|---|
| `SceneIR` identity | Retained scene lacks a portable source identity | `scene.document` | Source | Foundation stable scene ID |
| `SceneIR.materials` | Material instances and sharing | `scene.material` | Source | Q.3/Q.6 typed payload |
| `MaterialNode` | model, base color, roughness, IOR, dispersion, eta/k, emission | `scene.material` | Source | Q.3/Q.6 typed payload |
| `MaterialNode` thin film | thickness and film IOR | `scene.material.layer` | Source | Q.6 typed payload |
| `MaterialNode` medium | density, anisotropy, phase, sigma values, Mie link | `scene.medium` | Source | Q.6 typed payload |
| `MaterialGraph` | nodes, stable node IDs, typed inputs, output root | `scene.material.graph` | Source | Q.3/Q.6 typed payload |
| `MaterialGraphNodeKind` | constants, textures, arithmetic, mix, checker, noise, BSDFs, layer, output | `scene.material.graph.node` | Source | Q.6 registry |
| Material presets | preset origin and parameter provenance | `scene.material.preset` | Source | Q.6 typed provenance |
| `SceneIR.meshes` | mesh identity and shared mesh payload | `scene.geometry.mesh` | Source | Foundation resource; Q.3 payload |
| `SceneIR.spheres` | analytic sphere center/radius/material | `scene.geometry.analytic` | Source | Q.3 typed payload |
| `SceneIR.instances` | geometry/material links, transform, rigid-body config | `scene.graph.instance` | Source | Q.3 typed payload |
| Entity hierarchy | parent/child identity absent from glTF-derived IR | `scene.graph` | Source | Q.3 typed payload |
| Visibility, categories, AOV tags | planned per-object semantics | `scene.graph.visibility` | Source extension slot | Q.3/Q.7 |
| `ImageResource` | URI and linear/sRGB interpretation | `scene.resource.image` | Source | Foundation resource; Q.3 payload |
| `TextureResource` | image binding and UV set | `scene.resource.texture` | Source | Foundation resource; Q.6 payload |
| Source-sample spectral grids | wavelength/value carrier, not display RGB | `scene.resource.spectral` | Source | Foundation resource; Q.6 payload |
| `SceneIR.quad_lights` | corner, edge vectors, emissive material | `scene.light.area` | Source | Q.3 typed payload |
| Environment/background | radiance and direct-sampling request | `scene.light.environment` | Source/runtime request | Q.3/Q.7 |
| Light tree and guide tables | compiled selection/PDF structures | `cache.light_sampling` | Rebuildable cache | Q.11 only |
| Camera | pose, projection, film dimensions | `scene.camera` | Source | Q.3 typed payload |
| `SceneIR.width/height/spp` | legacy render request fields | `render.output` | Runtime request | Q.7 migration target |
| `SceneIR.physics` | rigid-body scene configuration | `scene.physics` | Source extension domain | Q.8 typed subset |
| `SceneIR` global medium | density, phase, Mie resource, sigma, max distance | `scene.medium.world` | Source | Q.6 typed payload |
| `MiePhaseResource` | wavelength/cos-theta phase and CDF tables | `scene.medium.mie_phase` | Source resource | Foundation descriptor; Q.6 payload |
| `MiePhaseResource` cross sections | scattering, extinction, absorption, asymmetry | `scene.medium.mie_phase.optics` | Source resource | Q.6 typed payload |
| `MieGenerationConfig` | optical samples, radius distribution, quadrature and convergence limits | `build.procedural.mie` | Deterministic build source | Q.4/Q.6 |
| Mie provenance/hashes | generator provenance, source hash, content hash | `scene.resource.provenance` | Source/provenance | Foundation hash; Q.6 payload |
| `RenderConfig.queue_capacity` | wavefront allocation request | `render.integrator.queue` | Runtime request | Q.7 |
| `RenderConfig.max_trace_depth` | transport depth | `render.integrator.path` | Runtime request | Q.7 |
| spectral domain bins | high-resolution source domain | `render.spectral.domain` | Runtime request | Foundation feature; Q.6/Q.7 |
| spectral packet lanes | GPU packet sampling width | `render.spectral.packet` | Runtime request | Foundation feature; Q.7 |
| spectral memory budget | resident spectral resource limit | `render.spectral.budget` | Runtime request | Foundation budget; Q.7 |
| spectral sampling mode | packet, sampled, stratified, importance, farm shard | `render.spectral.sampling` | Runtime request | Q.7 |
| `IntegratorRuntimeConfig` | mode, sampler, quality preset, biased reuse declaration | `render.integrator` | Runtime request | Foundation feature; Q.7 |
| `PathGuidingConfig` | mixture, learning, decay, spatial/directional resolution, budget | `render.integrator.path_guiding` | Runtime request | Q.7 |
| guiding learned state | product distribution and epoch | `cache.integrator.path_guiding` | Rebuildable cache | Q.11 |
| `RestirDirectConfig` | reuse modes, unbiased declaration, history and target floor | `render.integrator.restir_di` | Runtime request | Q.7 |
| `SpecularManifoldConfig` | event count, tolerance, Newton iterations | `render.integrator.specular_manifold` | Runtime request | Q.7 |
| `MltIntegratorConfig` | chains, mutations, step distribution and seed | `render.integrator.mlt` | Runtime request | Q.7 |
| workgroup/rays/pass fields | backend execution tuning | `render.backend.tuning` | Runtime hint, never source physics | Q.7/Q.10 |
| multi-GPU count | local execution topology | `render.backend.device_policy` | Runtime request | Q.7 |
| `WaveOpticsConfig` | radiometric, diffraction, coherent and partial-coherent modes | `render.wave` | Runtime request | Foundation feature; Q.7 |
| wave feature switches | diffractive material, fluorescence, manifold, local full-wave | `render.wave.feature` | Runtime request | Q.7 |
| preview degradation flag | explicit approximation consent | `render.wave.degradation_policy` | Runtime request | Q.7 |
| OPL/Jones/complex fields | coherent transport semantics | `scene.wave.field` | Source extension domain | Q.6/Q.7 |
| aperture/grating/DOE | diffractive source operators | `scene.wave.operator` | Source extension domain | Q.6/Q.7 |
| BLAS/TLAS hints | build/refit/dynamic intent | `render.acceleration` | Source/runtime request | Q.7 and Phase V |
| backend selection | CUDA, Vulkan, DXR, future providers | `render.backend` | Capability request | Q.7 and Phase T |
| backend handles | pointers, API objects, launch handles | no source owner | Rebuildable runtime state | Explicitly forbidden |
| `SceneDiff` replacement | whole retained-scene replacement | `scene.mutation.replace` | Session mutation | Q.3/Q.9 |
| `SceneDiff` camera | camera mutation and accumulation reset | `scene.mutation.camera` | Session mutation | Q.3/Q.9 |
| `SceneDiff` transforms | stable instance transform mutation | `scene.mutation.transform` | Session mutation | Q.3/Q.9 |
| `SceneDiff` material edits | stable material mutation | `scene.mutation.material` | Session mutation | Q.3/Q.9 |
| `SceneDiff` insert/remove | topology mutation scope | `scene.mutation.topology` | Session mutation | Q.3/Q.9 |
| `DistributedShardMetadata` spectral | shard ID/count, domain range, wavelength bounds/PDF | `render.distributed.spectral_shard` | Farm request/result metadata | Q.7/Q.11 |
| `DistributedShardMetadata` frame | frame index/count | `render.distributed.frame_shard` | Farm request/result metadata | Q.7/Q.11 |
| distributed sample range | node and sample partition contract | `render.distributed.sample_range` | Farm request | Q.7/Q.11 |
| framebuffer merge metadata | samples, dimensions, compatible shard identity | `render.distributed.merge` | Farm result | Q.7/Q.11 |
| coherent merge | complex-field merge mode and compatibility | `render.distributed.coherent_merge` | Farm request/result | Q.7/Q.11 |
| procedural scatter/instance/spectrum/light rig | planned deterministic graph | `build.procedural` | Build source | Q.4 reserved required domain |
| script build hook | interpreter, dependency lock, sandbox, inputs/outputs | `build.script` | Explicit opt-in build source | Q.5 reserved domain |
| physics coupling | solver version, time step, resources and channels | `scene.physics` | Source extension domain | Q.8 |
| acoustic coupling | emitters, listeners, materials, geometry coupling | `scene.acoustic` | Source extension domain | Q.8 |
| animation tracks | transform/camera time samples and shutter contract | `scene.animation` | Source extension domain | Q.3/Q.6 |
| spectral/video streams | frame rate, time sampling, spectral texture stream | `scene.video` | Source extension domain | Q.6 |
| fixture metrics | metric names, tolerances, expected failures and benchmark tags | `validation.contract` | Source validation contract | Q.12 |
| glTF/USD/MaterialX metadata | external names and adapter loss report | `adapter.metadata` | Adapter-only | Q.10 |

Every audited capability has a native typed owner or a versioned extension domain. No field requires glTF, USD, or MaterialX to define its meaning.

## Identities and package layout

Stable IDs are ASCII strings matching `[A-Za-z0-9][A-Za-z0-9._/-]{0,254}` with no empty, `.`, or `..` segment and no leading, trailing, or repeated slash. Human display names are separate UTF-8 NFC data. Content hashes are exactly 64 lowercase SHA-256 hexadecimal characters.

The unpacked logical layout is:

```text
manifest
scenes/
resources/
validation/
provenance/
cache/
```

Portable resource references are normalized package-relative paths or `ure+sha256://<hash>`. Absolute, drive, UNC, backslash, traversal, symlink, and reparse-point escapes are rejected before reading outside the package root.

## Version and migration contract

Container and semantic versions are separate unsigned major/minor pairs. Version 1 identities are `ure.container.scene/1.0`, `ure.container.package/1.0`, and `ure.scene/1.0`.

- container major mismatch fails;
- newer container minor loads only when flags, codecs, and required chunks are understood;
- semantic major mismatch requires an explicit migration;
- newer semantic minor loads only when required capabilities remain supported;
- writers never silently downgrade;
- removed identifiers remain reserved;
- migrations record source/target versions, tool ID/version, input/output hashes, and lossiness.

FlatBuffers fields carry explicit numeric IDs. `schemas/ure_native_v1.baseline.fbs` freezes version 1 and `flatc --conform` gates compatible evolution.

## Convention contract

Version 1 production scenes use metres, seconds, kilograms, radians, vacuum nanometres, right-handed `+Y` up coordinates, camera `-Z` forward, and linear radiometric color quantities. Unit or axis conversion occurs during import/build, not inside runtime kernels.

## Features and extensions

URE-owned features use `ure.*` canonical names. `required` unsupported data fails before SceneIR compilation. `optional` and `advisory` declarations remain attached with structured warnings. Dependencies are explicit and acyclic.

Third-party extension names cannot claim the reserved `ure.*` namespace. Unknown required extensions fail. Unknown optional extensions preserve their opaque payload bytes. Binary tools retain unknown optional chunks; a writer unable to preserve them must refuse overwrite.

## Binary safety contract

The v1 container uses a 128-byte fixed header, little-endian scalar encoding, 16-byte-aligned terminal directory, and independently aligned chunks. C++ structs are never written directly. Readers validate magic, endian marker, version, reserved bytes, flags, directory bounds, entry count, offsets, lengths, alignment, overlap, dependency graph, codec, SHA-256, decompression ratio, stored-byte budget, and resident/uncompressed budget before payload allocation.

Core metadata is FlatBuffers with identifier `UREM`. Q.3 adds `URIG` for the current source graph, `URMS` for mesh vertices/indices, and `URMI` for immutable Mie tables. The generated C++ headers and runtime are pinned to 25.12.19. Large payloads stay outside metadata and graph buffers.

## Q.3 SceneIR source contract

`NativeSceneArchive` owns a deep-frozen SceneIR, aligned stable source-ID tables, the Q.2 scene document, and preserved unknown optional chunks. Default IDs use eight-digit indices such as `material/00000000`; loaded IDs survive rewrite, and registry-size or reference mutations fail instead of silently renumbering objects.

The binary layout contains one metadata chunk, one typed source-graph chunk, and sorted content-addressed mesh/Mie chunks. Equal immutable payloads coalesce while distinct registered materials retain identity. The graph covers every current material and MaterialGraph field, images/textures, mesh records and tangents, instances and rigid-body fields, spheres, quad lights, camera, current physics/fluid fields, global/material media, spectral SPD references, Mie resources, and the current width/height/SPP compatibility fields.

The exploded `.ure` projection stores the complete graph as canonical JSON and keeps mesh/Mie arrays in separate typed files. Both encodings reconstruct shared registered references, preserve finite float bits including signed zero, and produce the same SceneIR semantic hash; signed zero is normalized only for semantic identity. File helpers use bounded reads, package-root containment, and same-directory atomic replacement.

Validation is verifier-first and fail-loud. It checks identity grammar and uniqueness, registered references, enum domains, MaterialGraph cycles/references, typed-payload identifiers and hashes, mesh topology/index bounds, Mie dimensions/normalization/CDF/optical domains, finite and physical scalar domains, resource budgets, missing or unreferenced payloads, and optional-chunk preservation before returning SceneIR to the compiler.

## Semantic identity

Semantic hashes traverse schema fields in a fixed order with little-endian integers and length-prefixed UTF-8 strings. They include stable IDs, versions, conventions, declared capabilities, dependencies, and authoritative resource hashes. They exclude physical chunk offsets, padding, compression choice, text whitespace, provenance timestamps, and `.urecache` descriptors. Thus text and binary encodings of the same document share an identity, and removing caches leaves a package valid with the same semantic hash.

## Diagnostics

Validation returns diagnostics and never prints directly. Stable code families are:

| Family | Meaning |
|---|---|
| `URE-Q-ID-*` | stable or duplicate identity |
| `URE-Q-PATH-*` | URI and package-root safety |
| `URE-Q-HASH-*` | physical or semantic hash |
| `URE-Q-VERSION-*` | compatibility and migration |
| `URE-Q-CONVENTION-*` | units, axes, spectral/color convention |
| `URE-Q-FEATURE-*` | feature support and parameters |
| `URE-Q-EXT-*` | extension support and preservation |
| `URE-Q-DEP-*` | missing or cyclic dependencies |
| `URE-Q-BUDGET-*` | checked arithmetic and allocation budgets |
| `URE-Q-TEXT-*` | canonical JSON structure |
| `URE-Q-METADATA-*` | FlatBuffers identity and verification |
| `URE-Q-CONTAINER-*` | binary header, directory, ranges, and alignment |
| `URE-Q-CHUNK-*` | chunk type support |
| `URE-Q-CODEC-*` | compression support |
| `URE-Q3-ID/REF-*` | Q.3 source identities and registered references |
| `URE-Q3-GRAPH/MESH/MIE-*` | typed SceneIR graph and physical resource validation |
| `URE-Q3-TEXT/CONTAINER/FILE-*` | Q.3 projection, composition, and atomic file I/O |

Structural and budget errors prevent a validated value. Unsupported optional data emits warnings and remains preserved.
