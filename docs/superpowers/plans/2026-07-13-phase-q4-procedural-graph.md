# Phase Q.4 Typed Procedural Graph Implementation Plan

> Archive status: completed execution record. Checkboxes, counts and next steps below are not a live plan.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a serialized, strongly typed, deterministic procedural DAG that builds validated SceneIR fragments through scatter, instancing, spectrum, and light-rig nodes.

**Architecture:** Store authoritative procedural source as an independent `URPG` graph beside Q.3 `URIG`, validate the complete typed DAG before evaluation, and build into an immutable fragment that is conflict-checked against a deep-frozen base archive. Use domain-separated SHA-256 counter draws and canonical ID ordering so source hash, cache key, generated IDs, artifacts, and output hash remain stable for one evaluator fingerprint.

**Tech Stack:** C++23, FlatBuffers 25.12.19 with checked-in generated C++17-compatible headers, nlohmann JSON canonical projection, SHA-256 helpers from `ure_sceneio`, CMake/Ninja, host CTest, PowerShell static gates.

## Global Constraints

- Keep `ure_sceneio` pure C++; it must not link `ure_core` or CUDA.
- Treat the graph as authoritative source and every generated fragment/artifact as disposable derived output.
- Use exact typed ports and parameter bindings; no property bags, implicit conversion, hidden RNG state, or runtime/GPU interpretation.
- Preserve Q.3 `URIG`, `URMS`, and `URMI` schemas unchanged; add independent `URPG` schema version 1.
- Preserve reserved core value 16; use `ChunkKind::ProceduralGraph = 17` and required binary chunk ID `procedural_graph`.
- Do not implement scripts, plugins, persistent `.urecache`, generalized Q.6 spectral resources, CLI/API adapters, or runtime solver semantics.
- Convert all parser/evaluator exceptions to structured `URE-Q4-*` diagnostics.
- Follow project governance: no implementation-stage commits. Record verified checkpoints and create one final implementation commit only after REPORT approval.
- Skip every ceremonial RED step whose only expected failure is a known missing file, symbol, target, or unsupported not-yet-created node. Retain behavior-first regression tests where a failure proves a real contract boundary.

## File Map

- Create `libs/ure_sceneio/include/ure/native_procedural_graph.hpp`: public graph, parameter, node, fragment, build option, and build result contracts.
- Create `libs/ure_sceneio/src/native_procedural_internal.hpp`: private canonical encoding, typed node metadata, evaluator value, and helper declarations.
- Create `libs/ure_sceneio/src/native_procedural_model.cpp`: graph validation, parameter resolution, canonical source hash, cache key, and output hash.
- Create `libs/ure_sceneio/src/native_procedural_schema.cpp`: `URPG` FlatBuffers encode/decode and canonical JSON projection helpers.
- Create `libs/ure_sceneio/src/native_procedural_build.cpp`: deterministic topological evaluator, fragment composition, and public build API.
- Create `libs/ure_sceneio/src/native_procedural_scatter.cpp`: source reference, counter PRF, and surface scatter evaluation.
- Create `libs/ure_sceneio/src/native_procedural_spectrum.cpp`: blackbody/Gaussian sampled SPD generation and content-addressed artifact creation.
- Create `libs/ure_sceneio/src/native_procedural_light.cpp`: instantiate, light-rig, and fragment node evaluation.
- Create `schemas/ure_procedural_graph_v1.fbs` and `.baseline.fbs`: versioned typed graph schema with identifier `URPG`.
- Create `libs/ure_sceneio/generated/ure_procedural_graph_v1_generated.h`: pinned FlatBuffers output.
- Modify `libs/ure_sceneio/include/ure/native_scene_ir.hpp`: attach an immutable optional graph to the source archive.
- Modify `libs/ure_types/include/ure/native_scene.hpp`: assign core procedural chunk kind 17 while retaining reserved value 16.
- Modify Q.3 binary/text/model internals to preserve and hash the graph without altering base SceneIR.
- Modify `scripts/regenerate_native_scene_schema.ps1`: conform and verify `URPG`.
- Create `scripts/check_phase_q4_static.ps1`: lock schema, public API, dependency boundary, fixture, and one-test registration.
- Create `tests/host/test_native_procedural_graph.cpp`: focused red/green graph, node, serialization, determinism, and adversarial tests.
- Create `tests/assets/native_scene/q4_procedural_scene/`: retained canonical source, binary scene, resources, and expected hashes.
- Modify `tests/host/CMakeLists.txt`: register `test_native_procedural_graph` linked to scene I/O, core compiler, and diagnostics.
- Modify `docs/Phase_Q_Native_Scene_Format.md` and `PLAN.md`: record verified Q.4 closure only after all gates pass.

