#include "ure/scene/obj_loader.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <ure/log.hpp>

namespace ure::scene {

std::shared_ptr<Mesh> ObjLoader::load(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        UR_LOG_ERROR(SceneIO, "Failed to open OBJ file: {}", filename);
        return nullptr;
    }

    auto mesh = std::make_shared<Mesh>(filename);
    std::string line;
    
    // 临时存储
    std::vector<core::Point3f> temp_vertices;
    std::vector<core::Normal3f> temp_normals;
    std::vector<core::Point2f> temp_uvs;

    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::stringstream ss(line);
        std::string type;
        ss >> type;

        if (type == "v") {
            float x, y, z;
            ss >> x >> y >> z;
            temp_vertices.emplace_back(x, y, z);
        } else if (type == "vn") {
            float x, y, z;
            ss >> x >> y >> z;
            temp_normals.emplace_back(x, y, z);
        } else if (type == "vt") {
            float u, v;
            ss >> u >> v;
            temp_uvs.emplace_back(u, v);
        } else if (type == "f") {
            std::string vertex_str;
            std::vector<int> face_indices;
            std::vector<int> face_normal_indices;
            std::vector<int> face_uv_indices;

            while (ss >> vertex_str) {
                std::replace(vertex_str.begin(), vertex_str.end(), '/', ' ');
                std::stringstream vss(vertex_str);
                int v_idx, vt_idx, vn_idx;
                
                // OBJ 索引从 1 开始
                vss >> v_idx;
                face_indices.push_back(v_idx - 1);

                if (vss >> vt_idx) {
                    face_uv_indices.push_back(vt_idx - 1);
                }
                if (vss >> vn_idx) {
                    face_normal_indices.push_back(vn_idx - 1);
                }
            }

            // 三角化 (Triangulate) - 简单的扇形三角化
            for (size_t i = 1; i < face_indices.size() - 1; ++i) {
                // 顶点处理
                // 我们需要将 OBJ 的这种 "索引到临时列表" 的结构，转换为 Mesh 的 "对齐的顶点/法线/UV" 结构
                // 最简单的方法是：把每个独特的 (v, vt, vn) 组合变成一个新的 Mesh 顶点
                // 但为了简化，我们先假设：如果提供了 vn/vt，它们必须和 v 一一对应，或者我们直接展开所有顶点
                
                // 这里的实现策略：直接展开 (Flatten)。虽然浪费空间，但最简单且 robust。
                // 每一个三角形的顶点都新加到 mesh->vertices 中
                
                auto add_vertex = [&](int idx, int uv_idx, int n_idx) {
                    mesh->vertices.push_back(temp_vertices[idx]);
                    if (!temp_normals.empty() && n_idx >= 0 && static_cast<size_t>(n_idx) < temp_normals.size()) {
                        mesh->normals.push_back(temp_normals[n_idx]);
                    }
                    if (!temp_uvs.empty() && uv_idx >= 0 && static_cast<size_t>(uv_idx) < temp_uvs.size()) {
                        mesh->uvs.push_back(temp_uvs[uv_idx]);
                    }
                    mesh->indices.push_back(static_cast<int>(mesh->vertices.size()) - 1);
                };

                // 第一个顶点
                int v0 = face_indices[0];
                int vt0 = (!face_uv_indices.empty()) ? face_uv_indices[0] : -1;
                int vn0 = (!face_normal_indices.empty()) ? face_normal_indices[0] : -1;
                add_vertex(v0, vt0, vn0);

                // 第二个顶点
                int v1 = face_indices[i];
                int vt1 = (!face_uv_indices.empty()) ? face_uv_indices[i] : -1;
                int vn1 = (!face_normal_indices.empty()) ? face_normal_indices[i] : -1;
                add_vertex(v1, vt1, vn1);

                // 第三个顶点
                int v2 = face_indices[i + 1];
                int vt2 = (!face_uv_indices.empty()) ? face_uv_indices[i + 1] : -1;
                int vn2 = (!face_normal_indices.empty()) ? face_normal_indices[i + 1] : -1;
                add_vertex(v2, vt2, vn2);
            }
        }
    }

    UR_LOG_INFO(SceneIO, "Loaded OBJ: {} ({} triangles)", filename, mesh->indices.size() / 3);
    return mesh;
}

} // namespace ure::scene
