#include "ure/scene_ir_frontend.hpp"
#include "ure/procedural.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_map>

namespace ure {

namespace {

using MaterialMap = std::unordered_map<std::string, std::shared_ptr<scene_ir::MaterialNode>>;
using ImageMap = std::unordered_map<std::string, std::shared_ptr<scene_ir::ImageResource>>;
using TextureMap = std::unordered_map<std::string, std::shared_ptr<scene_ir::TextureResource>>;

scene_ir::MaterialModel parse_material_model(const std::string& type_str) {
    if (type_str == "metal") return scene_ir::MaterialModel::Metal;
    if (type_str == "dielectric") return scene_ir::MaterialModel::Dielectric;
    if (type_str == "light") return scene_ir::MaterialModel::Light;
    if (type_str == "cloth") return scene_ir::MaterialModel::Cloth;
    return scene_ir::MaterialModel::Lambertian;
}

std::shared_ptr<Mesh> create_mesh_resource(const std::string& type, std::stringstream& ss) {
    if (type == "quad") return SceneBuilder::create_quad();
    if (type == "cube") return SceneBuilder::create_cube();
    if (type == "mesh_sphere") return SceneBuilder::create_sphere();
    if (type == "highpoly_sphere") {
        int slices = 200;
        int stacks = 100;
        if (ss >> slices) ss >> stacks;
        return SceneBuilder::create_sphere(0.5f, slices, stacks);
    }
    if (type == "mesh_cylinder") {
        float r = 0.5f, h = 1.0f;
        int segments = 32;
        if (ss >> r >> h >> segments) {}
        return SceneBuilder::create_cylinder(r, h, segments);
    }
    if (type == "mesh_cup") {
        float r = 0.5f, h = 1.0f, th = 0.05f;
        int segments = 32;
        if (ss >> r >> h >> th >> segments) {}
        return SceneBuilder::create_cup(r, h, th, segments);
    }
    if (type == "mesh_torus") {
        float major_r = 0.8f, minor_r = 0.1f;
        int major_seg = 48, minor_seg = 24;
        if (ss >> major_r >> minor_r >> major_seg >> minor_seg) {}
        return SceneBuilder::create_torus(major_r, minor_r, major_seg, minor_seg);
    }
    return nullptr;
}

RigidBodyConfig parse_rigid_body_config(std::stringstream& ss) {
    ss.clear();
    RigidBodyConfig rb_config;
    std::string extra_token;
    while (ss >> extra_token) {
        if (extra_token == "mass") ss >> rb_config.mass;
        else if (extra_token == "friction") ss >> rb_config.friction;
        else if (extra_token == "restitution") ss >> rb_config.restitution;
        else if (extra_token == "damping") ss >> rb_config.linear_damping;
        else if (extra_token == "velocity") ss >> rb_config.velocity.x >> rb_config.velocity.y >> rb_config.velocity.z;
        else if (extra_token == "material_id") ss >> rb_config.material_id;
        else if (extra_token == "collider") {
            ss >> rb_config.collider_type;
            if (rb_config.collider_type == "box") {
                ss >> rb_config.collider_size.x >> rb_config.collider_size.y >> rb_config.collider_size.z;
            } else if (rb_config.collider_type == "sphere") {
                ss >> rb_config.collider_radius;
            }
        }
    }

    if (rb_config.mass > 0.0f || rb_config.collider_type != "none") {
        rb_config.enabled = true;
    }

    return rb_config;
}

class LegacySceneIrParser {
public:
    explicit LegacySceneIrParser(const std::string& filepath) : filepath_(filepath) {}

    scene_ir::SceneIR parse() {
        std::ifstream file(filepath_);
        if (!file.is_open()) {
            std::cerr << "[SceneParser] Error: Could not open file " << filepath_ << std::endl;
            return scene_;
        }

        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;

            std::stringstream ss(line);
            std::string command;
            ss >> command;

            if (command == "camera") parse_camera(ss);
            else if (command == "resolution") parse_resolution(ss);
            else if (command == "spp") parse_spp(ss);
            else if (command == "medium") parse_medium(ss);
            else if (command == "physics") parse_physics(ss);
            else if (command == "fluid") parse_fluid(ss);
            else if (command == "define_image") parse_image(ss);
            else if (command == "define_texture") parse_texture(ss);
            else if (command == "define_material") parse_material(ss);
            else if (command == "add_entity") parse_entity(ss);
        }

