#pragma once

#include "scene.hpp"
#include "camera.hpp"
#include <string>
#include <memory>
#include <tuple>

namespace ure::scene {

/**
 * @brief 场景工厂类，负责创建预定义的场景
 */
class SceneFactory {
public:
    struct SceneData {
        std::shared_ptr<Scene> scene;
        std::shared_ptr<core::Camera> camera;
        int width;
        int height;
        int spp;
        std::string name;
    };

    /**
     * @brief 根据名称创建场景
     */
    static SceneData create_scene(const std::string& name);

    /**
     * @brief 打印所有可用场景
     */
    static void list_scenes();

private:
    static SceneData create_test_scene();
    static SceneData create_quick_test_scene();
    static SceneData create_obj_scene(const std::string& filename);
};

} // namespace ure::scene
