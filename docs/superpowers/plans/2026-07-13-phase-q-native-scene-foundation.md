# Phase Q Native Scene Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` and `superpowers:test-driven-development` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. UltraRender governance requires single-agent inline execution by default; do not dispatch subagents.

**Goal:** Implement the Q.0-Q.2 native scene foundation: backend-neutral schema types, deterministic text and binary encodings, safe package/resource validation, and a complete capability-ownership audit.

**Architecture:** `ure_types` owns plain backend-neutral value types. `ure_sceneio` owns validation, SHA-256 semantic identity, canonical JSON projection, FlatBuffers metadata conversion, and the checked chunk container used by `.urescene` and `.urepkg`. Large payloads remain independent hash-addressed chunks; `.urecache` entries are explicitly excluded from authoritative package identity.

**Tech Stack:** C++23, nlohmann/json 3.x, FlatBuffers `v25.12.19` C++ runtime and `flatc`-generated headers, CMake/Ninja, PowerShell static gates, CTest.

## Global Constraints

- Scope is Q.0-Q.2 only; complete SceneIR serialization and runtime compilation begin in Q.3.
- `.urescene` and `.urepkg` are indexed little-endian binary containers; `.ure` is canonical UTF-8 JSON; `.urecache` is non-authoritative.
- Normal configure/build is offline and never invokes `flatc` or downloads dependencies.
- Vendor the FlatBuffers C++ headers and Apache-2.0 license from release `v25.12.19`; generated headers record that exact generator version.
- All file offsets, lengths, counts, alignment operations, ratios, and aggregate budgets use checked 64-bit arithmetic before allocation.
- Unknown required features, extensions, chunks, and codecs are errors; unknown optional extensions and chunks retain opaque bytes.
- Stable IDs match `[A-Za-z0-9][A-Za-z0-9._/-]{0,254}` and reject empty, `.`, `..`, repeated, leading, and trailing path segments.
- Semantic hashes exclude offsets, compression, text whitespace, provenance timestamps, and cache entries.
- No CUDA or backend handles enter the schema; no GPU source is modified.
- Use Release development builds for only the configured `sm_120` architecture.
- Follow RED-GREEN-REFACTOR for every production behavior. No intermediate commits; report verified results and wait for explicit commit approval.

---

### Task 1: Foundation value types and test target

**Files:**
- Create: `libs/ure_types/include/ure/native_scene.hpp`
- Create: `tests/host/test_native_scene.cpp`
- Modify: `tests/host/CMakeLists.txt`

**Interfaces:**
- Produces namespace `ure::native_scene` with `Version`, `SceneConventions`, `FeatureDeclaration`, `ExtensionRecord`, `ResourceDescriptor`, `SceneDocument`, `PackageManifest`, `ValidationLimits`, `CapabilityRegistry`, `ValidationDiagnostic`, and `LoadResult<T>`.
- Produces enums `RequirementLevel`, `DiagnosticSeverity`, `ResourceKind`, `ChunkKind`, `CompressionCodec`, and `ContainerKind`.
- Later tasks consume these types without depending on `SceneIR`, CUDA, or renderer internals.

- [ ] **Step 1: Register a deliberately failing host test target**

  Add `test_native_scene` to `tests/host/CMakeLists.txt`, link `ure_sceneio ure_diag`, and define `URE_TEST_ASSET_DIR="${CMAKE_SOURCE_DIR}/tests/assets/native_scene"`. Create a test file that includes `<ure/native_scene.hpp>`, constructs version 1.0 scene/package objects, and statically checks that offsets and lengths use `std::uint64_t`.

- [ ] **Step 2: Verify RED because the public header does not exist**

  Run:

  ```powershell
  .\scripts\build_x64.ps1 -BuildDir build_modular_x64 -Config Release -SkipBuild
  .\scripts\build_x64.ps1 -BuildDir build_modular_x64 -Config Release -SkipConfigure -Targets test_native_scene
  ```

  Expected: compilation fails with `cannot open include file: 'ure/native_scene.hpp'`.

