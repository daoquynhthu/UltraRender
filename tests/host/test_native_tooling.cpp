#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include <ure/native_scene_tooling.hpp>

namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
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
    build_native_scene(source, binary);
    migrate_native_scene(binary, text);
    pack_native_scenes(package, {binary});
    const auto inspection = inspect_native_asset(package);
    check(inspection.ok() && inspection.kind == ContainerKind::Package, "package inspection failed");
    check(inspection.scene_count == 1, "package scene inventory is incorrect");
    const auto loaded = load_native_asset(package);
    check(loaded.ok() && loaded.value && !loaded.value->scene.meshes.empty(), "package did not load to SceneIR");
    const auto unpacked = root / "unpacked";
    unpack_native_package(package, unpacked);
    check(loaded.value && std::filesystem::exists(unpacked / (loaded.value->document.id + ".urescene")), "package unpack did not emit scene");
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
