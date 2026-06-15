#pragma once

#include "path_tracer_decl.cuh"
#include "path_tracer_intersect.cuh"
#include "path_tracer_polarization.cuh"
#include "path_tracer_bsdf.cuh"
#include "path_tracer_volume.cuh"
#include "ure/gpu_material_helpers.cuh"

__global__ __launch_bounds__(256) void extend_kernel(
    RayQueue ray_queue,
    HitQueue hit_queue,
    GpuScene scene
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int ray_count_local = *ray_queue.count;
    if (idx >= ray_count_local) return;

    GpuRay r;
    r.origin = ray_queue.origins[idx];
    r.direction = ray_queue.directions[idx];

    float t_min = 1e-4f;

    float t;
    GpuVec3 p, n, ng;
    GpuVec2 uv;
    int mat_idx;
    int hit_type;
    int hit_index;

    if (world_hit(scene, r, t_min, FLT_MAX, t, p, n, ng, uv, mat_idx, hit_type, hit_index)) {
        hit_queue.t[idx] = t;
        hit_queue.p[idx] = p;
        hit_queue.n[idx] = n;
        hit_queue.ng[idx] = ng;
        hit_queue.uv[idx] = uv;
        hit_queue.mat_ids[idx] = mat_idx;
        hit_queue.hit_types[idx] = hit_type;
        hit_queue.hit_indices[idx] = hit_index;
    } else {
        hit_queue.mat_ids[idx] = -1;
    }
}

__device__ inline GpuVec2 project_camera_screen(const GpuCamera& camera, const GpuVec3& p) {
    GpuVec3 h = camera.horizontal;
    GpuVec3 v = camera.vertical;
    GpuVec3 plane_origin = camera.lower_left_corner;
    GpuVec3 plane_normal = h.cross(v);
    GpuVec3 ray = p - camera.origin;
    float denom = ray.dot(plane_normal);
    float numer = (plane_origin - camera.origin).dot(plane_normal);
    if (fabsf(denom) < 1e-8f) {
        return GpuVec2(0.0f, 0.0f);
    }
    GpuVec3 q = camera.origin + ray * (numer / denom);
    GpuVec3 rel = q - plane_origin;
    float h_len_sq = fmaxf(1e-8f, h.dot(h));
    float v_len_sq = fmaxf(1e-8f, v.dot(v));
    return GpuVec2(rel.dot(h) / h_len_sq, rel.dot(v) / v_len_sq);
}

__device__ float sample_spectral_texture_resource(const GpuTexture& tex, int texel_index, float lambda) {
    if (tex.spectral_kind != SpectralTextureResourceKind::SourceSampleGrid ||
        !tex.spectral_source_values ||
        tex.spectral_sample_count <= 0) {
        return 0.0f;
    }

    if (tex.spectral_sample_count == 1 || tex.spectral_lambda_max <= tex.spectral_lambda_min) {
        return tex.spectral_source_values[static_cast<size_t>(texel_index) * static_cast<size_t>(tex.spectral_sample_count)];
    }

    const float normalized = fminf(1.0f, fmaxf(0.0f,
        (lambda - tex.spectral_lambda_min) / (tex.spectral_lambda_max - tex.spectral_lambda_min)));
    const float sample_pos = normalized * float(tex.spectral_sample_count - 1);
    const int s0 = min(static_cast<int>(floorf(sample_pos)), tex.spectral_sample_count - 1);
    const int s1 = min(s0 + 1, tex.spectral_sample_count - 1);
    const float ds = sample_pos - float(s0);
    const size_t base = static_cast<size_t>(texel_index) * static_cast<size_t>(tex.spectral_sample_count);
    const float v0 = tex.spectral_source_values[base + static_cast<size_t>(s0)];
    const float v1 = tex.spectral_source_values[base + static_cast<size_t>(s1)];
    return v0 * (1.0f - ds) + v1 * ds;
}

__device__ SpectralPacket sample_texture(const GpuScene& scene, int tex_idx, float u, float v, const float* wavelengths, int num_spec) {
    if (tex_idx < 0 || tex_idx >= scene.texture_count) return rgb_to_spectrum(GpuVec3(1,0,1), wavelengths, num_spec);

    GpuTexture tex = scene.textures[tex_idx];

    if (tex.texObj) {
        float4 val = tex2D<float4>(tex.texObj, u, v);
        return rgb_to_spectrum(GpuVec3(val.x, val.y, val.z), wavelengths, num_spec);
    }

    if (tex.spectral_kind != SpectralTextureResourceKind::SourceSampleGrid || !tex.spectral_source_values) {
        return rgb_to_spectrum(GpuVec3(0,0,0), wavelengths, num_spec);
    }

    u = u - floorf(u);
    v = v - floorf(v);

    float x = u * (tex.width - 1);
    float y = v * (tex.height - 1);

    int x0 = (int)x;
    int y0 = (int)y;
    int x1 = min(x0 + 1, tex.width - 1);
    int y1 = min(y0 + 1, tex.height - 1);

    float dx = x - x0;
    float dy = y - y0;

    const int idx00 = y0 * tex.width + x0;
    const int idx10 = y0 * tex.width + x1;
    const int idx01 = y1 * tex.width + x0;
    const int idx11 = y1 * tex.width + x1;

    SpectralPacket result;
    for (int c = 0; c < num_spec; ++c) {
        const float lambda = wavelengths[c];
        float v00 = sample_spectral_texture_resource(tex, idx00, lambda);
        float v10 = sample_spectral_texture_resource(tex, idx10, lambda);
        float v01 = sample_spectral_texture_resource(tex, idx01, lambda);
        float v11 = sample_spectral_texture_resource(tex, idx11, lambda);
        float v0 = v00 * (1.0f - dx) + v10 * dx;
        float v1 = v01 * (1.0f - dx) + v11 * dx;
        result.values[c] = v0 * (1.0f - dy) + v1 * dy;
        result.wavelengths[c] = lambda;
    }
    return result;
}

__device__ SpectralPacket eval_material_expression(
    const GpuScene& scene,
    const GpuMaterial& mat,
    int root,
    float u,
    float v,
    const float* wavelengths,
    int num_spec
) {
    SpectralPacket zero(0.0f);
    if (!scene.material_expression_nodes ||
        root < 0 ||
        mat.expression_node_start < 0 ||
        mat.expression_node_count <= 0 ||
        mat.expression_node_count > kMaxMaterialExpressionNodes) {
        return zero;
    }

    const int start = mat.expression_node_start;
    const int count = mat.expression_node_count;
    const int local_root = root - start;
    if (local_root < 0 || local_root >= count || start + count > scene.material_expression_node_count) {
        return zero;
    }

    SpectralPacket values[kMaxMaterialExpressionNodes];
    for (int i = 0; i < count; ++i) {
        const SpectralExpressionNode& node = scene.material_expression_nodes[start + i];
        SpectralPacket result(0.0f);
        switch (node.kind) {
            case SpectralExpressionNodeKind::Resource:
                for (int c = 0; c < num_spec; ++c) {
                    const float lambda = wavelengths[c];
                    result.wavelengths[c] = lambda;
                    result.values[c] = eval_spectral_resource(node.resource, lambda);
                }
                break;
            case SpectralExpressionNodeKind::Texture:
                result = sample_texture(scene, node.texture_index, u, v, wavelengths, num_spec);
                break;
            case SpectralExpressionNodeKind::Add: {
                int a = node.input_a - start;
                int b = node.input_b - start;
                if (a >= 0 && a < i && b >= 0 && b < i) {
                    result = values[a] + values[b];
                }
                break;
            }
            case SpectralExpressionNodeKind::Multiply: {
                int a = node.input_a - start;
                int b = node.input_b - start;
                if (a >= 0 && a < i && b >= 0 && b < i) {
                    result = values[a] * values[b];
                }
                break;
            }
            case SpectralExpressionNodeKind::Mix: {
                int a = node.input_a - start;
                int b = node.input_b - start;
                int f = node.input_factor - start;
                if (a >= 0 && a < i && b >= 0 && b < i && f >= 0 && f < i) {
                    for (int c = 0; c < num_spec; ++c) {
                        float t = fminf(1.0f, fmaxf(0.0f, values[f].values[c]));
                        result.wavelengths[c] = wavelengths[c];
                        result.values[c] = values[a].values[c] * (1.0f - t) + values[b].values[c] * t;
                    }
                }
                break;
            }
            case SpectralExpressionNodeKind::None:
            default:
                break;
        }
        values[i] = result;
    }
    return values[local_root];
}

