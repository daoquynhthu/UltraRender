#include "ure/render.hpp"
#include "ure/gpu_scene_compiler.hpp"

namespace ure {

void IRenderEngine::load_world(const World& world) {
    Scene scene = WorldSceneBuilder::build_scene(world);
    load_scene(scene);
}

void IRenderEngine::update_world_transforms(const World& world) {
    std::vector<gpu::GpuInstanceTransform> tmp;
    WorldSceneBuilder::build_transforms(world, tmp);
    update_transforms(tmp.data(), (int)tmp.size());
}

} // namespace ure