---

### Task 1: Public typed graph and frozen URPG schema

**Files:**
- Create: `libs/ure_sceneio/include/ure/native_procedural_graph.hpp`
- Create: `schemas/ure_procedural_graph_v1.fbs`
- Create: `schemas/ure_procedural_graph_v1.baseline.fbs`
- Create: `libs/ure_sceneio/generated/ure_procedural_graph_v1_generated.h`
- Modify: `libs/ure_sceneio/include/ure/native_scene_ir.hpp`
- Modify: `libs/ure_types/include/ure/native_scene.hpp`
- Modify: `scripts/regenerate_native_scene_schema.ps1`
- Modify: `libs/ure_sceneio/CMakeLists.txt`
- Test: `tests/host/test_native_procedural_graph.cpp`
- Modify: `tests/host/CMakeLists.txt`

**Interfaces:**
- Consumes: Q.3 `NativeSceneArchive`, `NativeSceneSourceIds`, `NamedResourcePayload`, `ValidationReport`, and `scene_ir::SceneIR`.
- Produces: `ProceduralGraph`, `ProceduralGraphNode`, typed payload structs, `SceneIRFragment`, `ProceduralBuildOptions`, `ProceduralBuildResult`, `validate_procedural_graph`, and `build_procedural_scene` declarations.

- [ ] **Step 1: Write a failing public-model test**

Add a test that constructs a graph with stable IDs, root seed, one `SourceMeshNode`, and one graph parameter:

```cpp
ure::native_scene::ProceduralGraph graph;
graph.id = "procedural/test";
graph.seed_high = 0x0123456789abcdefull;
graph.seed_low = 0xfedcba9876543210ull;
graph.parameters.push_back(ure::native_scene::GraphParameter::integer(
    "parameter/count", 8, 1, 1024));
graph.nodes.push_back(ure::native_scene::ProceduralGraphNode::source_mesh(
    "node/source_mesh", "mesh/00000000"));
graph.root = {"node/source_mesh", "out"};
CHECK(graph.nodes.front().output_type() == ure::native_scene::ProceduralPortType::MeshReference);
```

- [ ] **Step 2: Register and run the test to prove RED**

Run:

```powershell
.\scripts\build_x64.ps1 -BuildDir build_modular_x64 -Config Release -SkipConfigure -Targets test_native_procedural_graph
```

Expected: compilation fails because `ure/native_procedural_graph.hpp` does not exist.

- [ ] **Step 3: Define the exact public model**

Define strongly typed enums and structs. Node payload must be a closed `std::variant`:

```cpp
using ProceduralNodePayload = std::variant<
    SourceMeshNode,
    SourceMaterialNode,
    ScatterSurfaceNode,
    InstantiateNode,
    SpectrumGeneratorNode,
    LightRigNode,
    ComposeFragmentsNode>;

struct ProceduralGraphNode {
    std::string id;
    Version version{1, 0};
    ProceduralNodePayload payload;
    std::vector<ProceduralConnection> inputs;
    ProceduralPortType output_type() const;
};

struct ProceduralGraph {
    std::string id;
    Version schema_version{1, 0};
    std::uint64_t seed_high = 0;
    std::uint64_t seed_low = 0;
    std::vector<GraphParameter> parameters;
    std::vector<ProceduralGraphNode> nodes;
    ProceduralOutputReference root;
    std::vector<ProceduralExternalInput> external_inputs;
};
```

Use explicit `ParameterValueKind`, exact literal fields, and matching `ParameterDomain` optional bounds. Provide factory functions used by tests; do not add a generic string property map.

- [ ] **Step 4: Freeze the FlatBuffers schema**

Define `URPG` tables for graph identity, seed, parameter values/domains, connections, external inputs, each closed node payload, and a node-payload union. Assign every field an explicit `(id: N)`, copy it to the baseline, generate the checked-in header with pinned `flatc`, and add the schema to `regenerate_native_scene_schema.ps1` with `ObjectApi = $true`.

- [ ] **Step 5: Attach the graph without changing runtime SceneIR**

Forward-declare `ProceduralGraph` and add:

```cpp
std::shared_ptr<const ProceduralGraph> procedural_graph;
```

to `NativeSceneArchive`. Add `ProceduralGraph = 17` to `ChunkKind`; preserve reserved value 16 and extend the core-known chunk range through 17.

- [ ] **Step 6: Build and run the focused model test**

Run the targeted build and:

```powershell
ctest --test-dir build_modular_x64 -C Release -R "^test_native_procedural_graph$" --output-on-failure
.\scripts\regenerate_native_scene_schema.ps1
```

Expected: model test passes and FlatBuffers conformance/generated-header gate reports current output.

- [ ] **Step 7: Record checkpoint**

Run `git diff --check` and retain changes uncommitted under project governance.

### Task 2: Structural validation, canonical source hash, and cache key

**Files:**
- Create: `libs/ure_sceneio/src/native_procedural_internal.hpp`
- Create: `libs/ure_sceneio/src/native_procedural_model.cpp`
- Modify: `libs/ure_sceneio/CMakeLists.txt`
- Test: `tests/host/test_native_procedural_graph.cpp`

**Interfaces:**
- Consumes: public graph structs and Q.3 SHA-256/source-ID rules.
- Produces: `validate_procedural_graph`, `procedural_source_hash`, `procedural_cache_key`, resolved parameters, canonical node order, and stable `URE-Q4-*` diagnostics.

- [ ] **Step 1: Add failing adversarial validation tests**

Cover duplicate/invalid IDs, missing source IDs, external content-hash mismatch, unknown parameter override, wrong override type, out-of-domain value, duplicate/missing input, type mismatch, cycle, unreachable node, non-fragment root, and node/edge/output budgets. Assert diagnostic code prefixes and paths, not exception text.

- [ ] **Step 2: Add failing canonical identity tests**

Build equivalent graphs with reversed node/parameter/connection storage order and assert:

```cpp
CHECK(procedural_source_hash(a, source) == procedural_source_hash(b, source));
CHECK(procedural_cache_key(a, source, options) == procedural_cache_key(b, source, options));
```

Then vary seed, referenced source hash, resolved parameter, evaluator fingerprint, and math profile one at a time and assert cache-key inequality.

- [ ] **Step 3: Run RED**

Expected: link or assertion failure because validation and identity functions are not implemented.

- [ ] **Step 4: Implement layered structural validation**

Implement ID grammar, uniqueness, exact node signature metadata, schema-declared cardinality, source reference lookup, domain resolution, Kahn cycle detection with node-ID ready ordering, root reachability, and checked budgets. Only `ComposeFragments.fragments` accepts one-or-more connections.

- [ ] **Step 5: Implement canonical encoders**

Use domain-separated binary encoders and SHA-256. Sort semantic sets by stable ID. Include the base Q.3 semantic hash, declared external hashes, graph contents, resolved overrides, evaluator identity, and deterministic math profile exactly as specified. Normalize signed zero only in hash encoding.

- [ ] **Step 6: Run GREEN**

Run focused CTest. Expected: all structural, domain, canonicalization, and cache-key tests pass with no warnings.

- [ ] **Step 7: Record checkpoint**

Run `git diff --check`; do not commit.

### Task 3: URPG binary and canonical text roundtrip

**Files:**
- Create: `libs/ure_sceneio/src/native_procedural_schema.cpp`
- Modify: `libs/ure_sceneio/src/native_scene_ir_internal.hpp`
- Modify: `libs/ure_sceneio/src/native_scene_ir_io.cpp`
- Modify: `libs/ure_sceneio/src/native_scene_ir_text.cpp`
- Modify: `libs/ure_sceneio/src/native_scene_ir_model.cpp`
- Modify: `libs/ure_sceneio/CMakeLists.txt`
- Test: `tests/host/test_native_procedural_graph.cpp`

**Interfaces:**
- Consumes: `URPG` generated Object API and validated `ProceduralGraph`.
- Produces: private `encode_procedural_graph`, `decode_procedural_graph`, JSON projection, archive preservation, and graph-inclusive source semantic hashing.

- [ ] **Step 1: Add failing roundtrip tests**

Assert repeated binary writes are byte-identical, repeated text writes are identical, binary/text loads preserve a graph, and both projections have the same graph-inclusive source hash. Reverse source vector order and assert canonical writes remain identical.

- [ ] **Step 2: Add malformed-input tests**

