# Phase Q.3 Native SceneIR Serialization Implementation Plan

> Archive status: completed execution record. Checkboxes, counts and next steps below are not a live plan.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans and superpowers:test-driven-development to implement this plan task-by-task. Execute inline as one agent. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add deterministic, lossless native serialization for the complete current `scene_ir::SceneIR`, with stable source identities, independently typed mesh/Mie payloads, canonical exploded text, safe file I/O, and compatibility with `GpuSceneCompiler`.

**Architecture:** `ure_sceneio` owns a retained `NativeSceneArchive` above the Q.0-Q.2 container. A small FlatBuffers scene graph references content-addressed `URMS` mesh and `URMI` Mie chunks; the same graph has a canonical JSON projection for `.ure`. Decode is layered: container/document, typed payloads, identity/reference validation, immutable SceneIR reconstruction. `ure_sceneio` remains independent of CUDA and `ure_core`; only the test target crosses the compiler boundary.

**Tech Stack:** C++23 host code, FlatBuffers 25.12.19 schemas and checked-in C++ headers, nlohmann/json canonical projection, SHA-256 and checked Q.2 containers, CMake/Ninja, existing C-style host test harness.

## Global Constraints

- Implement only PLAN.md Q.3; Q.4-Q.12 remain out of scope.
- Preserve exact finite IEEE-754 scalar bits in typed binary payloads and normalize signed zero only in the semantic hash stream.
- Reject NaN, infinity, invalid enum values, dangling/unregistered references, unsafe paths, malformed payloads, overflow, and resource-budget violations before compiler entry.
- Keep large mesh and Mie arrays outside the scene graph and address them by SHA-256 identity.
- Keep `ure_sceneio` linked only to `ure_types` and `ure_diag`; no CUDA or `ure_core` includes in production code.
- Preserve unknown optional chunks byte-for-byte and reject unknown required chunks.
- Normal builds consume checked-in generated headers and never invoke `flatc`.
- Follow repository governance: no intermediate commits; report after full verification and wait for explicit commit approval.
- Development builds target the existing `build_modular_x64` tree and CUDA architecture 120 only.

## File Map

- Create `libs/ure_sceneio/include/ure/native_scene_ir.hpp`: public archive, resource payload, encode/decode, validation, hash, and file APIs.
- Create `libs/ure_sceneio/src/native_scene_ir_model.cpp`: identity creation, deep-freeze, reference maps, validation, semantic hash.
- Create `libs/ure_sceneio/src/native_scene_ir_resources.cpp`: `URMS` and `URMI` typed payload encode/decode and structural validation.
- Create `libs/ure_sceneio/src/native_scene_ir_graph.cpp`: `URIG` graph encode/decode and canonical JSON projection.
- Create `libs/ure_sceneio/src/native_scene_ir_io.cpp`: `.urescene` composition/decomposition, exploded archive checking, bounded reads, atomic replacement.
- Create `schemas/ure_scene_ir_v1.fbs`, `schemas/ure_mesh_v1.fbs`, `schemas/ure_mie_v1.fbs` plus matching `.baseline.fbs` copies.
- Create checked-in generated headers under `libs/ure_sceneio/generated/`.
- Modify `scripts/regenerate_native_scene_schema.ps1`: conform and verify all four schemas using exactly FlatBuffers 25.12.19.
- Create `tests/host/test_native_scene_ir.cpp`: Q.3 focused unit, negative, determinism, and compiler-boundary tests.
- Modify `libs/ure_sceneio/CMakeLists.txt` and `tests/host/CMakeLists.txt`: compile production sources and register the 28th CTest.
- Create `tests/assets/native_scene/q3_full_scene/`: reviewable `.ure`, typed resources, retained `.urescene`, and semantic-hash fixture.
- Create `scripts/check_phase_q3_static.ps1`: source/schema/fixture/architecture audit.
- Modify `docs/Phase_Q_Native_Scene_Format.md` and the Q.3 row/status in `PLAN.md` only after verification.

