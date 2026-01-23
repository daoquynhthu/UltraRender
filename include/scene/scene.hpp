#pragma once

#include "../accelerators/accelerator.hpp"
#include "light.hpp"
#include <vector>
#include <memory>
#include <string>
#include <map>

namespace ure::scene {

/**
 * @brief 场景类，管理几何体、光源和加速结构
 */
class Scene {
public:
    Scene(std::unique_ptr<core::Accelerator> accel);

    void add_light(std::shared_ptr<Light> light);

    const std::vector<std::shared_ptr<Light>>& lights() const;
    
    void finalize();

    std::optional<core::Interaction> intersect(const core::Rayf& ray) const;

    bool occluded(const core::Rayf& ray) const;

    core::Accelerator* get_accelerator();

private:
    std::unique_ptr<core::Accelerator> accelerator_;
    std::vector<std::shared_ptr<Light>> lights_;
};

/**
 * @brief 插件式工厂接口，允许外部注入自定义加速器
 */
class AcceleratorFactory {
public:
    using CreatorFunc = std::unique_ptr<core::Accelerator>(*)();

    static void register_accelerator(const std::string& name, CreatorFunc creator);

    static std::unique_ptr<core::Accelerator> create(const std::string& name);

private:
    static std::map<std::string, CreatorFunc>& get_registry();
};

} // namespace ure::scene
