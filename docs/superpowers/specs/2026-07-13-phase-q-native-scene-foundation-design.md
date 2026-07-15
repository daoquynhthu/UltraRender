# Phase Q Native Scene Foundation Design

> Archive status: historical design record. Phase Q is complete; use `PLAN.md`, `STATUS.md`, and current source/tests for present state.

## Scope

This specification covers Phase Q.0, Q.1, and Q.2:

- Q.0 audits the semantics that UltraRender must own independently of glTF, USD, MaterialX, and renderer backends.
- Q.1 defines the identities, encodings, package layout, resource addressing, and text/binary boundary of the URE native format.
- Q.2 defines versioning, conventions, feature declarations, extension handling, migration metadata, and validation diagnostics.

Serialization of the complete `SceneIR`, procedural graphs, script build hooks, solver contracts, CLI pack/unpack, adapters, and compiled farm caches remain in Q.3-Q.12. The foundation must leave stable typed owners and extension slots for them without pretending those later capabilities are implemented.

## Decision Summary

UltraRender will separate scene semantics from physical encoding. Production authoring and consumption use indexed binary scene and package formats. A lossless text projection exists for review, source control, migration diagnostics, and small hand-authored scenes. Both encodings map to the same URE native semantic model.

| Identity | Default encoding | Role |
|---|---|---|
| `.urescene` | Binary | Production scene source with indexed, lazy-loadable chunks |
| `.ure` | UTF-8 text | Lossless semantic projection and exploded-project manifest |
| `.urepkg` | Binary | Portable package containing scenes, resources, provenance, validation output, and optional caches |
| `.urecache` | Binary | Non-authoritative compiled/runtime cache, always rebuildable from source |

The `.ure` text projection must not embed large typed arrays through Base64 or JSON number lists. Geometry, dense spectra, Mie tables, volume fields, animation samples, audio, and video remain typed binary resources referenced by stable resource IDs and content hashes.

## Design Principles

1. URE native semantics are authoritative. External formats are adapters.
2. Binary is the production default; text is an equivalent semantic projection, not the runtime storage path.
3. Source data and compiled cache are distinct. Removing a cache cannot destroy authoring information.
4. Large data is chunked, indexed, hash-addressed, and lazy-loadable.
5. Required unknown capabilities fail before scene compilation. Optional unknown data is preserved byte-for-byte when possible.
6. No renderer backend handle, CUDA pointer, acceleration handle, or launch configuration enters the source schema.
7. All offsets, lengths, counts, and aggregate byte sizes use checked 64-bit arithmetic before allocation or mapping.
8. Paths and URIs are package-relative or content-addressed and cannot escape a package root.
9. Schema evolution is explicit and migration is a first-class operation, never an incidental parser side effect.
10. Deterministic serialization and stable hashing are required for farm distribution, cache identity, and reproducible builds.

## Q.0 Capability Ownership Audit

The Q.0 document must enumerate current and planned semantics and assign each one a native owner. The following table is the minimum audit baseline.

| Semantic domain | Current source | Native owner | Foundation representation |
|---|---|---|---|
| Entity hierarchy and instances | `SceneIR` | `scene.graph` | Required typed domain |
| Meshes, spheres, analytic primitives, volumes | `SceneIR` | `scene.geometry` | Required typed domain plus resource references |
| Cameras and transforms | `SceneIR` | `scene.camera`, `scene.transform` | Required typed domain |
| Material nodes and expression graphs | `MaterialGraph` | `scene.material` | Required typed domain |
| Textures and sampled resources | `HostTexture`, spectral resources | `scene.resource` | Typed resource descriptors |
| Runtime spectral domain and packet lanes | `RenderConfig` | `render.spectral` | Feature/config declaration; never inferred from glTF |
| Mie and volume phase resources | `MiePhaseResource` | `scene.medium` | Typed resource descriptor and chunk type |
| Lights and light sampling metadata | `SceneIR`, compiled light records | `scene.light` | Source light definitions only; compiled trees remain cache |
| Integrator modes and bias declarations | `IntegratorRuntimeConfig` | `render.integrator` | Typed requirement/config domain |
| Wave optics and coherent requests | `WaveOpticsConfig` | `render.wave` | Typed requirement/config domain |
| Session mutation scope | `SceneDiff` | `scene.mutation` | Stable object/resource IDs and mutation policy |
| Distributed sample/spectral shards | distributed metadata | `render.distributed` | Typed requirement/config domain |
| Physics and acoustic coupling | physics/acoustic types | `scene.physics`, `scene.acoustic` | Versioned typed extension domains |
| Procedural generation | planned Q.4 | `build.procedural` | Reserved required domain |
| Script build hooks | planned Q.5 | `build.script` | Reserved opt-in domain, disabled by default |
| Backend and acceleration selection | planned T/V | `render.backend`, `render.acceleration` | Capability declaration only |
| Animation and video streams | planned | `scene.animation`, `scene.video` | Reserved typed domains |
| Validation metrics and fixtures | existing scripts/tests | `validation.contract` | Typed validation domain |

