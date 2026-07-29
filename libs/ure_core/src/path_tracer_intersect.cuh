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

static __device__ bool hit_mesh_primitive(
    const GpuMesh& mesh,
    int primitive_index,
    const GpuRay& ray,
    float t_min,
    float& closest,
    float& t_out,
    GpuVec3& geometric_normal_out,
    GpuVec3& shading_normal_out,
    GpuVec2& uv_out,
    int& primitive_index_out) {
    if (primitive_index < 0 ||
        primitive_index >= mesh.triangle_count) {
        return false;
    }
    const int i0 = mesh.indices[primitive_index * 3 + 0];
    const int i1 = mesh.indices[primitive_index * 3 + 1];
    const int i2 = mesh.indices[primitive_index * 3 + 2];
    const GpuVec3 v0 = mesh.vertices[i0];
    const GpuVec3 v1 = mesh.vertices[i1];
    const GpuVec3 v2 = mesh.vertices[i2];
    const GpuVec3* n0 = mesh.normals ? &mesh.normals[i0] : nullptr;
    const GpuVec3* n1 = mesh.normals ? &mesh.normals[i1] : nullptr;
    const GpuVec3* n2 = mesh.normals ? &mesh.normals[i2] : nullptr;
    float triangle_t;
    GpuVec3 geometric_normal;
    GpuVec3 shading_normal;
    float u;
    float v;
    if (!hit_triangle(
            ray, v0, v1, v2, n0, n1, n2,
            t_min, closest, triangle_t,
            geometric_normal, shading_normal, u, v)) {
        return false;
    }
    closest = triangle_t;
    t_out = triangle_t;
    geometric_normal_out = geometric_normal;
    shading_normal_out = shading_normal;
    primitive_index_out = primitive_index;
    if (mesh.uvs) {
        const float w = 1.0f - u - v;
        uv_out =
            mesh.uvs[i0] * w +
            mesh.uvs[i1] * u +
            mesh.uvs[i2] * v;
    } else {
        uv_out = GpuVec2(0.0f, 0.0f);
    }
    return true;
}

template <typename Node>
static __device__ GpuVec3 decode_wide_child_minimum(
    const Node& node,
    const GpuVec3& scale,
    int child) {
    GpuVec3 result;
    for (int axis = 0; axis < 3; ++axis) {
        const float minimum =
            axis == 0 ? node.min_pt.x :
            axis == 1 ? node.min_pt.y : node.min_pt.z;
        const float axis_scale =
            axis == 0 ? scale.x :
            axis == 1 ? scale.y : scale.z;
        const float value =
            fmaf(
                axis_scale,
                static_cast<float>(
                    node.child_bounds[child][axis]),
                minimum);
        const float outward =
            nextafterf(value, -FLT_MAX);
        if (axis == 0) result.x = outward;
        else if (axis == 1) result.y = outward;
        else result.z = outward;
    }
    return result;
}

template <typename Node>
static __device__ GpuVec3 decode_wide_child_maximum(
    const Node& node,
    const GpuVec3& scale,
    int child) {
    GpuVec3 result;
    for (int axis = 0; axis < 3; ++axis) {
        const float minimum =
            axis == 0 ? node.min_pt.x :
            axis == 1 ? node.min_pt.y : node.min_pt.z;
        const float axis_scale =
            axis == 0 ? scale.x :
            axis == 1 ? scale.y : scale.z;
        const float value =
            fmaf(
                axis_scale,
                static_cast<float>(
                    node.child_bounds[child][axis + 3]),
                minimum);
        const float outward =
            nextafterf(value, FLT_MAX);
        if (axis == 0) result.x = outward;
        else if (axis == 1) result.y = outward;
        else result.z = outward;
    }
    return result;
}

