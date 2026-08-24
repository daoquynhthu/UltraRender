#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include <ure/native_scene_hash.hpp>
#include <ure/native_scene_tooling.hpp>

namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

std::string read_binary(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}
}

int main() {
    using namespace ure::native_scene;
    const std::filesystem::path source = std::filesystem::path(URE_TEST_ASSET_DIR) / "full_scene.urescene";
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "ure_q9_tooling";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto binary = root / "built.urescene";
    const auto text = root / "migrated.ure";
    const auto package = root / "fixture.urepkg";
    const auto package_alias = root / "renamed-destination.urepkg";
    const auto procedural_package = root / "procedural.urepkg";
    const auto procedural_build = root / "procedural-built.ure";
    const auto author = root / "author";
    build_native_scene(source, binary);
    migrate_native_scene(binary, text);
    std::filesystem::copy(
        source.parent_path(), author,
        std::filesystem::copy_options::recursive);
    pack_native_scenes(package, {author / source.filename()});
    pack_native_scenes(package_alias, {author / source.filename()});
    check(read_binary(package) == read_binary(package_alias),
          "package bytes depend on the destination filename");
    std::filesystem::remove_all(author);
    const auto inspection = inspect_native_asset(package);
    check(inspection.ok() && inspection.kind == ContainerKind::Package, "package inspection failed");
    check(inspection.scene_count == 1, "package scene inventory is incorrect");
    check(inspection.resource_count >= 5,
          "package did not retain its content-addressed resource inventory");
    const auto loaded = load_native_asset(package);
    check(loaded.ok() && loaded.value && !loaded.value->scene.meshes.empty(), "package did not load to SceneIR");
    const auto unpacked = root / "unpacked";
    unpack_native_package(package, unpacked);
    check(loaded.value && std::filesystem::exists(unpacked / (loaded.value->document.id + ".urescene")), "package unpack did not emit scene");
    std::size_t unpacked_resources = 0;
    if (std::filesystem::exists(unpacked / "resources")) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(
                 unpacked / "resources")) {
            if (entry.is_regular_file())
                ++unpacked_resources;
        }
    }
    check(unpacked_resources >= 5,
          "package unpack did not emit content-addressed resources");
    const std::filesystem::path procedural_source =
        URE_TEST_PROCEDURAL_ASSET;
    const auto procedural_input = load_native_asset(procedural_source);
    build_native_scene(procedural_source, procedural_build);
    const auto procedural_output = load_native_asset(procedural_build);
    check(procedural_input.ok() && procedural_input.value &&
              procedural_output.ok() && procedural_output.value &&
              !procedural_output.value->procedural_graph &&
              procedural_output.value->scene.instances.size() >
                  procedural_input.value->scene.instances.size() &&
              procedural_output.value->scene.quad_lights.size() >
                  procedural_input.value->scene.quad_lights.size(),
          "procedural build did not realize geometry and lighting");
    pack_native_scenes(procedural_package, {procedural_source});
    const auto procedural_inspection = inspect_native_asset(procedural_package);
    check(procedural_inspection.ok() &&
              procedural_inspection.cache_count == 1 &&
              procedural_inspection.resource_count > 1,
          "procedural package lacks resources, provenance, or realized cache");
    const auto procedural_loaded = load_native_package_scene(
        procedural_package, "scene/q4");
    if (procedural_loaded.ok() && procedural_loaded.value &&
        procedural_loaded.value->package_manifest) {
        auto without_cache = *procedural_loaded.value->package_manifest;
        without_cache.caches.clear();
        check(semantic_hash(without_cache) ==
                  semantic_hash(*procedural_loaded.value->package_manifest),
              "rebuildable cache changed package semantic identity");
    } else {
        check(false, "procedural package manifest was unavailable");
    }
    const auto procedural_unpacked = root / "procedural-unpacked";
    unpack_native_package(procedural_package, procedural_unpacked);
    check(std::filesystem::exists(procedural_unpacked / "caches" / "content"),
          "package unpack did not preserve its optional realized cache");
    const auto damaged = root / "damaged.urepkg";
    std::filesystem::copy_file(package, damaged);
    std::filesystem::resize_file(damaged, 64);
    check(!inspect_native_asset(damaged).ok(), "truncated package was accepted");
    const auto selected = load_native_package_scene(
        package,
        loaded.value ? loaded.value->document.id : "");
    const auto missing = load_native_package_scene(
        package,
        "missing_scene");
    check(selected.ok() && !missing.ok(),
          "package scene selection contract is incorrect");
    std::filesystem::remove_all(root);
    std::cout << "Phase Q.9 native tooling checks: " << (failures ? "FAILED" : "PASSED") << '\n';
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
