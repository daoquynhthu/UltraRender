#pragma once

#include "path_tracer_decl.cuh"

static __device__ bool hit_sphere(const GpuSphere& sphere, const GpuRay& r, float t_min, float t_max, float& t, GpuVec3& p, GpuVec3& n, int& mat_idx) {
    GpuVec3 oc = r.origin - sphere.center;
    float a = r.direction.dot(r.direction);
    float b = oc.dot(r.direction);
    float c = oc.dot(oc) - sphere.radius * sphere.radius;
    float discriminant = b * b - a * c;

    if (discriminant > 0) {
        float temp = (-b - sqrtf(discriminant)) / a;
        if (temp < t_max && temp > t_min) {
            t = temp;
            p = r.at(t);
            p = sphere.center + (p - sphere.center).normalize() * sphere.radius;
            n = (p - sphere.center) * (1.0f / sphere.radius);
            mat_idx = sphere.material_index;
            return true;
        }
        temp = (-b + sqrtf(discriminant)) / a;
        if (temp < t_max && temp > t_min) {
            t = temp;
            p = r.at(t);
            p = sphere.center + (p - sphere.center).normalize() * sphere.radius;
            n = (p - sphere.center) * (1.0f / sphere.radius);
            mat_idx = sphere.material_index;
            return true;
        }
    }
    return false;
}

static __device__ bool hit_triangle(const GpuRay& r, const GpuVec3& v0, const GpuVec3& v1, const GpuVec3& v2, const GpuVec3* n0, const GpuVec3* n1, const GpuVec3* n2, float t_min, float t_max, float& t, GpuVec3& ng, GpuVec3& ns, float& u_out, float& v_out) {
    GpuVec3 v0v1 = v1 - v0;
    GpuVec3 v0v2 = v2 - v0;
    GpuVec3 pvec = r.direction.cross(v0v2);
    float det = v0v1.dot(pvec);
    float determinant_scale = sqrtf(fmaxf(
        0.0f, v0v1.dot(v0v1) * pvec.dot(pvec)));
    if (!isfinite(det) || determinant_scale == 0.0f ||
        fabsf(det) <
            1e-8f * fminf(determinant_scale, 1.0f)) {
        return false;
    }

    float invDet = 1.0f / det;
    GpuVec3 tvec = r.origin - v0;
    float u = tvec.dot(pvec) * invDet;

    if (u < 0.0f || u > 1.0f) return false;

    GpuVec3 qvec = tvec.cross(v0v1);
    float v = r.direction.dot(qvec) * invDet;

    if (v < 0.0f || u + v > 1.0f) return false;

    float temp_t = v0v2.dot(qvec) * invDet;

    if (isfinite(temp_t) && temp_t < t_max && temp_t > t_min) {
        t = temp_t;
        GpuVec3 geometric = v0v1.cross(v0v2);
        float geometric_length_squared = geometric.dot(geometric);
        if (!(geometric_length_squared > 0.0f) ||
            !isfinite(geometric_length_squared)) {
            return false;
        }
        ng = geometric.normalize();

        if (n0 && n1 && n2) {
            float w = 1.0f - u - v;
            GpuVec3 interpolated =
                *n0 * w + *n1 * u + *n2 * v;
            float length_squared =
                interpolated.dot(interpolated);
            ns = length_squared > 0.0f &&
                    isfinite(length_squared)
                ? interpolated.normalize()
                : ng;
        } else {
            ns = ng;
        }

        u_out = u;
        v_out = v;
        return true;
    }
    return false;
}

static __device__ bool update_aabb_slab(
    float origin,
    float direction,
    float minimum,
    float maximum,
    float& near_t,
    float& far_t) {
    if (!isfinite(origin) || !isfinite(direction) ||
        !isfinite(minimum) || !isfinite(maximum) ||
        minimum > maximum) {
        return false;
    }
    if (fabsf(direction) <= 1e-30f) {
        return origin >= minimum && origin <= maximum;
    }
    float inverse_direction = 1.0f / direction;
    float first = (minimum - origin) * inverse_direction;
    float second = (maximum - origin) * inverse_direction;
    if (first > second) {
        float temp = first;
        first = second;
        second = temp;
    }
    near_t = fmaxf(near_t, first);
    far_t = fminf(far_t, second);
    return near_t <= far_t;
}

