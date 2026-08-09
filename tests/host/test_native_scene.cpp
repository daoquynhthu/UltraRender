#include <ure/native_scene.hpp>
#include <ure/native_scene_container.hpp>
#include <ure/native_scene_hash.hpp>
#include <ure/native_scene_metadata.hpp>
#include <ure/native_scene_text.hpp>
#include <ure/native_scene_validation.hpp>


#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(condition) do { if (condition) { ++g_passed; } else { ++g_failed; std::fprintf(stderr, "CHECK failed: %s at line %d\n", #condition, __LINE__); } } while (0)

static std::vector<std::uint8_t> as_bytes(const std::string& value) {
    return {value.begin(), value.end()};
}

static bool has_code(const ure::native_scene::ValidationReport& report, const std::string& code) {
    for (const auto& diagnostic : report.diagnostics) {
        if (diagnostic.code == code) {
            return true;
        }
    }
    return false;
}

template <typename T>
static bool has_code(const ure::native_scene::LoadResult<T>& result, const std::string& code) {
    for (const auto& diagnostic : result.diagnostics) {
        if (diagnostic.code == code) {
            return true;
        }
    }
    return false;
}

static ure::native_scene::CapabilityRegistry supported_registry() {
    ure::native_scene::CapabilityRegistry registry;
    registry.features.emplace("ure.scene.base", ure::native_scene::Version{1, 0});
    registry.extensions.emplace("org.ultrarender.test", ure::native_scene::Version{1, 0});
    return registry;
}

static ure::native_scene::SceneDocument valid_scene() {
    using namespace ure::native_scene;
    SceneDocument scene;
    scene.id = "scene/main";
    scene.features.push_back({"ure.scene.base", {1, 0}, RequirementLevel::Required, "", {}, "{}"});
    scene.resources.push_back({
        "resource/spectrum",
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        ResourceKind::SpectralTable,
        {1, 0},
        "resources/spectrum.bin",
        {},
        3,
        3
    });
    return scene;
}

