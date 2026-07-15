# Phase Q.3 Native SceneIR Serialization Design

> Archive status: historical design record. Phase Q is complete; use `PLAN.md`, `STATUS.md`, and current source/tests for present state.

## Scope

This specification covers Phase Q.3 only: lossless source serialization of the current `SceneIR`, deterministic reconstruction, and the loader boundary consumed by the existing `GpuSceneCompiler`.

Q.3 serializes the semantics that exist today:

- materials and every current `MaterialGraph` node kind;
- image and texture resources;
- meshes and mesh payloads;
- instances, spheres, and quad lights;
- camera, world medium, background, render-request compatibility fields, and current physics fields;
- spectral material SPD references;
- current Mie phase resources.

Q.3 does not introduce the general basis/tiled/video spectral resource system from Q.6, solver and backend requests from Q.7, expanded physics/acoustic models from Q.8, CLI commands from Q.9, adapter migration from Q.10, or compiled farm caches from Q.11.

`gpu::SpectralResource` and `gpu::HostSpectralResource` are compiled runtime representations and are not serialized as source objects. Q.3 preserves their current authoritative inputs through image/texture records, spectral material SPD references, medium coefficients, and Mie resources. Q.6 later introduces the general native spectral resource source schema.

## Decision Summary

UltraRender will serialize the current SceneIR through a URE-owned source graph with stable object IDs and independently typed resource chunks. It will not place the entire scene into one giant FlatBuffer and will not persist C++ object layouts or pointer values.

The production representation is:

```text
.urescene container
├── metadata            URE Q.0-Q.2 SceneDocument metadata
├── scene_graph         Q.3 typed source graph
├── mesh/*              independently addressable typed mesh payloads
├── mie/*               independently addressable typed Mie payloads
└── extension/*         preserved optional chunks
```

The exploded `.ure` representation contains the same source graph as canonical JSON and keeps mesh/Mie payloads as separate typed binary resources. Both encodings reconstruct the same SceneIR and produce the same SceneIR semantic hash.

## Rejected Alternatives

### Monolithic FlatBuffer

Embedding every mesh and Mie array inside the scene graph would be straightforward, but it would recreate a monolithic asset, force unrelated large arrays into the metadata traversal domain, and weaken chunk-level lazy loading and resource sharing. It is rejected.

### C++ object snapshot

Persisting `SceneIR`, `shared_ptr`, or container memory directly would depend on compiler ABI, standard library layout, pointer width, and process addresses. It would not be portable or migratable. It is rejected.

### Generic property bag

A generic string-keyed object tree could represent current fields but would discard strong schema evolution, enum validation, reference integrity, and direct generated accessors. It is rejected as the authoritative path. Unknown optional extensions remain the only opaque property domain.

## Public API Boundary

Q.3 adds a scene-I/O layer that depends on `ure_types` SceneIR but remains independent of `ure_core` and CUDA.

```cpp
namespace ure::native_scene {

struct NativeSceneSourceIds {
    std::vector<std::string> materials;
    std::vector<std::string> meshes;
    std::vector<std::string> images;
    std::vector<std::string> textures;
    std::vector<std::string> instances;
    std::vector<std::string> spheres;
    std::vector<std::string> quad_lights;
};

struct NativeSceneArchive {
    SceneDocument document;
    scene_ir::SceneIR scene;
    NativeSceneSourceIds source_ids;
    std::vector<ContainerChunk> preserved_optional_chunks;
};

struct ExplodedSceneArchive {
    std::string manifest;
    std::vector<NamedResourcePayload> resources;
};

NativeSceneArchive make_native_scene_archive(SceneDocument document,
                                             const scene_ir::SceneIR& scene);

std::vector<std::uint8_t> write_scene_ir_binary(const NativeSceneArchive& archive);
LoadResult<NativeSceneArchive> read_scene_ir_binary(
    std::span<const std::uint8_t> bytes,
    const CapabilityRegistry& registry,
    const ValidationLimits& limits = {});

ExplodedSceneArchive write_scene_ir_text(const NativeSceneArchive& archive);
LoadResult<NativeSceneArchive> read_scene_ir_text(
    const ExplodedSceneArchive& archive,
    const CapabilityRegistry& registry,
    const ValidationLimits& limits = {});

std::string scene_ir_semantic_hash(const NativeSceneArchive& archive);
ValidationReport validate_scene_ir_archive(const NativeSceneArchive& archive,
                                           const ValidationLimits& limits = {});

void save_native_scene(const std::filesystem::path& path,
                       const NativeSceneArchive& archive);
LoadResult<NativeSceneArchive> load_native_scene(
    const std::filesystem::path& path,
    const CapabilityRegistry& registry,
    const ValidationLimits& limits = {});

}
```

`NamedResourcePayload` contains a stable resource ID, a Q.1 `ResourceDescriptor`, and owned payload bytes. The archive owns all data needed to rewrite unknown optional chunks.

`make_native_scene_archive` deep-freezes the current SceneIR object graph into owned values. Mutating the caller's source objects after archive creation cannot change serialization output.

## Stable Identity Model