static __device__ bool hit_aabb(const GpuRay& r, const GpuVec3& min_pt, const GpuVec3& max_pt, float t_min, float t_max) {
    float near_t = t_min;
    float far_t = t_max;
    return update_aabb_slab(
               r.origin.x, r.direction.x,
               min_pt.x, max_pt.x, near_t, far_t) &&
           update_aabb_slab(
               r.origin.y, r.direction.y,
               min_pt.y, max_pt.y, near_t, far_t) &&
           update_aabb_slab(
               r.origin.z, r.direction.z,
               min_pt.z, max_pt.z, near_t, far_t);
}

enum class BvhTraversalResult : int {
    Miss = 0,
    Hit = 1,
    StackOverflow = 2,
    InvalidAcceleration = 3
};

static __device__ void record_acceleration_count(
    unsigned long long* counter) {
    if (counter) atomicAdd(counter, 1ull);
}

static __device__ BvhTraversalResult hit_bvh(
    const GpuMesh& mesh,
    const GpuRay& r,
    float t_min,
    float t_max,
    float& t_out,
    GpuVec3& ng_out,
    GpuVec3& ns_out,
    GpuVec2& uv_out,
    int& primitive_index_out,
    GpuAccelerationTelemetry* telemetry = nullptr,
    bool shadow = false,
    bool collect_stats = false) {
    if (!mesh.bvh_nodes || mesh.bvh_node_count <= 0 ||
        !mesh.vertices || !mesh.indices ||
        mesh.triangle_count <= 0) {
        if (telemetry) {
            record_acceleration_count(
                &telemetry->invalid_acceleration_count);
        }
        return BvhTraversalResult::InvalidAcceleration;
    }
    bool hit_anything = false;
    float t_closest = t_max;

    int stack[kBvhTraversalStackCapacity];
    int stack_ptr = 0;
    stack[stack_ptr++] = 0;

    while (stack_ptr > 0) {
        int node_idx = stack[--stack_ptr];
        if (node_idx < 0 || node_idx >= mesh.bvh_node_count) {
            if (telemetry) {
                record_acceleration_count(
                    &telemetry->invalid_acceleration_count);
            }
            return BvhTraversalResult::InvalidAcceleration;
        }
        if (collect_stats && telemetry) {
            record_acceleration_count(
                shadow
                    ? &telemetry->shadow_node_visits
                    : &telemetry->closest_node_visits);
        }
        const GpuBvhNode& node = mesh.bvh_nodes[node_idx];
        if (node.primitive_count < 0 ||
            !isfinite(node.min_pt.x) ||
            !isfinite(node.min_pt.y) ||
            !isfinite(node.min_pt.z) ||
            !isfinite(node.max_pt.x) ||
            !isfinite(node.max_pt.y) ||
            !isfinite(node.max_pt.z) ||
            node.min_pt.x > node.max_pt.x ||
            node.min_pt.y > node.max_pt.y ||
            node.min_pt.z > node.max_pt.z) {
            if (telemetry) {
                record_acceleration_count(
                    &telemetry->invalid_acceleration_count);
            }
            return BvhTraversalResult::InvalidAcceleration;
        }

        if (!hit_aabb(r, node.min_pt, node.max_pt, t_min, t_closest)) {
            continue;
        }

        if (node.primitive_count > 0) {
            int start_idx = node.child_or_primitive_index;
            if (start_idx < 0 ||
                start_idx > mesh.triangle_count ||
                node.primitive_count >
                    mesh.triangle_count - start_idx) {
                if (telemetry) {
                    record_acceleration_count(
                        &telemetry->invalid_acceleration_count);
                }
                return BvhTraversalResult::InvalidAcceleration;
            }
            int end_idx = start_idx + node.primitive_count;

            for (int i = start_idx; i < end_idx; ++i) {
                if (collect_stats && telemetry) {
                    record_acceleration_count(
                        shadow
                            ? &telemetry->shadow_triangle_tests
                            : &telemetry->closest_triangle_tests);
                }
                int i0 = mesh.indices[i * 3 + 0];
                int i1 = mesh.indices[i * 3 + 1];
                int i2 = mesh.indices[i * 3 + 2];

                GpuVec3 v0 = mesh.vertices[i0];
                GpuVec3 v1 = mesh.vertices[i1];
                GpuVec3 v2 = mesh.vertices[i2];

                const GpuVec3* n0_ptr = nullptr;
                const GpuVec3* n1_ptr = nullptr;
                const GpuVec3* n2_ptr = nullptr;

                if (mesh.normals) {
                    n0_ptr = &mesh.normals[i0];
                    n1_ptr = &mesh.normals[i1];
                    n2_ptr = &mesh.normals[i2];
                }

                float t_tri;
                GpuVec3 ng_tri, ns_tri;
                float u_tri, v_tri;

                if (hit_triangle(r, v0, v1, v2, n0_ptr, n1_ptr, n2_ptr, t_min, t_closest, t_tri, ng_tri, ns_tri, u_tri, v_tri)) {
                    hit_anything = true;
                    t_closest = t_tri;
                    t_out = t_tri;
                    ng_out = ng_tri;
                    ns_out = ns_tri;
                    primitive_index_out = i;

                    if (mesh.uvs) {
                        GpuVec2 uv0 = mesh.uvs[i0];
                        GpuVec2 uv1 = mesh.uvs[i1];
                        GpuVec2 uv2 = mesh.uvs[i2];
                        float w_tri = 1.0f - u_tri - v_tri;
                        uv_out = uv0 * w_tri + uv1 * u_tri + uv2 * v_tri;
                    } else {
                        uv_out = GpuVec2(0.0f, 0.0f);
                    }
                }
            }
        } else {
            int left_child = node_idx + 1;
            int right_child = node.child_or_primitive_index;

            if (left_child >= mesh.bvh_node_count ||
                right_child <= node_idx ||
                right_child >= mesh.bvh_node_count) {
                if (telemetry) {
                    record_acceleration_count(
                        &telemetry->invalid_acceleration_count);
                }
                return BvhTraversalResult::InvalidAcceleration;
            }
            if (stack_ptr >
                kBvhTraversalStackCapacity - 2) {
                if (telemetry) {
                    record_acceleration_count(
                        &telemetry->stack_overflow_count);
                }
                return BvhTraversalResult::StackOverflow;
            }
            stack[stack_ptr++] = right_child;
            stack[stack_ptr++] = left_child;
        }
    }

    return hit_anything
        ? BvhTraversalResult::Hit
        : BvhTraversalResult::Miss;
}