template <typename Node, int MaximumChildren>
static __device__ BvhTraversalResult hit_wide_bvh_nodes(
    const GpuMesh& mesh,
    const Node* nodes,
    int node_count,
    const GpuRay& ray,
    float t_min,
    float t_max,
    float& t_out,
    GpuVec3& geometric_normal_out,
    GpuVec3& shading_normal_out,
    GpuVec2& uv_out,
    int& primitive_index_out,
    GpuAccelerationTelemetry* telemetry,
    bool shadow,
    bool collect_stats) {
    if (!nodes || node_count <= 0 ||
        !mesh.primitive_references ||
        mesh.primitive_reference_count <= 0) {
        if (telemetry) {
            record_acceleration_count(
                &telemetry->invalid_acceleration_count);
        }
        return BvhTraversalResult::InvalidAcceleration;
    }
    int stack[kWideBvhTraversalStackCapacity];
    int stack_pointer = 0;
    stack[stack_pointer++] = 0;
    bool hit_anything = false;
    float closest = t_max;
    while (stack_pointer > 0) {
        const int node_index = stack[--stack_pointer];
        if (node_index < 0 ||
            node_index >= node_count) {
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
        const Node& node = nodes[node_index];
        if (node.child_count <= 0 ||
            node.child_count > MaximumChildren ||
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
        int internal_children[MaximumChildren];
        int internal_count = 0;
        const GpuVec3 quantization_scale =
            (node.max_pt - node.min_pt) *
            (1.0f / 255.0f);
        for (int child = 0;
             child < node.child_count;
             ++child) {
            const GpuVec3 child_minimum =
                decode_wide_child_minimum(
                    node, quantization_scale, child);
            const GpuVec3 child_maximum =
                decode_wide_child_maximum(
                    node, quantization_scale, child);
            if (!hit_aabb(
                    ray, child_minimum, child_maximum,
                    t_min, closest)) {
                continue;
            }
            const int child_index = node.child_indices[child];
            const int count = static_cast<int>(
                node.child_primitive_counts[child]);
            if (count > 0) {
                if (count <= 0 || child_index < 0 ||
                    child_index >
                        mesh.primitive_reference_count ||
                    count >
                        mesh.primitive_reference_count -
                            child_index) {
                    if (telemetry) {
                        record_acceleration_count(
                            &telemetry
                                ->invalid_acceleration_count);
                    }
                    return BvhTraversalResult::InvalidAcceleration;
                }
                for (int offset = 0; offset < count; ++offset) {
                    if (collect_stats && telemetry) {
                        record_acceleration_count(
                            shadow
                                ? &telemetry
                                       ->shadow_triangle_tests
                                : &telemetry
                                       ->closest_triangle_tests);
                    }
                    const int primitive_index =
                        mesh.primitive_references[
                            child_index + offset];
                    if (primitive_index < 0 ||
                        primitive_index >= mesh.triangle_count) {
                        if (telemetry) {
                            record_acceleration_count(
                                &telemetry
                                    ->invalid_acceleration_count);
                        }
                        return BvhTraversalResult::
                            InvalidAcceleration;
                    }
                    if (hit_mesh_primitive(
                            mesh, primitive_index, ray, t_min,
                            closest, t_out,
                            geometric_normal_out,
                            shading_normal_out, uv_out,
                            primitive_index_out)) {
                        hit_anything = true;
                    }
                }
            } else {
                if (child_index < 0 ||
                    child_index >= node_count) {
                    if (telemetry) {
                        record_acceleration_count(
                            &telemetry
                                ->invalid_acceleration_count);
                    }
                    return BvhTraversalResult::InvalidAcceleration;
                }
                internal_children[internal_count++] = child_index;
            }
        }
        if (stack_pointer >
            kWideBvhTraversalStackCapacity - internal_count) {
            if (telemetry) {
                record_acceleration_count(
                    &telemetry->stack_overflow_count);
            }
            return BvhTraversalResult::StackOverflow;
        }
        for (int index = internal_count - 1;
             index >= 0;
             --index) {
            stack[stack_pointer++] = internal_children[index];
        }
    }
    return hit_anything
        ? BvhTraversalResult::Hit
        : BvhTraversalResult::Miss;
}

static __device__ BvhTraversalResult hit_wide_bvh(
    const GpuMesh& mesh,
    const GpuRay& ray,
    float t_min,
    float t_max,
    float& t_out,
    GpuVec3& geometric_normal_out,
    GpuVec3& shading_normal_out,
    GpuVec2& uv_out,
    int& primitive_index_out,
    GpuAccelerationTelemetry* telemetry,
    bool shadow,
    bool collect_stats) {
    if (mesh.bvh_layout == GpuBvhLayout::Wide4) {
        return hit_wide_bvh_nodes<GpuBvh4Node, 4>(
            mesh, mesh.bvh4_nodes, mesh.bvh4_node_count,
            ray, t_min, t_max, t_out,
            geometric_normal_out, shading_normal_out,
            uv_out, primitive_index_out, telemetry,
            shadow, collect_stats);
    }
    return hit_wide_bvh_nodes<GpuWideBvhNode, 8>(
        mesh, mesh.wide_bvh_nodes,
        mesh.wide_bvh_node_count,
        ray, t_min, t_max, t_out,
        geometric_normal_out, shading_normal_out,
        uv_out, primitive_index_out, telemetry,
        shadow, collect_stats);
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
    if (!mesh.vertices || !mesh.indices ||
        mesh.triangle_count <= 0) {
        if (telemetry) {
            record_acceleration_count(
                &telemetry->invalid_acceleration_count);
        }
        return BvhTraversalResult::InvalidAcceleration;
    }
    if (mesh.bvh_layout == GpuBvhLayout::Wide4 ||
        mesh.bvh_layout == GpuBvhLayout::Wide8) {
        return hit_wide_bvh(
            mesh, r, t_min, t_max, t_out,
            ng_out, ns_out, uv_out,
            primitive_index_out, telemetry,
            shadow, collect_stats);
    }
    if (mesh.bvh_layout != GpuBvhLayout::Binary ||
        !mesh.bvh_nodes || mesh.bvh_node_count <= 0) {
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
                if (hit_mesh_primitive(
                        mesh, i, r, t_min, t_closest,
                        t_out, ng_out, ns_out, uv_out,
                        primitive_index_out)) {
                    hit_anything = true;
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

static __device__ BvhTraversalResult hit_instance_tlas(
    const GpuScene& scene,
    const GpuRay& ray,
    float t_min,
    float t_max,
    float& t_out,
    GpuVec3& geometric_normal_out,
    GpuVec3& shading_normal_out,
    GpuVec2& uv_out,
    int& material_index_out,
    int& instance_index_out,
    int& primitive_index_out,
    bool shadow) {
    if (scene.instance_count <= 0) {
        return BvhTraversalResult::Miss;
    }
    if (!scene.tlas_nodes || scene.tlas_node_count <= 0 ||
        !scene.tlas_instance_indices ||
        scene.tlas_instance_index_count != scene.instance_count ||
        !scene.instance_descs || !scene.instance_transforms ||
        !scene.meshes || scene.mesh_count <= 0) {
        if (scene.acceleration_telemetry) {
            record_acceleration_count(
                &scene.acceleration_telemetry
                    ->invalid_acceleration_count);
        }
        return BvhTraversalResult::InvalidAcceleration;
    }
    int stack[kBvhTraversalStackCapacity];
    int stack_pointer = 0;
    stack[stack_pointer++] = 0;
    bool hit_anything = false;
    float closest = t_max;
    while (stack_pointer > 0) {
        const int node_index = stack[--stack_pointer];
        if (node_index < 0 ||
            node_index >= scene.tlas_node_count) {
            if (scene.acceleration_telemetry) {
                record_acceleration_count(
                    &scene.acceleration_telemetry
                        ->invalid_acceleration_count);
            }
            return BvhTraversalResult::InvalidAcceleration;
        }
        if (scene.acceleration_collect_stats != 0 &&
            scene.acceleration_telemetry) {
            record_acceleration_count(
                shadow
                    ? &scene.acceleration_telemetry
                           ->shadow_tlas_node_visits
                    : &scene.acceleration_telemetry
                           ->closest_tlas_node_visits);
        }
        const GpuBvhNode& node = scene.tlas_nodes[node_index];
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
            if (scene.acceleration_telemetry) {
                record_acceleration_count(
                    &scene.acceleration_telemetry
                        ->invalid_acceleration_count);
            }
            return BvhTraversalResult::InvalidAcceleration;
        }
        if (!hit_aabb(
                ray, node.min_pt, node.max_pt,
                t_min, closest)) {
            continue;
        }
        if (node.primitive_count > 0) {
            const int first = node.child_or_primitive_index;
            if (first < 0 ||
                first > scene.tlas_instance_index_count ||
                node.primitive_count >
                    scene.tlas_instance_index_count - first) {
                if (scene.acceleration_telemetry) {
                    record_acceleration_count(
                        &scene.acceleration_telemetry
                            ->invalid_acceleration_count);
                }
                return BvhTraversalResult::InvalidAcceleration;
            }
            for (int offset = 0;
                 offset < node.primitive_count;
                 ++offset) {
                const int instance_index =
                    scene.tlas_instance_indices[first + offset];
                if (instance_index < 0 ||
                    instance_index >= scene.instance_count) {
                    if (scene.acceleration_telemetry) {
                        record_acceleration_count(
                            &scene.acceleration_telemetry
                                ->invalid_acceleration_count);
                    }
                    return BvhTraversalResult::InvalidAcceleration;
                }
                const GpuInstanceDesc& descriptor =
                    scene.instance_descs[instance_index];
                if (descriptor.mesh_index < 0 ||
                    descriptor.mesh_index >= scene.mesh_count) {
                    if (scene.acceleration_telemetry) {
                        record_acceleration_count(
                            &scene.acceleration_telemetry
                                ->invalid_acceleration_count);
                    }
                    return BvhTraversalResult::InvalidAcceleration;
                }
                const GpuInstanceTransform& transform =
                    scene.instance_transforms[instance_index];
                if (!hit_aabb(
                        ray, transform.min_pt, transform.max_pt,
                        t_min, closest)) {
                    continue;
                }
                GpuRay object_ray = ray;
                object_ray.origin =
                    transform.inverse_transform.transform_point(
                        ray.origin);
                object_ray.direction =
                    transform.inverse_transform.transform_vector(
                        ray.direction);
                const GpuMesh& mesh =
                    scene.meshes[descriptor.mesh_index];
                if (mesh.triangle_count <= 0) continue;
                float mesh_t;
                GpuVec3 mesh_geometric_normal;
                GpuVec3 mesh_shading_normal;
                GpuVec2 mesh_uv;
                int mesh_primitive_index = -1;
                const BvhTraversalResult traversal = hit_bvh(
                    mesh, object_ray, t_min, closest, mesh_t,
                    mesh_geometric_normal, mesh_shading_normal,
                    mesh_uv, mesh_primitive_index,
                    scene.acceleration_telemetry, shadow,
                    scene.acceleration_collect_stats != 0);
                if (traversal ==
                        BvhTraversalResult::StackOverflow ||
                    traversal ==
                        BvhTraversalResult::InvalidAcceleration) {
                    return traversal;
                }
                if (traversal != BvhTraversalResult::Hit) continue;
                hit_anything = true;
                closest = mesh_t;
                t_out = mesh_t;
                float x = mesh_shading_normal.x;
                float y = mesh_shading_normal.y;
                float z = mesh_shading_normal.z;
                mesh_shading_normal.x =
                    transform.inverse_transform.m[0][0] * x +
                    transform.inverse_transform.m[1][0] * y +
                    transform.inverse_transform.m[2][0] * z;
                mesh_shading_normal.y =
                    transform.inverse_transform.m[0][1] * x +
                    transform.inverse_transform.m[1][1] * y +
                    transform.inverse_transform.m[2][1] * z;
                mesh_shading_normal.z =
                    transform.inverse_transform.m[0][2] * x +
                    transform.inverse_transform.m[1][2] * y +
                    transform.inverse_transform.m[2][2] * z;
                mesh_shading_normal =
                    mesh_shading_normal.normalize();
                x = mesh_geometric_normal.x;
                y = mesh_geometric_normal.y;
                z = mesh_geometric_normal.z;
                mesh_geometric_normal.x =
                    transform.inverse_transform.m[0][0] * x +
                    transform.inverse_transform.m[1][0] * y +
                    transform.inverse_transform.m[2][0] * z;
                mesh_geometric_normal.y =
                    transform.inverse_transform.m[0][1] * x +
                    transform.inverse_transform.m[1][1] * y +
                    transform.inverse_transform.m[2][1] * z;
                mesh_geometric_normal.z =
                    transform.inverse_transform.m[0][2] * x +
                    transform.inverse_transform.m[1][2] * y +
                    transform.inverse_transform.m[2][2] * z;
                geometric_normal_out =
                    mesh_geometric_normal.normalize();
                shading_normal_out = mesh_shading_normal;
                uv_out = mesh_uv;
                material_index_out =
                    descriptor.material_index >= 0
                    ? descriptor.material_index
                    : mesh.material_index;
                instance_index_out = instance_index;
                primitive_index_out = mesh_primitive_index;
            }
        } else {
            const int left_child = node_index + 1;
            const int right_child =
                node.child_or_primitive_index;
            if (left_child >= scene.tlas_node_count ||
                right_child <= node_index ||
                right_child >= scene.tlas_node_count) {
                if (scene.acceleration_telemetry) {
                    record_acceleration_count(
                        &scene.acceleration_telemetry
                            ->invalid_acceleration_count);
                }
                return BvhTraversalResult::InvalidAcceleration;
            }
            if (stack_pointer >
                kBvhTraversalStackCapacity - 2) {
                if (scene.acceleration_telemetry) {
                    record_acceleration_count(
                        &scene.acceleration_telemetry
                            ->stack_overflow_count);
                }
                return BvhTraversalResult::StackOverflow;
            }
            stack[stack_pointer++] = right_child;
            stack[stack_pointer++] = left_child;
        }
    }
    return hit_anything
        ? BvhTraversalResult::Hit
        : BvhTraversalResult::Miss;
}

static __device__ GpuVec3 fallback_surface_tangent(
    const GpuVec3& normal) {
    const GpuVec3 axis =
        fabsf(normal.x) > 0.9f
        ? GpuVec3(0.0f, 1.0f, 0.0f)
        : GpuVec3(1.0f, 0.0f, 0.0f);
    return normal.cross(axis).normalize();
}

static __device__ GpuVec3 orthogonal_surface_tangent(
    GpuVec3 tangent,
    const GpuVec3& normal) {
    tangent =
        tangent - normal * tangent.dot(normal);
    if (tangent.length_sq() <= 1.0e-12f) {
        return fallback_surface_tangent(normal);
    }
    return tangent.normalize();
}

static __device__ GpuVec3 mesh_surface_tangent(
    const GpuMesh& mesh,
    int primitive_index,
    const GpuVec3& normal,
    const GpuMat4* transform) {
    if (primitive_index < 0 ||
        primitive_index >= mesh.triangle_count) {
        return fallback_surface_tangent(normal);
    }
    const int i0 =
        mesh.indices[primitive_index * 3 + 0];
    const int i1 =
        mesh.indices[primitive_index * 3 + 1];
    const int i2 =
        mesh.indices[primitive_index * 3 + 2];
    GpuVec3 tangent;
    if (mesh.uvs) {
        const GpuVec3 edge1 =
            mesh.vertices[i1] - mesh.vertices[i0];
        const GpuVec3 edge2 =
            mesh.vertices[i2] - mesh.vertices[i0];
        const float du1 =
            mesh.uvs[i1].u - mesh.uvs[i0].u;
        const float dv1 =
            mesh.uvs[i1].v - mesh.uvs[i0].v;
        const float du2 =
            mesh.uvs[i2].u - mesh.uvs[i0].u;
        const float dv2 =
            mesh.uvs[i2].v - mesh.uvs[i0].v;
        const float determinant =
            du1 * dv2 - dv1 * du2;
        if (fabsf(determinant) > 1.0e-12f) {
            tangent =
                (edge1 * dv2 - edge2 * dv1) *
                (1.0f / determinant);
        }
    }
    if (tangent.length_sq() <= 1.0e-12f &&
        mesh.tangents) {
        tangent =
            mesh.tangents[i0] +
            mesh.tangents[i1] +
            mesh.tangents[i2];
    }
    if (transform) {
        tangent =
            transform->transform_vector(tangent);
    }
    return orthogonal_surface_tangent(
        tangent,
        normal);
}

static __device__ GpuVec3 surface_tangent(
    const GpuScene& scene,
    int hit_type,
    int hit_index,
    int primitive_index,
    const GpuVec3& position,
    const GpuVec3& normal) {
    if (hit_type == 0 &&
        hit_index >= 0 &&
        hit_index < scene.sphere_count) {
        const GpuVec3 local =
            (position -
             scene.spheres[hit_index].center).normalize();
        return orthogonal_surface_tangent(
            GpuVec3(local.z, 0.0f, -local.x),
            normal);
    }
    if (hit_type == 1 &&
        hit_index >= 0 &&
        hit_index < scene.mesh_count) {
        return mesh_surface_tangent(
            scene.meshes[hit_index],
            primitive_index,
            normal,
            nullptr);
    }
    if (hit_type == 2 &&
        hit_index >= 0 &&
        hit_index < scene.instance_count) {
        const GpuInstance& instance =
            scene.instances[hit_index];
        if (instance.mesh_index >= 0 &&
            instance.mesh_index < scene.mesh_count) {
            return mesh_surface_tangent(
                scene.meshes[instance.mesh_index],
                primitive_index,
                normal,
                &instance.transform);
        }
    }
    return fallback_surface_tangent(normal);
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

    float instance_t;
    GpuVec3 instance_geometric_normal;
    GpuVec3 instance_shading_normal;
    GpuVec2 instance_uv;
    int instance_material_index = -1;
    int instance_index = -1;
    int instance_primitive_index = -1;
    const BvhTraversalResult instance_traversal =
        hit_instance_tlas(
            scene, r, t_min, t_closest, instance_t,
            instance_geometric_normal,
            instance_shading_normal, instance_uv,
            instance_material_index, instance_index,
            instance_primitive_index, ignore_lights);
    if (instance_traversal == BvhTraversalResult::Hit) {
        hit_anything = true;
        t_closest = instance_t;
        t_out = instance_t;
        p_out = r.at(instance_t);
        n_out = instance_shading_normal;
        ng_out = instance_geometric_normal;
        mat_idx_out = instance_material_index;
        uv_out = instance_uv;
        type_out = 2;
        index_out = instance_index;
        primitive_index_out = instance_primitive_index;
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
