#pragma once

#include "ure/scene_ir.hpp"
#include <string>

namespace ure {

class LegacySceneFrontend {
public:
    static scene_ir::SceneIR parse_file_to_ir(const std::string& filepath);
};

} // namespace ure
