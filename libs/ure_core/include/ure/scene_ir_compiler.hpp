#pragma once

#include "ure/ure_api.hpp"
#include "ure/scene_ir.hpp"

namespace ure {

struct SceneIrCompileOptions {
    bool preserve_physics = true;
};

class SceneIrCompiler {
public:
    static Scene compile_legacy(const scene_ir::SceneIR& scene_ir,
                                const SceneIrCompileOptions& options = {});
};

} // namespace ure