- [ ] **Step 3: Add backend-neutral value types**

  Define these exact public shapes in `native_scene.hpp`:

  ```cpp
  namespace ure::native_scene {
  struct Version { std::uint32_t major = 1; std::uint32_t minor = 0; auto operator<=>(const Version&) const = default; };
  enum class RequirementLevel { Required, Optional, Advisory };
  enum class DiagnosticSeverity { Error, Warning, Info };
  enum class ResourceKind { Scene, Geometry, MaterialGraph, Texture, SpectralTable, MiePhase, VolumeField, Animation, Physics, Acoustic, Video, Validation, Provenance, Cache, Extension };
  enum class ChunkKind : std::uint32_t { Metadata = 1, SceneGraph = 2, Geometry = 3, MaterialGraph = 4, Texture = 5, SpectralTable = 6, MiePhase = 7, VolumeField = 8, Animation = 9, Physics = 10, Acoustic = 11, Video = 12, Validation = 13, Provenance = 14, CacheReference = 15, Extension = 0x80000000u };
  enum class CompressionCodec : std::uint32_t { None = 0 };
  enum class ContainerKind { Scene, Package };

  struct SceneConventions {
      std::string length_unit = "metre";
      std::string time_unit = "second";
      std::string mass_unit = "kilogram";
      std::string angle_unit = "radian";
      std::string wavelength_unit = "vacuum_nanometre";
      std::string handedness = "right";
      std::string up_axis = "+Y";
      std::string camera_forward = "-Z";
      std::string color_encoding = "linear_radiometric";
  };

  struct FeatureDeclaration {
      std::string name;
      Version minimum_version;
      RequirementLevel requirement = RequirementLevel::Required;
      std::string provider;
      std::vector<std::string> dependencies;
      std::string canonical_parameters = "{}";
  };

  struct ExtensionRecord {
      std::string name;
      Version version;
      RequirementLevel requirement = RequirementLevel::Required;
      std::string payload_type;
      std::vector<std::uint8_t> opaque_payload;
  };

  struct ResourceDescriptor {
      std::string id;
      std::string content_hash;
      ResourceKind kind = ResourceKind::Extension;
      Version schema_version;
      std::string uri;
      std::vector<std::string> dependencies;
      std::uint64_t byte_length = 0;
      std::uint64_t resident_bytes = 0;
  };

  struct SceneDocument {
      std::string id;
      Version schema_version;
      SceneConventions conventions;
      std::vector<FeatureDeclaration> features;
      std::vector<ExtensionRecord> extensions;
      std::vector<ResourceDescriptor> resources;
      std::vector<MigrationRecord> migrations;
  };

  struct PackageManifest {
      std::string id;
      Version format_version;
      std::vector<SceneReference> scenes;
      std::vector<ResourceDescriptor> resources;
      std::vector<ResourceDescriptor> caches;
      std::vector<PackageDependency> dependencies;
  };
  }
  ```

  Also define exact constants for container/schema version 1.0, canonical identity strings, and permanently reserved chunk IDs/flag bits. `LoadResult<T>` contains `std::optional<T> value`, diagnostics, and `ok()` that is false when any error exists.

- [ ] **Step 4: Verify GREEN and run the registered test**

  Run the selected build and:

  ```powershell
  ctest --test-dir build_modular_x64 -C Release -R "^test_native_scene$" --output-on-failure
  ```

  Expected: one selected test passes and the target emits no warnings.

---

### Task 2: Structured validation and deterministic SHA-256 identity

**Files:**
- Create: `libs/ure_sceneio/include/ure/native_scene_validation.hpp`
- Create: `libs/ure_sceneio/include/ure/native_scene_hash.hpp`
- Create: `libs/ure_sceneio/src/native_scene_validation.cpp`
- Create: `libs/ure_sceneio/src/native_scene_hash.cpp`
- Modify: `libs/ure_sceneio/CMakeLists.txt`
- Modify: `tests/host/test_native_scene.cpp`

**Interfaces:**
- Produces `ValidationReport validate_scene_document(const SceneDocument&, const CapabilityRegistry&, const ValidationLimits&)`.
- Produces `ValidationReport validate_package_manifest(const PackageManifest&, const CapabilityRegistry&, const ValidationLimits&)`.
- Produces `std::string sha256_hex(std::span<const std::uint8_t>)`, `semantic_hash(const SceneDocument&)`, and `semantic_hash(const PackageManifest&)`.

- [ ] **Step 1: Write RED tests for hash and schema validation**

  Add independent cases that assert:

  ```cpp
  CHECK(sha256_hex(as_bytes("abc")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK(validate_scene_document(valid_scene(), supported_registry(), limits()).ok());
  CHECK(has_code(validate_scene_document(scene_with_id("../escape"), supported_registry(), limits()), "URE-Q-ID-001"));
  CHECK(has_code(validate_scene_document(scene_with_unknown_required_feature(), supported_registry(), limits()), "URE-Q-FEATURE-001"));
  CHECK(has_code(validate_scene_document(scene_with_unknown_optional_feature(), supported_registry(), limits()), "URE-Q-FEATURE-101"));
  CHECK(has_code(validate_package_manifest(package_with_cycle(), supported_registry(), limits()), "URE-Q-DEP-001"));
  ```

  Cover duplicate IDs, invalid/non-lowercase SHA-256, absolute/drive/UNC paths, traversal, required unsupported extension, compatible newer minor, unsupported major, invalid conventions, budget overflow, checked-add overflow, and reserved IDs.