static __device__ bool world_hit(const GpuScene& scene, const GpuRay& r, float t_min, float t_max, float& t_out, GpuVec3& p_out, GpuVec3& n_out, GpuVec3& ng_out, GpuVec2& uv_out, int& mat_idx_out, int& type_out, int& index_out, int& primitive_index_out, bool ignore_lights = false) {
    float t_closest = t_max;
    bool hit_anything = false;
    float t_temp;
    GpuVec3 p_temp, n_temp;
    int mat_idx_temp;

    for (int i = 0; i < scene.sphere_count; ++i) {
        if (ignore_lights && scene.materials[scene.spheres[i].material_index].type == MaterialType::Light) continue;

        if (hit_sphere(scene.spheres[i], r, t_min, t_closest, t_temp, p_temp, n_temp, mat_idx_temp)) {
            hit_anything = true;
            t_closest = t_temp;
            t_out = t_temp;
            p_out = p_temp;
            n_out = n_temp;
            ng_out = n_temp;
            mat_idx_out = mat_idx_temp;
            type_out = 0;
            index_out = i;
            primitive_index_out = -1;

            GpuVec3 p_local = (p_temp - scene.spheres[i].center).normalize();
            float phi = atan2f(p_local.z, p_local.x);
            float theta = asinf(p_local.y);
            float u = 1.0f - (phi + 3.14159265f) / (2.0f * 3.14159265f);
            float v = (theta + 3.14159265f / 2.0f) / 3.14159265f;
            uv_out = GpuVec2(u, v);
        }
    }

    for (int i = 0; i < scene.instance_count; ++i) {
        const GpuInstanceDesc& desc = scene.instance_descs[i];
        const GpuInstanceTransform& xform = scene.instance_transforms[i];

        if (!hit_aabb(r, xform.min_pt, xform.max_pt, t_min, t_closest)) {
            continue;
        }

        GpuRay r_obj = r;
        r_obj.origin = xform.inverse_transform.transform_point(r.origin);
        r_obj.direction = xform.inverse_transform.transform_vector(r.direction);

        const GpuMesh& mesh = scene.meshes[desc.mesh_index];

        float t_mesh;
        GpuVec3 ng_mesh, ns_mesh;
        GpuVec2 uv_mesh;
        bool hit_mesh = false;
        int primitive_index_mesh = -1;

        if (mesh.triangle_count > 0) {
            const auto traversal = hit_bvh(
                mesh, r_obj, t_min, t_closest, t_mesh,
                ng_mesh, ns_mesh, uv_mesh, primitive_index_mesh,
                scene.acceleration_telemetry, ignore_lights,
                scene.acceleration_collect_stats != 0);
            hit_mesh = traversal == BvhTraversalResult::Hit;
        }

        if (hit_mesh) {
            hit_anything = true;
            t_closest = t_mesh;
            t_out = t_mesh;

            p_out = r.at(t_mesh);

            float nx = ns_mesh.x; float ny = ns_mesh.y; float nz = ns_mesh.z;
            ns_mesh.x = xform.inverse_transform.m[0][0] * nx + xform.inverse_transform.m[1][0] * ny + xform.inverse_transform.m[2][0] * nz;
            ns_mesh.y = xform.inverse_transform.m[0][1] * nx + xform.inverse_transform.m[1][1] * ny + xform.inverse_transform.m[2][1] * nz;
            ns_mesh.z = xform.inverse_transform.m[0][2] * nx + xform.inverse_transform.m[1][2] * ny + xform.inverse_transform.m[2][2] * nz;
            ns_mesh = ns_mesh.normalize();

            nx = ng_mesh.x; ny = ng_mesh.y; nz = ng_mesh.z;
            ng_mesh.x = xform.inverse_transform.m[0][0] * nx + xform.inverse_transform.m[1][0] * ny + xform.inverse_transform.m[2][0] * nz;
            ng_mesh.y = xform.inverse_transform.m[0][1] * nx + xform.inverse_transform.m[1][1] * ny + xform.inverse_transform.m[2][1] * nz;
            ng_mesh.z = xform.inverse_transform.m[0][2] * nx + xform.inverse_transform.m[1][2] * ny + xform.inverse_transform.m[2][2] * nz;
            ng_mesh = ng_mesh.normalize();

            n_out = ns_mesh;
            ng_out = ng_mesh;

            mat_idx_out = (desc.material_index >= 0) ? desc.material_index : mesh.material_index;
            uv_out = uv_mesh;
            type_out = 2;
            index_out = i;
            primitive_index_out = primitive_index_mesh;
        }
    }

    DEVICE_LOG(4, scene.mesh_count, (unsigned long long)scene.meshes, 0, 0);
    for (int i = 0; i < scene.mesh_count; ++i) {
        GpuMesh& mesh = scene.meshes[i];

        if (mesh.material_index < 0) continue;

        if (!hit_aabb(r, mesh.min_pt, mesh.max_pt, t_min, t_closest)) {
            continue;
        }

        if (mesh.triangle_count > 0) {
            float t_mesh;
            GpuVec3 ng_mesh, ns_mesh;
            GpuVec2 uv_mesh;
            int primitive_index_mesh = -1;
            const auto traversal = hit_bvh(
                mesh, r, t_min, t_closest, t_mesh,
                ng_mesh, ns_mesh, uv_mesh, primitive_index_mesh,
                scene.acceleration_telemetry, ignore_lights,
                scene.acceleration_collect_stats != 0);
            if (traversal == BvhTraversalResult::Hit) {
                hit_anything = true;
                t_closest = t_mesh;
                t_out = t_mesh;
                p_out = r.at(t_mesh);
                n_out = ns_mesh;
                ng_out = ng_mesh;
                mat_idx_out = mesh.material_index;
                uv_out = uv_mesh;
                type_out = 1;
                index_out = i;
                primitive_index_out = primitive_index_mesh;
            }
        }
    }

    return hit_anything;
}
