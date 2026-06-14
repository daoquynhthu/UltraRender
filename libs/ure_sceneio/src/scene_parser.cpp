#include "ure/scene_parser.hpp"
#include "ure/scene_frontend.hpp"

namespace ure {

scene_ir::SceneIR SceneParser::parse_file_to_ir(const std::string& filepath) {
    return SceneFrontend::parse_file_to_ir(filepath);
}

} // namespace ure
