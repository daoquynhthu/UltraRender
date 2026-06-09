#include "ure/scene/mesh.hpp"
#include <cmath>
#include <iostream>

namespace ure::scene {

std::optional<ShapeIntersection> MeshTriangle::intersect(const core::Rayf& ray) const {
    // 获取三角形顶点
    int i0 = mesh_->indices[face_index_];
    int i1 = mesh_->indices[face_index_ + 1];
    int i2 = mesh_->indices[face_index_ + 2];

    const core::Point3f& p0 = mesh_->vertices[i0];
    const core::Point3f& p1 = mesh_->vertices[i1];
    const core::Point3f& p2 = mesh_->vertices[i2];

    // Möller-Trumbore 算法
    core::Vec3f edge1 = p1 - p0;
    core::Vec3f edge2 = p2 - p0;
    core::Vec3f pvec = ray.direction.cross(edge2);
    float det = edge1.dot(pvec);

    if (std::abs(det) < 1e-8f) return std::nullopt;
    float inv_det = 1.0f / det;

    core::Vec3f tvec = ray.origin - p0;
    float u = tvec.dot(pvec) * inv_det;
    if (u < 0.0f || u > 1.0f) return std::nullopt;

    core::Vec3f qvec = tvec.cross(edge1);
    float v = ray.direction.dot(qvec) * inv_det;
    if (v < 0.0f || u + v > 1.0f) return std::nullopt;

    float t = edge2.dot(qvec) * inv_det;
    if (t < 1e-4f || t > ray.t_max) return std::nullopt;

    ShapeIntersection isect;
    isect.t = t;
    isect.p = ray.at(t);
    
    // 计算法线插值 (如果有顶点法线)
    if (!mesh_->normals.empty()) {
        const core::Normal3f& n0 = mesh_->normals[i0];
        const core::Normal3f& n1 = mesh_->normals[i1];
        const core::Normal3f& n2 = mesh_->normals[i2];
        // 重心坐标插值: (1-u-v)*n0 + u*n1 + v*n2
        isect.n = (n0 * (1.0f - u - v) + n1 * u + n2 * v).normalize();
    } else {
        isect.n = edge1.cross(edge2).normalize();
    }

    // 计算 UV 插值
    if (!mesh_->uvs.empty()) {
        const core::Point2f& uv0 = mesh_->uvs[i0];
        const core::Point2f& uv1 = mesh_->uvs[i1];
        const core::Point2f& uv2 = mesh_->uvs[i2];
        isect.uv = uv0 * (1.0f - u - v) + uv1 * u + uv2 * v;
    } else {
        isect.uv = core::Point2f(u, v);
    }

    return isect;
}

float MeshTriangle::area() const {
    int i0 = mesh_->indices[face_index_];
    int i1 = mesh_->indices[face_index_ + 1];
    int i2 = mesh_->indices[face_index_ + 2];
    const core::Point3f& p0 = mesh_->vertices[i0];
    const core::Point3f& p1 = mesh_->vertices[i1];
    const core::Point3f& p2 = mesh_->vertices[i2];
    return 0.5f * (p1 - p0).cross(p2 - p0).length();
}

core::AABB MeshTriangle::bounds() const {
    int i0 = mesh_->indices[face_index_];
    int i1 = mesh_->indices[face_index_ + 1];
    int i2 = mesh_->indices[face_index_ + 2];
    const core::Point3f& p0 = mesh_->vertices[i0];
    const core::Point3f& p1 = mesh_->vertices[i1];
    const core::Point3f& p2 = mesh_->vertices[i2];
    
    core::AABB b(p0);
    b.expand(p1);
    b.expand(p2);
    return b;
}

} // namespace ure::scene
