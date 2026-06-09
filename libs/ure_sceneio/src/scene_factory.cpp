#include "ure/scene/scene_factory.hpp"
#include "ure/scene/sphere.hpp"
#include "ure/scene/directional_light.hpp"
#include "ure/scene/point_light.hpp"
#include "ure/materials/lambertian.hpp"
#include "ure/materials/dielectric.hpp"
#include "ure/materials/microfacet.hpp"
#include "ure/accelerators/bvh_accelerator.hpp"
#include "ure/scene/obj_loader.hpp"
#include <iostream>

namespace ure::scene {

SceneFactory::SceneData SceneFactory::create_scene(const std::string& name) {
    if (name == "test") {
        return create_test_scene();
    } else if (name == "quick") {
        return create_quick_test_scene();
    } else if (name.ends_with(".obj")) {
        return create_obj_scene(name);
    } else {
        std::cout << "Unknown scene name: " << name << ". Defaulting to 'test'.\n";
        return create_test_scene();
    }
}

void SceneFactory::list_scenes() {
    std::cout << "Available scenes:\n";
    std::cout << "  - test:  The original test scene with three balls (glass, metal, red).\n";
    std::cout << "  - quick: A fast rendering scene with a single red sphere.\n";
    std::cout << "  - *.obj: Load and render an OBJ file directly (e.g. 'model.obj').\n";
}

SceneFactory::SceneData SceneFactory::create_test_scene() {
    SceneData data;
    data.name = "test";
    data.width = 1280;
    data.height = 720;
    data.spp = 32; // Set to 32 SPP for fast iteration

    // Camera
    // 调整位置：往后挪一点，视野调大，确保拍全所有球
    core::Point3f look_from(0, 4, 18); 
    core::Point3f look_at(0, 1, 0);
    float vfov = 40.0f; 
    float aspect = (float)data.width / data.height;
    float aperture = 0.01f; // 进一步缩小光圈，增加景深范围
    float focus_dist = 18.0f;
    data.camera = std::make_shared<core::ThinLensCamera>(look_from, look_at, vfov, aspect, aperture, focus_dist);

    // Accelerator
    auto accel = std::make_unique<accelerators::BVHAccelerator>();

    // Materials
    // 地面：深灰色
    auto ground_material = std::make_shared<materials::LambertianBSDF>(spectral::SPD({{360.0f, 0.4f}, {830.0f, 0.4f}}));
    
    // 玻璃球：透明 (使用 Cauchy 方程模拟色散: n(lambda) = A + B/lambda^2)
    // BK7: A = 1.5046, B = 0.00420 (lambda in um) -> B in nm: 4200
    std::vector<spectral::SPD::Sample> glass_samples;
    for (float l = 360.0f; l <= 830.0f; l += 10.0f) {
        float n = 1.5046f + 4200.0f / (l * l);
        glass_samples.push_back({l, n});
    }
    spectral::SPD glass_eta(glass_samples);
    auto glass_material = std::make_shared<materials::DielectricBSDF>(glass_eta);
    
    // 金属球：金色近似
    auto gold_spd = spectral::Spectrum::spd_from_rgb(1.0f, 0.85f, 0.5f);
    auto metal_material = std::make_shared<materials::MicrofacetBSDF>(gold_spd, 0.05f);
    
    // 红色球：鲜艳的红色
    auto red_spd = spectral::Spectrum::spd_from_rgb(0.9f, 0.1f, 0.1f);
    auto red_material = std::make_shared<materials::LambertianBSDF>(red_spd);

    // Primitives
    accel->add_primitive(std::make_shared<Primitive>(
        std::make_shared<Sphere>(core::Point3f(0, -1000, 0), 1000.0f), ground_material));
    accel->add_primitive(std::make_shared<Primitive>(
        std::make_shared<Sphere>(core::Point3f(0, 1, 0), 1.0f), glass_material));
    accel->add_primitive(std::make_shared<Primitive>(
        std::make_shared<Sphere>(core::Point3f(-4, 1, 0), 1.0f), metal_material));
    accel->add_primitive(std::make_shared<Primitive>(
        std::make_shared<Sphere>(core::Point3f(4, 1, 0), 1.0f), red_material));

    accel->build();
    data.scene = std::make_shared<Scene>(std::move(accel));

    // Lights
    // 只保留一个强力的主点光源，简化阴影，让效果更清晰
    data.scene->add_light(std::make_shared<PointLight>(core::Point3f(10, 20, 15), spectral::Spectrum(1500.0f)));

    return data;
}

SceneFactory::SceneData SceneFactory::create_quick_test_scene() {
    SceneData data;
    data.name = "quick";
    data.width = 640;
    data.height = 480;
    data.spp = 16;
    
    // Camera
    core::Point3f look_from(0, 0, 10);
    core::Point3f look_at(0, 0, 0);
    float vfov = 40.0f;
    float aspect = (float)data.width / data.height;
    data.camera = std::make_shared<core::ThinLensCamera>(look_from, look_at, vfov, aspect, 0.0f, 10.0f);

    auto accel = std::make_unique<accelerators::BVHAccelerator>();
    auto red_bsdf = std::make_shared<materials::LambertianBSDF>(spectral::Spectrum::spd_from_rgb(0.8f, 0.05f, 0.05f));
    accel->add_primitive(std::make_shared<Primitive>(
        std::make_shared<Sphere>(core::Point3f(0, 0, 0), 1.0f), red_bsdf));
    accel->build();
    data.scene = std::make_shared<Scene>(std::move(accel));

    data.scene->add_light(std::make_shared<DirectionalLight>(core::Vec3f(-1, -1, -1), spectral::Spectrum(20.0f)));

    return data;
}

SceneFactory::SceneData SceneFactory::create_obj_scene(const std::string& filename) {
    SceneData data;
    data.name = filename;
    data.width = 800;
    data.height = 600;
    data.spp = 16;

    // Load Mesh
    std::cout << "Loading OBJ: " << filename << std::endl;
    auto mesh = ObjLoader::load(filename);
    if (!mesh) {
        std::cerr << "Failed to load mesh. Falling back to test scene.\n";
        return create_test_scene();
    }
    std::cout << "Loaded OBJ: " << filename << " (" << mesh->indices.size() / 3 << " triangles)\n";

    auto accel = std::make_unique<accelerators::BVHAccelerator>();
    
    // Default Material (Red Lambertian to distinguish from background)
    auto default_bsdf = std::make_shared<materials::LambertianBSDF>(spectral::Spectrum::spd_from_rgb(0.8f, 0.1f, 0.1f));

    // DEBUG: Print first triangle
    if (mesh->indices.size() >= 3) {
        int i0 = mesh->indices[0];
        int i1 = mesh->indices[1];
        int i2 = mesh->indices[2];
        auto p0 = mesh->vertices[i0];
        auto p1 = mesh->vertices[i1];
        auto p2 = mesh->vertices[i2];
        std::cout << "Triangle 0 Vertices: " 
                  << "(" << p0.x << "," << p0.y << "," << p0.z << ") "
                  << "(" << p1.x << "," << p1.y << "," << p1.z << ") "
                  << "(" << p2.x << "," << p2.y << "," << p2.z << ")\n";
    }

    // Add all triangles
    for (size_t i = 0; i < mesh->indices.size(); i += 3) {
        auto tri = std::make_shared<MeshTriangle>(mesh, static_cast<int>(i));
        accel->add_primitive(std::make_shared<Primitive>(tri, default_bsdf));
    }

    // Auto-adjust Camera
    core::AABB bounds;
    for (const auto& p : mesh->vertices) bounds.expand(p);
    
    core::Point3f center = bounds.center();
    float radius = (bounds.max - center).length();

    // DEBUG: Move camera further back
    core::Point3f look_from = center + core::Vec3f(0, radius * 1.0f, radius * 5.0f);
    core::Point3f look_at = center;
    
    data.camera = std::make_shared<core::ThinLensCamera>(look_from, look_at, 40.0f, 1.33f, 0.0f, radius * 2.5f);

    accel->build();
    data.scene = std::make_shared<Scene>(std::move(accel));

    // Lights
    // Drastically lower intensity to fix overexposure
    data.scene->add_light(std::make_shared<DirectionalLight>(core::Vec3f(-1, -1, -1), spectral::Spectrum(1.0f)));
    // Add a fill light (weaker)
    data.scene->add_light(std::make_shared<PointLight>(center + core::Vec3f(radius, radius, radius), spectral::Spectrum(10.0f)));

    return data;
}

} // namespace ure::scene
