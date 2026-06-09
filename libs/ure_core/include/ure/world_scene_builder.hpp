#pragma once

#include "ure/world.hpp"
#include "ure/ure_api.hpp"
#include <vector>

namespace ure::gpu { struct GpuInstanceTransform; }

namespace ure {

// Phase P.4: Bridge from World (ECS runtime bus) to Scene (GPU upload format)
// and to GpuInstanceTransform (hot-update path).
//
// World → Scene:   Initial GPU upload (engine->load_scene_once)
// World → Transforms: Per-frame hot-update (engine->update_transforms)
// Scene → World:   Populate World from existing Scene (adapter)

struct WorldSceneBuilder {
    static Scene build_scene(const World& world);

    static void build_transforms(const World& world,
                                  std::vector<gpu::GpuInstanceTransform>& out);

    static void from_scene(const Scene& scene, World& world);
};

} // namespace ure
