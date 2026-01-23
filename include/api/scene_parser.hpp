#pragma once
#include <string>
#include "ure_api.hpp"
#include "procedural.hpp"

namespace ure {

class SceneParser {
public:
    static Scene parse_file(const std::string& filepath);
};

}