- [ ] **Step 2: Verify RED at link time**

  Build `test_native_scene` and confirm unresolved validation/hash symbols.

- [ ] **Step 3: Implement SHA-256 and canonical semantic streams**

  Implement SHA-256 locally with `std::uint32_t` state and known FIPS round constants. Feed semantic fields in schema order; prefix strings/vectors with unsigned 64-bit little-endian lengths. Normalize signed zero for future floating fields. Sort only set-like collections by stable ID/name in a copy; preserve arrays whose order is semantic. Package hashing includes scene references and authoritative resources, and excludes `caches` and physical URIs for embedded resources.

- [ ] **Step 4: Implement four-stage validation**

  Validation must append diagnostics rather than print or throw for input errors:

  1. stable ID, URI, hash, version, and convention checks;
  2. duplicate/reserved identifier checks;
  3. resource/package dependency DFS with explicit white/gray/black state;
  4. checked aggregate byte/resident budgets and feature/extension capability checks.

  Reject non-finite or non-object `canonical_parameters` by parsing it with nlohmann/json and re-dumping canonically. Optional unsupported declarations remain valid with warning diagnostics.

- [ ] **Step 5: Verify GREEN and regression-test scene I/O**

  Run:

  ```powershell
  .\scripts\build_x64.ps1 -BuildDir build_modular_x64 -Config Release -SkipConfigure -Targets test_native_scene,test_gltf_frontend,test_mie_phase
  ctest --test-dir build_modular_x64 -C Release -R "^(test_native_scene|test_gltf_frontend|test_mie_phase)$" --output-on-failure
  ```

  Expected: 3/3 pass.

---

### Task 3: Canonical `.ure` text projection

**Files:**
- Create: `libs/ure_sceneio/include/ure/native_scene_text.hpp`
- Create: `libs/ure_sceneio/src/native_scene_text.cpp`
- Modify: `libs/ure_sceneio/CMakeLists.txt`
- Modify: `tests/host/test_native_scene.cpp`

**Interfaces:**
- Produces `std::string write_scene_text(const SceneDocument&)` and `LoadResult<SceneDocument> read_scene_text(std::string_view, const CapabilityRegistry&, const ValidationLimits&)`.
- Produces corresponding `write_package_text` and `read_package_text` functions for exploded package manifests.

- [ ] **Step 1: Write RED canonical text tests**

  Assert repeated writes are byte-identical, keys are stable, output ends in one newline, BOM/comments/NaN/Base64 payload fields are rejected, large inline numeric arrays above 64 scalars are rejected, and parse-write-parse preserves semantic hashes. Add an unknown optional extension with opaque bytes `{0x00, 0x7f, 0xff}` and assert its hex representation survives byte-for-byte.

- [ ] **Step 2: Verify RED because text API symbols are absent**

  Build the selected target and confirm the expected unresolved symbols.

- [ ] **Step 3: Implement strict JSON parsing and canonical writing**

  Use nlohmann/json only inside `ure_sceneio`. Require exact core field names and JSON types. Encode opaque payloads as lowercase hexadecimal under `opaque_payload_hex`; never Base64. Recursively count numeric array scalars before allocating typed data. Convert every parser exception into `URE-Q-TEXT-*` diagnostics. Canonical output uses ordered object keys supplied in schema order and two-space indentation with a single trailing newline.

- [ ] **Step 4: Verify GREEN**

  Build and run only `test_native_scene`; expected pass with no stderr output.

---

### Task 4: Version-1 FlatBuffers metadata schema and reproducible generation

**Files:**
- Create: `schemas/ure_native_v1.fbs`
- Create: `schemas/ure_native_v1.baseline.fbs`
- Create: `libs/ure_sceneio/generated/ure_native_v1_generated.h`
- Create: `scripts/regenerate_native_scene_schema.ps1`
- Create: `third_party/flatbuffers/LICENSE.txt`
- Vendor: `third_party/flatbuffers/include/flatbuffers/*.h` from tag `v25.12.19`
- Modify: `libs/ure_sceneio/CMakeLists.txt`
- Modify: `tests/host/test_native_scene.cpp`