        return scene_;
    }

private:
    void parse_camera(std::stringstream& ss) {
        Vec3 pos = {0, 0, 10};
        Vec3 lookat = {0, 0, 0};
        float fov = 45.0f;
        std::string first_token;
        if (ss >> first_token) {
            if (first_token == "pos") {
                while (true) {
                    if (first_token == "pos") ss >> pos.x >> pos.y >> pos.z;
                    else if (first_token == "lookat") ss >> lookat.x >> lookat.y >> lookat.z;
                    else if (first_token == "fov") ss >> fov;
                    if (!(ss >> first_token)) break;
                }
            } else {
                try {
                    pos.x = std::stof(first_token);
                    ss >> pos.y >> pos.z >> lookat.x >> lookat.y >> lookat.z >> fov;
                } catch (...) {
                }
            }
        }
        scene_.camera.position = pos;
        scene_.camera.look_at = lookat;
        scene_.camera.fov = fov;
    }

    void parse_resolution(std::stringstream& ss) {
        ss >> scene_.width >> scene_.height;
    }

    void parse_spp(std::stringstream& ss) {
        ss >> scene_.spp;
    }

    void parse_medium(std::stringstream& ss) {
        float density = 0.0f;
        Vec3 scattering = {0, 0, 0};
        Vec3 absorption = {0, 0, 0};
        float max_dist = 0.0f;
        float anisotropy = 0.0f;
        std::string token;
        while (ss >> token) {
            if (token == "density") ss >> density;
            else if (token == "scatter") ss >> scattering.x >> scattering.y >> scattering.z;
            else if (token == "absorb") ss >> absorption.x >> absorption.y >> absorption.z;
            else if (token == "max_dist") ss >> max_dist;
            else if (token == "g" || token == "anisotropy") ss >> anisotropy;
        }
        scene_.medium_density = density;
        scene_.medium_scattering = scattering;
        scene_.medium_absorption = absorption;
        scene_.medium_max_distance = max_dist;
        scene_.medium_anisotropy = anisotropy;
    }

    void parse_physics(std::stringstream& ss) {
        bool enabled = false;
        float dt = 1.0f / 60.0f;
        int frames = 180;
        int spp_per_frame = 32;
        std::string token;
        while (ss >> token) {
            if (token == "enabled") ss >> enabled;
            else if (token == "dt") ss >> dt;
            else if (token == "frames") ss >> frames;
            else if (token == "spp") ss >> spp_per_frame;
        }
        scene_.physics.enabled = enabled;
        scene_.physics.dt = dt;
        scene_.physics.total_frames = frames;
        scene_.physics.spp_per_frame = spp_per_frame;
    }

    void parse_fluid(std::stringstream& ss) {
        FluidConfig config;
        std::string token;
        while (ss >> token) {
            if (token == "enabled") ss >> config.enabled;
            else if (token == "spacing") ss >> config.particle_spacing;
            else if (token == "bounds") {
                ss >> config.bounds_min.x >> config.bounds_min.y >> config.bounds_min.z;
                ss >> config.bounds_max.x >> config.bounds_max.y >> config.bounds_max.z;
            } else if (token == "fill") {
                ss >> config.fill_min.x >> config.fill_min.y >> config.fill_min.z;
                ss >> config.fill_max.x >> config.fill_max.y >> config.fill_max.z;
            }
        }
        scene_.physics.fluid = config;
    }

    void parse_material(std::stringstream& ss) {
        std::string name;
        std::string type_str;
        ss >> name >> type_str;

        auto mat = std::make_shared<scene_ir::MaterialNode>();
        mat->name = name;
        mat->model = parse_material_model(type_str);

        if (type_str == "lambertian") {
            ss >> mat->base_color.x >> mat->base_color.y >> mat->base_color.z;
            if (ss >> mat->roughness) {}
        } else if (type_str == "metal") {
            ss >> mat->base_color.x >> mat->base_color.y >> mat->base_color.z;
            ss >> mat->roughness;
            if (ss >> mat->ior) {
                ss >> mat->metal_k.x >> mat->metal_k.y >> mat->metal_k.z;
            }
        } else if (type_str == "dielectric") {
            ss >> mat->base_color.x >> mat->base_color.y >> mat->base_color.z;
            ss >> mat->roughness;
            ss >> mat->ior;
            if (ss >> mat->dispersion) {}
        } else if (type_str == "light") {
            ss >> mat->emission.x >> mat->emission.y >> mat->emission.z;
        }

        ss.clear();
        std::string extra_token;
        while (ss >> extra_token) {
            if (extra_token == "thin_film") {
                ss >> mat->thin_film_thickness >> mat->thin_film_ior;
            } else if (extra_token == "eta_rgb") {
                ss >> mat->metal_eta.x >> mat->metal_eta.y >> mat->metal_eta.z;
            } else if (extra_token == "density") {
                ss >> mat->medium_density;
            } else if (extra_token == "scatter") {
                ss >> mat->medium_scattering.x >> mat->medium_scattering.y >> mat->medium_scattering.z;
            } else if (extra_token == "absorb") {
                ss >> mat->medium_absorption.x >> mat->medium_absorption.y >> mat->medium_absorption.z;
            } else if (extra_token == "g" || extra_token == "anisotropy") {
                ss >> mat->medium_anisotropy;
            } else if (extra_token == "base_color_tex") {
                std::string texture_name;
                ss >> texture_name;
                auto it = textures_.find(texture_name);
                if (it != textures_.end()) {
                    mat->base_color_texture = it->second;
                }
            } else if (extra_token == "roughness_tex") {
                std::string texture_name;
                ss >> texture_name;
                auto it = textures_.find(texture_name);
                if (it != textures_.end()) {
                    mat->roughness_texture = it->second;
                }
            } else if (extra_token == "emission_tex") {
                std::string texture_name;
                ss >> texture_name;
                auto it = textures_.find(texture_name);
                if (it != textures_.end()) {
                    mat->emission_texture = it->second;
                }
            }
        }

        materials_[name] = mat;
        scene_.add_material(mat);
    }