---

### Task 1: Public archive boundary and registered test target

**Files:**
- Create: `libs/ure_sceneio/include/ure/native_scene_ir.hpp`
- Create: `tests/host/test_native_scene_ir.cpp`
- Modify: `tests/host/CMakeLists.txt`

**Interfaces:**
- Consumes: `SceneDocument`, `ContainerChunk`, `ValidationLimits`, `CapabilityRegistry`, `scene_ir::SceneIR`.
- Produces: `NamedResourcePayload`, `NativeSceneSourceIds`, `NativeSceneArchive`, `ExplodedSceneArchive`, and the exact Q.3 public functions from the approved specification.

- [ ] **Step 1: Write the failing API smoke test**

```cpp
#include <ure/native_scene_ir.hpp>

static void test_archive_identity_defaults() {
    ure::scene_ir::SceneIR scene;
    scene.materials.push_back(std::make_shared<ure::scene_ir::MaterialNode>());
    ure::native_scene::SceneDocument document;
    document.id = "scene/q3";
    const auto archive = ure::native_scene::make_native_scene_archive(document, scene);
    CHECK(archive.source_ids.materials == std::vector<std::string>{"material/00000000"});
}
```

- [ ] **Step 2: Register and build the target to prove RED**

Run: `.\scripts\build_x64.ps1 -BuildDir build_modular_x64 -Config Release -Targets test_native_scene_ir`

Expected: compilation fails because `ure/native_scene_ir.hpp` does not exist.

- [ ] **Step 3: Add the complete public declarations**

```cpp
struct NamedResourcePayload {
    std::string id;
    ResourceDescriptor descriptor;
    std::vector<std::uint8_t> payload;
};

struct NativeSceneSourceIds {
    std::vector<std::string> materials, meshes, images, textures;
    std::vector<std::string> instances, spheres, quad_lights;
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
```

Declare every function signature verbatim from the approved Q.3 design, including `make_native_scene_archive`, binary/text read/write, semantic hash, validation, and filesystem save/load.

- [ ] **Step 4: Add a temporary compiling identity implementation in Task 2's source and prove GREEN**

Run the target and `ctest --test-dir build_modular_x64 -C Release -R "^test_native_scene_ir$" --output-on-failure`.

Expected: target builds and the identity-default test passes.

### Task 2: Stable IDs, deep freeze, reference integrity, and semantic model

**Files:**
- Create: `libs/ure_sceneio/src/native_scene_ir_model.cpp`
- Modify: `libs/ure_sceneio/CMakeLists.txt`
- Modify: `tests/host/test_native_scene_ir.cpp`

**Interfaces:**
- Consumes: public archive types from Task 1 and all current SceneIR fields.
- Produces: deterministic identity tables, owned archive graphs, `validate_scene_ir_archive`, `scene_ir_semantic_hash`.

- [ ] **Step 1: Add failing tests for all identity and ownership invariants**

Tests must mutate caller material, texture, image, mesh, graph, and Mie objects after `make_native_scene_archive` and assert the archive hash is unchanged; assert stable eight-digit IDs; assert loaded custom IDs survive rewrite; assert identity-length mismatch, duplicate ID, reserved namespace misuse, null registry entry, and unregistered material/mesh/image/texture references return `URE-Q3-ID-*` or `URE-Q3-REF-*`.

- [ ] **Step 2: Run the focused target to prove RED**

Expected: deep-freeze and validation assertions fail.

- [ ] **Step 3: Implement graph-aware deep copy and identity validation**

Use pointer maps for registered images, textures, meshes, and materials. Clone each registry exactly once, then resolve references only through those maps. Clone Mie resources through a content map so equal typed content shares one immutable instance. Generate IDs with:

```cpp
std::string indexed_id(std::string_view prefix, std::size_t index) {
    return std::format("{}/{:08}", prefix, index);
}
```

