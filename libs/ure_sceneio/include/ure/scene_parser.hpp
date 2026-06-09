#pragma once
#include <string>
#include "ure/ure_api.hpp"
#include "procedural.hpp"
#include "ure/scene_ir.hpp"

namespace ure {

class SceneParser {
public:
    static Scene parse_file(const std::string& filepath);
    static scene_ir::SceneIR parse_file_to_ir(const std::string& filepath);
};

}