**Interfaces:**
- Produces FlatBuffers root `ure.native.fb.MetadataEnvelope` with a `metadata_kind` union selecting `SceneMetadata` or `PackageMetadata` and file identifier `UREM`.
- Produces private conversion functions `encode_scene_metadata`, `decode_scene_metadata`, `encode_package_metadata`, and `decode_package_metadata`.

- [ ] **Step 1: Write RED metadata roundtrip tests**

  Test that FlatBuffers metadata starts with the `UREM` identifier, passes the generated verifier, and roundtrips all Q.0-Q.2 fields including opaque extension bytes. Corrupt a vtable offset and assert a structured metadata verification error.

- [ ] **Step 2: Verify RED before adding the schema/runtime**

  Build `test_native_scene`; expected failure is a missing private metadata encoder declaration.

- [ ] **Step 3: Add pinned runtime, schema, and generated header**

  Retrieve only the official release include tree and license. Generate with:

  ```powershell
  flatc --cpp --cpp-std c++17 --scoped-enums -o libs/ure_sceneio/generated schemas/ure_native_v1.fbs
  flatc --conform schemas/ure_native_v1.baseline.fbs schemas/ure_native_v1.fbs
  ```

  The maintenance script requires `flatc version 25.12.19`, runs both commands, and fails if `git diff --exit-code -- schemas libs/ure_sceneio/generated` detects stale output. Normal CMake only compiles the checked-in generated header.

- [ ] **Step 4: Implement metadata conversion and verifier boundary**

  Construct vectors in schema-defined deterministic order. Verify the buffer and `UREM` identifier before dereferencing. Reject unsupported schema majors before materializing nested vectors. When a newer compatible metadata minor is loaded, retain the original metadata byte buffer and mark the document rewrite-safe only while semantically unmodified; otherwise refuse a lossy rewrite.

- [ ] **Step 5: Verify GREEN and schema reproducibility**

  Run `test_native_scene` and the regeneration script with the pinned transient `flatc`; expected zero diff in schema/generated files.

---

### Task 5: Checked `.urescene` / `.urepkg` chunk container

**Files:**
- Create: `libs/ure_sceneio/include/ure/native_scene_container.hpp`
- Create: `libs/ure_sceneio/src/native_scene_container.cpp`
- Modify: `libs/ure_sceneio/CMakeLists.txt`
- Modify: `tests/host/test_native_scene.cpp`

**Interfaces:**
- Produces `ContainerChunk`, `NativeContainer`, `write_container(const NativeContainer&)`, and `read_container(std::span<const std::uint8_t>, const CapabilityRegistry&, const ValidationLimits&)`.
- Produces `write_scene_binary`, `read_scene_binary`, `write_package_binary`, and `read_package_binary` convenience APIs.

- [ ] **Step 1: Write RED container safety and determinism tests**

  Build minimal empty scene/package containers and assert repeated writes are byte-identical. Mutate byte fixtures to test bad magic, endian marker, unsupported major/minor flags, nonzero reserved bytes, directory aliasing header, offset+length overflow, overlapping ranges, non-power-of-two and >4096 alignment, hash mismatch, unsupported required codec, decompression budget, and resident budget. Add unknown required and optional chunks; required fails and optional payload bytes survive read-write exactly.

- [ ] **Step 2: Verify RED because binary APIs are absent**

  Build the selected target and confirm expected unresolved symbols.

- [ ] **Step 3: Implement fixed-width byte encoding**

  Never `fwrite` or `memcpy` a C++ struct. Append/read each integer explicitly in little-endian order. Use eight-byte magic `URES\r\n\x1a\n` for scenes and `UREP\r\n\x1a\n` for packages, a fixed 128-byte v1 header, 16-byte directory alignment, and a self-sized directory whose entries store ID length, type/version/requirement/codec, checked 64-bit ranges, alignment, SHA-256, dependencies, and extension owner.

- [ ] **Step 4: Validate before allocation and preserve opaque data**

  Parse the header and directory through bounded spans. Check every range and aggregate before copying a payload. Verify metadata eagerly and other required payloads before returning; retain optional opaque chunks as owned byte vectors. Compression codec `None` requires equal stored/uncompressed sizes; all other required codecs fail in Q.2.

- [ ] **Step 5: Verify GREEN**

  Build and run `test_native_scene`; expected all binary safety cases pass without process crashes or excessive allocation.

---

### Task 6: Package fixtures, cache exclusion, Q.0 audit, and static gate

