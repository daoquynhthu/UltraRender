#pragma once

#include "ure/scene_ir.hpp"
#include <string>
#include <string_view>

namespace ure::io {

scene_ir::MaterialGraph import_materialx_graph(std::string_view xml);
std::string export_materialx_graph(const scene_ir::MaterialGraph& graph, std::string_view material_name = "UREMaterial");

} // namespace ure::io