The audit must distinguish source semantics, runtime configuration, compiled cache data, and adapter-only metadata. Any field with no native owner is a Q.0 failure and must receive either a typed domain owner or a versioned extension slot.

## Semantic Model

The foundation model contains the following top-level concepts:

- `SceneDocument`: scene identity, schema version, conventions, feature declarations, root objects, resources, extensions, and migration provenance.
- `PackageManifest`: package identity, contained documents, resource index, optional cache index, validation reports, dependencies, and package provenance.
- `ResourceDescriptor`: stable ID, content hash, semantic type, schema version, storage location, dependencies, byte length, and budget metadata.
- `FeatureDeclaration`: canonical feature name, minimum feature version, requirement level, parameters, and optional provider constraint.
- `ExtensionRecord`: canonical extension name, version, required/optional flag, payload type, and opaque payload location.
- `ValidationDiagnostic`: stable diagnostic code, severity, document/object/resource path, message, and optional migration guidance.
- `MigrationRecord`: source schema version, target schema version, migration tool identity/version, input hash, output hash, and lossy flag.

Stable IDs are ASCII strings matching `[A-Za-z0-9][A-Za-z0-9._/-]{0,254}`. Empty path segments, `.` segments, `..` segments, leading/trailing slash, and repeated slash are invalid. Human-facing display names remain unrestricted NFC-normalized UTF-8. Content identity uses exactly 64 lowercase SHA-256 hex characters. Display names and provenance do not participate in physical resource identity.

## Binary `.urescene` Container

The production scene container uses a small fixed header followed by independently addressable chunks and a terminal or front-indexed directory. Metadata is encoded with a generated schema format; FlatBuffers is the selected initial metadata technology because it supports typed schemas, direct buffer traversal, fixed little-endian encoding, and forward/backward-compatible tables. Large payloads remain external container chunks rather than nested FlatBuffer vectors.

### Header

The header contains:

- eight-byte URE scene magic;
- container major/minor version;
- declared little-endian marker;
- header byte length;
- 64-bit chunk-directory offset and byte length;
- document UUID;
- source semantic hash;
- flags for directory placement, signatures, and compression support;
- reserved zeroed bytes for compatible header growth.

Readers reject invalid magic, unsupported container major versions, nonzero reserved bits, offsets outside the file, overlapping ranges, misalignment, arithmetic overflow, or a directory that aliases the header.

### Chunk Directory

Each entry contains:

- stable chunk/resource ID;
- semantic chunk type;
- chunk schema major/minor;
- required/optional flag;
- compression codec;
- 64-bit file offset;
- compressed and uncompressed byte lengths;
- required alignment;
- SHA-256 content hash;
- dependency IDs;
- opaque extension owner when the type is not core.

Core initial chunk types are metadata, scene graph, geometry, material graph, texture, spectral table, Mie phase, volume field, animation, physics, acoustic, video, validation, provenance, and compiled-cache reference.

Unknown required chunk types fail before allocation or scene compilation. Unknown optional chunks are retained as opaque directory entries and raw payload bytes during load-save operations. A tool that cannot preserve an optional chunk must refuse to overwrite the source rather than silently discard it.

A writer whose supported metadata schema minor is older than the loaded document minor must preserve the original metadata chunk byte-for-byte or refuse to rewrite it. It cannot decode known fields and silently discard unknown FlatBuffers fields. Canonical writers construct tables and vectors in schema-defined order.

### Access and Ownership