Validation must compare each identity vector length with its SceneIR vector, enforce grammar `[a-z][a-z0-9._-]*(/[a-z0-9._-]+)+`, uniqueness, and the fixed reserved prefixes for generated domains.

- [ ] **Step 4: Implement a complete canonical semantic stream**

Append explicit enum tags, stable IDs, ordered vector sizes, all scalar bit patterns with signed zero normalized, graph node/input order, reference IDs, and typed resource SHA-256 hashes. Prefix each variable-length item with checked `uint64_t` length and hash the stream with `sha256_hex`.

- [ ] **Step 5: Run the target and verify GREEN**

Expected: ownership, identity, and hash-coverage tests pass without warnings.

### Task 3: Versioned `URMS` mesh resource

**Files:**
- Create: `schemas/ure_mesh_v1.fbs`
- Create: `schemas/ure_mesh_v1.baseline.fbs`
- Create: `libs/ure_sceneio/generated/ure_mesh_v1_generated.h`
- Create/Modify: `libs/ure_sceneio/src/native_scene_ir_resources.cpp`
- Modify: `tests/host/test_native_scene_ir.cpp`

**Interfaces:**
- Produces internal `encode_mesh_payload(const scene::Mesh&)`, `decode_mesh_payload(span, limits)`, and `validate_mesh` helpers used by graph/container code.

- [ ] **Step 1: Write failing roundtrip and rejection tests**

Cover exact float-bit retention including `-0.0f`, empty optional normals/UVs, index-count not divisible by three, negative/out-of-range index, mismatched normals/UVs, NaN/infinity, truncated buffer, bad identifier, declared-count overflow, and resource budget overflow.

- [ ] **Step 2: Define the stable schema**

```flatbuffers
namespace ure.native.schema;
table MeshPayload {
  positions:[float] (id: 0);
  normals:[float] (id: 1);
  uvs:[float] (id: 2);
  indices:[int32] (id: 3);
}
root_type MeshPayload;
file_identifier "URMS";
```

Copy it byte-for-byte to the v1 baseline, generate the scoped C++ header with FlatBuffers 25.12.19, and check it in.

- [ ] **Step 3: Implement checked encode/decode**

Verify the identifier and FlatBuffers verifier before accessing vectors. Check multiplication/addition before allocations. Convert triples/pairs to `Point3f`, `Normal3f`, and `Point2f`; validate structure and finite values before returning.

- [ ] **Step 4: Run focused tests and schema regeneration gate**

Expected: every mesh positive/negative test passes; generated output equals checked-in header.

### Task 4: Versioned `URMI` Mie resource

**Files:**
- Create: `schemas/ure_mie_v1.fbs`
- Create: `schemas/ure_mie_v1.baseline.fbs`
- Modify: `libs/ure_sceneio/src/native_scene_ir_resources.cpp`
- Create: `libs/ure_sceneio/generated/ure_mie_v1_generated.h`
- Modify: `tests/host/test_native_scene_ir.cpp`

**Interfaces:**
- Produces internal `encode_mie_payload(const MiePhaseResource&)` and `decode_mie_payload(span, limits)`.

- [ ] **Step 1: Add failing full-field, content-addressing, and malformed-input tests**

Roundtrip every current array plus `polarization_model`, `provenance`, `source_hash`, and `content_hash`; assert two equal resources coalesce and distinct resources do not; reject invalid enum, non-finite values, invalid table shape/order/CDF, truncation, overflow, and budget excess.

- [ ] **Step 2: Define `URMI` with explicit field IDs**

The schema contains all eight float vectors, a fixed numeric polarization enum, and the three strings. Copy to baseline and check in the generated scoped header.

- [ ] **Step 3: Implement verifier-first decode and existing physical validation reuse**

Decode into owned vectors, enforce checked sizes before allocation, call current `validate_mie_phase_resource`, and translate all failures to stable `URE-Q3-MIE-*` diagnostics.

