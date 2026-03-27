#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <float.h>
#include <math.h>
#include "gpu/gpu_structs.hpp"
#include "gpu/path_tracer_sampling.cuh"

// Suppress "function was declared but never referenced" warning
#pragma diag_suppress 177

namespace ure::gpu {

static __device__ inline GpuVec3 reflect(const GpuVec3& v, const GpuVec3& n) {
    return v - 2.0f * v.dot(n) * n;
}

static __device__ inline bool refract(const GpuVec3& uv, const GpuVec3& n, float etai_over_etat, GpuVec3& refracted) {
    float cos_theta = fminf((-uv).dot(n), 1.0f);
    GpuVec3 r_out_perp = etai_over_etat * (uv + cos_theta * n);
    float r_out_parallel = -sqrtf(fabsf(1.0f - r_out_perp.length_sq()));
    refracted = r_out_perp + r_out_parallel * n;
    return true;
}

static __device__ inline float schlick(float cosine, float ref_idx) {
    float r0 = (1.0f - ref_idx) / (1.0f + ref_idx);
    r0 = r0 * r0;
    return r0 + (1.0f - r0) * powf((1.0f - cosine), 5.0f);
}

static __device__ inline float smith_G1(float NdotV, float k) {
    return NdotV / (NdotV * (1.0f - k) + k);
}

static __device__ inline bool hit_sphere(const GpuSphere& sphere, const GpuRay& r, float t_min, float t_max, float& t, GpuVec3& p, GpuVec3& n, int& mat_idx) {
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
            // Project p to the sphere surface for improved precision
            p = sphere.center + (p - sphere.center).normalize() * sphere.radius;
            n = (p - sphere.center) * (1.0f / sphere.radius);
            mat_idx = sphere.material_index;
            return true;
        }
        temp = (-b + sqrtf(discriminant)) / a;
        if (temp < t_max && temp > t_min) {
            t = temp;
            p = r.at(t);
            // Project p to the sphere surface for improved precision
            p = sphere.center + (p - sphere.center).normalize() * sphere.radius;
            n = (p - sphere.center) * (1.0f / sphere.radius);
            mat_idx = sphere.material_index;
            return true;
        }
    }
    return false;
}