Cover bad `URPG` identifier, truncation, unknown node union tag, unsupported node major version, bad connection, missing required `procedural_graph` dependency, duplicate procedural chunks, and count/budget overflow.

- [ ] **Step 3: Run RED**

Expected: roundtrip loses the graph because Q.3 serialization does not yet carry `URPG`.

- [ ] **Step 4: Implement FlatBuffers and JSON mapping**

Map every public enum/field explicitly. Never `static_cast` schema enums based on C++ declaration order. Decode through verifier and Object API into owned values, validate, and convert all exceptions to `URE-Q4-SCHEMA-*` diagnostics.

- [ ] **Step 5: Integrate binary container**

When a graph is present, write one required chunk:

```cpp
{"procedural_graph", static_cast<std::uint32_t>(ChunkKind::ProceduralGraph),
 {1, 0}, RequirementLevel::Required,
 static_cast<std::uint32_t>(CompressionCodec::None), 8,
 {}, dependencies, encode_procedural_graph(*archive.procedural_graph)}
```

Read exactly zero or one such chunk, validate dependencies, and attach an immutable graph. Preserve unknown optional chunks unchanged.

- [ ] **Step 6: Integrate canonical `.ure` projection and semantic hashing**

Add a top-level `procedural_graph` object only when present. Keep generated artifacts absent from source. Extend `scene_ir_semantic_hash` with a domain-separated canonical procedural payload while retaining Q.3 behavior for graph-free archives.

- [ ] **Step 7: Run GREEN and schema gate**

Run targeted CTest and `regenerate_native_scene_schema.ps1`. Expected: all roundtrip/malformed tests and conformance pass.

- [ ] **Step 8: Record checkpoint**

Run `git diff --check`; do not commit.

### Task 4: Deterministic source references and surface scatter

**Files:**
- Create: `libs/ure_sceneio/src/native_procedural_scatter.cpp`
- Create: `libs/ure_sceneio/src/native_procedural_build.cpp`
- Modify: `libs/ure_sceneio/src/native_procedural_internal.hpp`
- Modify: `libs/ure_sceneio/CMakeLists.txt`
- Test: `tests/host/test_native_procedural_graph.cpp`

**Interfaces:**
- Consumes: canonical topological order, resolved parameters, base archive IDs, and Q.3 mesh data.
- Produces: immutable `MeshReference`, `MaterialReference`, `TransformSet`, counter PRF, and evaluator dispatch skeleton.

- [ ] **Step 1: Add failing PRF and scatter tests**

Use a one-triangle and a two-triangle unequal-area mesh. Assert identical builds produce bit-identical transforms, output count and IDs are stable, every point lies on the source surface, scale/yaw bounds hold, and changed seed changes output.

- [ ] **Step 2: Add failing scatter-domain tests**

Reject missing mesh, empty mesh, zero total area, non-triangle indices, count zero, count over budget, reversed scale range, zero scale, non-finite offset, and invalid alignment mode.

- [ ] **Step 3: Run RED**

Expected: build reports unsupported node or has no transform output.

- [ ] **Step 4: Implement the counter PRF**

Hash the domain tag, seed words, canonical graph hash, node ID, seed salt, element index, and dimension lane. Convert the high 24 bits to `float(bits) * 0x1p-24f`. Assign fixed lane numbers to triangle choice, barycentric coordinates, scale components, and yaw.

- [ ] **Step 5: Implement area-weighted sampling**

Validate mesh topology, calculate canonical cumulative triangle weights, select using the fixed PRF lane, use reflected barycentric coordinates, interpolate position, derive the requested normal alignment, and emit transforms in element-index order.

- [ ] **Step 6: Run GREEN**

Run focused CTest twice in separate processes and compare printed source/cache/output hashes. Expected: both runs match exactly.

- [ ] **Step 7: Record checkpoint**

Run `git diff --check`; do not commit.

### Task 5: Instancing, fragment ownership, and deterministic composition

**Files:**
- Create: `libs/ure_sceneio/src/native_procedural_light.cpp`
- Modify: `libs/ure_sceneio/src/native_procedural_build.cpp`
- Modify: `libs/ure_sceneio/src/native_procedural_internal.hpp`
- Test: `tests/host/test_native_procedural_graph.cpp`

**Interfaces:**
- Consumes: `TransformSet`, source references, stable generated-ID helper, and base archive.
- Produces: `Instantiate` evaluation, owned `SceneIRFragment`, `ComposeFragments`, deep-frozen final SceneIR, and `output_hash` without spectrum/light generation yet.