__device__ bool any_hit_bvh(const GpuMesh& mesh, const GpuRay& r, float t_min, float t_max) {
    int stack[64];
    int stack_ptr = 0;
    stack[stack_ptr++] = 0;

    while (stack_ptr > 0) {
        int node_idx = stack[--stack_ptr];
        const GpuBvhNode& node = mesh.bvh_nodes[node_idx];

        if (!hit_aabb(r, node.min_pt, node.max_pt, t_min, t_max)) {
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

                float t_tri;
                GpuVec3 ng_tri, ns_tri;
                float u_tri, v_tri;

                float local_max = t_max;

                if (hit_triangle(r, v0, v1, v2, n0_ptr, n1_ptr, n2_ptr, t_min, local_max, t_tri, ng_tri, ns_tri, u_tri, v_tri)) {
                    return true;
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
    return false;
}

__device__ bool any_hit(const GpuScene& scene, const GpuRay& r, float t_min, float t_max) {
    for (int i = 0; i < scene.sphere_count; ++i) {
        GpuVec3 oc = r.origin - scene.spheres[i].center;
        float a = r.direction.dot(r.direction);
        float b = oc.dot(r.direction);
        float c = oc.dot(oc) - scene.spheres[i].radius * scene.spheres[i].radius;
        float discriminant = b * b - a * c;

        if (discriminant > 0) {
            float temp = (-b - sqrtf(discriminant)) / a;
            if (temp < t_max && temp > t_min) return true;
            temp = (-b + sqrtf(discriminant)) / a;
            if (temp < t_max && temp > t_min) return true;
        }
    }

    for (int i = 0; i < scene.mesh_count; ++i) {
        GpuMesh& mesh = scene.meshes[i];

        if (!hit_aabb(r, mesh.min_pt, mesh.max_pt, t_min, t_max)) {
            continue;
        }

        if (mesh.bvh_node_count > 0) {
            if (any_hit_bvh(mesh, r, t_min, t_max)) return true;
        } else {
            for (int j = 0; j < mesh.triangle_count; ++j) {
                int i0 = mesh.indices[j * 3 + 0];
                int i1 = mesh.indices[j * 3 + 1];
                int i2 = mesh.indices[j * 3 + 2];

                GpuVec3 v0 = mesh.vertices[i0];
                GpuVec3 v1 = mesh.vertices[i1];
                GpuVec3 v2 = mesh.vertices[i2];

                GpuVec3 v0v1 = v1 - v0;
                GpuVec3 v0v2 = v2 - v0;
                GpuVec3 pvec = r.direction.cross(v0v2);
                float det = v0v1.dot(pvec);

                if (fabsf(det) < 1e-8f) continue;

                float invDet = 1.0f / det;
                GpuVec3 tvec = r.origin - v0;
                float u = tvec.dot(pvec) * invDet;

                if (u < 0.0f || u > 1.0f) continue;

                GpuVec3 qvec = tvec.cross(v0v1);
                float v = r.direction.dot(qvec) * invDet;

                if (v < 0.0f || u + v > 1.0f) continue;

                float t = v0v2.dot(qvec) * invDet;

                if (t < t_max && t > t_min) return true;
            }
        }
    }
    return false;
}

__device__ inline int reserve_ray_slot(RayQueue& q) {
    int out_idx = atomicAdd(q.count, 1);
    if (out_idx >= q.capacity) {
        atomicMin(q.count, q.capacity);
        if (q.overflow_count) {
            atomicAdd(q.overflow_count, 1);
        }
        DEVICE_LOG(5101, q.capacity, (unsigned long long)q.count, 0ULL, 0.0f);
        return -1;
    }
    return out_idx;
}

__device__ inline void store_lane_throughput(RayQueue& q, int idx, const SpectralPacket& source, int channel, float value) {
    SpectralPacket t;
    for (int c = 0; c < q.num_spectral_channels; ++c) {
        t.values[c] = (c == channel) ? value : 0.0f;
        t.wavelengths[c] = source.wavelengths[c];
    }
    store_throughput(q, idx, t);
}

__device__ inline int next_dielectric_medium_index(
    int current_medium_idx,
    int material_idx,
    const GpuVec3& in_direction,
    const GpuVec3& out_direction,
    const GpuVec3& geometric_normal
) {
    if (in_direction.dot(geometric_normal) * out_direction.dot(geometric_normal) <= 0.0f) {
        return current_medium_idx;
    }
    if (current_medium_idx == -1) {
        return material_idx;
    }
    if (current_medium_idx == material_idx) {
        return -1;
    }
    return material_idx;
}

__device__ inline float sphere_light_solid_angle_pdf(
    const GpuSphere& light_sphere,
    const GpuVec3& reference_point,
    int light_count
) {
    if (light_count <= 0) return 0.0f;
    GpuVec3 wc = light_sphere.center - reference_point;
    float dist_sq = wc.length_sq();
    float radius_sq = light_sphere.radius * light_sphere.radius;
    if (dist_sq <= radius_sq) return 0.0f;
    float cos_theta_max = sqrtf(fmaxf(0.0f, 1.0f - radius_sq / dist_sq));
    float solid_angle = 6.2831853f * (1.0f - cos_theta_max);
    if (solid_angle <= 1e-8f) return 0.0f;
    return 1.0f / (solid_angle * float(light_count));
}

__device__ inline bool direct_light_direction_allowed(
    const GpuMaterial& mat,
    const GpuVec3& n,
    const GpuVec3& ng,
    const GpuVec3& wi
) {
    if (is_rough_dielectric_bsdf(mat)) {
        return fabsf(n.dot(wi)) > 1e-6f && fabsf(ng.dot(wi)) > 1e-6f;
    }
    return n.dot(wi) > 0.0f && ng.dot(wi) > 0.0f;
}

__device__ inline float direct_light_cosine_factor(
    const GpuMaterial& mat,
    const GpuVec3& n,
    const GpuVec3& wi
) {
    return is_rough_dielectric_bsdf(mat) ? fabsf(n.dot(wi)) : fmaxf(0.0f, n.dot(wi));
}

__device__ inline GpuVec3 direct_light_offset_normal(const GpuVec3& ng, const GpuVec3& wi) {
    return ng.dot(wi) >= 0.0f ? ng : -ng;
}

__device__ inline StokesVector load_packet_average_stokes(const RayQueue& q, int idx) {
    StokesVector s(0.0f, 0.0f, 0.0f, 0.0f);
    for (int c = 0; c < q.num_spectral_channels; ++c) {
        StokesVector lane = load_stokes(q, idx, c);
        s.I += lane.I;
        s.Q += lane.Q;
        s.U += lane.U;
        s.V += lane.V;
    }
    float inv_n = 1.0f / fmaxf(1.0f, float(q.num_spectral_channels));
    return s * inv_n;
}

__device__ inline void rotate_stokes_into_boundary_frame(StokesVector& s, const GpuVec3& ray_dir, const GpuVec3& boundary_normal) {
    GpuVec3 ref_in = get_reference_frame(ray_dir);
    GpuVec3 raw_s = ray_dir.cross(boundary_normal);
    float raw_len_sq = raw_s.length_sq();
    GpuVec3 s_axis = raw_len_sq < 1e-12f
        ? get_reference_frame(boundary_normal)
        : raw_s * (1.0f / sqrtf(raw_len_sq));
    float cos_phi = ref_in.dot(s_axis);
    float sin_phi = ref_in.cross(s_axis).dot(ray_dir);
    rotate_stokes(s, 2.0f * atan2f(sin_phi, cos_phi));
}

__device__ inline void rotate_stokes_from_boundary_frame(StokesVector& s, const GpuVec3& ray_dir, const GpuVec3& boundary_normal) {
    GpuVec3 ref_out = get_reference_frame(ray_dir);
    GpuVec3 raw_s = ray_dir.cross(boundary_normal);
    float raw_len_sq = raw_s.length_sq();
    GpuVec3 s_axis = raw_len_sq < 1e-12f
        ? get_reference_frame(boundary_normal)
        : raw_s * (1.0f / sqrtf(raw_len_sq));
    float cos_phi = s_axis.dot(ref_out);
    float sin_phi = s_axis.cross(ref_out).dot(ray_dir);
    rotate_stokes(s, 2.0f * atan2f(sin_phi, cos_phi));
}

__device__ inline void store_packet_scattered_stokes(
    const RayQueue& current_queue,
    RayQueue& next_queue,
    int in_idx,
    int out_idx,
    const GpuMaterial& mat,
    const GpuMaterialSoA& mat_soa,
    const GpuRay& r_in,
    const GpuRay& scattered,
    const GpuVec3& n,
    const GpuVec2& uv,
    const SpectralPacket& throughput,
    float ior_outside,
    float dispersion_clamp,
    int sample_index,
    int pixel_index,
    int depth
) {
    if (mat.type == MaterialType::Lambertian || mat.type == MaterialType::Cloth) {
        for (int c = 0; c < current_queue.num_spectral_channels; ++c) {
            StokesVector s = load_stokes(current_queue, in_idx, c);
            s.Q = 0.0f;
            s.U = 0.0f;
            s.V = 0.0f;
            store_stokes(next_queue, out_idx, c, s);
        }
        return;
    }

    if (mat.type == MaterialType::Metal) {
        GpuVec3 V = (-r_in.direction).normalize();
        GpuVec3 L = scattered.direction.normalize();
        GpuVec3 N = n;
        if (V.dot(N) < 0.0f) N = -N;
        GpuVec3 H = (V + L);
        if (H.length_sq() < 1e-12f) {
            for (int c = 0; c < current_queue.num_spectral_channels; ++c) {
                store_stokes(next_queue, out_idx, c, load_stokes(current_queue, in_idx, c));
            }
            return;
        }
        H = H.normalize();
        float cos_theta_h = fmaxf(0.0f, V.dot(H));
        ConductorMaterialSemantics conductor = eval_conductor_material_semantics(
            mat_soa.metal_eta, mat_soa.extinction, current_queue.num_spectral_channels);
        float effective_thickness = mat.thin_film_thickness;
        if (effective_thickness > 0.0f) {
            effective_thickness = effective_thickness * (1.5f - uv.v);
        }

        for (int c = 0; c < current_queue.num_spectral_channels; ++c) {
            StokesVector s = load_stokes(current_queue, in_idx, c);
            rotate_stokes_into_boundary_frame(s, r_in.direction, H);
            if (!conductor.measured_conductor) {
                float eta_equiv = conductor_f0_eta_from_albedo(mat_soa.albedo.values[c]);
                if (effective_thickness > 0.0f) {
                    DielectricSurfaceBoundary surface = eval_dielectric_surface_boundary(
                        throughput.wavelengths[c], effective_thickness, 1.0f, mat.thin_film_ior, eta_equiv, cos_theta_h);
                    apply_mueller_reflection_boundary(s, surface.rs, surface.rp, surface.Rs, surface.Rp);
                } else {
                    float r = (1.0f - eta_equiv) / (1.0f + eta_equiv);
                    apply_mueller_reflection_boundary(s, c_make(r, 0.0f), c_make(-r, 0.0f), r * r, r * r);
                }
            } else {
                float eta_c = conductor_eta_for_channel(conductor, mat_soa.metal_eta, mat.ior, c);
                if (effective_thickness > 0.0f) {
                    ThinFilmBoundary film = eval_thin_film_conductor_boundary(
                        throughput.wavelengths[c], effective_thickness, 1.0f, mat.thin_film_ior, eta_c, mat_soa.extinction.values[c], cos_theta_h);
                    apply_mueller_reflection_boundary(s, film.rs, film.rp, film.Rs, film.Rp);
                } else {
                    ConductorBoundary boundary = eval_conductor_boundary(eta_c, mat_soa.extinction.values[c], cos_theta_h);
                    apply_mueller_reflection_boundary(s, boundary.rs, boundary.rp, boundary.Rs, boundary.Rp);
                }
            }
            rotate_stokes_from_boundary_frame(s, scattered.direction, H);
            store_stokes(next_queue, out_idx, c, s);
        }
        return;
    }

    if (mat.type == MaterialType::Dielectric) {
        int dim_offset = 4 + depth * 6;
        float r_bsdf_1 = sample_dimension(sample_index, pixel_index, dim_offset + 0);
        float r_bsdf_2 = sample_dimension(sample_index, pixel_index, dim_offset + 1);
        GpuVec3 normal = r_in.direction.dot(n) < 0.0f ? n : -n;
        float jitter_scale = mat.roughness * 0.002f;
        if (jitter_scale > 0.0f) {
            normal = (normal + sample_unit_vector_lds(r_bsdf_1, r_bsdf_2) * jitter_scale).normalize();
        }
        GpuVec3 unit_direction = r_in.direction.normalize();
        GpuVec3 out_direction = scattered.direction.normalize();
        float cos_theta_i = fminf((-unit_direction).dot(normal), 1.0f);
        bool front_face = r_in.direction.dot(n) < 0.0f;
        bool is_reflection = unit_direction.dot(normal) * out_direction.dot(normal) < 0.0f;
        float effective_thickness = mat.thin_film_thickness;
        if (effective_thickness > 0.0f) {
            effective_thickness = effective_thickness * (1.5f - uv.v);
        }

        for (int c = 0; c < current_queue.num_spectral_channels; ++c) {
            float material_ior = dispersed_dielectric_ior(mat.ior, mat.dispersion, throughput.wavelengths[c], dispersion_clamp);
            float eta_i = front_face ? ior_outside : material_ior;
            float eta_t = front_face ? material_ior : ior_outside;
            DielectricSurfaceBoundary surface = eval_dielectric_surface_boundary(
                throughput.wavelengths[c], effective_thickness, eta_i, mat.thin_film_ior, eta_t, cos_theta_i);
            StokesVector s = load_stokes(current_queue, in_idx, c);
            rotate_stokes_into_boundary_frame(s, r_in.direction, normal);
            if (is_reflection || surface.tir) {
                apply_mueller_reflection_boundary(s, surface.rs, surface.rp, surface.Rs, surface.Rp);
            } else {
                apply_mueller_transmission_boundary(s, surface.ts, surface.tp, surface.Ts, surface.Tp, surface.eta_jacobian);
                s = s * surface.radiance_scale;
            }
            rotate_stokes_from_boundary_frame(s, scattered.direction, normal);
            store_stokes(next_queue, out_idx, c, s);
        }
        return;
    }

    for (int c = 0; c < current_queue.num_spectral_channels; ++c) {
        store_stokes(next_queue, out_idx, c, load_stokes(current_queue, in_idx, c));
    }
}

__device__ inline bool split_dispersive_dielectric_lanes(
    const RayQueue& current_queue,
    RayQueue& next_queue,
    int idx,
    const GpuMaterial& mat,
    const GpuMaterialSoA& mat_soa,
    const GpuVec3& p,
    const GpuVec3& n,
    const GpuVec3& ng,
    const GpuVec2& uv,
    const SpectralPacket& throughput,
    int current_medium_idx,
    int mat_idx,
    int pixel_index,
    int depth,
    unsigned int seed,
    float dispersion_clamp,
    float ior_outside
) {
    if (mat.type != MaterialType::Dielectric) return false;
    if (is_rough_dielectric_bsdf(mat)) return false;
    if (spectral_mode_is_sampled(current_queue.spectral_modes[idx])) return false;

    float effective_thickness = mat.thin_film_thickness;
    if (effective_thickness > 0.0f) {
        effective_thickness = effective_thickness * (1.5f - uv.v);
    }

    if (mat.dispersion <= 0.0f && effective_thickness <= 0.0f) return false;

    GpuRay r_in;
    r_in.origin = current_queue.origins[idx];
    r_in.direction = current_queue.directions[idx];
    GpuVec3 unit_direction = r_in.direction.normalize();

    bool front_face = r_in.direction.dot(n) < 0.0f;
    GpuVec3 normal = front_face ? n : -n;
    float cos_theta_i = fminf((-unit_direction).dot(normal), 1.0f);

    for (int c = 0; c < current_queue.num_spectral_channels; ++c) {
        float lambda = throughput.wavelengths[c];
        float material_ior = dispersed_dielectric_ior(mat.ior, mat.dispersion, lambda, dispersion_clamp);
        float eta_i = front_face ? ior_outside : material_ior;
        float eta_t = front_face ? material_ior : ior_outside;

        StokesVector lane_stokes = load_stokes(current_queue, idx, c);
        float Is = stokes_s_intensity(lane_stokes);
        float Ip = stokes_p_intensity(lane_stokes);

        DielectricSurfaceBoundary surface = eval_dielectric_surface_boundary(
            lambda, effective_thickness, eta_i, mat.thin_film_ior, eta_t, cos_theta_i);

        float R = fminf(1.0f, fmaxf(0.0f, (surface.Rs * Is + surface.Rp * Ip) / (lane_stokes.I + 1e-6f)));
        float T = surface.tir ? 0.0f : fminf(1.0f, fmaxf(0.0f, (surface.Ts * Is + surface.Tp * Ip) / (lane_stokes.I + 1e-6f)));

        if (R > 1e-6f) {
            int out_idx = reserve_ray_slot(next_queue);
            if (out_idx >= 0) {
                GpuVec3 out_direction = reflect(unit_direction, normal).normalize();
                GpuVec3 offset = (out_direction.dot(normal) > 0.0f) ? normal : -normal;
                next_queue.origins[out_idx] = p + offset * 1e-4f;
                next_queue.directions[out_idx] = out_direction;
                float wavelength_pdf = current_queue.wavelength_pdfs[idx];
                store_lane_throughput(next_queue, out_idx, throughput, c, throughput.values[c] * R * wavelength_pdf);
                for (int s = 0; s < current_queue.num_spectral_channels; ++s) {
                    store_stokes(next_queue, out_idx, s, StokesVector(0.0f, 0.0f, 0.0f, 0.0f));
                }
                StokesVector reflected_stokes = lane_stokes;
                apply_mueller_reflection_boundary(reflected_stokes, surface.rs, surface.rp, surface.Rs, surface.Rp);
                store_stokes(next_queue, out_idx, c, reflected_stokes);
                next_queue.medium_indices[out_idx] = current_medium_idx;
                next_queue.seeds[out_idx] = seed + 1664525u * unsigned(c + 1);
                next_queue.pixel_indices[out_idx] = pixel_index;
                next_queue.depths[out_idx] = depth + 1;
                next_queue.flags[out_idx] = 1;
                next_queue.last_pdf[out_idx] = 1.0f;
                next_queue.spectral_modes[out_idx] = SpectralRayModeLane;
                next_queue.active_channels[out_idx] = c;
                next_queue.wavelength_pdfs[out_idx] = wavelength_pdf;
            }
        }

        if (T > 1e-6f) {
            float eta = eta_i / eta_t;
            GpuVec3 perp = eta * (unit_direction + cos_theta_i * normal);
            GpuVec3 para = -sqrtf(fmaxf(0.0f, 1.0f - perp.length_sq())) * normal;
            GpuVec3 out_direction = (perp + para).normalize();

            int out_idx = reserve_ray_slot(next_queue);
            if (out_idx >= 0) {
                GpuVec3 offset = (out_direction.dot(normal) > 0.0f) ? normal : -normal;
                float transport_weight = T *
                    select_boundary_transport_scale(surface.radiance_scale, surface.importance_scale, BoundaryTransportMode::Radiance);
                float wavelength_pdf = current_queue.wavelength_pdfs[idx];
                next_queue.origins[out_idx] = p + offset * 1e-4f;
                next_queue.directions[out_idx] = out_direction;
                store_lane_throughput(next_queue, out_idx, throughput, c, throughput.values[c] * transport_weight * wavelength_pdf);
                for (int s = 0; s < current_queue.num_spectral_channels; ++s) {
                    store_stokes(next_queue, out_idx, s, StokesVector(0.0f, 0.0f, 0.0f, 0.0f));
                }
                StokesVector transmitted_stokes = lane_stokes;
                apply_mueller_transmission_boundary(transmitted_stokes, surface.ts, surface.tp, surface.Ts, surface.Tp, surface.eta_jacobian);
                transmitted_stokes = transmitted_stokes *
                    select_boundary_transport_scale(surface.radiance_scale, surface.importance_scale, BoundaryTransportMode::Radiance);
                store_stokes(next_queue, out_idx, c, transmitted_stokes);

                next_queue.medium_indices[out_idx] = next_dielectric_medium_index(
                    current_medium_idx, mat_idx, r_in.direction, out_direction, ng);
                next_queue.seeds[out_idx] = seed + 22695477u * unsigned(c + 1);
                next_queue.pixel_indices[out_idx] = pixel_index;
                next_queue.depths[out_idx] = depth + 1;
                next_queue.flags[out_idx] = 1;
                next_queue.last_pdf[out_idx] = 1.0f;
                next_queue.spectral_modes[out_idx] = SpectralRayModeLane;
                next_queue.active_channels[out_idx] = c;
                next_queue.wavelength_pdfs[out_idx] = wavelength_pdf;
            }
        }
    }
    return true;
}

__global__ __launch_bounds__(256) void extend_shadow_kernel(
    ShadowQueue shadow_queue,
    GpuVec3* accum_buffer,
    GpuScene scene,
    float dispersion_clamp
) {
    (void)dispersion_clamp;
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= *shadow_queue.count) return;

    GpuVec3 origin = shadow_queue.origins[idx];
    GpuVec3 direction = shadow_queue.directions[idx];
    float max_dist = shadow_queue.max_dist[idx];
    int pixel_index = shadow_queue.pixel_indices[idx];
    int spectral_mode = shadow_queue.spectral_modes ? shadow_queue.spectral_modes[idx] : SpectralRayModePacket;
    int active_channel = shadow_queue.active_channels ? shadow_queue.active_channels[idx] : -1;
    float wavelength_pdf = shadow_queue.wavelength_pdfs ? shadow_queue.wavelength_pdfs[idx] : 1.0f;
    SpectralPacket radiance;
    {
        const int cap = shadow_queue.capacity;
        for (int c = 0; c < scene.num_spectral_channels; ++c) {
            radiance.values[c] = shadow_queue.radiance_vals[c * cap + idx];
            radiance.wavelengths[c] = shadow_queue.radiance_wavelengths[c * cap + idx];
        }
    }

    GpuRay r(origin, direction, 1e-4f, max_dist);

    for (int pass = 0; pass < 8; ++pass) {
        float t;
        GpuVec3 p, n, ng;
        GpuVec2 uv;
        int mat_idx;
        int type_dummy; int index_dummy;

        if (!world_hit(scene, r, 1e-4f, r.t_max, t, p, n, ng, uv, mat_idx, type_dummy, index_dummy, true)) {
            break;
        }

        GpuMaterial mat = scene.materials[mat_idx];

        if (mat.type == MaterialType::Light) {
            r.origin = p + r.direction * 1e-4f;
            r.t_max -= (t + 1e-4f);
            if (r.t_max <= 1e-4f) break;
            continue;
        }

        if (mat.type != MaterialType::Dielectric) {
            return;
        }

        return;
    }

    GpuVec3 xyz = spectral_mode_is_sampled(spectral_mode)
        ? spectral_sample_to_xyz(radiance, scene.num_spectral_channels, active_channel, wavelength_pdf, spectral_mode)
        : spectrum_to_xyz(radiance, scene.num_spectral_channels);
    GpuVec3 rgb = xyz_to_rgb(xyz);

    float max_val = 1000.0f;
    rgb.x = fminf(rgb.x, max_val);
    rgb.y = fminf(rgb.y, max_val);
    rgb.z = fminf(rgb.z, max_val);

    if (isfinite(rgb.x) && isfinite(rgb.y) && isfinite(rgb.z)) {
        atomicAdd(&accum_buffer[pixel_index].x, rgb.x);
        atomicAdd(&accum_buffer[pixel_index].y, rgb.y);
        atomicAdd(&accum_buffer[pixel_index].z, rgb.z);
    }
}

__global__ __launch_bounds__(256) void shade_kernel(
    RayQueue current_queue,
    HitQueue hit_queue,
    RayQueue next_queue,
    ShadowQueue shadow_queue,
    GpuVec3* accum_buffer,
    GpuVec3* normal_buffer,
    GpuVec3* albedo_buffer,
    float* depth_buffer,
    GpuVec2* uv_buffer,
    GpuVec2* motion_vector_buffer,
    GpuCamera current_camera,
    GpuCamera previous_camera,
    GpuScene scene,
    int sample_index,
    float dispersion_clamp,
    float rr_min_prob
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= *current_queue.count) return;

    int pixel_index = current_queue.pixel_indices[idx];
    int mat_idx = hit_queue.mat_ids[idx];
    SpectralPacket throughput = load_throughput(current_queue, idx);
    int depth = current_queue.depths[idx];
    unsigned int seed = current_queue.seeds[idx];
    int flag = current_queue.flags[idx];

    int current_medium_idx = current_queue.medium_indices[idx];
    float t_hit = (mat_idx != -1) ? hit_queue.t[idx] : 1e30f;
    int spectral_mode = current_queue.spectral_modes[idx];
    int active_channel = current_queue.active_channels[idx];
    if (spectral_mode_is_sampled(spectral_mode)) {
        if (active_channel < 0) active_channel = 0;
        if (active_channel >= scene.num_spectral_channels) active_channel = scene.num_spectral_channels - 1;
    } else {
        active_channel = 0;
    }

    float density = 0.0f;
    float anisotropy = 0.0f;
    SpectralPacket sigma_s(0.0f);
    SpectralPacket sigma_a(0.0f);

    if (current_medium_idx == -1) {
        density = scene.medium_density;
        anisotropy = scene.medium_anisotropy;
        sigma_s = scene.medium_scattering;
        sigma_a = scene.medium_absorption;
    } else {
        GpuMaterial med_mat = scene.materials[current_medium_idx];
        density = med_mat.medium_density;
        anisotropy = med_mat.medium_anisotropy;
        GpuMaterialSoA med_soa = load_mat_spectra_6x(scene, current_medium_idx, throughput.wavelengths);
        sigma_s = med_soa.medium_scattering;
        sigma_a = med_soa.medium_absorption;
    }

    SpectralPacket sigma_t = (sigma_s + sigma_a) * density;
    float sigma_t_avg = 0.0f;
    for (int c = 0; c < scene.num_spectral_channels; ++c) {
        sigma_t_avg += sigma_t.values[c];
    }
    sigma_t_avg /= float(scene.num_spectral_channels);
    float sigma_t_proposal = spectral_mode_is_sampled(spectral_mode)
        ? sigma_t.values[active_channel]
        : sigma_t_avg;

    if (sigma_t_proposal > 1e-4f) {
        float r_dist = rand_float(seed);
        float t_medium = -logf(1.0f - r_dist) / sigma_t_proposal;

        float max_allowed = scene.medium_max_distance > 0.0f ? scene.medium_max_distance : 1e30f;
        if (t_medium < t_hit && t_medium < max_allowed) {
            float tr_vals[kMaxPacketLanes];
            for (int c = 0; c < scene.num_spectral_channels; ++c) {
                tr_vals[c] = expf(-sigma_t.values[c] * t_medium);
            }

            float pdf_t = sigma_t_proposal * expf(-sigma_t_proposal * t_medium);

            for (int c = 0; c < scene.num_spectral_channels; ++c) {
                throughput.values[c] *= tr_vals[c] * sigma_s.values[c] * density * (1.0f / pdf_t);
            }

            if (scene.light_count > 0) {
                int light_idx_idx = min(int(rand_float(seed) * scene.light_count), scene.light_count - 1);
                int light_idx = scene.light_indices[light_idx_idx];
                GpuSphere light_sphere = scene.spheres[light_idx];

                GpuVec3 p_vol = current_queue.origins[idx] + current_queue.directions[idx] * t_medium;
                GpuVec3 wc = light_sphere.center - p_vol;
                float dist_sq = wc.length_sq();
                float radius = light_sphere.radius;
                float radius_sq = radius * radius;

                if (dist_sq > radius_sq) {
                    float dist = sqrtf(dist_sq);
                    float sin_theta_max2 = radius_sq / dist_sq;
                    float cos_theta_max = sqrtf(fmaxf(0.0f, 1.0f - sin_theta_max2));

                    float r1 = rand_float(seed);
                    float r2 = rand_float(seed);
                    float cos_theta = 1.0f - r1 + r1 * cos_theta_max;
                    float sin_theta = sqrtf(fmaxf(0.0f, 1.0f - cos_theta * cos_theta));
                    float phi = 6.2831853f * r2;

                    GpuVec3 w = wc * (1.0f / dist);
                    GpuVec3 u = (fabsf(w.x) > 0.9f) ? GpuVec3(0, 1, 0) : GpuVec3(1, 0, 0);
                    u = u.cross(w).normalize();
                    GpuVec3 v = w.cross(u);
                    GpuVec3 l_dir = (u * cosf(phi) * sin_theta + v * sinf(phi) * sin_theta + w * cos_theta).normalize();

                    float phase_val = eval_henyey_greenstein(current_queue.directions[idx].dot(l_dir), anisotropy);
                    float solid_angle = 6.2831853f * (1.0f - cos_theta_max);
                    float pdf = 1.0f / (solid_angle * scene.light_count);

                    float M_dot_D = -wc.dot(l_dir);
                    float c_val = dist_sq - radius_sq;
                    float t_to_light = -M_dot_D - sqrtf(fmaxf(0.0f, M_dot_D * M_dot_D - c_val));

                    if (t_to_light > 1e-4f) {
                         float tr_light_vals[kMaxPacketLanes];
                         for (int c = 0; c < scene.num_spectral_channels; ++c) {
                             tr_light_vals[c] = expf(-sigma_t.values[c] * t_to_light);
                         }

                         int light_mat_idx = light_sphere.material_index;
                         SpectralPacket L_e = load_mat_emission_spectrum(scene, light_mat_idx, throughput.wavelengths);
                         SpectralPacket contribution;
                         for (int c = 0; c < scene.num_spectral_channels; ++c) {
                             L_e.wavelengths[c] = throughput.wavelengths[c];
                             contribution.values[c] = throughput.values[c] * L_e.values[c] * phase_val * tr_light_vals[c] * (1.0f / pdf);
                             contribution.wavelengths[c] = throughput.wavelengths[c];
                         }

                         int s_idx = atomicAdd(shadow_queue.count, 1);
                         if (s_idx < shadow_queue.capacity) {
                             const int cap = shadow_queue.capacity;
                             shadow_queue.origins[s_idx] = p_vol;
                             shadow_queue.directions[s_idx] = l_dir;
                             shadow_queue.max_dist[s_idx] = t_to_light - 1e-4f;
                             for (int c = 0; c < scene.num_spectral_channels; ++c) {
                                 shadow_queue.radiance_vals[c * cap + s_idx] = contribution.values[c];
                                 shadow_queue.radiance_wavelengths[c * cap + s_idx] = contribution.wavelengths[c];
                             }
                             shadow_queue.pixel_indices[s_idx] = pixel_index;
                             shadow_queue.spectral_modes[s_idx] = spectral_mode;
                             shadow_queue.active_channels[s_idx] = current_queue.active_channels[idx];
                             shadow_queue.wavelength_pdfs[s_idx] = current_queue.wavelength_pdfs[idx];
                         }
                    }
                }
            }

            GpuVec3 new_dir = sample_henyey_greenstein(current_queue.directions[idx], anisotropy, seed);
            GpuVec3 new_origin = current_queue.origins[idx] + current_queue.directions[idx] * t_medium;

             int out_idx = reserve_ray_slot(next_queue);
             if (out_idx >= 0) {
                next_queue.origins[out_idx] = new_origin;
                next_queue.directions[out_idx] = new_dir;
                store_throughput(next_queue, out_idx, throughput);
                for (int c = 0; c < scene.num_spectral_channels; ++c) {
                    store_stokes(next_queue, out_idx, c, load_stokes(current_queue, idx, c));
                }
                next_queue.medium_indices[out_idx] = current_medium_idx;
                next_queue.seeds[out_idx] = seed;
                next_queue.pixel_indices[out_idx] = pixel_index;
                next_queue.depths[out_idx] = depth + 1;
                next_queue.flags[out_idx] = 0;
                next_queue.last_pdf[out_idx] = 0.0f;
                next_queue.spectral_modes[out_idx] = current_queue.spectral_modes[idx];
                next_queue.active_channels[out_idx] = current_queue.active_channels[idx];
                next_queue.wavelength_pdfs[out_idx] = current_queue.wavelength_pdfs[idx];
             }
             return;
        } else {
            float tr_vals[kMaxPacketLanes];
            float prob_no_scatter = expf(-sigma_t_proposal * t_hit);
            for (int c = 0; c < scene.num_spectral_channels; ++c) {
                tr_vals[c] = expf(-sigma_t.values[c] * t_hit);
            }

            if (prob_no_scatter > 1e-6f) {
                for (int c = 0; c < scene.num_spectral_channels; ++c) {
                    throughput.values[c] *= tr_vals[c] * (1.0f / prob_no_scatter);
                }
            } else {
                throughput = SpectralPacket(0.0f);
            }
        }
    }

    if (mat_idx == -1) {
        GpuVec3 unit_direction = current_queue.directions[idx].normalize();
        float t_sky = 0.5f * (unit_direction.y + 1.0f);
        GpuVec3 sky_color;
        if (scene.medium_density > 1e-6f || current_medium_idx != -1) {
            float sky_luma = 0.05f + 0.15f * t_sky;
            sky_color = GpuVec3(sky_luma, sky_luma, sky_luma);
        } else {
            sky_color = (1.0f - t_sky) * GpuVec3(0.05f, 0.05f, 0.05f) + t_sky * GpuVec3(0.2f, 0.2f, 0.4f);
        }

        SpectralPacket sky_spectrum = emission_to_spectrum(sky_color, throughput.wavelengths, scene.num_spectral_channels);
        SpectralPacket contribution = throughput * sky_spectrum;

        GpuVec3 xyz = spectral_sample_to_xyz(
            contribution,
            scene.num_spectral_channels,
            current_queue.active_channels[idx],
            current_queue.wavelength_pdfs[idx],
            current_queue.spectral_modes[idx]);
        GpuVec3 rgb = xyz_to_rgb(xyz);

        if (isfinite(rgb.x) && isfinite(rgb.y) && isfinite(rgb.z)) {
            atomicAdd(&accum_buffer[pixel_index].x, rgb.x);
            atomicAdd(&accum_buffer[pixel_index].y, rgb.y);
            atomicAdd(&accum_buffer[pixel_index].z, rgb.z);
        }

        if (depth == 0) {
            if (normal_buffer) normal_buffer[pixel_index] = GpuVec3(0, 0, 0);
            if (albedo_buffer) albedo_buffer[pixel_index] = sky_color;
            if (depth_buffer) depth_buffer[pixel_index] = 0.0f;
            if (uv_buffer) uv_buffer[pixel_index] = GpuVec2(0.0f, 0.0f);
            if (motion_vector_buffer) motion_vector_buffer[pixel_index] = GpuVec2(0.0f, 0.0f);
        }
        return;
    }

    GpuMaterial mat = scene.materials[mat_idx];
    GpuMaterialSoA mat_soa = load_mat_spectra_6x(scene, mat_idx, throughput.wavelengths);

    GpuVec2 hit_uv = hit_queue.uv[idx];
    if (mat.albedo_expression_root != -1) {
        mat_soa.albedo = eval_material_expression(scene, mat, mat.albedo_expression_root, hit_uv.u, hit_uv.v, throughput.wavelengths, scene.num_spectral_channels);
    }
    if (mat.roughness_expression_root != -1) {
        SpectralPacket graph_roughness = eval_material_expression(scene, mat, mat.roughness_expression_root, hit_uv.u, hit_uv.v, throughput.wavelengths, scene.num_spectral_channels);
        float roughness_value = 0.0f;
        for (int c = 0; c < scene.num_spectral_channels; ++c) {
            roughness_value += graph_roughness.values[c];
        }
        roughness_value /= float(scene.num_spectral_channels);
        mat.roughness = fminf(1.0f, fmaxf(0.001f, roughness_value));
    }
    if (mat.emission_expression_root != -1) {
        mat_soa.emission = eval_material_expression(scene, mat, mat.emission_expression_root, hit_uv.u, hit_uv.v, throughput.wavelengths, scene.num_spectral_channels);
    }

    if (mat.texture_index != -1) {
        SpectralPacket tex_color = sample_texture(scene, mat.texture_index, hit_uv.u, hit_uv.v, throughput.wavelengths, scene.num_spectral_channels);
        mat_soa.albedo = mat_soa.albedo * tex_color;
    }
    if (mat.roughness_texture_index != -1) {
        SpectralPacket tex_roughness = sample_texture(scene, mat.roughness_texture_index, hit_uv.u, hit_uv.v, throughput.wavelengths, scene.num_spectral_channels);
        float tex_value = 0.0f;
        for (int c = 0; c < scene.num_spectral_channels; ++c) {
            tex_value += tex_roughness.values[c];
        }
        tex_value = fminf(1.0f, fmaxf(0.0f, tex_value / float(scene.num_spectral_channels)));
        mat.roughness = fminf(1.0f, fmaxf(0.001f, mat.roughness * tex_value));
    }
    if (mat.emission_texture_index != -1) {
        SpectralPacket tex_emission = sample_texture(scene, mat.emission_texture_index, hit_uv.u, hit_uv.v, throughput.wavelengths, scene.num_spectral_channels);
        mat_soa.emission = mat_soa.emission * tex_emission;
    }

    GpuVec3 p = hit_queue.p[idx];
    GpuVec3 n = hit_queue.n[idx];
    GpuVec3 ng = hit_queue.ng[idx];

    if (depth == 0) {
        if (normal_buffer) normal_buffer[pixel_index] = n;
        if (albedo_buffer) albedo_buffer[pixel_index] = xyz_to_rgb(spectrum_to_xyz(mat_soa.albedo, scene.num_spectral_channels));
        if (depth_buffer) depth_buffer[pixel_index] = t_hit;
        if (uv_buffer) uv_buffer[pixel_index] = hit_uv;
        if (motion_vector_buffer) {
            GpuVec3 previous_p = p;
            int hit_type = hit_queue.hit_types[idx];
            int hit_index = hit_queue.hit_indices[idx];
            if (hit_type == 2 &&
                hit_index >= 0 &&
                hit_index < scene.instance_count &&
                scene.instance_transforms &&
                scene.previous_instance_transforms) {
                const GpuInstanceTransform& current_xform = scene.instance_transforms[hit_index];
                const GpuInstanceTransform& previous_xform = scene.previous_instance_transforms[hit_index];
                GpuVec3 local_p = current_xform.inverse_transform.transform_point(p);
                previous_p = previous_xform.transform.transform_point(local_p);
            }
            GpuVec2 current_screen = project_camera_screen(current_camera, p);
            GpuVec2 previous_screen = project_camera_screen(previous_camera, previous_p);
            motion_vector_buffer[pixel_index] = GpuVec2(
                current_screen.u - previous_screen.u,
                current_screen.v - previous_screen.v);
        }
    }

    GpuVec3 emission_rgb = xyz_to_rgb(spectrum_to_xyz(mat_soa.emission, scene.num_spectral_channels));
    if (emission_rgb.length_sq() > 0) {
        float mis_weight = 1.0f;

        if (depth > 0 && !(flag & 1) && scene.light_count > 0) {
             float pdf_nee = 0.0f;

             for(int k=0; k<scene.light_count; ++k) {
                 int l_idx = scene.light_indices[k];
                 GpuSphere sph = scene.spheres[l_idx];
                 if (sph.material_index == mat_idx) {
                      pdf_nee = sphere_light_solid_angle_pdf(sph, current_queue.origins[idx], scene.light_count);
                      break;
                 }
             }

             if (pdf_nee > 0.0f) {
                 float last_pdf = current_queue.last_pdf[idx];
                 mis_weight = (last_pdf * last_pdf) / (last_pdf * last_pdf + pdf_nee * pdf_nee);
             } else {
                 mis_weight = 0.0f;
             }
        }

        if (mis_weight > 0.0f) {
            SpectralPacket emission_spectrum = mat_soa.emission;
            for (int c = 0; c < scene.num_spectral_channels; ++c) {
                emission_spectrum.wavelengths[c] = throughput.wavelengths[c];
            }
            SpectralPacket contribution = throughput * emission_spectrum * mis_weight;

            GpuVec3 xyz = spectral_sample_to_xyz(
                contribution,
                scene.num_spectral_channels,
                current_queue.active_channels[idx],
                current_queue.wavelength_pdfs[idx],
                current_queue.spectral_modes[idx]);
            GpuVec3 rgb = xyz_to_rgb(xyz);

            if (depth > 0) {
                float max_radiance = 1000.0f;
                if (rgb.x > max_radiance) rgb.x = max_radiance;
                if (rgb.y > max_radiance) rgb.y = max_radiance;
                 if (rgb.z > max_radiance) rgb.z = max_radiance;
            }

            if (isfinite(rgb.x) && isfinite(rgb.y) && isfinite(rgb.z)) {
                atomicAdd(&accum_buffer[pixel_index].x, rgb.x);
                atomicAdd(&accum_buffer[pixel_index].y, rgb.y);
                atomicAdd(&accum_buffer[pixel_index].z, rgb.z);
            }
        }
    }

    if (depth >= 50) return;

    if (scene.light_count > 0 && (mat.type == MaterialType::Lambertian ||
                                  mat.type == MaterialType::Cloth ||
                                  (mat.type == MaterialType::Metal && mat.roughness > 0.02f) ||
                                  is_rough_dielectric_bsdf(mat))) {
        int dim_offset = 4 + depth * 6;

        float r_light_pick = sample_dimension(sample_index, pixel_index, dim_offset + 3);
        float r_light_1 = sample_dimension(sample_index, pixel_index, dim_offset + 4);
        float r_light_2 = sample_dimension(sample_index, pixel_index, dim_offset + 5);

        int light_idx_idx = min(int(r_light_pick * scene.light_count), scene.light_count - 1);
        int light_idx = scene.light_indices[light_idx_idx];

        GpuSphere light_sphere = scene.spheres[light_idx];
        GpuVec3 wc = light_sphere.center - p;
        float dist_sq = wc.length_sq();
        float radius = light_sphere.radius;
        float radius_sq = radius * radius;

        if (dist_sq > radius_sq) {
            float dist = sqrtf(dist_sq);
            float sin_theta_max2 = radius_sq / dist_sq;
            float cos_theta_max = sqrtf(fmaxf(0.0f, 1.0f - sin_theta_max2));

            float r1 = r_light_1;
            float r2 = r_light_2;
            float cos_theta = 1.0f - r1 + r1 * cos_theta_max;
            float sin_theta = sqrtf(fmaxf(0.0f, 1.0f - cos_theta * cos_theta));
            float phi = 6.2831853f * r2;

            GpuVec3 w = wc * (1.0f / dist);
            GpuVec3 u = (fabsf(w.x) > 0.9f) ? GpuVec3(0, 1, 0) : GpuVec3(1, 0, 0);
            u = u.cross(w).normalize();
            GpuVec3 v = w.cross(u);

            GpuVec3 l_dir = (u * cosf(phi) * sin_theta + v * sinf(phi) * sin_theta + w * cos_theta).normalize();

            float cos_surf = direct_light_cosine_factor(mat, n, l_dir);

            if (direct_light_direction_allowed(mat, n, ng, l_dir)) {
                 float pdf = sphere_light_solid_angle_pdf(light_sphere, p, scene.light_count);
                 pdf = fmaxf(pdf, 1e-12f);

                 int light_mat_idx = light_sphere.material_index;
                 SpectralPacket L_e = load_mat_emission_spectrum(scene, light_mat_idx, throughput.wavelengths);
                 for (int c = 0; c < scene.num_spectral_channels; ++c) {
                     L_e.wavelengths[c] = throughput.wavelengths[c];
                 }

                 SpectralPacket f_r = eval_bsdf(mat, mat_soa.albedo, mat_soa.extinction, mat_soa.metal_eta, p, n, hit_uv, -current_queue.directions[idx], l_dir, throughput.wavelengths, scene.num_spectral_channels);

                 SpectralPacket pdf_mat = pdf_bsdf_spectral(
                     mat,
                     n,
                     hit_uv,
                     -current_queue.directions[idx],
                     l_dir,
                     throughput.wavelengths,
                     scene.num_spectral_channels,
                     dispersion_clamp);

                 SpectralPacket contribution = throughput * L_e * f_r * cos_surf * (1.0f / pdf);
                 for (int c = 0; c < scene.num_spectral_channels; ++c) {
                     float pdf_mat_c = pdf_mat.values[c];
                     float mis_weight = (pdf * pdf) / (pdf * pdf + pdf_mat_c * pdf_mat_c);
                     contribution.values[c] *= mis_weight;
                 }

                 float M_dot_D = -wc.dot(l_dir);
                 float c = dist_sq - radius_sq;
                 float discriminant_val = M_dot_D * M_dot_D - c;

                 if (discriminant_val > 0.0f) {
                    float t_hit_shadow = -M_dot_D - sqrtf(discriminant_val);

                    if (t_hit_shadow > 1e-4f) {
                        if (sigma_t_avg > 1e-4f) {
                            float tr_vals[kMaxPacketLanes];
                            for (int c = 0; c < scene.num_spectral_channels; ++c) {
                                tr_vals[c] = expf(-sigma_t.values[c] * t_hit_shadow);
                            }

                            for (int c = 0; c < scene.num_spectral_channels; ++c) {
                                contribution.values[c] *= tr_vals[c];
                            }
                        }

                         int s_idx = atomicAdd(shadow_queue.count, 1);
                         if (s_idx < shadow_queue.capacity) {
                             const int cap = shadow_queue.capacity;
                             GpuVec3 offset_normal = direct_light_offset_normal(ng, l_dir);
                             float adaptive_eps = 1e-4f / fmaxf(0.01f, fabsf(ng.dot(l_dir)));
                             shadow_queue.origins[s_idx] = p + offset_normal * adaptive_eps;
                             shadow_queue.directions[s_idx] = l_dir;
                             shadow_queue.max_dist[s_idx] = t_hit_shadow - adaptive_eps;
                             for (int c = 0; c < scene.num_spectral_channels; ++c) {
                                 shadow_queue.radiance_vals[c * cap + s_idx] = contribution.values[c];
                                 shadow_queue.radiance_wavelengths[c * cap + s_idx] = contribution.wavelengths[c];
                             }
                             shadow_queue.pixel_indices[s_idx] = pixel_index;
                             shadow_queue.spectral_modes[s_idx] = spectral_mode;
                             shadow_queue.active_channels[s_idx] = current_queue.active_channels[idx];
                             shadow_queue.wavelength_pdfs[s_idx] = current_queue.wavelength_pdfs[idx];
                         }
                    }
                }
            }
        }
    }

    GpuRay r_in;
    r_in.origin = current_queue.origins[idx];
    r_in.direction = current_queue.directions[idx];

    GpuRay scattered;
    SpectralPacket attenuation;

    StokesVector current_stokes = spectral_mode_is_sampled(spectral_mode)
        ? load_stokes(current_queue, idx, active_channel)
        : load_packet_average_stokes(current_queue, idx);

    GpuVec2 uv = hit_queue.uv[idx];

            float pdf_val = 0.0f;
            float ior_outside = 1.0f;
            bool front_face = r_in.direction.dot(ng) < 0.0f;
            if (front_face && current_medium_idx >= 0) {
                ior_outside = scene.materials[current_medium_idx].ior;
            }
            if (split_dispersive_dielectric_lanes(
                    current_queue,
                    next_queue,
                    idx,
                    mat,
                    mat_soa,
                    p,
                    n,
                    ng,
                    uv,
                    throughput,
                    current_medium_idx,
                    mat_idx,
                    pixel_index,
                    depth,
                    seed,
                    dispersion_clamp,
                    ior_outside)) {
                return;
            }
            if (scatter(r_in, mat, mat_soa.albedo, mat_soa.extinction, mat_soa.metal_eta, p, n, uv, throughput, attenuation, scattered, current_stokes, seed, pdf_val, dispersion_clamp, sample_index, pixel_index, depth, scene.num_spectral_channels, ior_outside, scene.materials[mat_idx].ior, spectral_mode, active_channel)) {
                SpectralPacket new_throughput = throughput * attenuation;

                for (int c = 0; c < scene.num_spectral_channels; ++c) {
                    if (!isfinite(new_throughput.values[c])) {
                        return;
                    }
                }

                if (depth > 3) {
                    float prob = spectral_survival_probability(new_throughput, scene.num_spectral_channels, rr_min_prob);

                    if (rand_float(seed) > prob) {
                        return;
                    }
                    new_throughput = new_throughput * (1.0f / prob);
                }

                int next_flag = 0;
                bool is_delta = scene.light_count == 0 ||
                    (mat.type == MaterialType::Metal && mat.roughness <= 0.02f) ||
                    (mat.type == MaterialType::Dielectric && pdf_val <= 0.0f);
                if (is_delta) {
                    next_flag = 1;
                }

        int out_idx = reserve_ray_slot(next_queue);
        if (out_idx >= 0) {
            next_queue.origins[out_idx] = scattered.origin;
            next_queue.directions[out_idx] = scattered.direction;
            store_throughput(next_queue, out_idx, new_throughput);
            if (spectral_mode_is_sampled(spectral_mode)) {
                for (int c = 0; c < scene.num_spectral_channels; ++c) {
                    store_stokes(next_queue, out_idx, c, StokesVector(0.0f, 0.0f, 0.0f, 0.0f));
                }
                store_stokes(next_queue, out_idx, active_channel, current_stokes);
            } else {
                store_packet_scattered_stokes(
                    current_queue,
                    next_queue,
                    idx,
                    out_idx,
                    mat,
                    mat_soa,
                    r_in,
                    scattered,
                    n,
                    uv,
                    throughput,
                    ior_outside,
                    dispersion_clamp,
                    sample_index,
                    pixel_index,
                    depth);
            }
            next_queue.last_pdf[out_idx] = pdf_val;

            int next_medium = current_medium_idx;
            if (mat.type == MaterialType::Dielectric) {
                next_medium = next_dielectric_medium_index(
                    current_medium_idx, mat_idx, r_in.direction, scattered.direction, ng);
            }
            next_queue.medium_indices[out_idx] = next_medium;

            next_queue.seeds[out_idx] = seed;
            next_queue.pixel_indices[out_idx] = pixel_index;
            next_queue.depths[out_idx] = depth + 1;
            next_queue.flags[out_idx] = next_flag;
            next_queue.spectral_modes[out_idx] = spectral_mode;
            next_queue.active_channels[out_idx] = current_queue.active_channels[idx];
            next_queue.wavelength_pdfs[out_idx] = current_queue.wavelength_pdfs[idx];
        }
    }
}
