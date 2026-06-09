#include "ure/gpu_scene_loader.hpp"
#include "ure/material_library.hpp"
#include <cmath>

namespace ure::gpu {

// Helper to add a cube mesh with rotation
void add_cube(GpuHostScene& scene, const GpuVec3& center, float size, int mat_idx, const GpuVec3& rot_deg = GpuVec3(0,0,0)) {
    HostMesh mesh;
    mesh.material_index = mat_idx;
    float h = size * 0.5f;
    
    // Rotation Matrices Helpers
    float rad_x = rot_deg.x * 3.14159265f / 180.0f;
    float rad_y = rot_deg.y * 3.14159265f / 180.0f;
    float rad_z = rot_deg.z * 3.14159265f / 180.0f;

    auto rotate = [&](GpuVec3 p) {
        // X
        float y1 = p.y * cosf(rad_x) - p.z * sinf(rad_x);
        float z1 = p.y * sinf(rad_x) + p.z * cosf(rad_x);
        p.y = y1; p.z = z1;
        // Y
        float x2 = p.x * cosf(rad_y) + p.z * sinf(rad_y);
        float z2 = -p.x * sinf(rad_y) + p.z * cosf(rad_y);
        p.x = x2; p.z = z2;
        // Z
        float x3 = p.x * cosf(rad_z) - p.y * sinf(rad_z);
        float y3 = p.x * sinf(rad_z) + p.y * cosf(rad_z);
        p.x = x3; p.y = y3;
        return p;
    };

    struct Face {
        GpuVec3 v[4]; // BL, BR, TR, TL
    };

    Face faces[6] = {
        // Front (+Z)
        {{ GpuVec3(-h,-h,h), GpuVec3(h,-h,h), GpuVec3(h,h,h), GpuVec3(-h,h,h) }},
        // Back (-Z)
        {{ GpuVec3(h,-h,-h), GpuVec3(-h,-h,-h), GpuVec3(-h,h,-h), GpuVec3(h,h,-h) }},
        // Top (+Y)
        {{ GpuVec3(-h,h,h), GpuVec3(h,h,h), GpuVec3(h,h,-h), GpuVec3(-h,h,-h) }},
        // Bottom (-Y)
        {{ GpuVec3(-h,-h,-h), GpuVec3(h,-h,-h), GpuVec3(h,-h,h), GpuVec3(-h,-h,h) }},
        // Right (+X)
        {{ GpuVec3(h,-h,h), GpuVec3(h,-h,-h), GpuVec3(h,h,-h), GpuVec3(h,h,h) }},
        // Left (-X)
        {{ GpuVec3(-h,-h,-h), GpuVec3(-h,-h,h), GpuVec3(-h,h,h), GpuVec3(-h,h,-h) }}
    };

    int idx_offset = 0;
    for (int i = 0; i < 6; ++i) {
        for (int j = 0; j < 4; ++j) {
            GpuVec3 p = rotate(faces[i].v[j]);
            p = p + center;
            mesh.vertices.push_back(p.x);
            mesh.vertices.push_back(p.y);
            mesh.vertices.push_back(p.z);
        }

        // Normals (Rotate face normal)
        GpuVec3 normal_local;
        if (i == 0) normal_local = GpuVec3(0, 0, 1);
        else if (i == 1) normal_local = GpuVec3(0, 0, -1);
        else if (i == 2) normal_local = GpuVec3(0, 1, 0);
        else if (i == 3) normal_local = GpuVec3(0, -1, 0);
        else if (i == 4) normal_local = GpuVec3(1, 0, 0);
        else if (i == 5) normal_local = GpuVec3(-1, 0, 0);

        GpuVec3 n = rotate(normal_local); // Reuse rotate for normals (since it's pure rotation, no scale/shear)
        
        for (int j = 0; j < 4; ++j) {
            mesh.normals.push_back(n.x);
            mesh.normals.push_back(n.y);
            mesh.normals.push_back(n.z);
        }
        
        // UVs: 0,0  1,0  1,1  0,1
        mesh.uvs.push_back(0.0f); mesh.uvs.push_back(0.0f);
        mesh.uvs.push_back(1.0f); mesh.uvs.push_back(0.0f);
        mesh.uvs.push_back(1.0f); mesh.uvs.push_back(1.0f);
        mesh.uvs.push_back(0.0f); mesh.uvs.push_back(1.0f);

        // Tangents: +X direction default
        mesh.tangents.push_back(1.0f); mesh.tangents.push_back(0.0f); mesh.tangents.push_back(0.0f);
        mesh.tangents.push_back(1.0f); mesh.tangents.push_back(0.0f); mesh.tangents.push_back(0.0f);
        mesh.tangents.push_back(1.0f); mesh.tangents.push_back(0.0f); mesh.tangents.push_back(0.0f);
        mesh.tangents.push_back(1.0f); mesh.tangents.push_back(0.0f); mesh.tangents.push_back(0.0f);

        // Indices (Two triangles: 0,1,2 and 0,2,3)
        mesh.indices.push_back(idx_offset + 0);
        mesh.indices.push_back(idx_offset + 1);
        mesh.indices.push_back(idx_offset + 2);
        mesh.indices.push_back(idx_offset + 0);
        mesh.indices.push_back(idx_offset + 2);
        mesh.indices.push_back(idx_offset + 3);

        idx_offset += 4;
    }
    
    scene.meshes.push_back(mesh);
}

// Helper to create a procedural spectral test texture
HostTexture create_spectral_test_texture() {
    HostTexture tex;
    tex.width = 2048;
    tex.height = 2048;
    tex.data.resize(tex.width * tex.height * 3);

    for (int y = 0; y < tex.height; ++y) {
        for (int x = 0; x < tex.width; ++x) {
            float u = (float)x / (tex.width - 1);
            float v = (float)y / (tex.height - 1);
            
            // Generate a spectral-like pattern (Rainbow Gradient)
            // Mapping U to Hue roughly
            float r = fabsf(6.0f * u - 3.0f) - 1.0f;
            float g = 2.0f - fabsf(6.0f * u - 2.0f);
            float b = 2.0f - fabsf(6.0f * u - 4.0f);
            
            // Clamp
            r = std::max(0.0f, std::min(1.0f, r));
            g = std::max(0.0f, std::min(1.0f, g));
            b = std::max(0.0f, std::min(1.0f, b));

            // Modulate with V to use it and create gradient
            float v_mod = 0.5f + 0.5f * v;
            r *= v_mod;
            g *= v_mod;
            b *= v_mod;
            
            // Add some "spectral lines" (vertical stripes)
            if ((x % 50) < 5) {
                r += 0.5f; g += 0.5f; b += 0.5f;
            }

            // Grid pattern
            if ((x % 256) < 5 || (y % 256) < 5) {
                r = 0.8f; g = 0.8f; b = 0.8f;
            }

            int idx = (y * tex.width + x) * 3;
            tex.data[idx + 0] = r;
            tex.data[idx + 1] = g;
            tex.data[idx + 2] = b;
        }
    }
    return tex;
}

GpuHostScene load_default_scene(bool has_mesh) {
    GpuHostScene scene;
    
    if (!has_mesh) {
        HostTexture tex = create_spectral_test_texture();
        scene.textures.push_back(tex);
    }

    // Use Material Library for templates
    // 0: Ground (Replaced with Gray Lambertian)
    // auto ground_mat = MaterialLibrary::cloth_grey_procedural();
    // ground_mat.texture_index = 0; // Use texture 0
    
    // Simple Gray-White Lambertian (0.8)
    GpuMaterial ground_mat;
    ground_mat.type = MaterialType::Lambertian;
    ground_mat.albedo = GpuSpectrum::from_rgb(GpuVec3(0.8f, 0.8f, 0.8f));
    ground_mat.roughness = 1.0f;
    ground_mat.ior = 1.0f;
    ground_mat.extinction = GpuSpectrum(0.0f);
    ground_mat.dispersion = 0.0f;
    ground_mat.thin_film_thickness = 0.0f;
    ground_mat.thin_film_ior = 1.0f;
    ground_mat.emission = GpuSpectrum(0.0f);
    ground_mat.texture_index = -1;
    
    scene.materials.push_back(ground_mat);
    
    // 1: Glass
    scene.materials.push_back(MaterialLibrary::glass_clear());
    // 2: Metal
    scene.materials.push_back(MaterialLibrary::metal_gold_fuzzy());
    // 3: Red Sphere
    scene.materials.push_back(MaterialLibrary::red_matte());
    // 4: Light Source (High Emission)
    scene.materials.push_back(MaterialLibrary::bright_light());
    
    // 5: Mesh Material (Blue-ish)
    scene.materials.push_back(MaterialLibrary::blue_mesh());
    
    // 6: Diamond (High Dispersion)
    scene.materials.push_back(MaterialLibrary::glass_diamond());

    // Spheres
    if (!has_mesh) {
        scene.spheres.push_back({GpuVec3(0, -1000, 0), 1000.0f, 0}); // Ground

        // Triangle Configuration
        // Camera is roughly at (0, 4, 18) looking at (0, 1, 0)
        
        // 1. Back Left: Glass Sphere
        scene.spheres.push_back({GpuVec3(-2.5f, 1.0f, -1.0f), 1.0f, 1});
        
        // 2. Back Right: Metal Sphere
        scene.spheres.push_back({GpuVec3(2.5f, 1.0f, -1.0f), 1.0f, 2});
        
        // 3. Front Center: Diamond Cube - REMOVED

        // Light Source
        // Decreased radius to 1.0 for Sharper Caustics/Shadows
        // Intensity increased in MaterialLibrary (200.0)
        scene.spheres.push_back({GpuVec3(0, 15, 5), 1.5f, 4});

    } else {
        // When using external meshes, do not add default ground/light.
        // The scene is fully controlled by the host.
        // scene.spheres.push_back({GpuVec3(0, -1000, 0), 1000.0f, 0});
        // scene.spheres.push_back({GpuVec3(10, 20, 15), 8.0f, 4});
    }
    
    return scene;
}

}