- [ ] **Step 1: Add failing instancing tests**

Assert the generated instance count equals transform count; mesh/material pointers resolve to registered deep-frozen base objects; stable instance IDs use graph/node/local index; and source/base inputs remain unchanged after success.

- [ ] **Step 2: Add failing composition tests**

Cover canonical fragment order, duplicate generated IDs, collision with base IDs, invalid internal reference, duplicate URI with different bytes, equal content coalescing, and transactional failure with no partial output.

- [ ] **Step 3: Run RED**

Expected: unsupported `Instantiate`/`ComposeFragments` or empty final instance list.

- [ ] **Step 4: Implement owned fragment evaluation**

Build instances against ID-indexed base registries. Store aligned source IDs in the fragment. Sanitize graph/node IDs into Q.3 grammar and format local indices as eight decimal digits.

- [ ] **Step 5: Implement canonical transactional composition**

Sort input fragments by producer node ID, preflight every ID/reference/resource conflict, content-deduplicate artifacts, append generated records in stable ID order, and run Q.3 archive validation before returning a value.

- [ ] **Step 6: Run GREEN**

Expected: instancing/composition tests pass and graph-free identity builds retain the original Q.3 semantic hash.

- [ ] **Step 7: Record checkpoint**

Run `git diff --check`; do not commit.

### Task 6: Deterministic blackbody and Gaussian spectrum artifacts

**Files:**
- Create: `libs/ure_sceneio/src/native_procedural_spectrum.cpp`
- Modify: `libs/ure_sceneio/src/native_procedural_build.cpp`
- Modify: `libs/ure_sceneio/src/native_procedural_internal.hpp`
- Test: `tests/host/test_native_procedural_graph.cpp`

**Interfaces:**
- Consumes: resolved spectrum parameters, canonical artifact writer, generated-byte budget.
- Produces: `SpectrumArtifact` with canonical SPD bytes and `NamedResourcePayload` descriptor.

- [ ] **Step 1: Add failing physical spectrum tests**

For blackbody and Gaussian-lines modes, assert sample count, strictly increasing wavelengths, finite/nonnegative values, deterministic bytes, normalization behavior, SHA-256 descriptor hash, byte length, and `resources/generated/spectrum/<hash>.spd` URI.

- [ ] **Step 2: Add failing domain/budget tests**

Reject nonpositive temperature, fewer than two samples, reversed/equal wavelength interval, nonpositive Gaussian amplitude/width, line center outside the interval, non-finite values, and generated-byte budget overflow.

- [ ] **Step 3: Run RED**

Expected: unsupported spectrum node or missing artifact.

- [ ] **Step 4: Implement physical evaluation and canonical SPD output**

Evaluate Planck radiance in double precision with stable overflow handling, evaluate Gaussian sums, apply explicit normalization, and serialize decimal wavelength/value pairs with locale-independent `std::to_chars` plus LF line endings. Reparse with the existing SPD loader in tests.

- [ ] **Step 5: Build the content-addressed descriptor**

Hash exact bytes; set `ResourceKind::SpectralTable`, schema version 1.0, byte/resident length, empty dependencies, and the canonical generated URI.

- [ ] **Step 6: Run GREEN**

Expected: spectrum tests pass and repeated artifact bytes/hash/URI are identical.

- [ ] **Step 7: Record checkpoint**

Run `git diff --check`; do not commit.

### Task 7: Ring, grid, and three-point light rigs

**Files:**
- Modify: `libs/ure_sceneio/src/native_procedural_light.cpp`
- Modify: `libs/ure_sceneio/src/native_procedural_build.cpp`
- Test: `tests/host/test_native_procedural_graph.cpp`

**Interfaces:**
- Consumes: optional `SpectrumArtifact`, typed rig parameters, stable IDs, and fragment composition.
- Produces: valid generated emissive materials and quad lights for all three version-1 layouts.

- [ ] **Step 1: Add failing layout tests**

Assert ring angular spacing and target-facing normals, grid row-major positions and counts, three-point key/fill/rim ratios, nonzero quad area, registered material references, stable IDs, and optional `emission_spd` URI propagation.

- [ ] **Step 2: Add failing physical-domain tests**

Reject zero/negative extent, zero count, coincident center/target, degenerate basis, nonpositive emission or ratio, non-finite values, and light/material budget overflow.