Current SceneIR uses vector position, names, and `shared_ptr` references rather than persistent source IDs. Q.3 introduces an aligned identity table without adding serialization concerns to runtime SceneIR types.

Default IDs are deterministic:

| Object | Generated ID |
|---|---|
| Material | `material/<zero-padded-index>` |
| Mesh record | `mesh/<zero-padded-index>` |
| Image | `image/<zero-padded-index>` |
| Texture | `texture/<zero-padded-index>` |
| Instance | `instance/<zero-padded-index>` |
| Sphere | `sphere/<zero-padded-index>` |
| Quad light | `light/quad/<zero-padded-index>` |

Generated width is fixed at eight decimal digits in version 1. Display names remain independent data and do not become IDs.

When an archive is loaded, its source IDs are retained and reused on rewrite. Identity-table lengths must match the corresponding SceneIR vectors. A vector mutation without an explicit identity-table update fails rather than silently renumbering existing objects.

Material, mesh-record, image, and texture references must point to objects registered in the corresponding SceneIR vector. Unregistered references fail with a structured diagnostic. This prevents hidden reachable objects from receiving unstable implicit identities.

Mie resources have no central SceneIR registry. They are immutable physical resources and use content-addressed IDs derived from the SHA-256 hash of the Q.3 typed Mie payload. Equal physical content coalesces to one resource; every reconstructed reference shares the same immutable instance.

Mesh payload identity is content-addressed separately from the mesh record. Two mesh records with the same physical payload may reference one payload chunk while preserving distinct mesh-record IDs and display names.

## Typed Schemas

Q.3 adds three explicit FlatBuffers schemas and checked-in generated C++ headers:

| Schema | Identifier | Responsibility |
|---|---|---|
| `ure_scene_ir_v1.fbs` | `URIG` | source graph, object records, references, scalar scene settings |
| `ure_mesh_v1.fbs` | `URMS` | one mesh payload: positions, normals, UVs, and signed 32-bit indices |
| `ure_mie_v1.fbs` | `URMI` | one current MiePhaseResource payload and its current provenance fields |

Every table field has an explicit numeric ID. Baseline schemas freeze version 1 and are checked with `flatc --conform`. Normal builds use checked-in generated headers and do not run `flatc`.

Large payloads are not nested inside `URIG`. The graph stores stable resource IDs and required SHA-256 hashes.

## Scene Graph Coverage

### Materials

Every `MaterialNode` field is serialized:

- name and material model;
- base color, roughness, IOR, dispersion, metal eta/k;
- thin-film thickness and IOR;
- emission;
- medium density, anisotropy, phase model, scattering, and absorption;
- normal scale;
- base-color, roughness, emission, and normal texture references;
- optional spectral extension fields;
- optional material graph reference;
- optional Mie resource reference.

Enums use fixed schema numeric values that are independent of C++ declaration order. Unknown enum values fail.

### MaterialGraph

The graph preserves vector order, `MaterialGraphNodeId`, output node ID, node names, node kinds, color/value payloads, texture references, input names, input node IDs, and output socket names. All current node kinds are registered explicitly.

The loader reconstructs the graph and runs `MaterialGraph::validate`. Duplicate IDs, missing roots, missing inputs, and cycles fail before returning a SceneIR.

### Images and textures

Image name, URI, and color space are serialized. Texture name, image reference, and UV set are serialized. Images remain external resources; Q.3 does not embed decoded pixel arrays.

### Geometry and instances

Mesh records preserve their display names and reference a typed mesh payload. Mesh payloads preserve exact IEEE-754 float bits and signed 32-bit index values.

Instances preserve name, mesh/material references, position, scale, quaternion in `(w, x, y, z)` order, and all current rigid-body fields.

Spheres preserve name, center, radius, and material reference. Quad lights preserve name, corner, edge vectors, and material reference.

### Camera and scene settings

Camera position, look-at, up, FOV, aspect ratio, aperture, and focus distance are serialized. Background, global medium fields, global Mie reference, maximum medium distance, width, height, and SPP are serialized.

The current physics subset is serialized exactly: enabled state, time step, total frames, samples per frame, and every current fluid field. Q.3 treats it as the current typed subset under the `scene.physics` owner; it does not define the future solver-versioned Q.8 schema.

## Losslessness Definition

Q.3 losslessness is semantic and bit-exact for current scalar/payload values:

- vector order is preserved where SceneIR exposes vector order;
- every current field is preserved;
- finite float payloads retain IEEE-754 bits, including signed zero;
- shared registered-object references reconstruct as shared references to the same object;
- distinct registered materials remain distinct even when their values are equal;
- equal immutable Mie or mesh payload content may be coalesced by content identity;
- display names and URIs are preserved;
- optional unknown chunks are preserved byte-for-byte;
- physical chunk offsets, padding, and compression are not semantic.

NaN and infinity are rejected. They cannot participate in deterministic physical validation or canonical text.

## Canonical Text Projection

The exploded `.ure` manifest extends the Q.0-Q.2 scene document with a `scene_ir` object. The Q.2 foundation parser remains strict and unchanged; Q.3 uses a dedicated parser that knows this additional field.

