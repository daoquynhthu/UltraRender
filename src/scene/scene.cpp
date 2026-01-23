#include "../../include/scene/scene.hpp"

namespace ure::scene {

Scene::Scene(std::unique_ptr<core::Accelerator> accel) : accelerator_(std::move(accel)) {}

void Scene::add_light(std::shared_ptr<Light> light) {
    lights_.push_back(light);
}

const std::vector<std::shared_ptr<Light>>& Scene::lights() const { 
    return lights_; 
}

void Scene::finalize() {
    if (accelerator_) {
        accelerator_->build();
    }
}

std::optional<core::Interaction> Scene::intersect(const core::Rayf& ray) const {
    return accelerator_ ? accelerator_->intersect(ray) : std::nullopt;
}

bool Scene::occluded(const core::Rayf& ray) const {
    return accelerator_ ? accelerator_->occluded(ray) : false;
}

core::Accelerator* Scene::get_accelerator() { 
    return accelerator_.get(); 
}

// AcceleratorFactory Implementation
void AcceleratorFactory::register_accelerator(const std::string& name, CreatorFunc creator) {
    get_registry()[name] = creator;
}

std::unique_ptr<core::Accelerator> AcceleratorFactory::create(const std::string& name) {
    auto& registry = get_registry();
    if (registry.contains(name)) {
        return registry[name]();
    }
    return nullptr;
}

std::map<std::string, AcceleratorFactory::CreatorFunc>& AcceleratorFactory::get_registry() {
    static std::map<std::string, CreatorFunc> registry;
    return registry;
}

} // namespace ure::scene
