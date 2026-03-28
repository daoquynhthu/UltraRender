#include "api/ure_api.hpp"
#include "gpu/gpu_driver.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <vector>
#include <map>
#include <tuple>

namespace ure {

class GpuRenderEngine : public IRenderEngine {
public:
    ~GpuRenderEngine() {
        if (gpu_context_) {
            ure::gpu::free_gpu_renderer(gpu_context_);
            gpu_context_ = nullptr;
        }
    }

    void load_scene(const Scene& scene) override {
        if (gpu_context_) {
            ure::gpu::free_gpu_renderer(gpu_context_);
            gpu_context_ = nullptr;
        }

        cached_meshes_.clear();
        cached_spheres_.clear();
        cached_materials_.clear();
        std::vector<ure::gpu::GpuInstance> instances;
        
        // Store camera
        current_scene_camera_ = scene.camera;

        // Store medium
        medium_density_ = scene.medium_density;
        medium_anisotropy_ = scene.medium_anisotropy;
        auto to_gpu_vec3_local = [](const Vec3& v) { return ure::gpu::GpuVec3(v.x, v.y, v.z); };
        medium_scattering_ = ure::gpu::GpuSpectrum::from_rgb(to_gpu_vec3_local(scene.medium_scattering));
        medium_absorption_ = ure::gpu::GpuSpectrum::from_rgb(to_gpu_vec3_local(scene.medium_absorption));
        medium_max_distance_ = scene.medium_max_distance;

        int material_offset = 7; 

        // Helper to cache material
        auto cache_material = [&](std::shared_ptr<Material> mat) -> int {
            if (!mat) return 0; // Default material
            ure::gpu::GpuMaterial gpu_mat = {};
            
            switch (mat->type) {
                case MaterialType::Lambertian: gpu_mat.type = ure::gpu::MaterialType::Lambertian; break;
                case MaterialType::Metal:      gpu_mat.type = ure::gpu::MaterialType::Metal; break;
                case MaterialType::Dielectric: gpu_mat.type = ure::gpu::MaterialType::Dielectric; break;
                case MaterialType::Light:      gpu_mat.type = ure::gpu::MaterialType::Light; break;
                default:                       gpu_mat.type = ure::gpu::MaterialType::Lambertian; break;
            }
            
            auto to_gpu_vec3 = [](const Vec3& v) { return ure::gpu::GpuVec3(v.x, v.y, v.z); };
            gpu_mat.albedo = ure::gpu::GpuSpectrum::from_rgb(to_gpu_vec3(mat->albedo));
            gpu_mat.emission = ure::gpu::GpuSpectrum::from_rgb(to_gpu_vec3(mat->emission));
            gpu_mat.roughness = mat->roughness;
            gpu_mat.ior = mat->ior;
            gpu_mat.metal_eta = ure::gpu::GpuSpectrum::from_rgb(to_gpu_vec3(mat->metal_eta));
            gpu_mat.dispersion = mat->dispersion;
            gpu_mat.thin_film_thickness = mat->thin_film_thickness;
            gpu_mat.thin_film_ior = mat->thin_film_ior;
            
            // Phase 3: Volume / SSS
            gpu_mat.medium_density = mat->medium_density;
            gpu_mat.medium_anisotropy = mat->medium_anisotropy;
            gpu_mat.medium_scattering = ure::gpu::GpuSpectrum::from_rgb(to_gpu_vec3(mat->medium_scattering));
            gpu_mat.medium_absorption = ure::gpu::GpuSpectrum::from_rgb(to_gpu_vec3(mat->medium_absorption));
            
            gpu_mat.extinction = ure::gpu::GpuSpectrum::from_rgb(to_gpu_vec3(mat->extinction));
            gpu_mat.texture_index = -1; 
            
            cached_materials_.push_back(gpu_mat);
            return material_offset + (int)cached_materials_.size() - 1;
        };

        // Map to track unique meshes
        std::map<std::shared_ptr<Mesh>, int> mesh_map;

        // 1. Handle Entities (Instances)
        for (const auto& entity : scene.entities) {
            if (!entity.mesh) continue;
            
            int mesh_idx = -1;
            auto it = mesh_map.find(entity.mesh);
            if (it != mesh_map.end()) {
                mesh_idx = it->second;
            } else {
                // Create new RenderMesh (Raw, no transform)
                ure::gpu::RenderMesh mesh;
                
                // Copy vertices
                for (const auto& v : entity.mesh->vertices) {
                    mesh.vertices.push_back(v.position.x);
                    mesh.vertices.push_back(v.position.y);
                    mesh.vertices.push_back(v.position.z);
                    
                    mesh.normals.push_back(v.normal.x);
                    mesh.normals.push_back(v.normal.y);
                    mesh.normals.push_back(v.normal.z);
                    
                    mesh.uvs.push_back(v.uv.u);
                    mesh.uvs.push_back(v.uv.v);
                }
                
                mesh.indices = entity.mesh->indices;
                mesh.material_index = -1; // Material is handled by Instance
                
                cached_meshes_.push_back(mesh);
                mesh_idx = (int)cached_meshes_.size() - 1;
                mesh_map[entity.mesh] = mesh_idx;
            }

            // Create Instance
            ure::gpu::GpuInstance inst;
            inst.mesh_index = mesh_idx;
            inst.material_index = cache_material(entity.material);

            // Calculate Transform Matrices
            float rx = entity.rotation.x * 3.14159f / 180.0f;
            float ry = entity.rotation.y * 3.14159f / 180.0f;
            float rz = entity.rotation.z * 3.14159f / 180.0f;
            float cx = cos(rx), sx = sin(rx);
            float cy = cos(ry), sy = sin(ry);
            float cz = cos(rz), sz = sin(rz);

            // Basis vectors for Rotation
            auto compute_rot = [&](float x, float y, float z) {
                // Rx
                float y1 = y*cx - z*sx;
                float z1 = y*sx + z*cx;
                // Ry
                float x2 = x*cy + z1*sy;
                float z2 = -x*sy + z1*cy;
                // Rz
                float x3 = x2*cz - y1*sz;
                float y3 = x2*sz + y1*cz;
                return ure::gpu::GpuVec3(x3, y3, z2);
            };

            ure::gpu::GpuVec3 r0 = compute_rot(1,0,0);
            ure::gpu::GpuVec3 r1 = compute_rot(0,1,0);
            ure::gpu::GpuVec3 r2 = compute_rot(0,0,1);

            // Fill Transform Matrix (T * R * S)
            inst.transform.m[0][0] = r0.x * entity.scale.x; inst.transform.m[0][1] = r1.x * entity.scale.y; inst.transform.m[0][2] = r2.x * entity.scale.z; inst.transform.m[0][3] = entity.position.x;
            inst.transform.m[1][0] = r0.y * entity.scale.x; inst.transform.m[1][1] = r1.y * entity.scale.y; inst.transform.m[1][2] = r2.y * entity.scale.z; inst.transform.m[1][3] = entity.position.y;
            inst.transform.m[2][0] = r0.z * entity.scale.x; inst.transform.m[2][1] = r1.z * entity.scale.y; inst.transform.m[2][2] = r2.z * entity.scale.z; inst.transform.m[2][3] = entity.position.z;
            inst.transform.m[3][0] = 0;                     inst.transform.m[3][1] = 0;                     inst.transform.m[3][2] = 0;                     inst.transform.m[3][3] = 1;

            // Fill Inverse Transform Matrix (S^-1 * R^T * T^-1)
            float isx = 1.0f / entity.scale.x;
            float isy = 1.0f / entity.scale.y;
            float isz = 1.0f / entity.scale.z;

            // Linear part: S^-1 * R^T
            inst.inverse_transform.m[0][0] = r0.x * isx; inst.inverse_transform.m[0][1] = r0.y * isx; inst.inverse_transform.m[0][2] = r0.z * isx;
            inst.inverse_transform.m[1][0] = r1.x * isy; inst.inverse_transform.m[1][1] = r1.y * isy; inst.inverse_transform.m[1][2] = r1.z * isy;
            inst.inverse_transform.m[2][0] = r2.x * isz; inst.inverse_transform.m[2][1] = r2.y * isz; inst.inverse_transform.m[2][2] = r2.z * isz;
            
            // Translation part: - (Linear * Pos)
            float tx = -(inst.inverse_transform.m[0][0]*entity.position.x + inst.inverse_transform.m[0][1]*entity.position.y + inst.inverse_transform.m[0][2]*entity.position.z);
            float ty = -(inst.inverse_transform.m[1][0]*entity.position.x + inst.inverse_transform.m[1][1]*entity.position.y + inst.inverse_transform.m[1][2]*entity.position.z);
            float tz = -(inst.inverse_transform.m[2][0]*entity.position.x + inst.inverse_transform.m[2][1]*entity.position.y + inst.inverse_transform.m[2][2]*entity.position.z);
            
            inst.inverse_transform.m[0][3] = tx;
            inst.inverse_transform.m[1][3] = ty;
            inst.inverse_transform.m[2][3] = tz;
            inst.inverse_transform.m[3][0] = 0; inst.inverse_transform.m[3][1] = 0; inst.inverse_transform.m[3][2] = 0; inst.inverse_transform.m[3][3] = 1;

            // Compute World AABB
            // 1. Find local AABB
            float min_x = 1e30f, min_y = 1e30f, min_z = 1e30f;
            float max_x = -1e30f, max_y = -1e30f, max_z = -1e30f;
            for (const auto& v : entity.mesh->vertices) {
                if (v.position.x < min_x) min_x = v.position.x;
                if (v.position.y < min_y) min_y = v.position.y;
                if (v.position.z < min_z) min_z = v.position.z;
                if (v.position.x > max_x) max_x = v.position.x;
                if (v.position.y > max_y) max_y = v.position.y;
                if (v.position.z > max_z) max_z = v.position.z;
            }
            
            // 2. Transform 8 corners
            ure::gpu::GpuVec3 corners[8];
            corners[0] = {min_x, min_y, min_z};
            corners[1] = {max_x, min_y, min_z};
            corners[2] = {min_x, max_y, min_z};
            corners[3] = {max_x, max_y, min_z};
            corners[4] = {min_x, min_y, max_z};
            corners[5] = {max_x, min_y, max_z};
            corners[6] = {min_x, max_y, max_z};
            corners[7] = {max_x, max_y, max_z};
            
            float w_min_x = 1e30f, w_min_y = 1e30f, w_min_z = 1e30f;
            float w_max_x = -1e30f, w_max_y = -1e30f, w_max_z = -1e30f;
            
            for (int k=0; k<8; ++k) {
                ure::gpu::GpuVec3 p = corners[k];
                ure::gpu::GpuVec3 tp = inst.transform.transform_point(p);
                if (tp.x < w_min_x) w_min_x = tp.x;
                if (tp.y < w_min_y) w_min_y = tp.y;
                if (tp.z < w_min_z) w_min_z = tp.z;
                if (tp.x > w_max_x) w_max_x = tp.x;
                if (tp.y > w_max_y) w_max_y = tp.y;
                if (tp.z > w_max_z) w_max_z = tp.z;
            }
            
            inst.min_pt = {w_min_x, w_min_y, w_min_z};
            inst.max_pt = {w_max_x, w_max_y, w_max_z};
            
            instances.push_back(inst);
        }

        // 2. Handle Analytical Spheres
        for (const auto& sphere : scene.spheres) {
            ure::gpu::GpuSphere gpu_sphere;
            gpu_sphere.center = {sphere.center.x, sphere.center.y, sphere.center.z};
            gpu_sphere.radius = sphere.radius;
            gpu_sphere.material_index = cache_material(sphere.material);
            cached_spheres_.push_back(gpu_sphere);
        }

        // --- Initialize GPU Context ---
        int w = (scene.width > 0) ? scene.width : 1920;
        int h = (scene.height > 0) ? scene.height : 1080;

        gpu_context_ = ure::gpu::init_gpu_renderer(w, h, cached_meshes_, instances, cached_spheres_, cached_materials_);
        
        // Setup Camera
        float cam_pos[3] = {current_scene_camera_.position.x, current_scene_camera_.position.y, current_scene_camera_.position.z};
        float cam_look[3] = {current_scene_camera_.look_at.x, current_scene_camera_.look_at.y, current_scene_camera_.look_at.z};
        ure::gpu::update_camera_gpu(gpu_context_, cam_pos, cam_look, current_scene_camera_.fov);

        // Setup Medium
        ure::gpu::update_medium_gpu(gpu_context_, 
            medium_density_,
            medium_anisotropy_,
            medium_scattering_,
            medium_absorption_,
            medium_max_distance_
        );

        // Prepare Host Buffer
        frame_buffer_.resize(w * h * 3);
        current_spp_ = 0;
    }

