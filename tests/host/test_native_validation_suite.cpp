#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

#include <ure/native_scene_tooling.hpp>

namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
}

int main() {
    const std::filesystem::path root = URE_Q12_FIXTURE_DIR;
    std::ifstream input(root / "fixture_manifest.json");
    nlohmann::json manifest;
    input >> manifest;
    check(manifest.at("schema") == "ure.validation.fixture-set/1.0", "fixture-set schema identity is invalid");
    const std::set<std::string> required{
        "basic_scene", "procedural_scene", "spectral_resource", "mie_volume", "wave_optics_request",
        "integrator_request", "physics_placeholder", "acoustic_placeholder", "video_stream_placeholder",
        "adapter_loss", "roundtrip", "fail_loud", "package_build"};
    std::set<std::string> covered;
    std::set<std::string> ids;
    for (const auto& fixture : manifest.at("fixtures")) {
        check(ids.insert(fixture.at("id").get<std::string>()).second, "fixture IDs are not unique");
        for (const auto& coverage : fixture.at("coverage")) covered.insert(coverage.get<std::string>());
        if (!fixture.contains("path")) continue;
        const auto path = std::filesystem::weakly_canonical(root / fixture.at("path").get<std::string>());
        check(std::filesystem::exists(path), "retained native fixture is missing");
        const auto inspection = ure::native_scene::inspect_native_asset(path);
        check(inspection.ok() && inspection.scene_count == 1, "retained native fixture failed validation");
    }
    for (const auto& capability : required) check(covered.contains(capability), "required Phase Q.12 fixture coverage is missing");
    std::cout << "Phase Q.12 validation fixture checks: " << (failures ? "FAILED" : "PASSED") << '\n';
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