    void parse_image(std::stringstream& ss) {
        std::string name;
        std::string uri;
        std::string color_space_token = "srgb";
        ss >> name >> uri;
        if (ss >> color_space_token) {}

        scene_ir::ImageColorSpace color_space = scene_ir::ImageColorSpace::SRGB;
        if (color_space_token == "linear") {
            color_space = scene_ir::ImageColorSpace::Linear;
        }

        std::filesystem::path image_path = uri;
        if (image_path.is_relative()) {
            image_path = std::filesystem::path(filepath_).parent_path() / image_path;
        }

        auto image = scene_.register_image(name, image_path.lexically_normal().string(), color_space);
        images_[name] = image;
    }

    void parse_texture(std::stringstream& ss) {
        std::string name;
        std::string image_name;
        int uv_set = 0;
        ss >> name >> image_name;
        if (ss >> uv_set) {}

        auto it = images_.find(image_name);
        if (it == images_.end()) {
            std::cerr << "[SceneParser] Warning: Image '" << image_name << "' not found." << std::endl;
            return;
        }

        auto texture = scene_.register_texture(name, it->second, uv_set);
        textures_[name] = texture;
    }

    void parse_entity(std::stringstream& ss) {
        std::string type;
        std::string mat_name;
        ss >> type >> mat_name;

        Vec3 pos = {0, 0, 0};
        Vec3 scale = {1, 1, 1};
        Vec3 rot = {0, 0, 0};

        if (ss >> pos.x >> pos.y >> pos.z) {
            if (ss >> scale.x >> scale.y >> scale.z) {
                if (!(ss >> rot.x >> rot.y >> rot.z)) {
                    ss.clear();
                }
            } else {
                ss.clear();
            }
        }

        ss.clear();
        bool is_analytical_sphere = (type == "sphere");
        std::shared_ptr<Mesh> mesh = is_analytical_sphere ? nullptr : create_mesh_resource(type, ss);
        RigidBodyConfig rb_config = parse_rigid_body_config(ss);

        if (is_analytical_sphere) {
            if (rb_config.enabled) {
                mesh = SceneBuilder::create_sphere();
            } else {
                if (materials_.count(mat_name)) {
                    scene_ir::SphereNode sphere;
                    sphere.name = type;
                    sphere.center = pos;
                    sphere.radius = 0.5f * scale.x;
                    sphere.material = materials_[mat_name];
                    scene_.spheres.push_back(sphere);
                }
                return;
            }
        }

        if (!mesh || !materials_.count(mat_name)) {
            if (!materials_.count(mat_name)) {
                std::cerr << "[SceneParser] Warning: Material '" << mat_name << "' not found." << std::endl;
            }
            return;
        }

        std::shared_ptr<scene_ir::MeshResource> mesh_res = scene_.register_mesh(type, mesh);

        scene_ir::InstanceNode instance;
        instance.name = type;
        instance.mesh = mesh_res;
        instance.material = materials_[mat_name];
        instance.position = pos;
        instance.scale = scale;
        instance.rotation = rot;
        instance.rigid_body = rb_config;
        scene_.instances.push_back(instance);
    }

    std::string filepath_;
    scene_ir::SceneIR scene_;
    MaterialMap materials_;
    ImageMap images_;
    TextureMap textures_;
};

}

scene_ir::SceneIR LegacySceneFrontend::parse_file_to_ir(const std::string& filepath) {
    return LegacySceneIrParser(filepath).parse();
}

} // namespace ure