The text manifest contains object records and resource descriptors, never mesh or Mie numeric arrays. `ExplodedSceneArchive.resources` carries the corresponding `URMS` and `URMI` payloads. Canonical key and set ordering follow the foundation rules. SceneIR vector order remains explicit in ordered ID arrays.

Text parsing rejects missing payloads, unreferenced required payloads, hash/length mismatch, duplicate resource IDs, and unsafe paths before constructing SceneIR objects.

## Semantic Hashing

`scene_ir_semantic_hash` includes:

- the Q.2 SceneDocument semantic hash;
- stable object IDs and SceneIR vector order;
- every current scalar and enum field;
- ordered graph nodes and inputs;
- reference IDs;
- SHA-256 identities of typed mesh and Mie payloads.

It excludes container offsets, padding, compression choice, exploded filesystem paths, provenance timestamps, and preserved cache chunks. Signed zero is normalized only in the semantic hash stream; the serialized payload retains the original float bit.

Binary and exploded text encodings of the same archive must produce the same hash. Repeated writes of one archive are byte-identical.

## Validation and Error Handling

Validation is layered and returns diagnostics without printing:

1. Q.2 document/container/resource validation;
2. source-ID table length, grammar, uniqueness, and reserved namespace validation;
3. registered-object reference integrity;
4. graph enum/reference/cycle validation;
5. typed payload identifier, verifier, SHA-256, byte-length, and budget validation;
6. mesh structural validation;
7. finite scalar and basic physical-domain validation;
8. immutable archive construction.

Mesh validation requires:

- index count divisible by three;
- every index nonnegative and smaller than the vertex count;
- normals either empty or equal to vertex count;
- UVs either empty or equal to vertex count;
- all floats finite;
- checked byte/count arithmetic before vector allocation.

Camera validation requires finite values, `0 < fov < 180`, positive aspect ratio, nonnegative aperture, and positive focus distance. Sphere radii must be positive. Scales must be finite and nonzero on every axis. Medium and material coefficients that are physically nonnegative cannot be negative.

Every parser exception becomes a stable `URE-Q3-*` diagnostic. No malformed input reaches `GpuSceneCompiler`.

## Compiler Boundary

`ure_sceneio` returns an ordinary retained `scene_ir::SceneIR`. It does not link `ure_core`, include CUDA headers, or invoke `GpuSceneCompiler`.

Compatibility is verified in tests by feeding the reconstructed SceneIR to the existing `GpuSceneCompiler`. The compiler remains the sole SceneIR-to-GPU lowering owner.

## File I/O

Byte-oriented encode/decode functions are authoritative and fully testable without filesystem access. File convenience functions perform bounded binary reads and use the Q.1 package-root containment rules for exploded resources.

Writes use a same-directory temporary file, flush and close it, then replace the destination. A failed write cannot leave a partially rewritten authoritative source at the requested path. Cross-volume replacement is not attempted.

## Test Asset

Q.3 adds a deterministic full-coverage native scene fixture under `tests/assets/native_scene/q3_full_scene/`. It contains:

- multiple shared materials and textures;
- one mesh with positions, normals, UVs, and indices;
- an instance with non-default transform and rigid body;
- a sphere and quad light;
- a complete MaterialGraph exercising every current node kind across the fixture set;
- local and global current Mie references;
- non-default camera, medium, spectral extension, physics, and render compatibility fields.

The fixture's binary `.urescene` output is retained as a real future end-to-end rendering asset. Its exploded `.ure` form and typed resources remain the reviewable canonical source used to regenerate it.

## Acceptance Tests

Q.3 is complete only when tests prove:

- a full-coverage SceneIR survives `.urescene -> SceneIR -> .urescene` with identical semantic hash;
- repeated binary writes are byte-identical;
- binary and exploded text produce the same semantic hash;
- every current SceneIR and MaterialGraph field participates in the comparator/hash coverage;
- registered material, mesh, image, and texture sharing is restored;
- equal immutable mesh/Mie resources are content-addressed and shared;
- distinct equal-valued registered materials remain distinct;
- source IDs survive load/rewrite and illegal identity-table mutations fail;
- unregistered references, missing resources, duplicate IDs, invalid enums, graph cycles, non-finite floats, bad mesh indices, hash mismatch, truncation, count overflow, and budget overflow fail before compiler entry;
- unknown optional chunks survive byte-for-byte and unknown required chunks fail;
- file replacement cannot leave a partial destination after an injected write failure;
- reconstructed minimal and full fixtures compile through `GpuSceneCompiler`;
- the retained full fixture is suitable for a later real GPU image-rendering regression;
- all existing 27 CTest targets remain green and the new Q.3 host target raises the total to 28.

## Non-Goals

- No general spectral basis/tile/video resource schema.
- No new MaterialGraph nodes or BSDF behavior.
- No procedural graph or script execution.
- No new integrator, backend, acceleration, wave, physics, or acoustic execution semantics.
- No CLI or Python native-scene loading command.
- No glTF/USD/MaterialX adapter changes.
- No compiled GPU/farm cache serialization.
- No production compression codec beyond the Q.2 preservation/rejection contract.
