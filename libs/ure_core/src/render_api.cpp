#include "ure/render.hpp"
#include "ure/gpu_scene_compiler.hpp"

namespace ure {

int aov_channel_count(AovType type) {
    switch (type) {
    case AovType::Beauty:
    case AovType::Normal:
    case AovType::Albedo:
        return 3;
    case AovType::Depth:
        return 1;
    case AovType::Uv:
    case AovType::MotionVector:
        return 2;
    }
    return 0;
}

void IRenderEngine::load_world(const World& world) {
    Scene scene = WorldSceneBuilder::build_scene(world);
    load_scene(scene);
}

void IRenderEngine::update_world_transforms(const World& world) {
    thread_local std::vector<gpu::GpuInstanceTransform> tmp;
    WorldSceneBuilder::build_transforms(world, tmp);
    update_transforms(tmp.data(), (int)tmp.size());
}

} // namespace ure