- [ ] **Step 4: Run focused tests and prove GREEN**

Expected: Mie roundtrip, coalescing, and all rejection cases pass.

### Task 5: `URIG` source graph and complete SceneIR reconstruction

**Files:**
- Create: `schemas/ure_scene_ir_v1.fbs`
- Create: `schemas/ure_scene_ir_v1.baseline.fbs`
- Create: `libs/ure_sceneio/generated/ure_scene_ir_v1_generated.h`
- Create: `libs/ure_sceneio/src/native_scene_ir_graph.cpp`
- Modify: `tests/host/test_native_scene_ir.cpp`

**Interfaces:**
- Consumes: stable IDs and typed resource IDs/hashes.
- Produces internal graph encode/decode used by binary and text paths.

- [ ] **Step 1: Add a failing full-current-field fixture test**

Construct multiple materials/images/textures/meshes plus instance, sphere, quad light, camera, physics/fluid, world medium, render compatibility fields, local/global Mie, spectral extension, rigid body, and MaterialGraphs whose union exercises all 15 current node kinds. Compare every field and shared-pointer relation after decode.

- [ ] **Step 2: Add failing graph/enum/reference rejection tests**

Cover invalid material/image/phase/node enums, duplicate graph node IDs, missing root/input, cycles, dangling object/resource IDs, non-finite scalar, invalid FOV/aspect/aperture/focus, nonpositive sphere radius, zero scale component, negative physical coefficients, and invalid render dimensions.

- [ ] **Step 3: Define `URIG` with explicit IDs and reference strings**

Use fixed schema enums independent of C++ declaration order. Store vectors/quaternion as scalar structs or tables; quaternion order is `w,x,y,z`. Store object tables in SceneIR vector order and carry ordered source IDs. Mesh/Mie references contain both resource ID and required SHA-256.

- [ ] **Step 4: Implement deterministic encode and verifier-first decode**

Map every enum through explicit switch statements with default rejection. Decode records into temporary owned values, validate graph and references, then build shared registries. Run `MaterialGraph::validate()` and translate exceptions into `URE-Q3-GRAPH-*` diagnostics.

- [ ] **Step 5: Run focused tests and schema gate**

Expected: full fixture field comparator passes; every malformed graph fails before compiler use.

### Task 6: Binary `.urescene` composition and optional chunk preservation

**Files:**
- Modify: `libs/ure_sceneio/src/native_scene_ir_io.cpp`
- Modify: `tests/host/test_native_scene_ir.cpp`

**Interfaces:**
- Implements: `write_scene_ir_binary`, `read_scene_ir_binary`.

- [ ] **Step 1: Add failing determinism, roundtrip, sharing, and chunk tests**

Assert repeated writes are byte-identical; binary roundtrip retains semantic hash/custom IDs/sharing; equal mesh/Mie payloads produce one content chunk; unknown optional chunks survive byte-for-byte; unknown required chunks fail; missing, duplicate, extra, wrong-type, wrong-hash, and truncated typed chunks fail.

- [ ] **Step 2: Compose the Q.2 native container**

Write one required `metadata` chunk, one required `scene_graph` chunk, sorted content-addressed `mesh/<sha256>` and `mie/<sha256>` chunks, then preserved optional chunks in stable ID order. Resource descriptors must carry exact hash, byte length, resident bytes, kind, schema version, and safe relative URI.

- [ ] **Step 3: Decode in validation layers**

Call `read_container`; locate exactly one metadata and graph; decode document; index typed chunks; verify descriptor/hash/type/dependency consistency; decode resources; reconstruct graph; preserve only unknown optional chunks; combine diagnostics without printing or throwing across the public load boundary.

- [ ] **Step 4: Run focused binary tests**

Expected: deterministic bytes, semantic roundtrip, coalescing, and preservation tests pass.

### Task 7: Canonical exploded `.ure` projection

