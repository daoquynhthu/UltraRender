#pragma once

#include "shape.hpp"
#include <vector>
#include <memory>
#include <string>

namespace ure::scene {

/**
 * @brief 三角形网格数据容器
 * 存储共享的顶点、法线和纹理坐标
 */
class Mesh {
public:
    std::vector<core::Point3f> vertices;
    std::vector<core::Normal3f> normals;
    std::vector<core::Point2f> uvs;
    std::vector<int> indices; // 顶点索引 (每3个组成一个三角形)

    Mesh(const std::string& name) : name_(name) {}

    const std::string& name() const { return name_; }

private:
    std::string name_;
};

/**
 * @brief 网格中的单个三角形
 * 引用 Mesh 中的数据，而不是存储副本，以节省内存
 */
class MeshTriangle : public Shape {
public:
    MeshTriangle(std::shared_ptr<Mesh> mesh, int face_index)
        : mesh_(mesh), face_index_(face_index) {}

    std::optional<ShapeIntersection> intersect(const core::Rayf& ray) const override;

    float area() const override;

    core::AABB bounds() const override;

private:
    std::shared_ptr<Mesh> mesh_;
    int face_index_; // 在 indices 中的起始位置 (i.e., indices[face_index_] 是第一个顶点索引)
};

} // namespace ure::scene
