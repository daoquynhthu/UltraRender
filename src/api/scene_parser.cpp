#include "api/scene_parser.hpp"
#include "api/scene_frontend.hpp"

namespace ure {

Scene SceneParser::parse_file(const std::string& filepath) {
    return scene_ir::to_legacy_scene(parse_file_to_ir(filepath));
}

scene_ir::SceneIR SceneParser::parse_file_to_ir(const std::string& filepath) {
    return SceneFrontend::parse_file_to_ir(filepath);
}

} // namespace ure