**Files:**
- Modify: `libs/ure_sceneio/src/native_scene_ir_graph.cpp`
- Modify: `libs/ure_sceneio/src/native_scene_ir_io.cpp`
- Modify: `tests/host/test_native_scene_ir.cpp`

**Interfaces:**
- Implements: `write_scene_ir_text`, `read_scene_ir_text`.

- [ ] **Step 1: Add failing canonical projection tests**

Assert canonical manifest is byte-identical across writes, ends with one newline, contains `scene_ir`, contains no Base64 or large numeric mesh/Mie arrays, and produces the same semantic hash as binary. Reject BOM, unknown keys, duplicate/missing/unreferenced resource IDs, unsafe URI, wrong kind/hash/length, and numeric inline arrays over the Q.2 limit.

- [ ] **Step 2: Implement the dedicated strict Q.3 JSON writer/parser**

Reuse the Q.2 document canonical representation rules while explicitly recognizing one `scene_ir` object. Sort set-like features/extensions/resources, preserve SceneIR ordered ID arrays, emit floats through exact finite round-trippable formatting, and store mesh/Mie bytes only in `ExplodedSceneArchive.resources`.

- [ ] **Step 3: Validate resources before graph construction**

Run safe-relative-path checks, ID uniqueness, exact byte length, SHA-256, resource kind/schema, missing/extra reference checks, and budgets before decoding any typed payload.

- [ ] **Step 4: Run focused text/binary equivalence tests**

Expected: canonical text and binary archives have identical semantic hashes and all malformed exploded archives fail loudly.

### Task 8: Bounded atomic file I/O

**Files:**
- Modify: `libs/ure_sceneio/src/native_scene_ir_io.cpp`
- Modify: `tests/host/test_native_scene_ir.cpp`

**Interfaces:**
- Implements: `save_native_scene`, `load_native_scene`.

- [ ] **Step 1: Add failing filesystem tests**

Cover `.urescene` save/load, exploded `.ure` plus sibling resource directory, maximum stored-byte enforcement before allocation, traversal and symlink escape, and a test-only injected replacement failure that leaves the original destination bytes unchanged and removes the temporary file.

- [ ] **Step 2: Implement bounded reads and same-directory replacement**

Use `file_size` with checked conversion before vector allocation. Write a uniquely named same-directory temporary, flush/close and check stream state, then replace destination using same-volume filesystem operations. On any failure, remove only the verified temporary path and preserve the old destination.

- [ ] **Step 3: Run filesystem tests**

Expected: successful roundtrips pass and injected failures preserve the previous authoritative file exactly.

### Task 9: Full fixture and compiler boundary

**Files:**
- Create: `tests/assets/native_scene/q3_full_scene/full_scene.ure`
- Create: typed resources and `full_scene.urescene` in the same fixture tree
- Create: `tests/assets/native_scene/q3_full_scene/semantic_hash.txt`
- Modify: `tests/host/test_native_scene_ir.cpp`
- Modify: `tests/host/CMakeLists.txt`

**Interfaces:**
- Consumes: public Q.3 load APIs.
- Verifies: reconstructed SceneIR is accepted by existing `GpuSceneCompiler` without production dependency inversion.

- [ ] **Step 1: Add failing fixture and compiler tests**

Load exploded and binary fixtures, compare their pinned semantic hash, inspect full-field coverage, and call `gpu::GpuSceneCompiler::compile` for both a minimal archive and the full fixture.

- [ ] **Step 2: Link only the test to `ure_core` and add asset root definition**

```cmake
target_link_libraries(test_native_scene_ir PRIVATE ure_sceneio ure_core ure_diag)
target_compile_definitions(test_native_scene_ir PRIVATE
    URE_TEST_ASSET_DIR="${CMAKE_SOURCE_DIR}/tests/assets/native_scene/q3_full_scene")
```

- [ ] **Step 3: Generate deterministic fixture artifacts through the public writers**