static void test_hash_and_validation() {
    using namespace ure::native_scene;
    const auto abc = as_bytes("abc");
    CHECK(sha256_hex(abc) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const std::vector<std::uint8_t> million_a(1'000'000, static_cast<std::uint8_t>('a'));
    CHECK(sha256_hex(million_a) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

    const CapabilityRegistry registry = supported_registry();
    const ValidationLimits limits;
    const SceneDocument scene = valid_scene();
    CHECK(validate_scene_document(scene, registry, limits).ok());
    CHECK(semantic_hash(scene) == semantic_hash(scene));

    SceneDocument traversal = scene;
    traversal.resources[0].uri = "../escape.bin";
    CHECK(has_code(validate_scene_document(traversal, registry, limits), "URE-Q-PATH-001"));

    SceneDocument absolute = scene;
    absolute.resources[0].uri = "C:/escape.bin";
    CHECK(has_code(validate_scene_document(absolute, registry, limits), "URE-Q-PATH-001"));

    SceneDocument duplicate = scene;
    duplicate.resources.push_back(duplicate.resources.front());
    CHECK(has_code(validate_scene_document(duplicate, registry, limits), "URE-Q-ID-002"));

    SceneDocument unknown_required = scene;
    unknown_required.features.push_back({"ure.render.unknown", {1, 0}, RequirementLevel::Required, "", {}, "{}"});
    CHECK(has_code(validate_scene_document(unknown_required, registry, limits), "URE-Q-FEATURE-001"));

    SceneDocument unknown_optional = scene;
    unknown_optional.features.push_back({"ure.render.unknown", {1, 0}, RequirementLevel::Optional, "", {}, "{}"});
    const ValidationReport optional_report = validate_scene_document(unknown_optional, registry, limits);
    CHECK(optional_report.ok());
    CHECK(has_code(optional_report, "URE-Q-FEATURE-101"));

    SceneDocument unknown_extension = scene;
    unknown_extension.extensions.push_back({"org.unknown.data", {1, 0}, RequirementLevel::Required, "bytes", {1, 2, 3}});
    CHECK(has_code(validate_scene_document(unknown_extension, registry, limits), "URE-Q-EXT-001"));

    SceneDocument bad_major = scene;
    bad_major.schema_version = {3, 0};
    CHECK(has_code(validate_scene_document(bad_major, registry, limits), "URE-Q-VERSION-001"));

    SceneDocument newer_minor = scene;
    newer_minor.schema_version = {1, 1};
    CHECK(validate_scene_document(newer_minor, registry, limits).ok());

    SceneDocument cycle = scene;
    cycle.resources.push_back({
        "resource/second",
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        ResourceKind::Texture,
        {1, 0},
        "resources/second.bin",
        {"resource/spectrum"},
        3,
        3
    });
    cycle.resources[0].dependencies = {"resource/second"};
    CHECK(has_code(validate_scene_document(cycle, registry, limits), "URE-Q-DEP-001"));

    ValidationLimits small_limits;
    small_limits.max_resident_resource_bytes = 2;
    CHECK(has_code(validate_scene_document(scene, registry, small_limits), "URE-Q-BUDGET-001"));

    PackageManifest package;
    package.id = "package/main";
    package.resources = scene.resources;
    package.caches.push_back({"cache/scene", std::string(64, '0'), ResourceKind::Cache, {1, 0}, "cache/scene.urecache", {}, 12, 12});
    const std::string hash_with_cache = semantic_hash(package);
    package.caches.clear();
    CHECK(hash_with_cache == semantic_hash(package));

    SceneDocument feature_cycle = scene;
    feature_cycle.features = {
        {"ure.test.first", {1, 0}, RequirementLevel::Required, "", {"ure.test.second"}, "{}"},
        {"ure.test.second", {1, 0}, RequirementLevel::Required, "", {"ure.test.first"}, "{}"}
    };
    CapabilityRegistry cycle_registry = registry;
    cycle_registry.features.emplace("ure.test.first", Version{1, 0});
    cycle_registry.features.emplace("ure.test.second", Version{1, 0});
    CHECK(has_code(validate_scene_document(feature_cycle, cycle_registry, limits), "URE-Q-DEP-004"));
}

static void test_text_projection() {
    using namespace ure::native_scene;
    SceneDocument scene = valid_scene();
    scene.extensions.push_back({"org.unknown.data", {1, 0}, RequirementLevel::Optional, "bytes", {0x00, 0x7f, 0xff}});

    const std::string first = write_scene_text(scene);
    const std::string second = write_scene_text(scene);
    CHECK(first == second);
    CHECK(!first.empty() && first.back() == '\n');
    CHECK(first.find("base64") == std::string::npos);
    CHECK(first.find("007fff") != std::string::npos);

    const auto loaded = read_scene_text(first, supported_registry());
    CHECK(loaded.ok());
    CHECK(loaded.value.has_value());
    if (loaded.value) {
        CHECK(loaded.value->extensions[0].opaque_payload == scene.extensions[0].opaque_payload);
        CHECK(semantic_hash(*loaded.value) == semantic_hash(scene));
    }

    const std::string bom = std::string("\xef\xbb\xbf") + first;
    CHECK(has_code(read_scene_text(bom, supported_registry()), "URE-Q-TEXT-001"));
    CHECK(has_code(read_scene_text(R"({"kind":"scene","base64":"AA=="})", supported_registry()), "URE-Q-TEXT-002"));

    std::string large_array = R"({"inline_data":[)";
    for (int i = 0; i < 65; ++i) {
        if (i > 0) large_array += ',';
        large_array += std::to_string(i);
    }
    large_array += "]}";
    CHECK(has_code(read_scene_text(large_array, supported_registry()), "URE-Q-TEXT-003"));

    PackageManifest package;
    package.id = "package/main";
    package.scenes.push_back({scene.id, semantic_hash(scene), "scenes/main.urescene"});
    package.resources = scene.resources;
    const std::string package_text = write_package_text(package);
    const auto loaded_package = read_package_text(package_text, supported_registry());
    CHECK(loaded_package.ok());
    if (loaded_package.value) CHECK(semantic_hash(*loaded_package.value) == semantic_hash(package));

    std::string nested_unknown = first;
    const std::string resource_marker = "\"byte_length\": 3,";
    nested_unknown.replace(nested_unknown.find(resource_marker), resource_marker.size(),
                           resource_marker + "\n      \"unexpected\": 1,");
    CHECK(has_code(read_scene_text(nested_unknown, supported_registry()), "URE-Q-TEXT-002"));

    SceneDocument many_features = valid_scene();
    CapabilityRegistry many_registry = supported_registry();
    for (int i = 0; i < 40; ++i) {
        const std::string name = "ure.test.feature" + std::to_string(i);
        many_features.features.push_back({name, {1, 0}, RequirementLevel::Optional, "", {}, "{}"});
        many_registry.features.emplace(name, Version{1, 0});
    }
    CHECK(read_scene_text(write_scene_text(many_features), many_registry).ok());

    SceneDocument canonical_a = scene;
    canonical_a.features.push_back({"ure.scene.alpha", {1, 0}, RequirementLevel::Optional, "", {}, "{}"});
    canonical_a.extensions.push_back({"org.alpha.data", {1, 0}, RequirementLevel::Optional, "bytes", {1}});
    canonical_a.resources.push_back({"resource/alpha", std::string(64, 'a'), ResourceKind::Texture, {1, 0},
                                     "resources/alpha.bin", {}, 1, 1});
    SceneDocument canonical_b = canonical_a;
    std::ranges::reverse(canonical_b.features);
    std::ranges::reverse(canonical_b.extensions);
    std::ranges::reverse(canonical_b.resources);
    CHECK(write_scene_text(canonical_a) == write_scene_text(canonical_b));
}

static void test_binary_metadata() {
    using namespace ure::native_scene;
    SceneDocument scene = valid_scene();
    scene.extensions.push_back({"org.unknown.data", {1, 0}, RequirementLevel::Optional, "bytes", {0x00, 0x7f, 0xff}});

    const std::vector<std::uint8_t> encoded = encode_scene_metadata(scene);
    CHECK(encoded.size() > 8);
    CHECK(metadata_buffer_has_identifier(encoded));
    const auto decoded = decode_scene_metadata(encoded, supported_registry());
    CHECK(decoded.ok());
    if (decoded.value) {
        CHECK(semantic_hash(*decoded.value) == semantic_hash(scene));
        CHECK(decoded.value->extensions[0].opaque_payload == scene.extensions[0].opaque_payload);
    }

    std::vector<std::uint8_t> corrupt = encoded;
    corrupt[0] = 0xff;
    CHECK(has_code(decode_scene_metadata(corrupt, supported_registry()), "URE-Q-METADATA-001"));

    PackageManifest package;
    package.id = "package/main";
    package.scenes.push_back({scene.id, semantic_hash(scene), "scenes/main.urescene"});
    package.resources = scene.resources;
    const auto package_encoded = encode_package_metadata(package);
    const auto package_decoded = decode_package_metadata(package_encoded, supported_registry());
    CHECK(package_decoded.ok());
    if (package_decoded.value) CHECK(semantic_hash(*package_decoded.value) == semantic_hash(package));

    SceneDocument canonical_a = scene;
    canonical_a.features.push_back({"ure.scene.alpha", {1, 0}, RequirementLevel::Optional, "", {}, "{}"});
    canonical_a.extensions.push_back({"org.alpha.data", {1, 0}, RequirementLevel::Optional, "bytes", {1}});
    canonical_a.resources.push_back({"resource/alpha", std::string(64, 'a'), ResourceKind::Texture, {1, 0},
                                     "resources/alpha.bin", {}, 1, 1});
    SceneDocument canonical_b = canonical_a;
    std::ranges::reverse(canonical_b.features);
    std::ranges::reverse(canonical_b.extensions);
    std::ranges::reverse(canonical_b.resources);
    CHECK(encode_scene_metadata(canonical_a) == encode_scene_metadata(canonical_b));
}

static std::uint64_t read_u64_le(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    std::uint64_t value = 0;
    for (int shift = 0; shift < 64; shift += 8) value |= static_cast<std::uint64_t>(bytes[offset++]) << shift;
    return value;
}

static void write_u64_le(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) bytes[offset++] = static_cast<std::uint8_t>(value >> shift);
}

static void test_binary_container() {
    using namespace ure::native_scene;
    NativeContainer container;
    container.kind = ContainerKind::Scene;
    container.semantic_hash = semantic_hash(valid_scene());
    container.chunks.push_back({"metadata", static_cast<std::uint32_t>(ChunkKind::Metadata), {1, 0},
                                RequirementLevel::Required, static_cast<std::uint32_t>(CompressionCodec::None),
                                8, {}, {}, encode_scene_metadata(valid_scene())});
    container.chunks.push_back({"extension/opaque", 9000, {1, 0}, RequirementLevel::Optional,
                                static_cast<std::uint32_t>(CompressionCodec::None), 16,
                                "org.unknown.data", {"metadata"}, {0x00, 0x7f, 0xff}});

    const auto first = write_container(container);
    const auto second = write_container(container);
    CHECK(first == second);
    const auto loaded = read_container(first, supported_registry());
    CHECK(loaded.ok());
    if (loaded.value) {
        CHECK(loaded.value->chunks.size() == 2);
        bool preserved = false;
        for (const auto& chunk : loaded.value->chunks) {
            if (chunk.id == "extension/opaque") preserved = chunk.payload == container.chunks[1].payload;
        }
        CHECK(preserved);
    }

    std::vector<std::uint8_t> bad_magic = first;
    bad_magic[0] ^= 0xff;
    CHECK(has_code(read_container(bad_magic, supported_registry()), "URE-Q-CONTAINER-001"));

    std::vector<std::uint8_t> bad_reserved = first;
    bad_reserved[96] = 1;
    CHECK(has_code(read_container(bad_reserved, supported_registry()), "URE-Q-CONTAINER-002"));

    std::vector<std::uint8_t> bad_directory = first;
    write_u64_le(bad_directory, 24, 64);
    CHECK(has_code(read_container(bad_directory, supported_registry()), "URE-Q-CONTAINER-003"));

    std::vector<std::uint8_t> bad_hash = first;
    bad_hash[128] ^= 1;
    CHECK(has_code(read_container(bad_hash, supported_registry()), "URE-Q-HASH-002"));

    NativeContainer unknown_required = container;
    unknown_required.chunks[1].requirement = RequirementLevel::Required;
    CHECK(has_code(read_container(write_container(unknown_required), supported_registry()), "URE-Q-CHUNK-001"));

    NativeContainer unknown_codec = container;
    unknown_codec.chunks[0].codec = 99;
    CHECK(has_code(read_container(write_container(unknown_codec), supported_registry()), "URE-Q-CODEC-001"));

    NativeContainer optional_codec = container;
    optional_codec.chunks[1].codec = 99;
    const auto optional_codec_loaded = read_container(write_container(optional_codec), supported_registry());
    CHECK(optional_codec_loaded.ok());
    CHECK(has_code(optional_codec_loaded, "URE-Q-CODEC-101"));

    NativeContainer chunk_cycle = container;
    chunk_cycle.chunks[0].dependencies = {"extension/opaque"};
    chunk_cycle.chunks[1].dependencies = {"metadata"};
    CHECK(has_code(read_container(write_container(chunk_cycle), supported_registry()), "URE-Q-DEP-005"));

    NativeContainer reserved_type = container;
    reserved_type.chunks[0].type = 16;
    bool reserved_threw = false;
    try {
        static_cast<void>(write_container(reserved_type));
    } catch (const std::invalid_argument&) {
        reserved_threw = true;
    }
    CHECK(reserved_threw);

    ValidationLimits tiny_container_limits;
    tiny_container_limits.max_total_stored_bytes = 2;
    CHECK(has_code(read_container(first, supported_registry(), tiny_container_limits), "URE-Q-BUDGET-001"));

    NativeContainer fractional_ratio = container;
    fractional_ratio.chunks[1].codec = 99;
    fractional_ratio.chunks[1].payload = {1, 2};
    fractional_ratio.chunks[1].uncompressed_size = 513;
    CHECK(has_code(read_container(write_container(fractional_ratio), supported_registry()), "URE-Q-BUDGET-003"));

    NativeContainer bad_alignment = container;
    bad_alignment.chunks[0].alignment = 3;
    bool alignment_threw = false;
    try {
        static_cast<void>(write_container(bad_alignment));
    } catch (const std::invalid_argument&) {
        alignment_threw = true;
    }
    CHECK(alignment_threw);

    const auto scene_binary = write_scene_binary(valid_scene());
    const auto decoded_scene = read_scene_binary(scene_binary, supported_registry());
    CHECK(decoded_scene.ok());
    if (decoded_scene.value) CHECK(semantic_hash(*decoded_scene.value) == semantic_hash(valid_scene()));

    PackageManifest package;
    package.id = "package/main";
    const auto package_binary = write_package_binary(package);
    const auto decoded_package = read_package_binary(package_binary, supported_registry());
    CHECK(decoded_package.ok());
}

static std::vector<std::uint8_t> read_file_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

static void test_native_scene_fixtures() {
    using namespace ure::native_scene;
    const std::filesystem::path root = URE_TEST_ASSET_DIR;
    for (const std::string name : {"empty_package.ure", "single_scene.ure", "shared_resources.ure"}) {
        const auto bytes = read_file_bytes(root / name);
        CHECK(!bytes.empty());
        const std::string text(bytes.begin(), bytes.end());
        const auto package = read_package_text(text, supported_registry());
        CHECK(package.ok());
        if (package.value) {
            const auto binary = write_package_binary(*package.value);
            const auto decoded = read_package_binary(binary, supported_registry());
            CHECK(decoded.ok());
            if (decoded.value) CHECK(semantic_hash(*decoded.value) == semantic_hash(*package.value));
        }
    }

    const auto shared_bytes = read_file_bytes(root / "shared_resources.ure");
    const auto shared = read_package_text(std::string(shared_bytes.begin(), shared_bytes.end()), supported_registry());
    if (shared.value && !shared.value->resources.empty()) {
        const auto payload = read_file_bytes(root / shared.value->resources[0].uri);
        CHECK(payload.size() == shared.value->resources[0].byte_length);
        CHECK(sha256_hex(payload) == shared.value->resources[0].content_hash);
    } else {
        CHECK(false);
    }

    CHECK(validate_exploded_resource_path(root, "resources/shared_spectrum.bin").ok());
    CHECK(has_code(validate_exploded_resource_path(root, "../escape.bin"), "URE-Q-PATH-002"));

    const std::filesystem::path sandbox = std::filesystem::temp_directory_path() / "ure_native_scene_path_test";
    std::error_code error;
    std::filesystem::remove_all(sandbox, error);
    std::filesystem::create_directories(sandbox / "root", error);
    std::filesystem::create_directories(sandbox / "outside", error);
    {
        std::ofstream output(sandbox / "outside" / "payload.bin", std::ios::binary);
        output << "escape";
    }
    std::filesystem::create_directory_symlink(sandbox / "outside", sandbox / "root" / "link", error);
    if (!error) {
        CHECK(has_code(validate_exploded_resource_path(sandbox / "root", "link/payload.bin"), "URE-Q-PATH-002"));
    }
    error.clear();
    std::filesystem::remove_all(sandbox, error);
}

int main() {
    using namespace ure::native_scene;

    static_assert(std::is_same_v<decltype(ResourceDescriptor{}.byte_length), std::uint64_t>);
    static_assert(std::is_same_v<decltype(ResourceDescriptor{}.resident_bytes), std::uint64_t>);

    SceneDocument scene;
    scene.id = "scene/main";
    scene.schema_version = kSceneSchemaVersion;

    PackageManifest package;
    package.id = "package/main";
    package.format_version = kPackageFormatVersion;

    const Version version_one{1, 0};
    CHECK(scene.schema_version == version_one);
    CHECK(package.format_version == version_one);
    CHECK(scene.conventions.length_unit == "metre");

    test_hash_and_validation();
    test_text_projection();
    test_binary_metadata();
    test_binary_container();
    test_native_scene_fixtures();

    std::printf("native scene foundation: %d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
