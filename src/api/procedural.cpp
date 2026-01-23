#include "api/procedural.hpp"
#include <cmath>

namespace ure {

SceneBuilder& SceneBuilder::add_entity(std::shared_ptr<Mesh> mesh, std::shared_ptr<Material> mat, 
                        Vec3 pos, Vec3 scale, Vec3 rot) {
    RenderEntity entity;
    entity.mesh = mesh;
    entity.material = mat;
    entity.position = pos;
    entity.scale = scale;
    entity.rotation = rot;
    scene_.entities.push_back(entity);
    return *this;
}

SceneBuilder& SceneBuilder::add_sphere(Vec3 center, float radius, std::shared_ptr<Material> mat) {
    SphereEntity sphere;
    sphere.center = center;
    sphere.radius = radius;
    sphere.material = mat;
    scene_.spheres.push_back(sphere);
    return *this;
}

SceneBuilder& SceneBuilder::set_camera(Vec3 pos, Vec3 look_at, float fov) {
    scene_.camera.position = pos;
    scene_.camera.look_at = look_at;
    scene_.camera.fov = fov;
    return *this;
}

SceneBuilder& SceneBuilder::set_resolution(int width, int height) {
    scene_.width = width;
    scene_.height = height;
    return *this;
}

Scene SceneBuilder::build() {
    return scene_;
}

std::shared_ptr<Mesh> SceneBuilder::create_quad() {
    auto mesh = std::make_shared<Mesh>();
    // Simple Quad on XZ plane, facing up (+Y)
    // Vertices: Pos, Normal, UV
    mesh->vertices = {
        {{-1, 0,  1}, {0, 1, 0}, {0, 0}},
        {{ 1, 0,  1}, {0, 1, 0}, {1, 0}},
        {{ 1, 0, -1}, {0, 1, 0}, {1, 1}},
        {{-1, 0, -1}, {0, 1, 0}, {0, 1}}
    };
    mesh->indices = {0, 1, 2, 0, 2, 3};
    return mesh;
}

std::shared_ptr<Mesh> SceneBuilder::create_cube(float size) {
    auto mesh = std::make_shared<Mesh>();
    float h = size * 0.5f;

    // 24 vertices (4 per face * 6 faces) to support flat shading / sharp edges
    // Normals must be unique per face.
    
    // Front Face (+Z)
    mesh->vertices.push_back({{ -h, -h,  h}, { 0,  0,  1}, {0, 0}});
    mesh->vertices.push_back({{  h, -h,  h}, { 0,  0,  1}, {1, 0}});
    mesh->vertices.push_back({{  h,  h,  h}, { 0,  0,  1}, {1, 1}});
    mesh->vertices.push_back({{ -h,  h,  h}, { 0,  0,  1}, {0, 1}});

    // Back Face (-Z)
    mesh->vertices.push_back({{  h, -h, -h}, { 0,  0, -1}, {0, 0}});
    mesh->vertices.push_back({{ -h, -h, -h}, { 0,  0, -1}, {1, 0}});
    mesh->vertices.push_back({{ -h,  h, -h}, { 0,  0, -1}, {1, 1}});
    mesh->vertices.push_back({{  h,  h, -h}, { 0,  0, -1}, {0, 1}});

    // Top Face (+Y)
    mesh->vertices.push_back({{ -h,  h,  h}, { 0,  1,  0}, {0, 0}});
    mesh->vertices.push_back({{  h,  h,  h}, { 0,  1,  0}, {1, 0}});
    mesh->vertices.push_back({{  h,  h, -h}, { 0,  1,  0}, {1, 1}});
    mesh->vertices.push_back({{ -h,  h, -h}, { 0,  1,  0}, {0, 1}});

    // Bottom Face (-Y)
    mesh->vertices.push_back({{ -h, -h, -h}, { 0, -1,  0}, {0, 0}});
    mesh->vertices.push_back({{  h, -h, -h}, { 0, -1,  0}, {1, 0}});
    mesh->vertices.push_back({{  h, -h,  h}, { 0, -1,  0}, {1, 1}});
    mesh->vertices.push_back({{ -h, -h,  h}, { 0, -1,  0}, {0, 1}});

    // Right Face (+X)
    mesh->vertices.push_back({{  h, -h,  h}, { 1,  0,  0}, {0, 0}});
    mesh->vertices.push_back({{  h, -h, -h}, { 1,  0,  0}, {1, 0}});
    mesh->vertices.push_back({{  h,  h, -h}, { 1,  0,  0}, {1, 1}});
    mesh->vertices.push_back({{  h,  h,  h}, { 1,  0,  0}, {0, 1}});

    // Left Face (-X)
    mesh->vertices.push_back({{ -h, -h, -h}, {-1,  0,  0}, {0, 0}});
    mesh->vertices.push_back({{ -h, -h,  h}, {-1,  0,  0}, {1, 0}});
    mesh->vertices.push_back({{ -h,  h,  h}, {-1,  0,  0}, {1, 1}});
    mesh->vertices.push_back({{ -h,  h, -h}, {-1,  0,  0}, {0, 1}});

    // Indices
    for (int i = 0; i < 6; ++i) {
        int base = i * 4;
        mesh->indices.push_back(base + 0);
        mesh->indices.push_back(base + 1);
        mesh->indices.push_back(base + 2);
        mesh->indices.push_back(base + 0);
        mesh->indices.push_back(base + 2);
        mesh->indices.push_back(base + 3);
    }

    return mesh;
}

std::shared_ptr<Mesh> SceneBuilder::create_sphere(float radius, int slices, int stacks) {
    auto mesh = std::make_shared<Mesh>();
    
    // UV Sphere generation
    for (int i = 0; i <= stacks; ++i) {
        float v = (float)i / stacks;
        float phi = v * 3.14159265f; // 0 to Pi

        for (int j = 0; j <= slices; ++j) {
            float u = (float)j / slices;
            float theta = u * 2.0f * 3.14159265f; // 0 to 2Pi

            float x = radius * sinf(phi) * cosf(theta);
            float y = radius * cosf(phi); // Y is up
            float z = radius * sinf(phi) * sinf(theta);

            Vec3 pos = {x, y, z};
            Vec3 norm = {x/radius, y/radius, z/radius}; // Normalized if radius > 0
            Vec2 uv = {u, 1.0f - v}; // Flip V usually

            mesh->vertices.push_back({pos, norm, uv});
        }
    }

    // Indices
    for (int i = 0; i < stacks; ++i) {
        for (int j = 0; j < slices; ++j) {
            int p1 = i * (slices + 1) + j;
            int p2 = p1 + (slices + 1);
            
            // Two triangles per quad
            // p1 -- p1+1
            // |      |
            // p2 -- p2+1
            
            mesh->indices.push_back(p1);
            mesh->indices.push_back(p2);
            mesh->indices.push_back(p1 + 1);

            mesh->indices.push_back(p1 + 1);
            mesh->indices.push_back(p2);
            mesh->indices.push_back(p2 + 1);
        }
    }

    return mesh;
}

std::shared_ptr<Mesh> SceneBuilder::create_cylinder(float radius, float height, int segments) {
    auto mesh = std::make_shared<Mesh>();
    float h = height * 0.5f;

    // Helper to add vertex
    auto add_vert = [&](float x, float y, float z, float nx, float ny, float nz, float u, float v) {
        mesh->vertices.push_back({{x, y, z}, {nx, ny, nz}, {u, v}});
    };

    // 1. Side Surface
    for (int i = 0; i <= segments; ++i) {
        float theta = (float)i / segments * 2.0f * 3.14159265f;
        float x = radius * cosf(theta);
        float z = radius * sinf(theta);
        float u = (float)i / segments;

        // Top edge vertex
        add_vert(x, h, z, x/radius, 0, z/radius, u, 1.0f);
        // Bottom edge vertex
        add_vert(x, -h, z, x/radius, 0, z/radius, u, 0.0f);
    }

    // Side Indices
    int side_start = 0;
    for (int i = 0; i < segments; ++i) {
        int top1 = side_start + i * 2;
        int bot1 = top1 + 1;
        int top2 = top1 + 2;
        int bot2 = bot1 + 2;

        mesh->indices.push_back(top1);
        mesh->indices.push_back(bot1);
        mesh->indices.push_back(top2);

        mesh->indices.push_back(top2);
        mesh->indices.push_back(bot1);
        mesh->indices.push_back(bot2);
    }

    // 2. Top Cap
    int top_center_idx = mesh->vertices.size();
    add_vert(0, h, 0, 0, 1, 0, 0.5f, 0.5f); // Center
    int top_start = mesh->vertices.size();
    
    for (int i = 0; i <= segments; ++i) {
        float theta = (float)i / segments * 2.0f * 3.14159265f;
        float x = radius * cosf(theta);
        float z = radius * sinf(theta);
        // Map [-r, r] to [0, 1]
        float u = (x / radius + 1.0f) * 0.5f;
        float v = (z / radius + 1.0f) * 0.5f;
        add_vert(x, h, z, 0, 1, 0, u, v);
    }

    for (int i = 0; i < segments; ++i) {
        mesh->indices.push_back(top_center_idx);
        mesh->indices.push_back(top_start + i + 1);
        mesh->indices.push_back(top_start + i);
    }

    // 3. Bottom Cap
    int bot_center_idx = mesh->vertices.size();
    add_vert(0, -h, 0, 0, -1, 0, 0.5f, 0.5f); // Center
    int bot_start = mesh->vertices.size();

    for (int i = 0; i <= segments; ++i) {
        float theta = (float)i / segments * 2.0f * 3.14159265f;
        float x = radius * cosf(theta);
        float z = radius * sinf(theta);
        float u = (x / radius + 1.0f) * 0.5f;
        float v = (z / radius + 1.0f) * 0.5f;
        add_vert(x, -h, z, 0, -1, 0, u, v);
    }

    for (int i = 0; i < segments; ++i) {
        mesh->indices.push_back(bot_center_idx);
        mesh->indices.push_back(bot_start + i);
        mesh->indices.push_back(bot_start + i + 1);
    }

    return mesh;
}

std::shared_ptr<Mesh> SceneBuilder::create_cup(float radius, float height, float thickness, int segments) {
    auto mesh = std::make_shared<Mesh>();
    float h = height * 0.5f;
    float r_out = radius;
    float r_in = radius - thickness;
    float y_bot_out = -h;
    float y_bot_in = -h + thickness;
    float y_top = h;

    auto add_vert = [&](float x, float y, float z, float nx, float ny, float nz) {
        mesh->vertices.push_back({{x, y, z}, {nx, ny, nz}, {0, 0}}); // UV ignored for now
    };

    // 1. Outer Side (Normals Out)
    int outer_start = mesh->vertices.size();
    for (int i = 0; i <= segments; ++i) {
        float theta = (float)i / segments * 2.0f * 3.14159265f;
        float c = cosf(theta), s = sinf(theta);
        
        add_vert(r_out * c, y_top, r_out * s, c, 0, s);     // Top
        add_vert(r_out * c, y_bot_out, r_out * s, c, 0, s); // Bottom
    }
    for (int i = 0; i < segments; ++i) {
        int top1 = outer_start + i * 2;
        int bot1 = top1 + 1;
        int top2 = top1 + 2;
        int bot2 = bot1 + 2;
        mesh->indices.push_back(top1); mesh->indices.push_back(bot1); mesh->indices.push_back(top2);
        mesh->indices.push_back(top2); mesh->indices.push_back(bot1); mesh->indices.push_back(bot2);
    }

    // 2. Inner Side (Normals In)
    int inner_start = mesh->vertices.size();
    for (int i = 0; i <= segments; ++i) {
        float theta = (float)i / segments * 2.0f * 3.14159265f;
        float c = cosf(theta), s = sinf(theta);
        
        add_vert(r_in * c, y_top, r_in * s, -c, 0, -s);      // Top
        add_vert(r_in * c, y_bot_in, r_in * s, -c, 0, -s);   // Bottom
    }
    for (int i = 0; i < segments; ++i) {
        int top1 = inner_start + i * 2;
        int bot1 = top1 + 1;
        int top2 = top1 + 2;
        int bot2 = bot1 + 2;
        // Winding reversed for inward normals? No, vertices already have inward normals.
        // We need triangles to face inward.
        // Standard CCW winding for "front" face.
        // If normal is -N, and we view from inside, we want CCW.
        // So just standard topology.
        mesh->indices.push_back(top1); mesh->indices.push_back(top2); mesh->indices.push_back(bot1);
        mesh->indices.push_back(top2); mesh->indices.push_back(bot2); mesh->indices.push_back(bot1);
    }

    // 3. Rim (Annulus at Top)
    int rim_start = mesh->vertices.size();
    for (int i = 0; i <= segments; ++i) {
        float theta = (float)i / segments * 2.0f * 3.14159265f;
        float c = cosf(theta), s = sinf(theta);
        
        add_vert(r_out * c, y_top, r_out * s, 0, 1, 0); // Outer
        add_vert(r_in * c, y_top, r_in * s, 0, 1, 0);   // Inner
    }
    for (int i = 0; i < segments; ++i) {
        int out1 = rim_start + i * 2;
        int in1 = out1 + 1;
        int out2 = out1 + 2;
        int in2 = in1 + 2;
        mesh->indices.push_back(out1); mesh->indices.push_back(in1); mesh->indices.push_back(out2);
        mesh->indices.push_back(out2); mesh->indices.push_back(in1); mesh->indices.push_back(in2);
    }

    // 4. Inner Bottom (Disk facing up at y_bot_in)
    int in_bot_center = mesh->vertices.size();
    add_vert(0, y_bot_in, 0, 0, 1, 0);
    int in_bot_start = mesh->vertices.size();
    for (int i = 0; i <= segments; ++i) {
        float theta = (float)i / segments * 2.0f * 3.14159265f;
        add_vert(r_in * cosf(theta), y_bot_in, r_in * sinf(theta), 0, 1, 0);
    }
    for (int i = 0; i < segments; ++i) {
        mesh->indices.push_back(in_bot_center);
        mesh->indices.push_back(in_bot_start + i + 1);
        mesh->indices.push_back(in_bot_start + i);
    }

    // 5. Outer Bottom (Disk facing down at y_bot_out)
    int out_bot_center = mesh->vertices.size();
    add_vert(0, y_bot_out, 0, 0, -1, 0);
    int out_bot_start = mesh->vertices.size();
    for (int i = 0; i <= segments; ++i) {
        float theta = (float)i / segments * 2.0f * 3.14159265f;
        add_vert(r_out * cosf(theta), y_bot_out, r_out * sinf(theta), 0, -1, 0);
    }
    for (int i = 0; i < segments; ++i) {
        mesh->indices.push_back(out_bot_center);
        mesh->indices.push_back(out_bot_start + i);
        mesh->indices.push_back(out_bot_start + i + 1);
    }

    return mesh;
}

std::shared_ptr<Mesh> SceneBuilder::create_torus(float major_radius, float minor_radius, int major_segments, int minor_segments) {
    auto mesh = std::make_shared<Mesh>();
    
    for (int i = 0; i <= major_segments; ++i) {
        float theta = (float)i / major_segments * 2.0f * 3.14159265f;
        float cos_theta = cosf(theta);
        float sin_theta = sinf(theta);

        for (int j = 0; j <= minor_segments; ++j) {
            float phi = (float)j / minor_segments * 2.0f * 3.14159265f;
            float cos_phi = cosf(phi);
            float sin_phi = sinf(phi);

            // Position
            float x = (major_radius + minor_radius * cos_phi) * cos_theta;
            float y = minor_radius * sin_phi;
            float z = (major_radius + minor_radius * cos_phi) * sin_theta;

            // Normal (pointing outwards from the tube center)
            float nx = cos_phi * cos_theta;
            float ny = sin_phi;
            float nz = cos_phi * sin_theta;

            Vec3 pos = {x, y, z};
            Vec3 norm = {nx, ny, nz};
            Vec2 uv = {(float)i / major_segments, (float)j / minor_segments};

            mesh->vertices.push_back({pos, norm, uv});
        }
    }

    for (int i = 0; i < major_segments; ++i) {
        for (int j = 0; j < minor_segments; ++j) {
            int p1 = i * (minor_segments + 1) + j;
            int p2 = p1 + (minor_segments + 1);
            
            mesh->indices.push_back(p1);
            mesh->indices.push_back(p2);
            mesh->indices.push_back(p1 + 1);

            mesh->indices.push_back(p1 + 1);
            mesh->indices.push_back(p2);
            mesh->indices.push_back(p2 + 1);
        }
    }

    return mesh;
}

}
