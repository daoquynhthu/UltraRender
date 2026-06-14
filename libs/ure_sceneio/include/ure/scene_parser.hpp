#pragma once
#include <string>
#include "ure/scene_ir.hpp"

namespace ure {

class SceneParser {
public:
    static scene_ir::SceneIR parse_file_to_ir(const std::string& filepath);
};

}
