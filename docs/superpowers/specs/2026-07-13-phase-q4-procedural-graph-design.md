# Phase Q.4 Typed Procedural Graph Design

> Archive status: historical design record. Phase Q is complete; use `PLAN.md`, `STATUS.md`, and current source/tests for present state.

## Scope

This specification covers Phase Q.4 only: a URE-owned typed procedural graph, deterministic evaluation, parameter domains, build cache identity, and the first scatter, instancing, spectrum, and light-rig nodes.

The procedural graph is authoritative build source serialized in `.ure` and `.urescene`. Its generated `SceneIRFragment` and resource artifacts are deterministic derived products. They never replace the source graph as authority and may always be discarded and rebuilt.

Q.4 does not add script execution, native plugins, general Q.6 spectral basis/tile resources, solver requests, CLI build commands, adapter conversion, or persistent `.urecache` storage.

## Decision Summary

UltraRender will use a typed, pure-function directed acyclic graph. Each node has a stable source ID, an explicit versioned kind, typed input and output ports, typed parameter bindings, and no hidden mutable state. Evaluation proceeds only after complete static validation.

The source and build flow is:

```text
.ure / .urescene
  |-- base URIG SceneIR source
  `-- URPG procedural source graph
             |
             | schema, identity, type, domain, DAG and budget validation
             | canonical source hash and cache-key construction
             ` deterministic topological evaluation
                           |
                           `-- immutable SceneIRFragment + generated artifacts
                                             |
                                             ` conflict-checked composition
                                                           |
                                                           `-- final SceneIR