- [ ] **Step 3: Run RED**

Expected: unsupported light-rig node or empty light list.

- [ ] **Step 4: Implement deterministic rig bases and layouts**

Construct a checked orthonormal frame from target and up vectors, emit layout positions in fixed index order, construct corner/edge vectors with consistent winding, and generate one material per distinct power/spectrum setting.

- [ ] **Step 5: Connect optional spectral artifacts**

When present, add the artifact once to the fragment and set the generated material's current spectral extension `emission_spd` to its canonical URI. Do not add Q.6 resource fields.

- [ ] **Step 6: Run GREEN**

Expected: all layout and domain tests pass, and final Q.3 archive validation succeeds.

- [ ] **Step 7: Record checkpoint**

Run `git diff --check`; do not commit.

### Task 8: Retained fixture, compiler boundary, static audit, and complete gate

**Files:**
- Create: `tests/assets/native_scene/q4_procedural_scene/*`
- Create: `scripts/check_phase_q4_static.ps1`
- Modify: `tests/host/test_native_procedural_graph.cpp`
- Modify: `docs/Phase_Q_Native_Scene_Format.md`
- Modify: `PLAN.md`

**Interfaces:**
- Consumes: complete source serialization/build API and existing `GpuSceneCompiler`.
- Produces: retained Q4 source/test asset, static governance gate, full verification evidence, and authoritative Q.4 completion status.

- [ ] **Step 1: Add the failing retained-fixture test**

Load both fixture projections, build them, compare expected source/cache/output hashes, write generated SPD artifacts into an isolated temporary asset root, set that root as the compiler working directory, and compile the final SceneIR through `GpuSceneCompiler`.

- [ ] **Step 2: Generate and retain the fixture**

Create a base triangle mesh/material, scatter transforms into instances, generate a blackbody spectrum, feed it to a ring rig, and compose both fragments. Retain `.ure`, `.urescene`, typed base resources, expected hashes, and reviewable SPD input/resource files. Ensure no unreferenced generated or typed resource remains.

- [ ] **Step 3: Add the Q4 static audit**

Require the `URPG` schema/baseline/generated triple, pinned regeneration entry, public APIs, reserved value 16, procedural chunk kind 17, one host CTest registration, fixture files, graph presence in canonical text, absence of Base64/generated fragment payloads, and `ure_sceneio` independence from `ure_core`/CUDA.

- [ ] **Step 4: Run targeted Release verification**

```powershell
.\scripts\build_x64.ps1 -BuildDir build_modular_x64 -Config Release -SkipConfigure -Targets test_native_procedural_graph
ctest --test-dir build_modular_x64 -C Release -R "^test_native_procedural_graph$" --output-on-failure
.\scripts\regenerate_native_scene_schema.ps1
.\scripts\check_phase_q4_static.ps1
```

Expected: all commands exit zero; retained source/cache/output hashes match.

- [ ] **Step 5: Run the full Release build and CTest gate**

```powershell
.\scripts\build_x64.ps1 -BuildDir build_modular_x64 -Config Release
ctest --test-dir build_modular_x64 -C Release --output-on-failure
```

Expected: build succeeds without warnings and all registered tests pass, raising the CTest total from 28 to 29.

- [ ] **Step 6: Run project static gates**

```powershell
.\scripts\check_phase_q_static.ps1
.\scripts\check_phase_q3_static.ps1
.\scripts\run_phase_l_static_tests.ps1
.\scripts\run_phase_r_static_tests.ps1
.\scripts\run_physics_optics_validation.ps1
```

Expected: every gate exits zero.

- [ ] **Step 7: Review warning/error logs and implementation scope**

Scan the Release build log for warnings/errors, run `git diff --check`, verify only Q.4 files changed, confirm no script/plugin/cache/Q.6 semantics entered, and inspect generated resources for exact manifest correspondence.

- [ ] **Step 8: Update authoritative documentation after evidence exists**

Mark Q.4 complete and Q.5 current only after the complete gate passes. Record exact schema identity, deterministic build contract, fixture, CTest count, and static gates in `docs/Phase_Q_Native_Scene_Format.md` and the Phase Q row in `PLAN.md`.

- [ ] **Step 9: REPORT without committing**

Report changed files, focused/full verification results, static review findings, and any residual limitations. Wait for explicit approval before creating the final Phase Q implementation commit.