Retain the exploded canonical manifest, each typed resource, binary production scene, and lowercase semantic hash. Regenerating from the same source must yield identical bytes.

- [ ] **Step 4: Build and run the Q.3 target**

Expected: fixture parity and both compiler-boundary tests pass.

### Task 10: Schema/static gates, documentation, and full verification

**Files:**
- Modify: `scripts/regenerate_native_scene_schema.ps1`
- Create: `scripts/check_phase_q3_static.ps1`
- Modify: `docs/Phase_Q_Native_Scene_Format.md`
- Modify: `PLAN.md` Q.3 status row and Phase Q cursor only

**Interfaces:**
- Produces: reproducible schema audit and governance completion evidence.

- [ ] **Step 1: Extend the FlatBuffers reproducibility gate**

Loop over `ure_native_v1`, `ure_scene_ir_v1`, `ure_mesh_v1`, and `ure_mie_v1`; for each, run `flatc --conform`, generate to a verified system-temp directory, compare SHA-256 against the checked-in header, and safely delete only that temp directory. Require version text exactly `flatc version 25.12.19`.

- [ ] **Step 2: Add and run the Phase Q.3 static audit**

The script must assert all schema/baseline/generated triples exist, identifiers are `URIG/URMS/URMI`, public API names exist, production `ure_sceneio` has no `ure_core`/CUDA include or link, fixture files exist, no Base64/inline large arrays appear in `.ure`, and Q.3 is registered as one host CTest.

- [ ] **Step 3: Run focused Release verification**

Run:

```powershell
.\scripts\build_x64.ps1 -BuildDir build_modular_x64 -Config Release -Targets test_native_scene,test_native_scene_ir
ctest --test-dir build_modular_x64 -C Release -R "^test_native_scene$|^test_native_scene_ir$" --output-on-failure
.\scripts\regenerate_native_scene_schema.ps1 -Flatc <pinned-flatc-25.12.19>
.\scripts\check_phase_q_static.ps1
.\scripts\check_phase_q3_static.ps1
```

Expected: both tests pass and all schema/static gates print success.

- [ ] **Step 4: Run the complete project gate**

Run:

```powershell
.\scripts\build_x64.ps1 -BuildDir build_modular_x64 -Config Release
ctest --test-dir build_modular_x64 -C Release --output-on-failure
.\scripts\check_phase_l_static.ps1
.\scripts\check_phase_r_static.ps1
.\scripts\check_physics_optics_build.ps1
```

Expected: Release build succeeds with no new warnings, all 28/28 CTests pass, and every long-lived audit gate passes.

- [ ] **Step 5: Self-review before status updates**

Review every approved design requirement against tests; scan modified production files for CUDA/core leakage, comments outside the allowed rule, unchecked allocations, exception leakage, duplicate IDs, and incomplete current SceneIR field coverage. Inspect `git diff --check`, `git diff --stat`, and the exact PLAN diff.

- [ ] **Step 6: Update authoritative status and report without committing**

Mark only Q.3 complete and advance Phase Q's internal cursor to Q.4. Report changed behavior, verification evidence, and review findings in Chinese. Wait for explicit user approval before creating the single Phase Q.3 implementation commit; do not push.

## Plan Self-Review Record

- Spec coverage: Tasks 1-9 map every approved API, identity, typed schema, graph field, binary/text representation, validation, compiler boundary, file I/O, and retained fixture requirement; Task 10 covers gates and governance.
- Placeholder scan: no deferred implementation markers or unspecified error-handling steps remain; each negative domain has explicit expected diagnostics/tests.
- Type consistency: the public types and function names match the approved design; internal resource/graph helpers are produced before the binary/text/file layers consume them.
- Scope: Q.6 generalized spectral resources, Q.7 solver declarations, Q.8 expanded physics/acoustics, Q.9 CLI/Python, Q.10 adapters, and Q.11 caches are explicitly excluded.