```

The evaluator remains pure C++ inside `ure_sceneio`. It depends on the existing Q.3 native source model and `ure_types`, but it does not link `ure_core`, include CUDA, or mutate the caller's SceneIR.

## Rejected Alternatives

### Sequential command stream

An imperative list of scatter, mutate, and append commands would be easy to execute but would make results depend on hidden mutable state and command order. It would weaken composition, local validation, cache identity, and future parallel evaluation. It is rejected.

### Generic property graph

A string-keyed node and property system would be flexible but would move type errors and physical-domain errors into runtime, weaken schema migration, and encourage silent coercion. It is rejected as an authoritative format. Opaque data remains limited to the existing optional extension contract.

### Runtime procedural execution

Interpreting graphs in the GPU renderer or per frame would mix authoring with execution and make farm reproducibility dependent on runtime state. It is rejected. Q.4 evaluates only during scene build/compile.

## Public Model and Boundaries

Q.4 adds a dedicated public header under `ure_sceneio`, conceptually exposing:

```cpp
namespace ure::native_scene {

struct ProceduralGraph;
struct ProceduralBuildOptions;
struct SceneIRFragment;

struct ProceduralBuildResult {
    scene_ir::SceneIR scene;
    NativeSceneSourceIds source_ids;
    std::vector<NamedResourcePayload> generated_resources;
    std::string source_hash;
    std::string cache_key;
    std::string output_hash;
};

ValidationReport validate_procedural_graph(
    const ProceduralGraph& graph,
    const NativeSceneArchive& source,
    const ProceduralBuildOptions& options = {});

LoadResult<ProceduralBuildResult> build_procedural_scene(
    const NativeSceneArchive& source,
    const ProceduralBuildOptions& options = {});

}
```

`NativeSceneArchive` gains an optional owned `ProceduralGraph`. Its existing `scene` remains the immutable base SceneIR source, not a partially built runtime scene. `build_procedural_scene` deep-freezes the base, evaluates the graph, composes the fragment, validates the resulting archive, and returns a distinct final SceneIR.

An archive without a procedural graph builds as an identity operation. Loading never silently evaluates or overwrites source data. Runtime and later Q.9 tooling must call the explicit build boundary before passing a procedural scene to `GpuSceneCompiler`.

## Typed Graph Schema

Q.4 adds `ure_procedural_graph_v1.fbs` with file identifier `URPG`, a conforming baseline schema, and a checked-in generated C++ header. Every field has an explicit numeric field ID. Normal builds consume the checked-in header; the existing pinned FlatBuffers regeneration gate verifies conformance and generated output.

The graph contains:

- graph ID and schema version;
- a 128-bit root seed represented as two unsigned 64-bit words;
- stable graph parameters and their domains;
- nodes with stable IDs and versioned typed payloads;
- explicit typed input connections;
- one root output of type `SceneFragment`;
- declared external source-object IDs and resource hashes;
- declared deterministic-math and evaluator compatibility profile.

Node and parameter vectors are storage order only. Canonicalization sorts them by stable ID before hashing or evaluation. Connections are sorted by destination port and source identity. Reordering equivalent source records therefore cannot change the build.

### Stable identifiers

Graph, node, and parameter IDs use the Q.3 lowercase segmented source-ID grammar. IDs are unique within their domain. Generated IDs use:

```text
generated/<graph-id>/<node-id>/<object-kind>/<zero-padded-local-index>
```

The logical components are hash-escaped into valid Q.3 ID segments. Display names never become identity. Equal generated values from distinct node IDs remain distinct objects; equal generated resource payloads coalesce only by content hash.

### Port types

Version 1 exposes only these port types:

| Type | Meaning |
|---|---|
| `MeshReference` | Stable reference to a registered base mesh record |
| `MaterialReference` | Stable reference to a registered base material |
| `TransformSet` | Ordered immutable generated transforms |
| `SpectrumArtifact` | Immutable generated sampled SPD artifact |
| `SceneFragment` | Owned additions to the base SceneIR |

Connections require exact type equality. There are no implicit conversions, wildcard ports, or string-selected outputs. Every node kind has schema-declared cardinality; only `ComposeFragments` has a variadic input, bounded to one or more `SceneFragment` values. Missing required inputs, duplicate bindings, unknown ports, and type mismatch fail before evaluation.

### Parameter values and domains

Graph parameters support `bool`, signed 64-bit integer, finite double, finite `Vec3`, and closed enum values. Node payloads use typed bindings that contain either a literal of the exact type or one graph-parameter ID.

Each parameter declares a default and a domain appropriate to its type:

- integer and floating-point closed or half-open bounds;
- per-component vector bounds;
- explicit enum membership;
- finite-only enforcement;
- maximum generated-count and sample-count constraints.

Overrides are supplied through `ProceduralBuildOptions` by parameter ID. Unknown overrides, duplicate overrides, type mismatch, and out-of-domain values fail. Resolved parameter values participate in the cache key.

## Node Set

Infrastructure reference and composition nodes are included only where required to connect the four planned generator families.

### SourceMesh and SourceMaterial

These leaf nodes resolve a stable Q.3 source ID to `MeshReference` or `MaterialReference`. Resolution is against the immutable base archive and includes the referenced source object's semantic or resource hash in downstream node identity. Missing, ambiguous, or unregistered references fail.

### ScatterSurface

`ScatterSurface` consumes one `MeshReference` and produces one `TransformSet`.

Its typed parameters include count, position offset, scale range, yaw range, normal-alignment mode, and node-local seed salt. Version 1 uses area-weighted triangle sampling over the canonical mesh triangle order. Degenerate triangles have zero weight; an empty or zero-area surface fails.

Random draws use a counter-based SHA-256 PRF keyed by the graph seed, canonical graph hash, node ID, local seed salt, and output element index. Each random dimension has a fixed numeric lane. The high 24 bits of a lane map exactly to `[0, 1)`. Barycentric sampling uses a fixed reflection construction and does not share mutable RNG state between elements.

Output order is local element index order. Count zero is rejected because it usually indicates an authoring error rather than a useful fragment. Non-finite transforms, zero scale components, and output-budget overflow fail.

### Instantiate

`Instantiate` consumes one `TransformSet`, one `MeshReference`, and one `MaterialReference`, and produces one `SceneFragment` containing instances.

It copies no mesh or material objects. Every instance references registered base objects, receives a stable generated source ID, and preserves transform order. Optional fixed rigid-body parameters use the current Q.3 typed subset. An empty transform set or any invalid transform fails.

### SpectrumGenerator

`SpectrumGenerator` produces one `SpectrumArtifact`. Version 1 supports two explicit modes:

- blackbody spectral radiance over a declared wavelength interval;
- a nonnegative sum of Gaussian spectral lines over a declared wavelength interval.

Both modes declare sample count, wavelength minimum and maximum, and normalization mode. Temperature must be positive. Gaussian amplitude and width must be positive and centers must lie in the declared interval. Sample count is at least two and bounded by the Q.4 build budget.

The output is canonical UTF-8 SPD text with strictly increasing finite wavelengths and finite nonnegative values. Its URI is content-addressed under `resources/generated/spectrum/`. The artifact is returned as `NamedResourcePayload` with `ResourceKind::SpectralTable`; it does not introduce the generalized Q.6 runtime spectral representation.

### LightRig

`LightRig` produces one `SceneFragment` containing generated emissive materials and quad lights. It may consume one optional `SpectrumArtifact`; when present, generated light materials reference the artifact through the current `SpectralMaterialExtension::emission_spd` path.

Version 1 supports three explicit layouts:

- `Ring`: evenly spaced lights around a target;
- `Grid`: row-major rectangular layout facing a target;
- `ThreePoint`: key, fill, and rim lights with explicit ratios.

All layouts use typed center, target, basis, extent, count, emission, and power-ratio parameters. Zero-area quads, coincident center/target vectors, nonpositive power, invalid counts, non-finite values, and degenerate orientation bases fail. Emissive materials are generated once per distinct spectral/power setting and referenced by the lights.

### ComposeFragments

`ComposeFragments` consumes one or more `SceneFragment` values and produces the graph's root `SceneFragment`. Inputs are composed in canonical source-node-ID order, never connection storage order.

Duplicate source IDs, conflicting resource URIs, mismatched content for one content hash, unregistered references, and attempts to modify base camera, physics, or global medium fields fail. Q.4 fragments may add materials, instances, and quad lights plus generated spectral artifacts; other SceneIR domains remain out of scope.

## Fragment Ownership and Composition

`SceneIRFragment` owns all generated values and aligned source IDs. It cannot hold pointers into an evaluator scratch arena. References to base objects are resolved against the deep-frozen base archive. References to generated materials are resolved within the fragment by stable ID.

Composition follows these rules:

1. deep-freeze the base archive;
2. reserve all base source IDs;
3. canonicalize generated records by stable ID;
4. content-deduplicate generated resource payloads;
5. reject every identity or URI conflict;
6. append generated objects in canonical ID order;
7. run `validate_scene_ir_archive` on the complete result.

The source archive is unchanged on success or failure. No partial fragment escapes after an error.

## Determinism and Build Cache Identity

Q.4 distinguishes three hashes:

- `source_hash`: the canonical base SceneIR source plus canonical procedural graph and declared external input hashes;
- `cache_key`: source hash plus resolved parameter overrides, Q.4 evaluator identity/version, schema identity, deterministic math profile, and every build option that can affect output;
- `output_hash`: the canonical final SceneIR plus generated artifact content hashes.

The cache key is SHA-256 over a domain-separated binary encoding, not concatenated display text. Map and set inputs are sorted. IEEE signed zero is normalized in semantic hashing while serialized source values remain bit-preserving under the existing Q.3 rule.

Identical source, seed, input hashes, parameter overrides, evaluator fingerprint, and math profile must produce byte-identical generated artifacts, stable generated source IDs, and the same output hash. A different evaluator/compiler fingerprint produces a different cache key rather than falsely claiming cross-toolchain equivalence.

Q.4 computes cache identity only and does not serialize an expected cache key or persist `.urecache`. Cache lookup, stale-cache mismatch handling, and cache storage remain Q.11, avoiding a circular cache-key field in authoritative source.

## Serialization

Binary `.urescene` stores the authoritative graph in one required `procedural_graph` chunk with core chunk kind 17 and `URPG` payload. Core value 16 remains reserved by the frozen Q.2 container contract. The chunk depends on `scene_graph` and on every declared external typed resource chunk. Q.3 `URIG`, `URMS`, and `URMI` schemas remain unchanged.

Canonical `.ure` adds a `procedural_graph` object alongside `scene_ir`. It contains no generated fragment or generated SPD bytes. Generated artifacts exist only in build results until later tooling explicitly materializes them.

The archive semantic hash includes the canonical procedural source graph when present. Repeated binary or text writes are deterministic, and binary/text source projections produce the same source hash. Unknown node kinds and unknown required node payloads fail; Q.4 does not add opaque optional nodes because an unevaluated node cannot safely contribute to a required root result.

## Validation and Diagnostics

Validation is fail-loud and ordered:

1. FlatBuffers identifier, verifier, schema version, count and byte budgets;
2. graph/node/parameter ID grammar and uniqueness;
3. external source ID and content-hash integrity;
4. parameter value, domain, and override validation;
5. node kind/version, fixed port, cardinality, and exact type validation;
6. missing references, cycle detection, root type, and root reachability;
7. deterministic topological order construction;
8. per-node physical-domain and output-budget validation;
9. immutable fragment composition and resource conflict validation;
10. final Q.3 SceneIR archive validation.

Every failure is returned as a structured `URE-Q4-*` diagnostic with a stable graph path. Parser and evaluator exceptions are converted at the public boundary. Evaluation stops at the first failed node but retains diagnostics already produced; no warning authorizes a semantic fallback.

Default Q.4 budgets include maximum nodes, edges, parameters, transforms, instances, lights, spectrum samples, aggregate generated bytes, and per-node output count. All multiplication and allocation arithmetic is checked before allocation.

## Evaluation Algorithm

Static validation builds a dependency graph from typed connections. Kahn topological sorting uses stable node ID as the ready-set tie breaker. Only nodes reachable from the root are legal; unreachable nodes fail as stale authoring data instead of being silently ignored.

Each node receives immutable resolved inputs, immutable parameters, its deterministic node key, and remaining output budgets. It returns an owned typed value or diagnostics. Results are stored by `(node ID, output port)` and cannot be mutated by downstream nodes.

The evaluator may later execute independent ready nodes in parallel without changing output because nodes share no RNG stream or mutable state. Q.4 initially uses a deterministic single-threaded evaluator; parallel scheduling is not required for acceptance.

## Test Strategy

Q.4 adds one pure-host CTest target and a retained procedural native-scene fixture. No CUDA target is modified.

Tests must prove:

- binary and canonical text preserve the same procedural source hash;
- repeated source writes and builds are byte-identical;
- node source-vector reordering does not change source hash, cache key, generated IDs, artifacts, or output hash;
- changing seed, source hash, relevant parameter, evaluator fingerprint, or deterministic math profile changes the cache key;
- changing an unused storage-order detail does not change the cache key;
- scatter results remain stable and lie on the referenced mesh surface;
- instancing preserves base references and stable transform order;
- blackbody and Gaussian spectra are finite, nonnegative, ordered, deterministic, and content-addressed;
- ring, grid, and three-point rigs create valid materials and nondegenerate quad lights;
- composed results pass Q.3 validation and compile through the existing `GpuSceneCompiler` after the integration fixture materializes returned generated SPD artifacts into its isolated temporary asset root;
- the base archive remains unchanged after successful and failed builds;
- duplicate IDs, unknown references, invalid port types, missing inputs, cycles, unreachable nodes, invalid roots, invalid domains, non-finite values, zero-area sources/lights, external hash mismatch, and every output budget fail before compiler entry;
- malformed `URPG` payloads and unknown node kinds fail with `URE-Q4-*` diagnostics;
- all existing registered CTests and Phase Q static/schema gates remain green.

The retained fixture contains a base mesh and materials, deterministic surface scatter feeding instancing, a generated spectrum feeding a ring light rig, and a composed root. It is retained as a future real end-to-end rendered-image asset.

## Acceptance Criteria

Q.4 is complete only when:

- `.ure` and `.urescene` roundtrip a typed authoritative procedural graph;
- the graph generates a validated immutable SceneIR fragment and a complete final SceneIR;
- identical seed/source/build identity produces stable generated IDs, artifacts, cache key, and output hash;
- illegal graphs and invalid physical domains fail loudly with structured diagnostics;
- scatter, instancing, spectrum, and light-rig nodes are covered by retained and adversarial host tests;
- the generated fixture crosses the existing `GpuSceneCompiler` boundary without linking `ure_sceneio` to `ure_core`;
- Q.5-Q.12 semantics have not been pulled forward.

## Non-Goals

- No Python, Lua, WASM, or other script hook.
- No native procedural plugin or Phase X ABI.
- No runtime/GPU graph interpreter and no per-frame procedural evaluation.
- No persistent `.urecache` implementation.
- No generalized basis, tiled, video, or packet spectral source model.
- No volume field, camera path, animation, or batch-variation nodes in Q.4.
- No mutation of camera, physics, acoustic, global medium, integrator, backend, acceleration, or distributed settings.
- No CLI, C API, Python API, glTF, USD, or MaterialX integration changes.