**Files:**
- Create: `tests/assets/native_scene/empty_package.ure`
- Create: `tests/assets/native_scene/single_scene.ure`
- Create: `tests/assets/native_scene/shared_resources.ure`
- Create: `tests/assets/native_scene/resources/shared_spectrum.bin`
- Create: `docs/Phase_Q_Native_Scene_Format.md`
- Create: `scripts/check_phase_q_static.ps1`
- Modify: `tests/host/test_native_scene.cpp`

**Interfaces:**
- Fixtures exercise empty, one-scene, and two-scene/shared-resource package contracts.
- Static gate checks schema ownership, reserved identifiers, dependency pinning, fixture presence, and the absence of backend types in native headers.

- [ ] **Step 1: Write RED fixture and cache-removal tests**

  Load all three committed text fixtures, resolve the shared resource beneath the fixture root, verify its SHA-256 and byte length, produce binary packages, and compare text/binary semantic hashes. Add a cache descriptor, remove it, and assert both package validity and semantic hash remain unchanged. Attempt root escape through `..`, drive, UNC, and symlink/reparse resolution paths and assert rejection.

- [ ] **Step 2: Verify RED because fixtures and audit do not exist**

  Run the selected test and `scripts/check_phase_q_static.ps1`; expected failures name the missing fixture and audit document.

- [ ] **Step 3: Add deterministic fixtures and complete ownership audit**

  `docs/Phase_Q_Native_Scene_Format.md` must enumerate every current field or semantic family from `SceneIR`, `RenderConfig`, `WaveOpticsConfig`, `IntegratorRuntimeConfig`, `MaterialGraph`, Mie/spectral resources, `SceneDiff`, and distributed shard metadata. Each row records current owner, native schema owner, source/runtime/cache/adapter classification, Q step, and whether it is typed now or assigned to a versioned extension slot. Include the binary/text/package contract, version/migration rules, diagnostic code families, and explicitly state that Q.3-Q.12 are not implemented.

- [ ] **Step 4: Add the Phase Q static gate**

  The script fails unless:

  - the design, plan, audit, schema, baseline schema, generated header, license, and three manifests exist;
  - pinned FlatBuffers version strings agree;
  - all audit source domains and native owner prefixes occur;
  - `native_scene.hpp` contains no `cuda`, `Vk`, `D3D12`, `Optix`, or `SceneIR` dependency;
  - `.urecache` is documented and excluded from semantic hash code;
  - schema field IDs are unique and reserved IDs remain declared.

- [ ] **Step 5: Verify GREEN for Q.0-Q.2 gates**

  Run:

  ```powershell
  .\scripts\check_phase_q_static.ps1
  .\scripts\build_x64.ps1 -BuildDir build_modular_x64 -Config Release -SkipConfigure -Targets test_native_scene
  ctest --test-dir build_modular_x64 -C Release -R "^test_native_scene$" --output-on-failure
  ```

  Expected: static gate succeeds and 1/1 selected test passes.

---

### Task 7: Full verification and governance review

**Files:**
- Modify only files found defective by the checks above, always by adding a failing regression test first.

**Interfaces:**
- Produces the evidence needed for REPORT; does not commit.

- [ ] **Step 1: Run a clean Release sm_120 build**

  ```powershell
  .\scripts\build_x64.ps1 -BuildDir build_modular_x64 -Config Release
  ```

  Expected: all configured targets build with no compiler warnings; cache remains `CMAKE_CUDA_ARCHITECTURES=120`.

- [ ] **Step 2: Run all registered tests**

  ```powershell
  ctest --test-dir build_modular_x64 -C Release --output-on-failure
  ```

  Expected: 27/27 pass, including the original 26.

- [ ] **Step 3: Run static and repository hygiene gates**

  ```powershell
  .\scripts\check_phase_q_static.ps1
  .\scripts\check_phase_l_static.ps1
  .\scripts\check_phase_r_static.ps1
  git diff --check
  git status --short
  ```

  Expected: all scripts exit zero, no whitespace errors, and only intentional Q.0-Q.2 files are modified/untracked.

- [ ] **Step 4: Self-review against the approved design**

  Verify every acceptance test has a named test case, public types remain backend-neutral, parser errors are structured, no unchecked allocation can precede range/budget validation, unknown optional bytes are retained, semantic hashes exclude physical/cache data, and no Q.3-Q.12 capability is claimed as implemented.

- [ ] **Step 5: Report and await explicit commit approval**

  Report changed components, exact verification counts, dependency pin, audit findings, and review results in Chinese. Do not commit or push until the user explicitly approves the implementation commit.