    void render(const RenderSettings& settings) override {
        if (!gpu_context_) {
            std::cerr << "[GpuRenderEngine] No scene loaded!" << std::endl;
            return;
        }

        // NOTE: We assume gpu_context_ is already initialized with correct width/height from load_scene
        // If settings.width/height differ, we might need to re-init (not implemented here for speed)

        std::cout << "[GpuRenderEngine] Starting render: " << settings.spp << " spp" << std::endl;
        
        reset_accumulation();
        
        while (current_spp_ < settings.spp) {
            render_pass();
            if (current_spp_ % 10 == 0) {
                 std::cout << "\rSPP: " << current_spp_ << " / " << settings.spp << std::flush;
            }
        }
        std::cout << std::endl;
        
        // Copy to host buffer
        ure::gpu::copy_frame_buffer_gpu(gpu_context_, frame_buffer_.data());
    }

    int render_pass() override {
        if (!gpu_context_) return 0;
        current_spp_ = ure::gpu::render_pass_gpu(gpu_context_, 1);
        return current_spp_;
    }

    void reset_accumulation() override {
        if (!gpu_context_) return;
        ure::gpu::reset_accumulation_gpu(gpu_context_);
        current_spp_ = 0;
    }

    void update_camera(const Camera& camera) override {
        if (!gpu_context_) return;
        current_scene_camera_ = camera;
        float cam_pos[3] = {camera.position.x, camera.position.y, camera.position.z};
        float cam_look[3] = {camera.look_at.x, camera.look_at.y, camera.look_at.z};
        ure::gpu::update_camera_gpu(gpu_context_, cam_pos, cam_look, camera.fov);
        current_spp_ = 0; 
    }

    int get_current_spp() const override {
        return current_spp_;
    }

    const std::vector<float>& get_frame_buffer() const override {
        if (gpu_context_) {
            // Update the buffer before returning
            ure::gpu::copy_frame_buffer_gpu(gpu_context_, const_cast<float*>(frame_buffer_.data()));
        }
        return frame_buffer_;
    }

private:
    std::vector<float> frame_buffer_;
    std::vector<ure::gpu::RenderMesh> cached_meshes_;
    std::vector<ure::gpu::GpuSphere> cached_spheres_;
    std::vector<ure::gpu::GpuMaterial> cached_materials_;
    Camera current_scene_camera_;
    
    // Medium parameters
    float medium_density_ = 0.0f;
    float medium_anisotropy_ = 0.0f;
    ure::gpu::GpuSpectrum medium_scattering_;
    ure::gpu::GpuSpectrum medium_absorption_;
    float medium_max_distance_ = 0.0f;

    // GPU Context
    ure::gpu::GpuContext* gpu_context_ = nullptr;
    int current_spp_ = 0;
};

std::unique_ptr<IRenderEngine> RenderEngineFactory::create_gpu_engine() {
    return std::make_unique<GpuRenderEngine>();
}

}