static __device__ inline bool hit_triangle(const GpuRay& r, const GpuVec3& v0, const GpuVec3& v1, const GpuVec3& v2, const GpuVec3* n0, const GpuVec3* n1, const GpuVec3* n2, float t_min, float t_max, float& t, GpuVec3& ng, GpuVec3& ns, float& u_out, float& v_out) {
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

static __device__ inline bool hit_aabb(const GpuRay& r, const GpuVec3& min_pt, const GpuVec3& max_pt, float t_min, float t_max) {
    float3 invD = make_float3(1.0f / r.direction.x, 1.0f / r.direction.y, 1.0f / r.direction.z);
    
    float3 t0 = make_float3((min_pt.x - r.origin.x) * invD.x, (min_pt.y - r.origin.y) * invD.y, (min_pt.z - r.origin.z) * invD.z);
    float3 t1 = make_float3((max_pt.x - r.origin.x) * invD.x, (max_pt.y - r.origin.y) * invD.y, (max_pt.z - r.origin.z) * invD.z);
    
    float3 tsmall = make_float3(fminf(t0.x, t1.x), fminf(t0.y, t1.y), fminf(t0.z, t1.z));
    float3 tbig = make_float3(fmaxf(t0.x, t1.x), fmaxf(t0.y, t1.y), fmaxf(t0.z, t1.z));
    
    float tmin = fmaxf(t_min, fmaxf(tsmall.x, fmaxf(tsmall.y, tsmall.z)));
    float tmax = fminf(t_max, fminf(tbig.x, fminf(tbig.y, tbig.z)));
    
    return tmin <= tmax;
}

static __device__ inline bool hit_bvh(const GpuMesh& mesh, const GpuRay& r, float t_min, float t_max, float& t_out, GpuVec3& ng_out, GpuVec3& ns_out, GpuVec2& uv_out) {
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

static __device__ inline bool world_hit(const GpuScene& scene, const GpuRay& r, float t_min, float t_max, float& t_out, GpuVec3& p_out, GpuVec3& n_out, GpuVec3& ng_out, GpuVec2& uv_out, int& mat_idx_out) {
    float t_closest = t_max;
    bool hit_anything = false;
    float t_temp;
    GpuVec3 p_temp, n_temp;
    int mat_idx_temp;

    for (int i = 0; i < scene.sphere_count; ++i) {
        if (hit_sphere(scene.spheres[i], r, t_min, t_closest, t_temp, p_temp, n_temp, mat_idx_temp)) {
            hit_anything = true;
            t_closest = t_temp;
            t_out = t_temp;
            p_out = p_temp;
            n_out = n_temp;
            ng_out = n_temp;
            mat_idx_out = mat_idx_temp;
            
            GpuVec3 p_local = (p_temp - scene.spheres[i].center).normalize();
            float phi = atan2f(p_local.z, p_local.x);
            float theta = asinf(p_local.y);
            float u = 1.0f - (phi + 3.14159265f) / (2.0f * 3.14159265f);
            float v = (theta + 3.14159265f / 2.0f) / 3.14159265f;
            uv_out = GpuVec2(u, v);
        }
    }

    for (int i = 0; i < scene.mesh_count; ++i) {
        GpuMesh& mesh = scene.meshes[i];
        
        if (!hit_aabb(r, mesh.min_pt, mesh.max_pt, t_min, t_closest)) {
            continue;
        }

        if (mesh.bvh_node_count > 0) {
            float t_mesh;
            GpuVec3 ng_mesh, ns_mesh;
            GpuVec2 uv_mesh;
            if (hit_bvh(mesh, r, t_min, t_closest, t_mesh, ng_mesh, ns_mesh, uv_mesh)) {
                hit_anything = true;
                t_closest = t_mesh;
                t_out = t_mesh;
                p_out = r.at(t_mesh);
                n_out = ns_mesh;
                ng_out = ng_mesh;
                mat_idx_out = mesh.material_index;
                uv_out = uv_mesh;
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

__device__ inline GpuVec3 get_reference_frame(const GpuVec3& dir) {
    if (fabsf(dir.y) > 0.999f) {
        return GpuVec3(1.0f, 0.0f, 0.0f);
    }
    return GpuVec3(0.0f, 1.0f, 0.0f).cross(dir).normalize();
}

__device__ inline void rotate_stokes(StokesVector& s, float two_phi) {
    float c = cosf(two_phi);
    float si = sinf(two_phi);
    float new_Q = s.Q * c + s.U * si;
    float new_U = -s.Q * si + s.U * c;
    s.Q = new_Q;
    s.U = new_U;
}

__device__ inline void apply_mueller_reflection_dielectric(StokesVector& s, float rs, float rp, float delta = 0.0f) {
    float Rs = rs * rs;
    float Rp = rp * rp;
    
    float A = 0.5f * (Rs + Rp);
    float B = 0.5f * (Rs - Rp);
    float C = rs * rp * cosf(delta);
    float D = rs * rp * sinf(delta);
    
    float new_I = A * s.I + B * s.Q;
    float new_Q = B * s.I + A * s.Q;
    float new_U = C * s.U - D * s.V;
    float new_V = D * s.U + C * s.V;
    
    s.I = new_I;
    s.Q = new_Q;
    s.U = new_U;
    s.V = new_V;
}

__device__ inline void apply_mueller_reflection_conductor(StokesVector& s, float n, float k, float cos_theta) {
    float sin_theta2 = 1.0f - cos_theta * cos_theta;
    
    float n2_minus_k2 = n * n - k * k;
    float two_nk = 2.0f * n * k;
    
    float re_inner = n2_minus_k2 - sin_theta2;
    float im_inner = two_nk;
    
    float r = sqrtf(re_inner * re_inner + im_inner * im_inner);
    float a = sqrtf(fmaxf(0.0f, 0.5f * (r + re_inner)));
    float b = sqrtf(fmaxf(0.0f, 0.5f * (r - re_inner)));
    
    float den_s_re = cos_theta + a;
    float den_s_im = b;
    float num_s_re = cos_theta - a;
    float num_s_im = -b;
    float den_s_sq = den_s_re * den_s_re + den_s_im * den_s_im;
    
    float rs_re = (num_s_re * den_s_re + num_s_im * den_s_im) / den_s_sq;
    float rs_im = (num_s_im * den_s_re - num_s_re * den_s_im) / den_s_sq;
    
    float n2_cos_re = n2_minus_k2 * cos_theta;
    float n2_cos_im = two_nk * cos_theta;
    
    float num_p_re = n2_cos_re - a;
    float num_p_im = n2_cos_im - b;
    float den_p_re = n2_cos_re + a;
    float den_p_im = n2_cos_im + b;
    float den_p_sq = den_p_re * den_p_re + den_p_im * den_p_im;
    
    float rp_re = (num_p_re * den_p_re + num_p_im * den_p_im) / den_p_sq;
    float rp_im = (num_p_im * den_p_re - num_p_re * den_p_im) / den_p_sq;
    
    float Rs = rs_re * rs_re + rs_im * rs_im;
    float Rp = rp_re * rp_re + rp_im * rp_im;
    
    float rs_abs = sqrtf(Rs);
    float rp_abs = sqrtf(Rp);
    
    float phi_s = atan2f(rs_im, rs_re);
    float phi_p = atan2f(rp_im, rp_re);
    float delta = phi_s - phi_p;
    
    float A = 0.5f * (Rs + Rp);
    float B = 0.5f * (Rs - Rp);
    float C = rs_abs * rp_abs * cosf(delta);
    float D = rs_abs * rp_abs * sinf(delta);
    
    float new_I = A * s.I + B * s.Q;
    float new_Q = B * s.I + A * s.Q;
    float new_U = C * s.U - D * s.V;
    float new_V = D * s.U + C * s.V;
    
    s.I = new_I;
    s.Q = new_Q;
    s.U = new_U;
    s.V = new_V;
}

__device__ inline void apply_mueller_transmission_dielectric(StokesVector& s, float ts, float tp, float eta_rel) {
    float Ts = ts * ts * eta_rel;
    float Tp = tp * tp * eta_rel;
    
    float A = 0.5f * (Ts + Tp);
    float B = 0.5f * (Ts - Tp);
    float C = ts * tp * eta_rel;
    
    float new_I = A * s.I + B * s.Q;
    float new_Q = B * s.I + A * s.Q;
    float new_U = C * s.U;
    float new_V = C * s.V;
    
    s.I = new_I;
    s.Q = new_Q;
    s.U = new_U;
    s.V = new_V;
}

__device__ inline float get_thin_film_interference(float wavelength, float thickness, float ior_film, float cos_theta, float r12) {
    if (thickness <= 0.0f) return r12;
    
    float sin_theta = sqrtf(fmaxf(0.0f, 1.0f - cos_theta * cos_theta));
    float cos_theta_film = sqrtf(fmaxf(0.0f, 1.0f - (sin_theta / ior_film) * (sin_theta / ior_film)));
    
    float opd = 2.0f * ior_film * thickness * cos_theta_film;
    
    float phase = (2.0f * 3.14159265f * opd) / wavelength;
    
    float interference = 0.5f - 0.5f * cosf(phase);
    float R_tf = 4.0f * r12 * interference; 
    
    return fminf(0.99f, fmaxf(0.0f, R_tf));
}

__device__ inline float get_dielectric_thin_film_reflectance(
    float wavelength, float thickness, float ior_film, 
    float ior_incident, float ior_substrate, float cos_theta_i
) {
    float sin2_i = fmaxf(0.0f, 1.0f - cos_theta_i * cos_theta_i);
    float sin_i = sqrtf(sin2_i);
    float sin_t = (ior_incident / ior_film) * sin_i;
    
    if (sin_t >= 1.0f) return 1.0f;
    
    float cos_t = sqrtf(fmaxf(0.0f, 1.0f - sin_t * sin_t));
    
    float sin_s = (ior_film / ior_substrate) * sin_t;
    bool tir_substrate = (sin_s >= 1.0f);
    float cos_s = tir_substrate ? 0.0f : sqrtf(fmaxf(0.0f, 1.0f - sin_s * sin_s));
    
    float num1_s = ior_incident * cos_theta_i - ior_film * cos_t;
    float den1_s = ior_incident * cos_theta_i + ior_film * cos_t;
    float r1_s = num1_s / den1_s;
    
    float num1_p = ior_film * cos_theta_i - ior_incident * cos_t;
    float den1_p = ior_film * cos_theta_i + ior_incident * cos_t;
    float r1_p = num1_p / den1_p;
    
    float r2_s = 1.0f, r2_p = 1.0f;
    if (!tir_substrate) {
        float num2_s = ior_film * cos_t - ior_substrate * cos_s;
        float den2_s = ior_film * cos_t + ior_substrate * cos_s;
        r2_s = num2_s / den2_s;
        
        float num2_p = ior_substrate * cos_t - ior_film * cos_s;
        float den2_p = ior_substrate * cos_t + ior_film * cos_s;
        r2_p = num2_p / den2_p;
    }
    
    float opd = 2.0f * ior_film * thickness * cos_t;
    float phi = (2.0f * 3.14159265f * opd) / wavelength;
    float cos_phi = cosf(phi);
    
    float num_s = r1_s*r1_s + r2_s*r2_s + 2.0f*r1_s*r2_s*cos_phi;
    float den_s = 1.0f + r1_s*r1_s*r2_s*r2_s + 2.0f*r1_s*r2_s*cos_phi;
    float R_s = num_s / den_s;
    
    float num_p = r1_p*r1_p + r2_p*r2_p + 2.0f*r1_p*r2_p*cos_phi;
    float den_p = 1.0f + r1_p*r1_p*r2_p*r2_p + 2.0f*r1_p*r2_p*cos_phi;
    float R_p = num_p / den_p;
    
    return 0.5f * (R_s + R_p);
}

static __device__ inline bool scatter(
    const GpuRay& r_in, const GpuMaterial& mat, const GpuVec3& p, const GpuVec3& n, const GpuVec2& uv,
    const GpuSpectrum& current_throughput,
    GpuSpectrum& attenuation, GpuRay& scattered, StokesVector& stokes, unsigned int& seed,
    float dispersion_clamp,
    int sample_index,
    int pixel_index,
    int depth,
    int& spectral_channel
) {
    int dim_offset = 4 + depth * 6;
    float r_bsdf_1 = sample_dimension(sample_index, pixel_index, dim_offset + 0);
    float r_bsdf_2 = sample_dimension(sample_index, pixel_index, dim_offset + 1);
    float r_bsdf_3 = sample_dimension(sample_index, pixel_index, dim_offset + 2);

    float effective_thickness = mat.thin_film_thickness;
    if (effective_thickness > 0.0f) {
        effective_thickness = effective_thickness * (1.5f - 1.0f * uv.v);
    }

    if (mat.type == MaterialType::Lambertian) {
        GpuVec3 scatter_direction = n + sample_unit_vector_lds(r_bsdf_1, r_bsdf_2);
        
        if (scatter_direction.length_sq() < 1e-16f)
            scatter_direction = n;
        
        scattered.direction = scatter_direction.normalize();
        
        GpuVec3 offset = (scattered.direction.dot(n) > 0.0f) ? n : -n;
        scattered.origin = p + offset * 1e-4f; 
        
        scattered.t_min = 1e-4f; 
        scattered.t_max = FLT_MAX;
        attenuation = mat.albedo;
        
        stokes.Q = 0.0f;
        stokes.U = 0.0f;
        stokes.V = 0.0f;
        
        return true;
    } else if (mat.type == MaterialType::Metal) {
        GpuVec3 V = (-r_in.direction).normalize();
        GpuVec3 N = n;
        if (V.dot(N) < 0.0f) N = -N;

        float r1 = r_bsdf_1;
        float r2 = r_bsdf_2;
        
        GpuVec3 H = ImportanceSampleGGXVisible(r1, r2, V, N, mat.roughness);
        GpuVec3 L = reflect(-V, H);
        
        scattered.direction = L.normalize();
        
        int channel = sample_index % 3;
        
        float n_val = mat.ior;
        
        float k_r = mat.extinction.values.x;
        float k_g = mat.extinction.values.y;
        float k_b = mat.extinction.values.z;
        
        float k_val = (channel == 0) ? k_r : ((channel == 1) ? k_g : k_b);
        
        GpuVec3 ref_in = get_reference_frame(r_in.direction);
        
        GpuVec3 raw_s = r_in.direction.cross(H);
        float raw_len_sq = raw_s.length_sq();
        GpuVec3 s_axis;
        
        if (raw_len_sq < 1e-12f) {
            s_axis = get_reference_frame(H);
        } else {
            s_axis = raw_s * (1.0f / sqrtf(raw_len_sq));
        }

        float cos_phi_in = 1.0f;
        float sin_phi_in = 0.0f;
        
        if (s_axis.length_sq() > 1e-6f) {
            cos_phi_in = ref_in.dot(s_axis);
            sin_phi_in = ref_in.cross(s_axis).dot(r_in.direction);
        }
        
        float phi_in = atan2f(sin_phi_in, cos_phi_in);
        rotate_stokes(stokes, 2.0f * phi_in);
        
        float cos_theta_h = fmaxf(0.0f, V.dot(H));
        
        float stokes_I_in = stokes.I;
        apply_mueller_reflection_conductor(stokes, n_val, k_val, cos_theta_h);
        float stokes_I_out = stokes.I;
        
        float fresnel_reflectance = 0.0f;
        if (stokes_I_in > 1e-6f) {
             fresnel_reflectance = stokes_I_out / stokes_I_in;
        } else {
             fresnel_reflectance = 1.0f; 
        }

        float NdotV = N.dot(V);
        float NdotL = N.dot(scattered.direction);
        float NdotH = N.dot(H);
        float VdotH = V.dot(H);
        
        if (NdotL <= 0.0f || NdotV <= 0.0f || NdotH <= 0.0f || VdotH <= 0.0f) {
            return false;
        }
        
        NdotV = fmaxf(1e-6f, NdotV);
        NdotH = fmaxf(1e-6f, NdotH);
        VdotH = fmaxf(1e-6f, VdotH);
        
        float rough = fmaxf(0.001f, mat.roughness);
        float k = (rough + 1.0f);
        k = (k * k) * 0.125f;
        
        float G1_L = smith_G1(NdotL, k);
        float microfacet_weight = (G1_L * VdotH) / fmaxf(1e-6f, NdotH * NdotV);
        
        float tf_boost = 1.0f;
        if (effective_thickness > 0.0f) {
            float r_base_r = mat.albedo.values.x;
            float r_base_g = mat.albedo.values.y;
            float r_base_b = mat.albedo.values.z;
            
            float R_tf_r = get_thin_film_interference(650.0f, effective_thickness, mat.thin_film_ior, cos_theta_h, r_base_r);
            float R_tf_g = get_thin_film_interference(550.0f, effective_thickness, mat.thin_film_ior, cos_theta_h, r_base_g);
            float R_tf_b = get_thin_film_interference(450.0f, effective_thickness, mat.thin_film_ior, cos_theta_h, r_base_b);

            float boost_r = R_tf_r / fmaxf(1e-6f, r_base_r);
            float boost_g = R_tf_g / fmaxf(1e-6f, r_base_g);
            float boost_b = R_tf_b / fmaxf(1e-6f, r_base_b);
            
            tf_boost = (boost_r + boost_g + boost_b) / 3.0f;
            
            stokes.I *= tf_boost;
            stokes.Q *= tf_boost;
            stokes.U *= tf_boost;
            stokes.V *= tf_boost;
            
            attenuation.values.x = mat.albedo.values.x * boost_r * fresnel_reflectance * microfacet_weight;
            attenuation.values.y = mat.albedo.values.y * boost_g * fresnel_reflectance * microfacet_weight;
            attenuation.values.z = mat.albedo.values.z * boost_b * fresnel_reflectance * microfacet_weight;
        } else {
            attenuation.values.x = mat.albedo.values.x * fresnel_reflectance * microfacet_weight;
            attenuation.values.y = mat.albedo.values.y * fresnel_reflectance * microfacet_weight;
            attenuation.values.z = mat.albedo.values.z * fresnel_reflectance * microfacet_weight;
        }

        GpuVec3 ref_out = get_reference_frame(scattered.direction);
        GpuVec3 raw_s_out = scattered.direction.cross(H);
        float raw_len_sq_out = raw_s_out.length_sq();
        GpuVec3 s_axis_out;

        if (raw_len_sq_out < 1e-12f) {
            s_axis_out = get_reference_frame(H);
        } else {
            s_axis_out = raw_s_out * (1.0f / sqrtf(raw_len_sq_out));
        }

        float cos_phi_out = s_axis_out.dot(ref_out);
        float sin_phi_out = s_axis_out.cross(ref_out).dot(scattered.direction);
        float phi_out = atan2f(sin_phi_out, cos_phi_out);

        rotate_stokes(stokes, 2.0f * phi_out);
        
        // Use Robust Offset based on scatter direction and geometric normal N (not H)
        GpuVec3 offset = (scattered.direction.dot(N) > 0.0f) ? N : -N;
        scattered.origin = p + offset * 1e-4f; 
        scattered.t_min = 1e-4f;
        scattered.t_max = FLT_MAX;

        return (scattered.direction.dot(N) > 0);
    } else if (mat.type == MaterialType::Dielectric) {
        attenuation = mat.albedo; // Use material albedo for tint
        float refraction_ratio = mat.ior; 

        if (mat.dispersion > 0.0f || mat.thin_film_thickness > 0.0f) {
            int channel;
            if (spectral_channel > 0) {
                channel = spectral_channel - 1;
            } else {
                float r_spec = sample_dimension(sample_index, pixel_index, dim_offset + 6);
                channel = min(int(r_spec * 3.0f), 2);
            }

            float lambda = 550.0f;
            
            if (channel == 0) lambda = 650.0f;
            else if (channel == 1) lambda = 550.0f;
            else lambda = 450.0f;

            if (mat.dispersion > 0.0f) {
                float inv_lambda2 = 1.0f / (lambda * lambda);
                float inv_ref2 = 1.0f / (550.0f * 550.0f);
                float offset = (inv_lambda2 - inv_ref2) * 4e5f;
                refraction_ratio = mat.ior + mat.dispersion * offset;
                if (refraction_ratio < 1.01f) refraction_ratio = 1.01f;
            }

            float b_val = 1.0f;
            if (b_val > dispersion_clamp) b_val = dispersion_clamp; 
            
            attenuation = GpuSpectrum(b_val);
        }

        bool front_face = r_in.direction.dot(n) < 0;
        GpuVec3 normal = front_face ? n : -n;

        if (mat.type == MaterialType::Dielectric) {
            float jitter_scale = mat.roughness * 0.002f;
            if (jitter_scale > 0.0f) {
                GpuVec3 jitter = sample_unit_vector_lds(r_bsdf_1, r_bsdf_2) * jitter_scale; 
                normal = (normal + jitter).normalize();
            }
        }
        
        GpuVec3 ref_in = get_reference_frame(r_in.direction);
        
        GpuVec3 raw_s = r_in.direction.cross(normal);
        float raw_len_sq = raw_s.length_sq();
        GpuVec3 s_axis;
        
        if (raw_len_sq < 1e-12f) {
            s_axis = get_reference_frame(normal);
        } else {
            s_axis = raw_s * (1.0f / sqrtf(raw_len_sq));
        }

        float cos_phi_in = ref_in.dot(s_axis);
        float sin_phi_in = ref_in.cross(s_axis).dot(r_in.direction);
        float phi_in = atan2f(sin_phi_in, cos_phi_in);
        
        rotate_stokes(stokes, 2.0f * phi_in);

        float eta_i = front_face ? 1.0f : refraction_ratio;
        float eta_t = front_face ? refraction_ratio : 1.0f;
        
        GpuVec3 unit_direction = r_in.direction.normalize();
        float cos_theta_i = fminf((-unit_direction).dot(normal), 1.0f);
        float sin_theta_i = sqrtf(fmaxf(0.0f, 1.0f - cos_theta_i * cos_theta_i));
        float sin_theta_t = (eta_i / eta_t) * sin_theta_i;
        
        bool is_tir = sin_theta_t >= 1.0f;
        float cos_theta_t = is_tir ? 0.0f : sqrtf(fmaxf(0.0f, 1.0f - sin_theta_t * sin_theta_t));

        float rs = 1.0f, rp = 1.0f;
        float ts = 0.0f, tp = 0.0f;

        if (!is_tir) {
            float n1c1 = eta_i * cos_theta_i;
            float n2c2 = eta_t * cos_theta_t;
            float n2c1 = eta_t * cos_theta_i;
            float n1c2 = eta_i * cos_theta_t;
            
            rs = (n1c1 - n2c2) / (n1c1 + n2c2);
            rp = (n2c1 - n1c2) / (n2c1 + n1c2);
            
            ts = (2.0f * n1c1) / (n1c1 + n2c2);
            tp = (2.0f * n1c1) / (n2c1 + n1c2);
        }

        float Is = 0.5f * (stokes.I - stokes.Q);
        float Ip = 0.5f * (stokes.I + stokes.Q);
        
        float Rs = rs * rs;
        float Rp = rp * rp;
        
        float reflect_prob = (Rs * Is + Rp * Ip) / (stokes.I + 1e-6f);
        if (is_tir) reflect_prob = 1.0f;

        GpuVec3 R_spectral(1.0f, 1.0f, 1.0f); 
        GpuVec3 T_spectral(1.0f, 1.0f, 1.0f);
        bool has_thin_film = (!is_tir && effective_thickness > 0.0f);

        if (has_thin_film) {
            float R_r = get_dielectric_thin_film_reflectance(650.0f, effective_thickness, mat.thin_film_ior, eta_i, eta_t, cos_theta_i);
            float R_g = get_dielectric_thin_film_reflectance(550.0f, effective_thickness, mat.thin_film_ior, eta_i, eta_t, cos_theta_i);
            float R_b = get_dielectric_thin_film_reflectance(450.0f, effective_thickness, mat.thin_film_ior, eta_i, eta_t, cos_theta_i);
            
            R_spectral = GpuVec3(R_r, R_g, R_b);
            T_spectral = GpuVec3(1.0f - R_r, 1.0f - R_g, 1.0f - R_b);
            
            reflect_prob = (R_r + R_g + R_b) / 3.0f;
        }

        GpuVec3 out_direction;
        float delta = 0.0f;

        if (r_bsdf_3 < reflect_prob) {
            out_direction = reflect(unit_direction, normal);
            
            if (is_tir) {
                float n_rel = eta_t / eta_i;
                float sin2_i = sin_theta_i * sin_theta_i;
                float cos_i = cos_theta_i;
                float term = sqrtf(fmaxf(0.0f, sin2_i - n_rel * n_rel));
                
                float phase_s = 2.0f * atan2f(term, cos_i);
                float phase_p = 2.0f * atan2f(term, n_rel * n_rel * cos_i);
                delta = phase_s - phase_p;
                
                apply_mueller_reflection_dielectric(stokes, 1.0f, 1.0f, delta);
            } else {
                apply_mueller_reflection_dielectric(stokes, rs, rp);
            }
            
            if (has_thin_film) {
                attenuation.values.x *= R_spectral.x;
                attenuation.values.y *= R_spectral.y;
                attenuation.values.z *= R_spectral.z;
            } else {
                attenuation = attenuation * reflect_prob;
            }

            float pdf = fmaxf(1e-6f, reflect_prob);
            stokes = stokes * (1.0f / pdf);
            attenuation = attenuation * (1.0f / pdf);
        } else {
            GpuSpectrum transmission_color = mat.albedo;
            
            if (has_thin_film) {
                 transmission_color.values.x *= T_spectral.x;
                 transmission_color.values.y *= T_spectral.y;
                 transmission_color.values.z *= T_spectral.z;
            } else {
                transmission_color = transmission_color * (1.0f - reflect_prob);
            }
            attenuation = transmission_color;

            GpuVec3 perp = (eta_i / eta_t) * (unit_direction + cos_theta_i * normal);
            GpuVec3 para = -sqrtf(fmaxf(0.0f, 1.0f - perp.length_sq())) * normal;
            out_direction = perp + para;
            
            float transmit_prob = 1.0f - reflect_prob;
            apply_mueller_transmission_dielectric(stokes, ts, tp, (eta_t * cos_theta_t) / (eta_i * cos_theta_i));
            
            float eta_ratio = eta_t / eta_i;
            float radiance_scale = eta_ratio * eta_ratio;
            if (radiance_scale > 1.5f) radiance_scale = 1.5f;
            stokes = stokes * radiance_scale;
            attenuation = attenuation * radiance_scale;

            float pdf = fmaxf(1e-6f, transmit_prob);
            stokes = stokes * (1.0f / pdf);
            attenuation = attenuation * (1.0f / pdf);
        }

        scattered.direction = out_direction.normalize();
        
        GpuVec3 ref_out = get_reference_frame(scattered.direction);
        
        GpuVec3 raw_s_out = scattered.direction.cross(normal);
        float raw_len_sq_out = raw_s_out.length_sq();
        GpuVec3 s_axis_out;
        
        if (raw_len_sq_out < 1e-12f) {
            s_axis_out = get_reference_frame(normal);
        } else {
            s_axis_out = raw_s_out * (1.0f / sqrtf(raw_len_sq_out));
        }

        float cos_phi_out = s_axis_out.dot(ref_out);
        float sin_phi_out = s_axis_out.cross(ref_out).dot(scattered.direction);
        float phi_out = atan2f(sin_phi_out, cos_phi_out);
        
        rotate_stokes(stokes, 2.0f * phi_out);

        GpuVec3 offset = (scattered.direction.dot(n) > 0.0f) ? n : -n;
        scattered.origin = p + offset * 1e-4f; 
        scattered.t_min = 1e-4f;
        scattered.t_max = FLT_MAX;
        
        return true;
    } else if (mat.type == MaterialType::Cloth) {
        float freq = 20.0f;
        float noise = sinf(p.x * freq) * sinf(p.z * freq);
        
        float intensity = 0.75f + noise * 0.25f;
        
        attenuation = mat.albedo * intensity;
        
        GpuVec3 scatter_direction = n + sample_unit_vector_lds(r_bsdf_1, r_bsdf_2);
        if (scatter_direction.length_sq() < 1e-16f) scatter_direction = n;
        
        scattered.direction = scatter_direction.normalize();
        
        GpuVec3 offset = (scattered.direction.dot(n) > 0.0f) ? n : -n;
        scattered.origin = p + offset * 1e-4f;
        scattered.t_min = 1e-4f;
        scattered.t_max = FLT_MAX;
        return true;
    }
    return false;
}

} // namespace ure::gpu
