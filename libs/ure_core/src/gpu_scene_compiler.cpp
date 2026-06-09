#include "ure/gpu_scene_compiler.hpp"
#include "ure/image_loader.hpp"
#include <algorithm>
#include <cmath>
#include <map>

namespace ure {

namespace {

ure::gpu::GpuVec3 to_gpu_vec3(const core::Vec3f& v) {
    return ure::gpu::GpuVec3(v.x, v.y, v.z);
}

ure::gpu::GpuMaterial compile_material_node(scene_ir::MaterialModel model,
                                            const core::Vec3f& albedo,
                                            const core::Vec3f& emission,
                                            float roughness,
                                            float ior,
                                            const core::Vec3f& metal_eta,
                                            float dispersion,
                                            float thin_film_thickness,
                                            float thin_film_ior,
                                            float medium_density,
                                            float medium_anisotropy,
                                            const core::Vec3f& medium_scattering,
                                            const core::Vec3f& medium_absorption,
                                            const core::Vec3f& extinction,
                                            int texture_index = -1,
                                            int roughness_texture_index = -1,
                                            int emission_texture_index = -1) {
    ure::gpu::GpuMaterial gpu_mat = {};
    switch (model) {
        case scene_ir::MaterialModel::Lambertian: gpu_mat.type = ure::gpu::MaterialType::Lambertian; break;
        case scene_ir::MaterialModel::Metal: gpu_mat.type = ure::gpu::MaterialType::Metal; break;
        case scene_ir::MaterialModel::Dielectric: gpu_mat.type = ure::gpu::MaterialType::Dielectric; break;
        case scene_ir::MaterialModel::Light: gpu_mat.type = ure::gpu::MaterialType::Light; break;
        default: gpu_mat.type = ure::gpu::MaterialType::Lambertian; break;
    }

    gpu_mat.albedo = ure::gpu::GpuSpectrum::from_rgb(to_gpu_vec3(albedo));
    gpu_mat.emission = ure::gpu::GpuSpectrum::from_rgb(to_gpu_vec3(emission));
    gpu_mat.roughness = roughness;
    gpu_mat.ior = ior;
    gpu_mat.metal_eta = ure::gpu::GpuSpectrum::from_rgb(to_gpu_vec3(metal_eta));
    gpu_mat.dispersion = dispersion;
    gpu_mat.thin_film_thickness = thin_film_thickness;
    gpu_mat.thin_film_ior = thin_film_ior;
    gpu_mat.medium_density = medium_density;
    gpu_mat.medium_anisotropy = medium_anisotropy;
    gpu_mat.medium_scattering = ure::gpu::GpuSpectrum::from_rgb(to_gpu_vec3(medium_scattering));
    gpu_mat.medium_absorption = ure::gpu::GpuSpectrum::from_rgb(to_gpu_vec3(medium_absorption));
    gpu_mat.extinction = ure::gpu::GpuSpectrum::from_rgb(to_gpu_vec3(extinction));
    gpu_mat.texture_index = texture_index;
    gpu_mat.roughness_texture_index = roughness_texture_index;
    gpu_mat.emission_texture_index = emission_texture_index;
    return gpu_mat;
}

ure::gpu::GpuMaterial compile_material(const std::shared_ptr<Material>& mat) {
    if (!mat) {
        return compile_material_node(scene_ir::MaterialModel::Lambertian,
                                     {0.8f, 0.8f, 0.8f},
                                     {0.0f, 0.0f, 0.0f},
                                     0.5f,
                                     1.45f,
                                     {0.0f, 0.0f, 0.0f},
                                     0.0f,
                                     0.0f,
                                     1.0f,
                                     0.0f,
                                     0.0f,
                                     {0.0f, 0.0f, 0.0f},
                                     {0.0f, 0.0f, 0.0f},
                                     {0.0f, 0.0f, 0.0f});
    }

    scene_ir::MaterialModel model = scene_ir::MaterialModel::Lambertian;
    switch (mat->type) {
        case MaterialType::Lambertian: model = scene_ir::MaterialModel::Lambertian; break;
        case MaterialType::Metal: model = scene_ir::MaterialModel::Metal; break;
        case MaterialType::Dielectric: model = scene_ir::MaterialModel::Dielectric; break;
        case MaterialType::Light: model = scene_ir::MaterialModel::Light; break;
        default: break;
    }

    return compile_material_node(model,
                                 mat->albedo,
                                 mat->emission,
                                 mat->roughness,
                                 mat->ior,
                                 mat->metal_eta,
                                 mat->dispersion,
                                 mat->thin_film_thickness,
                                 mat->thin_film_ior,
                                 mat->medium_density,
                                 mat->medium_anisotropy,
                                 mat->medium_scattering,
                                 mat->medium_absorption,
                                 mat->extinction);
}

ure::gpu::GpuMaterial compile_material(const std::shared_ptr<scene_ir::MaterialNode>& mat,
                                       int texture_index = -1,
                                       int roughness_texture_index = -1,
                                       int emission_texture_index = -1) {
    if (!mat) {
        return compile_material_node(scene_ir::MaterialModel::Lambertian,
                                     {0.8f, 0.8f, 0.8f},
                                     {0.0f, 0.0f, 0.0f},
                                     0.5f,
                                     1.45f,
                                     {0.0f, 0.0f, 0.0f},
                                     0.0f,
                                     0.0f,
                                     1.0f,
                                     0.0f,
                                     0.0f,
                                     {0.0f, 0.0f, 0.0f},
                                     {0.0f, 0.0f, 0.0f},
                                     {0.0f, 0.0f, 0.0f},
                                     texture_index,
                                     roughness_texture_index,
                                     emission_texture_index);
    }

    return compile_material_node(mat->model,
                                 mat->base_color,
                                 mat->emission,
                                 mat->roughness,
                                 mat->ior,
                                 mat->metal_eta,
                                 mat->dispersion,
                                 mat->thin_film_thickness,
                                 mat->thin_film_ior,
                                 mat->medium_density,
                                 mat->medium_anisotropy,
                                 mat->medium_scattering,
                                 mat->medium_absorption,
                                 mat->metal_k,
                                 texture_index,
                                 roughness_texture_index,
                                 emission_texture_index);
}

} // anonymous namespace (material helpers)

// ── free functions ──
void compile_instance_transform(const core::Vec3f& position,
                                const core::Vec3f& scale,
                                const core::Quat& rotation,
                                ure::gpu::GpuInstanceTransform& out) {
    core::Matrix4x4f rot_mat = rotation.to_matrix();
    ure::gpu::GpuVec3 r0 = {rot_mat.m[0][0], rot_mat.m[1][0], rot_mat.m[2][0]};
    ure::gpu::GpuVec3 r1 = {rot_mat.m[0][1], rot_mat.m[1][1], rot_mat.m[2][1]};
    ure::gpu::GpuVec3 r2 = {rot_mat.m[0][2], rot_mat.m[1][2], rot_mat.m[2][2]};
    out.transform.m[0][0] = r0.x * scale.x; out.transform.m[0][1] = r1.x * scale.y; out.transform.m[0][2] = r2.x * scale.z; out.transform.m[0][3] = position.x;
    out.transform.m[1][0] = r0.y * scale.x; out.transform.m[1][1] = r1.y * scale.y; out.transform.m[1][2] = r2.y * scale.z; out.transform.m[1][3] = position.y;
    out.transform.m[2][0] = r0.z * scale.x; out.transform.m[2][1] = r1.z * scale.y; out.transform.m[2][2] = r2.z * scale.z; out.transform.m[2][3] = position.z;
    out.transform.m[3][0] = 0; out.transform.m[3][1] = 0; out.transform.m[3][2] = 0; out.transform.m[3][3] = 1;
    float isx = 1.0f / scale.x, isy = 1.0f / scale.y, isz = 1.0f / scale.z;
    out.inverse_transform.m[0][0] = r0.x * isx; out.inverse_transform.m[0][1] = r0.y * isx; out.inverse_transform.m[0][2] = r0.z * isx;
    out.inverse_transform.m[1][0] = r1.x * isy; out.inverse_transform.m[1][1] = r1.y * isy; out.inverse_transform.m[1][2] = r1.z * isy;
    out.inverse_transform.m[2][0] = r2.x * isz; out.inverse_transform.m[2][1] = r2.y * isz; out.inverse_transform.m[2][2] = r2.z * isz;
    float tx = -(out.inverse_transform.m[0][0] * position.x + out.inverse_transform.m[0][1] * position.y + out.inverse_transform.m[0][2] * position.z);
    float ty = -(out.inverse_transform.m[1][0] * position.x + out.inverse_transform.m[1][1] * position.y + out.inverse_transform.m[1][2] * position.z);
    float tz = -(out.inverse_transform.m[2][0] * position.x + out.inverse_transform.m[2][1] * position.y + out.inverse_transform.m[2][2] * position.z);
    out.inverse_transform.m[0][3] = tx; out.inverse_transform.m[1][3] = ty; out.inverse_transform.m[2][3] = tz;
    out.inverse_transform.m[3][0] = 0; out.inverse_transform.m[3][1] = 0; out.inverse_transform.m[3][2] = 0; out.inverse_transform.m[3][3] = 1;
}

void GpuSceneCompiler::build_instance_transform(const core::Vec3f& position,
                                                const core::Vec3f& scale,
                                                const core::Quat& rotation,
                                                const std::shared_ptr<Mesh>& mesh,
                                                gpu::GpuInstanceTransform& out) {
    compile_instance_transform(position, scale, rotation, out);

    // Compute world-space AABB from mesh local bounds
    if (mesh && !mesh->vertices.empty()) {
        float min_x = 1e30f, min_y = 1e30f, min_z = 1e30f;
        float max_x = -1e30f, max_y = -1e30f, max_z = -1e30f;
        for (const auto& v : mesh->vertices) {
            float wx = out.transform.m[0][0]*v.position.x + out.transform.m[0][1]*v.position.y + out.transform.m[0][2]*v.position.z + out.transform.m[0][3];
            float wy = out.transform.m[1][0]*v.position.x + out.transform.m[1][1]*v.position.y + out.transform.m[1][2]*v.position.z + out.transform.m[1][3];
            float wz = out.transform.m[2][0]*v.position.x + out.transform.m[2][1]*v.position.y + out.transform.m[2][2]*v.position.z + out.transform.m[2][3];
            min_x = (std::min)(min_x, wx); min_y = (std::min)(min_y, wy); min_z = (std::min)(min_z, wz);
            max_x = (std::max)(max_x, wx); max_y = (std::max)(max_y, wy); max_z = (std::max)(max_z, wz);
        }
        out.min_pt = {min_x, min_y, min_z};
        out.max_pt = {max_x, max_y, max_z};
    } else {
        out.min_pt = {0, 0, 0};
        out.max_pt = {0, 0, 0};
    }
}

namespace {

void compile_transform(const core::Vec3f& position,
                       const core::Vec3f& scale,
                       const core::Quat& rotation,
                       ure::gpu::GpuInstance& inst) {
    core::Matrix4x4f rot_mat = rotation.to_matrix();

    ure::gpu::GpuVec3 r0 = {rot_mat.m[0][0], rot_mat.m[1][0], rot_mat.m[2][0]};
    ure::gpu::GpuVec3 r1 = {rot_mat.m[0][1], rot_mat.m[1][1], rot_mat.m[2][1]};
    ure::gpu::GpuVec3 r2 = {rot_mat.m[0][2], rot_mat.m[1][2], rot_mat.m[2][2]};

    inst.transform.m[0][0] = r0.x * scale.x; inst.transform.m[0][1] = r1.x * scale.y; inst.transform.m[0][2] = r2.x * scale.z; inst.transform.m[0][3] = position.x;
    inst.transform.m[1][0] = r0.y * scale.x; inst.transform.m[1][1] = r1.y * scale.y; inst.transform.m[1][2] = r2.y * scale.z; inst.transform.m[1][3] = position.y;
    inst.transform.m[2][0] = r0.z * scale.x; inst.transform.m[2][1] = r1.z * scale.y; inst.transform.m[2][2] = r2.z * scale.z; inst.transform.m[2][3] = position.z;
    inst.transform.m[3][0] = 0; inst.transform.m[3][1] = 0; inst.transform.m[3][2] = 0; inst.transform.m[3][3] = 1;

    float isx = 1.0f / scale.x;
    float isy = 1.0f / scale.y;
    float isz = 1.0f / scale.z;

    inst.inverse_transform.m[0][0] = r0.x * isx; inst.inverse_transform.m[0][1] = r0.y * isx; inst.inverse_transform.m[0][2] = r0.z * isx;
    inst.inverse_transform.m[1][0] = r1.x * isy; inst.inverse_transform.m[1][1] = r1.y * isy; inst.inverse_transform.m[1][2] = r1.z * isy;
    inst.inverse_transform.m[2][0] = r2.x * isz; inst.inverse_transform.m[2][1] = r2.y * isz; inst.inverse_transform.m[2][2] = r2.z * isz;

    float tx = -(inst.inverse_transform.m[0][0] * position.x + inst.inverse_transform.m[0][1] * position.y + inst.inverse_transform.m[0][2] * position.z);
    float ty = -(inst.inverse_transform.m[1][0] * position.x + inst.inverse_transform.m[1][1] * position.y + inst.inverse_transform.m[1][2] * position.z);
    float tz = -(inst.inverse_transform.m[2][0] * position.x + inst.inverse_transform.m[2][1] * position.y + inst.inverse_transform.m[2][2] * position.z);

    inst.inverse_transform.m[0][3] = tx;
    inst.inverse_transform.m[1][3] = ty;
    inst.inverse_transform.m[2][3] = tz;
    inst.inverse_transform.m[3][0] = 0; inst.inverse_transform.m[3][1] = 0; inst.inverse_transform.m[3][2] = 0; inst.inverse_transform.m[3][3] = 1;
}

void compute_world_aabb(const std::shared_ptr<Mesh>& mesh, ure::gpu::GpuInstance& inst) {
    float min_x = 1e30f, min_y = 1e30f, min_z = 1e30f;
    float max_x = -1e30f, max_y = -1e30f, max_z = -1e30f;
    for (const auto& v : mesh->vertices) {
        min_x = std::min(min_x, v.position.x);
        min_y = std::min(min_y, v.position.y);
        min_z = std::min(min_z, v.position.z);
        max_x = std::max(max_x, v.position.x);
        max_y = std::max(max_y, v.position.y);
        max_z = std::max(max_z, v.position.z);
    }

    ure::gpu::GpuVec3 corners[8] = {
        {min_x, min_y, min_z}, {max_x, min_y, min_z}, {min_x, max_y, min_z}, {max_x, max_y, min_z},
        {min_x, min_y, max_z}, {max_x, min_y, max_z}, {min_x, max_y, max_z}, {max_x, max_y, max_z}
    };

    float w_min_x = 1e30f, w_min_y = 1e30f, w_min_z = 1e30f;
    float w_max_x = -1e30f, w_max_y = -1e30f, w_max_z = -1e30f;
    for (int k = 0; k < 8; ++k) {
        ure::gpu::GpuVec3 tp = inst.transform.transform_point(corners[k]);
        w_min_x = std::min(w_min_x, tp.x);
        w_min_y = std::min(w_min_y, tp.y);
        w_min_z = std::min(w_min_z, tp.z);
        w_max_x = std::max(w_max_x, tp.x);
        w_max_y = std::max(w_max_y, tp.y);
        w_max_z = std::max(w_max_z, tp.z);
    }

    inst.min_pt = {w_min_x, w_min_y, w_min_z};
    inst.max_pt = {w_max_x, w_max_y, w_max_z};
}

}

CompiledGpuScene GpuSceneCompiler::compile_legacy(const Scene& scene) {
    CompiledGpuScene compiled;
    compiled.camera = scene.camera;
    compiled.medium_density = scene.medium_density;
    compiled.medium_anisotropy = scene.medium_anisotropy;
    compiled.medium_scattering = ure::gpu::GpuSpectrum::from_rgb(to_gpu_vec3(scene.medium_scattering));
    compiled.medium_absorption = ure::gpu::GpuSpectrum::from_rgb(to_gpu_vec3(scene.medium_absorption));
    compiled.medium_max_distance = scene.medium_max_distance;
    compiled.width = scene.width > 0 ? scene.width : 1920;
    compiled.height = scene.height > 0 ? scene.height : 1080;

    int material_offset = 7;
    std::map<std::shared_ptr<Material>, int> material_map;
    auto cache_material = [&](const std::shared_ptr<Material>& mat) -> int {
        if (!mat) return 0;
        auto it = material_map.find(mat);
        if (it != material_map.end()) return it->second;
        compiled.materials.push_back(compile_material(mat));
        int material_index = material_offset + static_cast<int>(compiled.materials.size()) - 1;
        material_map[mat] = material_index;
        return material_index;
    };

    std::map<std::shared_ptr<Mesh>, int> mesh_map;

    for (const auto& entity : scene.entities) {
        if (!entity.mesh) continue;

        int mesh_idx = -1;
        auto it = mesh_map.find(entity.mesh);
        if (it != mesh_map.end()) {
            mesh_idx = it->second;
        } else {
            ure::gpu::RenderMesh mesh;
            for (const auto& v : entity.mesh->vertices) {
                mesh.vertices.push_back(v.position.x);
                mesh.vertices.push_back(v.position.y);
                mesh.vertices.push_back(v.position.z);
                mesh.normals.push_back(v.normal.x);
                mesh.normals.push_back(v.normal.y);
                mesh.normals.push_back(v.normal.z);
                mesh.uvs.push_back(v.uv.x);
                mesh.uvs.push_back(v.uv.y);
                mesh.tangents.push_back(v.tangent.x);
                mesh.tangents.push_back(v.tangent.y);
                mesh.tangents.push_back(v.tangent.z);
            }
            mesh.indices = entity.mesh->indices;
            mesh.material_index = -1;
            compiled.meshes.push_back(mesh);
            mesh_idx = static_cast<int>(compiled.meshes.size()) - 1;
            mesh_map[entity.mesh] = mesh_idx;
        }

        ure::gpu::GpuInstance inst;
        inst.mesh_index = mesh_idx;
        inst.material_index = cache_material(entity.material);
        compile_transform(entity.position, entity.scale, entity.rotation, inst);
        compute_world_aabb(entity.mesh, inst);
        compiled.instances.push_back(inst);
    }

    for (const auto& sphere : scene.spheres) {
        ure::gpu::GpuSphere gpu_sphere;
        gpu_sphere.center = to_gpu_vec3(sphere.center);
        gpu_sphere.radius = sphere.radius;
        gpu_sphere.material_index = cache_material(sphere.material);
        compiled.spheres.push_back(gpu_sphere);
    }

    return compiled;
}

CompiledGpuScene GpuSceneCompiler::compile(const scene_ir::SceneIR& scene_ir) {
    CompiledGpuScene compiled;
    compiled.camera = scene_ir.camera;
    compiled.medium_density = scene_ir.medium_density;
    compiled.medium_anisotropy = scene_ir.medium_anisotropy;
    compiled.medium_scattering = ure::gpu::GpuSpectrum::from_rgb(to_gpu_vec3(scene_ir.medium_scattering));
    compiled.medium_absorption = ure::gpu::GpuSpectrum::from_rgb(to_gpu_vec3(scene_ir.medium_absorption));
    compiled.medium_max_distance = scene_ir.medium_max_distance;
    compiled.width = scene_ir.width > 0 ? scene_ir.width : 1920;
    compiled.height = scene_ir.height > 0 ? scene_ir.height : 1080;

    std::map<std::shared_ptr<scene_ir::TextureResource>, int> texture_map;
    auto cache_texture = [&](const std::shared_ptr<scene_ir::TextureResource>& texture) -> int {
        if (!texture || !texture->image) return -1;

        auto existing = texture_map.find(texture);
        if (existing != texture_map.end()) {
            return existing->second;
        }

        ure::gpu::HostTexture host_texture;
        if (!io::load_image_rgb32f(texture->image->uri, host_texture)) {
            return -1;
        }
        io::apply_image_color_space(host_texture, texture->image->color_space);

        compiled.textures.push_back(std::move(host_texture));
        int index = static_cast<int>(compiled.textures.size()) - 1;
        texture_map[texture] = index;
        return index;
    };

    int material_offset = 7;
    std::map<std::shared_ptr<scene_ir::MaterialNode>, int> material_map;
    auto cache_material = [&](const std::shared_ptr<scene_ir::MaterialNode>& mat) -> int {
        if (!mat) return 0;
        auto it = material_map.find(mat);
        if (it != material_map.end()) return it->second;
        int texture_index = cache_texture(mat->base_color_texture);
        int roughness_texture_index = cache_texture(mat->roughness_texture);
        int emission_texture_index = cache_texture(mat->emission_texture);
        compiled.materials.push_back(compile_material(mat, texture_index, roughness_texture_index, emission_texture_index));
        int material_index = material_offset + static_cast<int>(compiled.materials.size()) - 1;
        material_map[mat] = material_index;
        return material_index;
    };

    std::map<std::shared_ptr<scene_ir::MeshResource>, int> mesh_map;
    for (const auto& instance : scene_ir.instances) {
        if (!instance.mesh || !instance.mesh->mesh) continue;

        int mesh_idx = -1;
        auto it = mesh_map.find(instance.mesh);
        if (it != mesh_map.end()) {
            mesh_idx = it->second;
        } else {
            ure::gpu::RenderMesh mesh;
            for (const auto& v : instance.mesh->mesh->vertices) {
                mesh.vertices.push_back(v.position.x);
                mesh.vertices.push_back(v.position.y);
                mesh.vertices.push_back(v.position.z);
                mesh.normals.push_back(v.normal.x);
                mesh.normals.push_back(v.normal.y);
                mesh.normals.push_back(v.normal.z);
                mesh.uvs.push_back(v.uv.x);
                mesh.uvs.push_back(v.uv.y);
                mesh.tangents.push_back(v.tangent.x);
                mesh.tangents.push_back(v.tangent.y);
                mesh.tangents.push_back(v.tangent.z);
            }
            mesh.indices = instance.mesh->mesh->indices;
            mesh.material_index = -1;
            compiled.meshes.push_back(mesh);
            mesh_idx = static_cast<int>(compiled.meshes.size()) - 1;
            mesh_map[instance.mesh] = mesh_idx;
        }

        ure::gpu::GpuInstance gpu_instance;
        gpu_instance.mesh_index = mesh_idx;
        gpu_instance.material_index = cache_material(instance.material);
        compile_transform(instance.position, instance.scale, instance.rotation, gpu_instance);
        compute_world_aabb(instance.mesh->mesh, gpu_instance);
        compiled.instances.push_back(gpu_instance);
    }

    for (const auto& sphere : scene_ir.spheres) {
        ure::gpu::GpuSphere gpu_sphere;
        gpu_sphere.center = to_gpu_vec3(sphere.center);
        gpu_sphere.radius = sphere.radius;
        gpu_sphere.material_index = cache_material(sphere.material);
        compiled.spheres.push_back(gpu_sphere);
    }

    return compiled;
}

} // namespace ure
