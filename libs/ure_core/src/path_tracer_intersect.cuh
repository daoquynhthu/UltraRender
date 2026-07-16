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

    if (fabsf(det) < 1e-8f) return false;

    float invDet = 1.0f / det;
    GpuVec3 tvec = r.origin - v0;
    float u = tvec.dot(pvec) * invDet;

    if (u < 0.0f || u > 1.0f) return false;

    GpuVec3 qvec = tvec.cross(v0v1);
    float v = r.direction.dot(qvec) * invDet;

    if (v < 0.0f || u + v > 1.0f) return false;

    float temp_t = v0v2.dot(qvec) * invDet;

    if (temp_t < t_max && temp_t > t_min) {
        t = temp_t;
        ng = v0v1.cross(v0v2).normalize();

        if (n0 && n1 && n2) {
             float w = 1.0f - u - v;
             ns = (*n0 * w + *n1 * u + *n2 * v).normalize();
        } else {
             ns = ng;
        }

        u_out = u;
        v_out = v;
        return true;
    }
    return false;
}

static __device__ bool hit_aabb(const GpuRay& r, const GpuVec3& min_pt, const GpuVec3& max_pt, float t_min, float t_max) {
    float3 invD = make_float3(1.0f / r.direction.x, 1.0f / r.direction.y, 1.0f / r.direction.z);
    float3 t0 = make_float3((min_pt.x - r.origin.x) * invD.x, (min_pt.y - r.origin.y) * invD.y, (min_pt.z - r.origin.z) * invD.z);
    float3 t1 = make_float3((max_pt.x - r.origin.x) * invD.x, (max_pt.y - r.origin.y) * invD.y, (max_pt.z - r.origin.z) * invD.z);
    float3 tsmall = make_float3(fminf(t0.x, t1.x), fminf(t0.y, t1.y), fminf(t0.z, t1.z));
    float3 tbig = make_float3(fmaxf(t0.x, t1.x), fmaxf(t0.y, t1.y), fmaxf(t0.z, t1.z));
    float tmin = fmaxf(t_min, fmaxf(tsmall.x, fmaxf(tsmall.y, tsmall.z)));
    float tmax = fminf(t_max, fminf(tbig.x, fminf(tbig.y, tbig.z)));
    return tmin <= tmax;
}

static __device__ bool hit_bvh(const GpuMesh& mesh, const GpuRay& r, float t_min, float t_max, float& t_out, GpuVec3& ng_out, GpuVec3& ns_out, GpuVec2& uv_out, int& primitive_index_out) {
    bool hit_anything = false;
    float t_closest = t_max;

    int stack[64];
    int stack_ptr = 0;
    stack[stack_ptr++] = 0;

    while (stack_ptr > 0) {
        int node_idx = stack[--stack_ptr];
        const GpuBvhNode& node = mesh.bvh_nodes[node_idx];

        if (!hit_aabb(r, node.min_pt, node.max_pt, t_min, t_closest)) {
            continue;
        }

        if (node.primitive_count > 0) {
            int start_idx = node.child_or_primitive_index;
            int end_idx = start_idx + node.primitive_count;

            for (int i = start_idx; i < end_idx; ++i) {
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

            if (stack_ptr < 64) {
                stack[stack_ptr++] = right_child;
                stack[stack_ptr++] = left_child;
            }
        }
    }

    return hit_anything;
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

        if (mesh.bvh_node_count > 0) {
             hit_mesh = hit_bvh(mesh, r_obj, t_min, t_closest, t_mesh, ng_mesh, ns_mesh, uv_mesh, primitive_index_mesh);
        } else {
             for (int j = 0; j < mesh.triangle_count; ++j) {
                int i0 = mesh.indices[j * 3 + 0];
                int i1 = mesh.indices[j * 3 + 1];
                int i2 = mesh.indices[j * 3 + 2];
                GpuVec3 v0 = mesh.vertices[i0];
                GpuVec3 v1 = mesh.vertices[i1];
                GpuVec3 v2 = mesh.vertices[i2];

                const GpuVec3* n0_ptr = mesh.normals ? &mesh.normals[i0] : nullptr;
                const GpuVec3* n1_ptr = mesh.normals ? &mesh.normals[i1] : nullptr;
                const GpuVec3* n2_ptr = mesh.normals ? &mesh.normals[i2] : nullptr;

                float t_tri, u_tri, v_tri;
                GpuVec3 ng_tri, ns_tri;
                if (hit_triangle(r_obj, v0, v1, v2, n0_ptr, n1_ptr, n2_ptr, t_min, t_closest, t_tri, ng_tri, ns_tri, u_tri, v_tri)) {
                    hit_mesh = true;
                    t_closest = t_tri;
                    t_mesh = t_tri;
                    ng_mesh = ng_tri;
                    ns_mesh = ns_tri;
                    primitive_index_mesh = j;

                    if (mesh.uvs) {
                        GpuVec2 uv0 = mesh.uvs[i0];
                        GpuVec2 uv1 = mesh.uvs[i1];
                        GpuVec2 uv2 = mesh.uvs[i2];
                        float w_tri = 1.0f - u_tri - v_tri;
                        uv_mesh = uv0 * w_tri + uv1 * u_tri + uv2 * v_tri;
                    } else {
                        uv_mesh = GpuVec2(0.0f, 0.0f);
                    }
                }
             }
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

        if (mesh.bvh_node_count > 0) {
            float t_mesh;
            GpuVec3 ng_mesh, ns_mesh;
            GpuVec2 uv_mesh;
            int primitive_index_mesh = -1;
            if (hit_bvh(mesh, r, t_min, t_closest, t_mesh, ng_mesh, ns_mesh, uv_mesh, primitive_index_mesh)) {
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
        } else {
            for (int j = 0; j < mesh.triangle_count; ++j) {
                int i0 = mesh.indices[j * 3 + 0];
                int i1 = mesh.indices[j * 3 + 1];
                int i2 = mesh.indices[j * 3 + 2];

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

                GpuVec3 ng_tri, ns_tri;
                float t_tri;
                float u_b, v_b;

                if (hit_triangle(r, v0, v1, v2, n0_ptr, n1_ptr, n2_ptr, t_min, t_closest, t_tri, ng_tri, ns_tri, u_b, v_b)) {
                    hit_anything = true;
                    t_closest = t_tri;
                    t_out = t_tri;
                    p_out = r.at(t_tri);
                    n_out = ns_tri;
                    ng_out = ng_tri;
                    mat_idx_out = mesh.material_index;
                    primitive_index_out = j;

                    if (mesh.uvs) {
                        GpuVec2 uv0 = mesh.uvs[i0];
                        GpuVec2 uv1 = mesh.uvs[i1];
                        GpuVec2 uv2 = mesh.uvs[i2];
                        float w_b = 1.0f - u_b - v_b;
                        uv_out = uv0 * w_b + uv1 * u_b + uv2 * v_b;
                    } else {
                        uv_out = GpuVec2(0.0f, 0.0f);
                    }
                }
            }
        }
    }

    return hit_anything;
}
