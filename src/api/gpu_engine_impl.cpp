#include "api/ure_api.hpp"
#include "gpu/gpu_driver.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <vector>

namespace ure {

class GpuRenderEngine : public IRenderEngine {
public:
    void load_scene(const Scene& scene) override {
        cached_meshes_.clear();
        cached_spheres_.clear();
        cached_materials_.clear();
        
        // Store camera
        current_scene_camera_ = scene.camera;

        // Base material index offset (7 default materials 0-6 in scene loader)
        int material_offset = 7; 

        // Helper to cache material
        auto cache_material = [&](std::shared_ptr<Material> mat) -> int {
            if (!mat) return 0; // Default material
            ure::gpu::GpuMaterial gpu_mat;
            
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
            gpu_mat.dispersion = mat->dispersion;
            gpu_mat.thin_film_thickness = mat->thin_film_thickness;
            gpu_mat.thin_film_ior = mat->thin_film_ior;
            gpu_mat.texture_index = -1; 
            
            cached_materials_.push_back(gpu_mat);
            return material_offset + (int)cached_materials_.size() - 1;
        };

        // 1. Handle Entities (Meshes)
        for (const auto& entity : scene.entities) {
            if (!entity.mesh) continue;
            
            ure::gpu::RenderMesh mesh;
            
            // Apply transform: T * R * S * v
            float rx = entity.rotation.x * 3.14159f / 180.0f;
            float ry = entity.rotation.y * 3.14159f / 180.0f;
            float rz = entity.rotation.z * 3.14159f / 180.0f;

            float cx = cos(rx), sx = sin(rx);
            float cy = cos(ry), sy = sin(ry);
            float cz = cos(rz), sz = sin(rz);

            for (const auto& v : entity.mesh->vertices) {
                // Scale
                float x = v.position.x * entity.scale.x;
                float y = v.position.y * entity.scale.y;
                float z = v.position.z * entity.scale.z;
                
                // Rotate X
                float y1 = y * cx - z * sx;
                float z1 = y * sx + z * cx;
                y = y1; z = z1;

                // Rotate Y
                float x2 = x * cy + z * sy;
                float z2 = -x * sy + z * cy;
                x = x2; z = z2;

                // Rotate Z
                float x3 = x * cz - y * sz;
                float y3 = x * sz + y * cz;
                x = x3; y = y3;
                
                // Translate
                x += entity.position.x;
                y += entity.position.y;
                z += entity.position.z;
                
                mesh.vertices.push_back(x);
                mesh.vertices.push_back(y);
                mesh.vertices.push_back(z);

                // Normal Transform (Inverse Transpose of Scale * Rotate)
                // Since Rotate is orthogonal, R^-T = R.
                // Scale is diagonal, S^-T = S^-1.
                // So N' = R * S^-1 * N
                
                float nx = v.normal.x / entity.scale.x;
                float ny = v.normal.y / entity.scale.y;
                float nz = v.normal.z / entity.scale.z;

                // Rotate X
                float ny1 = ny * cx - nz * sx;
                float nz1 = ny * sx + nz * cx;
                ny = ny1; nz = nz1;

                // Rotate Y
                float nx2 = nx * cy + nz * sy;
                float nz2 = -nx * sy + nz * cy;
                nx = nx2; nz = nz2;

                // Rotate Z
                float nx3 = nx * cz - ny * sz;
                float ny3 = nx * sz + ny * cz;
                nx = nx3; ny = ny3;

                // Normalize
                float len = sqrtf(nx*nx + ny*ny + nz*nz);
                if (len > 1e-6f) {
                    float inv_len = 1.0f / len;
                    nx *= inv_len;
                    ny *= inv_len;
                    nz *= inv_len;
                }

                mesh.normals.push_back(nx);
                mesh.normals.push_back(ny);
                mesh.normals.push_back(nz);

                // UVs
                mesh.uvs.push_back(v.uv.u);
                mesh.uvs.push_back(v.uv.v);
            }
            
            mesh.indices = entity.mesh->indices;
            mesh.material_index = cache_material(entity.material);
            cached_meshes_.push_back(mesh);
        }

        // 2. Handle Analytical Spheres
        for (const auto& sphere : scene.spheres) {
            ure::gpu::GpuSphere gpu_sphere;
            gpu_sphere.center = {sphere.center.x, sphere.center.y, sphere.center.z};
            gpu_sphere.radius = sphere.radius;
            gpu_sphere.material_index = cache_material(sphere.material);
            cached_spheres_.push_back(gpu_sphere);
        }
    }

    void render(const RenderSettings& settings) override {
        frame_buffer_.resize(settings.width * settings.height * 3);
        
        float cam_pos[3] = {current_scene_camera_.position.x, current_scene_camera_.position.y, current_scene_camera_.position.z};
        float cam_look[3] = {current_scene_camera_.look_at.x, current_scene_camera_.look_at.y, current_scene_camera_.look_at.z};
        
        ure::gpu::render_frame_gpu(
            frame_buffer_.data(), 
            settings.width, 
            settings.height, 
            settings.spp,
            cached_meshes_,
            cached_spheres_,
            cached_materials_,
            cam_pos,
            cam_look,
            current_scene_camera_.fov
        );
    }

    const std::vector<float>& get_frame_buffer() const override {
        return frame_buffer_;
    }

private:
    std::vector<float> frame_buffer_;
    std::vector<ure::gpu::RenderMesh> cached_meshes_;
    std::vector<ure::gpu::GpuSphere> cached_spheres_;
    std::vector<ure::gpu::GpuMaterial> cached_materials_;
    Camera current_scene_camera_;
};

std::unique_ptr<IRenderEngine> RenderEngineFactory::create_gpu_engine() {
    return std::make_unique<GpuRenderEngine>();
}

}