Readers load and validate the header and directory first. Chunks are mapped or read only when requested. A chunk view owns or shares the underlying file mapping, so no view outlives its storage. Decompression output is budgeted and owned by an immutable resource object. Hash verification can be eager for manifests and required metadata, and lazy-before-first-use for large payloads.

## Text `.ure` Projection

The text encoding is canonical UTF-8 JSON for the foundation phase because the project already carries nlohmann/json and deterministic validation infrastructure. It represents the same semantic metadata as the binary schema.

Canonical text rules are:

- UTF-8 without BOM;
- NFC-normalized identifiers;
- sorted object keys during canonical export;
- stable array order where order is semantic;
- finite numeric values only;
- explicit unit/convention declarations;
- lowercase SHA-256 hashes;
- no comments in canonical files;
- no Base64 payloads;
- no inline typed numeric array above 64 scalar values; individual later schemas may impose a lower limit;
- package-relative resource URIs or `ure+sha256://<hash>` content URIs.

An exploded text project stores `scene.ure` plus typed binary resources. Binary-to-text conversion creates this exploded representation. Text-to-binary conversion is lossless when every referenced resource and opaque optional extension payload is present and hash-valid.

## Binary `.urepkg` Package

The package reuses the chunk-container safety rules but adds a package index. It may contain multiple `.urescene` documents, shared resource chunks, validation reports, provenance, and optional `.urecache` entries. Scene documents reference resources by stable ID/content hash, not by physical file extension, so text/binary conversion does not invalidate references.

The logical package layout is:

```text
manifest
scenes/
resources/
validation/
provenance/
cache/
```

This layout is visible when unpacked, but the production `.urepkg` is one indexed binary container. Cache entries are explicitly non-authoritative. Package validation must still succeed after all cache entries are removed.

Package dependencies can reference other packages through pinned package ID plus manifest hash. Cyclic package dependencies are invalid. Network retrieval and registries are outside Q.0-Q.2.

## Versioning

Container and semantic schema versions are separate pairs of unsigned major/minor integers.

- A container major mismatch is unsupported.
- A newer container minor is accepted only when all used flags and required chunk types are understood.
- A semantic schema major mismatch requires an explicit migration tool.
- A newer semantic minor may load when all required features/extensions are supported.
- Writers never silently downgrade a semantic major version.
- Removed field/chunk identifiers remain permanently reserved.
- Migration output records input/output hashes and whether the operation was lossy.

The initial identities are `ure.container.scene/1.0`, `ure.container.package/1.0`, and `ure.scene/1.0`.

## Conventions

Every scene declares conventions, and version 1 validation accepts only the canonical production values unless a supported adapter performs an explicit conversion:

- metres for length;
- seconds for time;
- kilograms for mass;
- radians for angles;
- vacuum nanometres for spectral wavelength coordinates;
- right-handed coordinates;
- positive Y up;
- camera local forward is negative Z, local right is positive X, and local up is positive Y;
- linear radiometric quantities without implicit display transfer functions;
- UTF-8 NFC identifiers;
- SHA-256 physical resource hashes.

Unit conversion is an import/build operation. Runtime kernels do not carry mixed-unit branches.

## Feature and Extension Contract

Features use reverse-domain canonical names owned by URE, such as `ure.scene.mie_phase`, `ure.render.wave.coherent_field`, and `ure.render.integrator.restir_di`.

Requirement levels are:

- `required`: validation fails if the feature/version/provider is unsupported;
- `optional`: metadata is preserved and a structured warning is emitted when unsupported;
- `advisory`: affects authoring/tooling only and cannot change rendered semantics.

An optional feature cannot own data needed to interpret a required scene object. Dependencies between features are explicit and acyclic. Capability validation occurs before SceneIR compilation or Session creation.

Extensions have registered names, versions, payload type IDs, and requirement levels. Core readers preserve unknown optional binary payloads and unknown optional text objects. Unknown required extensions fail. Extension namespaces cannot use the reserved `ure.*` prefix unless defined by the core registry.

## Resource Addressing and Security

Accepted resource references are package-relative normalized paths, stable package resource IDs, and `ure+sha256` content URIs. Validation rejects:

- absolute paths in portable packages;
- `..` traversal after normalization;
- Windows drive/UNC escape paths;
- symlink or reparse-point escape when reading exploded packages;
- duplicate stable IDs;
- mismatched hashes or byte lengths;
- overlapping binary chunks;
- chunk alignment that is not a power of two in the inclusive range 1-4096 bytes;
- decompression ratios or total output beyond configured budgets;
- cyclic resource dependencies;
- unsupported required compression codecs;
- integer overflow in offsets, lengths, counts, alignment, or aggregate allocation plans.

Digital signatures and encrypted packages are reserved for a later security step. Their header flags and signature chunk identities are reserved in version 1 and must remain disabled until implemented.

## Validation API Boundary

Q.0-Q.2 produce a host-only validation layer in `ure_sceneio`. Validation returns structured diagnostics and does not print directly. Diagnostic severities are `error`, `warning`, and `info`. Fatal structural errors prevent construction of a validated document. Warnings remain attached to the validated result.

The API separates parsing from capability validation:

1. container/text structural validation;
2. schema and convention validation;
3. resource graph and budget validation;
4. feature/extension capability validation;
5. later Q.3 semantic compilation to SceneIR.

This separation allows `ure_cli validate` in Q.9 to report schema errors without loading CUDA or constructing a renderer.

## Determinism and Hashing

Semantic hashes exclude physical chunk offsets, compression choice, text whitespace, provenance timestamps, and compiled cache entries. They include canonical schema values, stable IDs, ordered dependencies, and physical resource hashes. The hash stream traverses fields in schema order, prefixes variable-length values with unsigned 64-bit little-endian byte lengths, encodes integers little-endian, encodes finite IEEE-754 values with signed zero normalized to positive zero, and NFC-normalizes text before UTF-8 encoding. Binary and text encodings of the same semantic document must produce the same semantic hash.

Package hashes include the canonical manifest and authoritative contained resources, but exclude optional cache chunks. Repacking with a different supported compression codec preserves the semantic package hash.

## Q.0-Q.2 Deliverables

1. `docs/Phase_Q_Native_Scene_Format.md` with the capability ownership audit and encoding contract.
2. Backend-neutral foundation types for versions, conventions, feature declarations, extensions, resource descriptors, diagnostics, scene documents, and package manifests.
3. Version-1 FlatBuffers schema sources and generated-code integration policy.
4. Header/directory reader-writer for minimal `.urescene` and `.urepkg` containers.
5. Canonical `.ure` text reader-writer for foundation metadata.
6. Empty package, single-scene package, shared-resource package, and exploded-text fixtures.
7. Host validation tests and a Phase Q static audit script.

## Acceptance Tests

The foundation is complete only when host tests prove:

- empty, single-scene, and shared-resource packages validate;
- binary and text forms have the same semantic hash;
- deterministic repeated binary/text writes are byte-identical for the same encoding settings;
- cache removal preserves package validity and semantic hash;
- unknown required features, extensions, chunks, and codecs fail loudly;
- unknown optional extensions and chunks survive load-save byte-for-byte;
- newer compatible minor versions load and unsupported majors fail;
- path traversal, absolute paths, duplicate IDs, dependency cycles, hash mismatch, overlapping chunks, malformed alignment, and checked-size overflow fail before allocation;
- decompression and resident-resource budgets are enforced;
- deleted/reserved identifiers cannot be reused by the schema registry;
- Q.0 assigns every audited capability a native typed owner or extension slot;
- all existing 26 CTest targets remain green.

## Non-Goals

- Complete SceneIR serialization, which begins in Q.3.
- Procedural graph execution or scripting, which belongs to Q.4-Q.5.
- Full spectral/material/medium payload schemas, which belong to Q.6.
- Integrator/backend/wave execution, which belongs to Q.7.
- Physics/acoustic solver implementation, which is not a scene-format responsibility.
- CLI pack/unpack/migrate commands, which belong to Q.9.
- USD, glTF, or MaterialX adapter changes, which belong to Q.10.
- Production compiled cache and farm packaging, which belong to Q.11.
- Compression codec selection beyond `none` in the first executable foundation; codec IDs and rejection behavior are defined now.

## Rationale

Blender demonstrates a linked data-block database rather than a monolithic document tree. Houdini demonstrates that production binary and reviewable text can coexist. OpenUSD demonstrates lossless semantic equivalence between human-readable and random-access binary encodings. UltraRender adopts those lessons without making any external format authoritative: a URE-owned semantic schema, binary production containers, lossless text projection, typed resource chunks, and explicit capability validation.
