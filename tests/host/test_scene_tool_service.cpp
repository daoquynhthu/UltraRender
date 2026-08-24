#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>

#include <ure/product/scene_tool_service.hpp>

namespace {

int failures{};

void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

bool nonzero(const ure::product::Identity& identity) {
    return std::ranges::any_of(
        identity, [](std::uint8_t value) { return value != 0; });
}

}

int main() {
    using ure::product::SceneToolOperation;
    using ure::product::SceneToolRequest;
    const auto root = std::filesystem::temp_directory_path() /
                      "ultrarender_prv2_scene_tool";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    SceneToolRequest realize;
    realize.operation = SceneToolOperation::Realize;
    realize.inputs = {std::filesystem::path(URE_SCENE_TOOL_PROCEDURAL)};
    const auto realized = ure::product::execute_scene_tool(realize);
    const auto realized_report = realized.report_json();
    check(realized.ok() && nonzero(realized.snapshot_identity),
          "scene-tool procedural realization failed");
    check(realized_report.find("ure.scene.procedural") != std::string::npos &&
              realized_report.find("Executed") != std::string::npos,
          "scene-tool report omitted procedural disposition");
    check(realized_report.find(realize.inputs.front().parent_path().string()) ==
              std::string::npos,
          "scene-tool report leaked an author path");

    const auto author = root / "author";
    const auto source = std::filesystem::path(URE_SCENE_TOOL_RESOURCE);
    std::filesystem::copy(source.parent_path(), author,
                          std::filesystem::copy_options::recursive);
    const auto package = root / "self-contained.urepkg";
    SceneToolRequest pack;
    pack.operation = SceneToolOperation::Pack;
    pack.inputs = {author / source.filename()};
    pack.output = package;
    const auto packed = ure::product::execute_scene_tool(pack);
    check(packed.ok() && packed.resource_count >= 5,
          "scene-tool package operation did not retain resources");
    std::filesystem::remove_all(author);

    SceneToolRequest validate;
    validate.operation = SceneToolOperation::Validate;
    validate.inputs = {package};
    validate.package_scene_id = "scene/full";
    const auto validated = ure::product::execute_scene_tool(validate);
    check(validated.ok() && nonzero(validated.snapshot_identity) &&
              validated.resource_count >= 5,
          "scene-tool self-contained package validation failed");

    validate.package_scene_id = "missing";
    const auto missing = ure::product::execute_scene_tool(validate);
    check(!missing.ok() &&
              missing.report_json().find(root.string()) == std::string::npos,
          "scene-tool missing-scene diagnostic was absent or unredacted");

    SceneToolRequest procedural_pack;
    procedural_pack.operation = SceneToolOperation::Pack;
    procedural_pack.inputs = {
        std::filesystem::path(URE_SCENE_TOOL_PROCEDURAL)};
    procedural_pack.output = root / "procedural.urepkg";
    const auto procedural =
        ure::product::execute_scene_tool(procedural_pack);
    check(procedural.ok() && procedural.cache_count == 1,
          "scene-tool procedural package omitted its rebuildable cache");

    std::filesystem::remove_all(root);
    if (failures == 0)
        std::printf("product scene-tool service tests passed\n");
    return failures == 0 ? 0 : 1;
}
