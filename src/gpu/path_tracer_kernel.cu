#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <stdio.h>
#include <float.h>
#include <vector>
#include <iostream>
#include <iomanip>
#include <chrono>

#include "gpu/gpu_driver.hpp"
#include "gpu/gpu_structs.hpp"
#include "gpu/path_tracer_sampling.cuh"
#include "gpu/gpu_scene_loader.hpp"
#include "gpu/bvh_builder.hpp"

#define checkCudaErrors(val) check_cuda( (val), #val, __FILE__, __LINE__ )

void check_cuda(cudaError_t result, char const *const func, const char *const file, int const line) {
    if (result) {
        std::cerr << "CUDA error = " << static_cast<unsigned int>(result) << " at " <<
            file << ":" << line << " '" << func << "' \n";
        cudaDeviceReset();
        exit(99);
    }
}

namespace ure::gpu {

__global__ void resolve_framebuffer_kernel(
    GpuVec3* accum_buffer,
    int* sample_counts,
    GpuVec3* output,
    int width,
    int height
);

__global__ void atrous_filter_kernel(
    GpuVec3* output_buffer,
    const GpuVec3* input_buffer,
    const GpuVec3* normal_buffer,
    const GpuVec3* albedo_buffer,
    int width,
    int height,
    int step_size,
    float c_phi,
    float n_phi,
    float p_phi
);

__global__ void suppress_dark_outliers_kernel(
    GpuVec3* output_buffer,
    const GpuVec3* input_buffer,
    const GpuVec3* normal_buffer,
    const GpuVec3* albedo_buffer,
    int width,
    int height,
    float k_sigma,
    float min_luma,
    float normal_phi,
    float albedo_phi
);

__global__ void fxaa_kernel(
    GpuVec3* output,
    const GpuVec3* input,
    int width,
    int height
);

__device__ GpuVec3 reflect(const GpuVec3& v, const GpuVec3& n) {
    return v - 2.0f * v.dot(n) * n;
}

__device__ bool refract(const GpuVec3& uv, const GpuVec3& n, float etai_over_etat, GpuVec3& refracted) {
    float cos_theta = fminf((-uv).dot(n), 1.0f);
    GpuVec3 r_out_perp = etai_over_etat * (uv + cos_theta * n);
    float r_out_parallel = -sqrtf(fabsf(1.0f - r_out_perp.length_sq()));
    refracted = r_out_perp + r_out_parallel * n;
    return true;
}

__device__ inline float ggx_D(float NdotH, float a) {
    float a2 = a * a;
    float denom = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
    return a2 / (3.14159265f * denom * denom);
}

__device__ inline float smith_G1(float NdotV, float k) {
    return NdotV / (NdotV * (1.0f - k) + k);
}

__device__ inline float smith_G(float NdotV, float NdotL, float k) {
    return smith_G1(NdotV, k) * smith_G1(NdotL, k);
}

__device__ float schlick(float cosine, float ref_idx) {
    float r0 = (1.0f - ref_idx) / (1.0f + ref_idx);
    r0 = r0 * r0;
    return r0 + (1.0f - r0) * powf((1.0f - cosine), 5.0f);
}

__device__ bool hit_sphere(const GpuSphere& sphere, const GpuRay& r, float t_min, float t_max, float& t, GpuVec3& p, GpuVec3& n, int& mat_idx) {
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
            n = (p - sphere.center) * (1.0f / sphere.radius);
            mat_idx = sphere.material_index;
            return true;
        }
        temp = (-b + sqrtf(discriminant)) / a;
        if (temp < t_max && temp > t_min) {
            t = temp;
            p = r.at(t);
            n = (p - sphere.center) * (1.0f / sphere.radius);
            mat_idx = sphere.material_index;
            return true;
        }
    }
    return false;
}

__device__ bool hit_triangle(const GpuRay& r, const GpuVec3& v0, const GpuVec3& v1, const GpuVec3& v2, const GpuVec3* n0, const GpuVec3* n1, const GpuVec3* n2, float t_min, float t_max, float& t, GpuVec3& ng, GpuVec3& ns, float& u_out, float& v_out) {
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

__device__ bool hit_aabb(const GpuRay& r, const GpuVec3& min_pt, const GpuVec3& max_pt, float t_min, float t_max) {
    float3 invD = make_float3(1.0f / r.direction.x, 1.0f / r.direction.y, 1.0f / r.direction.z);
    
    // t0 = (min - org) * invD
    // t1 = (max - org) * invD
    float3 t0 = make_float3((min_pt.x - r.origin.x) * invD.x, (min_pt.y - r.origin.y) * invD.y, (min_pt.z - r.origin.z) * invD.z);
    float3 t1 = make_float3((max_pt.x - r.origin.x) * invD.x, (max_pt.y - r.origin.y) * invD.y, (max_pt.z - r.origin.z) * invD.z);
    
    float3 tsmall = make_float3(fminf(t0.x, t1.x), fminf(t0.y, t1.y), fminf(t0.z, t1.z));
    float3 tbig = make_float3(fmaxf(t0.x, t1.x), fmaxf(t0.y, t1.y), fmaxf(t0.z, t1.z));
    
    float tmin = fmaxf(t_min, fmaxf(tsmall.x, fmaxf(tsmall.y, tsmall.z)));
    float tmax = fminf(t_max, fminf(tbig.x, fminf(tbig.y, tbig.z)));
    
    return tmin <= tmax;
}

__device__ bool hit_bvh(const GpuMesh& mesh, const GpuRay& r, float t_min, float t_max, float& t_out, GpuVec3& ng_out, GpuVec3& ns_out, GpuVec2& uv_out) {
    bool hit_anything = false;
    float t_closest = t_max;
    
    // Stack for traversal (Fixed size for GPU)
    int stack[64];
    int stack_ptr = 0;
    stack[stack_ptr++] = 0; // Push root node index

    while (stack_ptr > 0) {
        int node_idx = stack[--stack_ptr];
        
        // Fetch node from global memory
        const GpuBvhNode& node = mesh.bvh_nodes[node_idx];

        // Check AABB intersection
        if (!hit_aabb(r, node.min_pt, node.max_pt, t_min, t_closest)) {
            continue;
        }

        // Leaf Node Check
        if (node.primitive_count > 0) {
            // Intersect triangles in this leaf
            int start_idx = node.child_or_primitive_index;
            int end_idx = start_idx + node.primitive_count;

            for (int i = start_idx; i < end_idx; ++i) {
                // Fetch triangle indices
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
                    
                    // Interpolate UVs
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
            // Internal Node
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

__device__ bool world_hit(const GpuScene& scene, const GpuRay& r, float t_min, float t_max, float& t_out, GpuVec3& p_out, GpuVec3& n_out, GpuVec3& ng_out, GpuVec2& uv_out, int& mat_idx_out) {
    float t_closest = t_max;
    bool hit_anything = false;
    float t_temp;
    GpuVec3 p_temp, n_temp;
    int mat_idx_temp;

    // Check Spheres
    for (int i = 0; i < scene.sphere_count; ++i) {
        if (hit_sphere(scene.spheres[i], r, t_min, t_closest, t_temp, p_temp, n_temp, mat_idx_temp)) {
            hit_anything = true;
            t_closest = t_temp;
            t_out = t_temp;
            p_out = p_temp;
            n_out = n_temp;
            ng_out = n_temp; // Spheres have perfect geometry
            mat_idx_out = mat_idx_temp;
            
            // Spherical UV mapping (simple)
            // p_temp is point on sphere. center is scene.spheres[i].center
            GpuVec3 p_local = (p_temp - scene.spheres[i].center).normalize();
            float phi = atan2f(p_local.z, p_local.x);
            float theta = asinf(p_local.y);
            float u = 1.0f - (phi + 3.14159265f) / (2.0f * 3.14159265f);
            float v = (theta + 3.14159265f / 2.0f) / 3.14159265f;
            uv_out = GpuVec2(u, v);
        }
    }

    // Check Meshes (AABB + BVH Optimized)
    for (int i = 0; i < scene.mesh_count; ++i) {
        GpuMesh& mesh = scene.meshes[i];
        
        // AABB Check (Mesh Level)
        if (!hit_aabb(r, mesh.min_pt, mesh.max_pt, t_min, t_closest)) {
            continue;
        }

        if (mesh.bvh_node_count > 0) {
            // Use BVH
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
            // Linear Fallback
            for (int j = 0; j < mesh.triangle_count; ++j) {
                // Fetch vertices
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
                    
                    // Interpolate UVs
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

// Phase 3: Polarization Helpers
__device__ inline GpuVec3 get_reference_frame(const GpuVec3& dir) {
    // Generate a consistent reference frame (horizontal axis) for a given direction
    // Using global UP (0, 1, 0)
    if (fabsf(dir.y) > 0.999f) {
        return GpuVec3(1.0f, 0.0f, 0.0f); // Singularity handling
    }
    return GpuVec3(0.0f, 1.0f, 0.0f).cross(dir).normalize();
}

__device__ inline void rotate_stokes(StokesVector& s, float two_phi) {
    // Rotate Stokes vector by angle phi (frame rotation)
    // S' = R(2*phi) * S
    // R(2phi) = [1 0 0 0; 0 cos(2phi) sin(2phi) 0; 0 -sin(2phi) cos(2phi) 0; 0 0 0 1]
    float c = cosf(two_phi);
    float si = sinf(two_phi);
    float new_Q = s.Q * c + s.U * si;
    float new_U = -s.Q * si + s.U * c;
    s.Q = new_Q;
    s.U = new_U;
}

__device__ inline void apply_mueller_reflection_dielectric(StokesVector& s, float rs, float rp, float delta = 0.0f) {
    // Mueller matrix for reflection from dielectric
    // rs, rp are amplitude coefficients
    float Rs = rs * rs;
    float Rp = rp * rp;
    
    // Construct Mueller matrix multiplication (including phase shift delta = delta_s - delta_p)
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
    // Exact Fresnel for conductors
    // Using n, k at specific wavelength (handled by caller if spectral)
    
    float sin_theta2 = 1.0f - cos_theta * cos_theta;
    
    // Complex refractive index squared: (n+ik)^2 = (n2-k2) + i(2nk)
    float n2_minus_k2 = n * n - k * k;
    float two_nk = 2.0f * n * k;
    
    // Let complex value Z = sqrt( (n+ik)^2 - sin^2 theta )
    // Z = sqrt( (n2-k2-sin_theta2) + i(2nk) )
    float re_inner = n2_minus_k2 - sin_theta2;
    float im_inner = two_nk;
    
    // sqrt(re + i*im)
    float r = sqrtf(re_inner * re_inner + im_inner * im_inner);
    float a = sqrtf(fmaxf(0.0f, 0.5f * (r + re_inner)));
    float b = sqrtf(fmaxf(0.0f, 0.5f * (r - re_inner)));
    // Z = a + i*b
    
    // rs = (cos_theta - (a+ib)) / (cos_theta + (a+ib))
    float den_s_re = cos_theta + a;
    float den_s_im = b;
    float num_s_re = cos_theta - a;
    float num_s_im = -b;
    float den_s_sq = den_s_re * den_s_re + den_s_im * den_s_im;
    
    float rs_re = (num_s_re * den_s_re + num_s_im * den_s_im) / den_s_sq;
    float rs_im = (num_s_im * den_s_re - num_s_re * den_s_im) / den_s_sq;
    
    // rp = ((n+ik)^2 * cos_theta - (a+ib)) / ((n+ik)^2 * cos_theta + (a+ib))
    // (n+ik)^2 = n2_minus_k2 + i*two_nk
    float n2_cos_re = n2_minus_k2 * cos_theta;
    float n2_cos_im = two_nk * cos_theta;
    
    float num_p_re = n2_cos_re - a;
    float num_p_im = n2_cos_im - b;
    float den_p_re = n2_cos_re + a;
    float den_p_im = n2_cos_im + b;
    float den_p_sq = den_p_re * den_p_re + den_p_im * den_p_im;
    
    float rp_re = (num_p_re * den_p_re + num_p_im * den_p_im) / den_p_sq;
    float rp_im = (num_p_im * den_p_re - num_p_re * den_p_im) / den_p_sq;
    
    // Rs = |rs|^2, Rp = |rp|^2
    float Rs = rs_re * rs_re + rs_im * rs_im;
    float Rp = rp_re * rp_re + rp_im * rp_im;
    
    // Amplitude coefficients (moduli)
    float rs_abs = sqrtf(Rs);
    float rp_abs = sqrtf(Rp);
    
    // Phases
    float phi_s = atan2f(rs_im, rs_re);
    float phi_p = atan2f(rp_im, rp_re);
    float delta = phi_s - phi_p;
    
    // Construct Mueller Matrix
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

__device__ inline float conductor_fresnel_reflectance(float n, float k, float cos_theta) {
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
    
    return 0.5f * (Rs + Rp);
}

__device__ inline void apply_mueller_transmission_dielectric(StokesVector& s, float ts, float tp, float eta_rel) {
    // Mueller matrix for transmission
    // ts, tp are amplitude transmission coefficients
    // eta_rel is n_t / n_i (relative IOR)
    
    // Radiance scaling factor: (n_t / n_i) * (cos_t / cos_i) is already in ts, tp for amplitude?
    // Actually, Ts = ts^2 * (n_t * cos_t) / (n_i * cos_i)
    // For Stokes vector representing radiance, we need to be careful.
    
    float Ts = ts * ts * eta_rel; // Simplified scaling for radiance conservation
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
    
    // Physical thin-film interference (Air-Film-Medium)
    // r12 is the reflectance (R = |r|^2) of the first interface
    
    float sin_theta = sqrtf(fmaxf(0.0f, 1.0f - cos_theta * cos_theta));
    float cos_theta_film = sqrtf(fmaxf(0.0f, 1.0f - (sin_theta / ior_film) * (sin_theta / ior_film)));
    
    // Optical path difference
    float opd = 2.0f * ior_film * thickness * cos_theta_film;
    
    // Phase shift (in radians)
    float phase = (2.0f * 3.14159265f * opd) / wavelength;
    
    // For a dielectric film, we approximate the total reflectance R_tf
    // R_tf = (r1 + r2)^2 / (1 + r1*r2)^2  -- roughly
    // A better approximation for interference factor:
    // The factor is |1 + r_rel * e^(-i*phase)|^2 where r_rel is r23/r12
    // For soap bubble, r_rel = -1 (phase shift of pi at first interface)
    // R = r12 * |1 - e^(-i*phase)|^2 = r12 * (2 - 2*cos(phase))
    
    // We'll use a slightly more general form that allows for "rainbow" colors
    // and stays within [0, 1] if r12 is small.
    float interference = 0.5f - 0.5f * cosf(phase);
    float R_tf = 4.0f * r12 * interference; 
    
    return fminf(0.99f, fmaxf(0.0f, R_tf));
}

__device__ inline float get_dielectric_thin_film_reflectance(
    float wavelength, float thickness, float ior_film, 
    float ior_incident, float ior_substrate, float cos_theta_i
) {
    // Snell's Law for Film Angle
    float sin2_i = fmaxf(0.0f, 1.0f - cos_theta_i * cos_theta_i);
    float sin_i = sqrtf(sin2_i);
    float sin_t = (ior_incident / ior_film) * sin_i;
    
    if (sin_t >= 1.0f) return 1.0f; // TIR at Incident-Film interface
    
    float cos_t = sqrtf(fmaxf(0.0f, 1.0f - sin_t * sin_t));
    
    // Snell's Law for Substrate Angle (Medium 3)
    float sin_s = (ior_film / ior_substrate) * sin_t;
    bool tir_substrate = (sin_s >= 1.0f);
    float cos_s = tir_substrate ? 0.0f : sqrtf(fmaxf(0.0f, 1.0f - sin_s * sin_s));
    
    // Amplitude Reflection Coefficients (Fresnel)
    // Interface 1: Incident -> Film
    float num1_s = ior_incident * cos_theta_i - ior_film * cos_t;
    float den1_s = ior_incident * cos_theta_i + ior_film * cos_t;
    float r1_s = num1_s / den1_s;
    
    float num1_p = ior_film * cos_theta_i - ior_incident * cos_t;
    float den1_p = ior_film * cos_theta_i + ior_incident * cos_t;
    float r1_p = num1_p / den1_p;
    
    // Interface 2: Film -> Substrate
    float r2_s = 1.0f, r2_p = 1.0f;
    if (!tir_substrate) {
        float num2_s = ior_film * cos_t - ior_substrate * cos_s;
        float den2_s = ior_film * cos_t + ior_substrate * cos_s;
        r2_s = num2_s / den2_s;
        
        float num2_p = ior_substrate * cos_t - ior_film * cos_s;
        float den2_p = ior_substrate * cos_t + ior_film * cos_s;
        r2_p = num2_p / den2_p;
    }
    
    // Phase Shift (Round trip in film)
    // OPD = 2 * n_film * d * cos_t
    float opd = 2.0f * ior_film * thickness * cos_t;
    float phi = (2.0f * 3.14159265f * opd) / wavelength;
    float cos_phi = cosf(phi);
    
    // Airy Summation (Intensity) for infinite reflections
    // R = | (r1 + r2 * e^-i*phi) / (1 + r1 * r2 * e^-i*phi) |^2
    // R = (r1^2 + r2^2 + 2r1r2cos(phi)) / (1 + r1^2r2^2 + 2r1r2cos(phi))
    
    float num_s = r1_s*r1_s + r2_s*r2_s + 2.0f*r1_s*r2_s*cos_phi;
    float den_s = 1.0f + r1_s*r1_s*r2_s*r2_s + 2.0f*r1_s*r2_s*cos_phi;
    float R_s = num_s / den_s;
    
    float num_p = r1_p*r1_p + r2_p*r2_p + 2.0f*r1_p*r2_p*cos_phi;
    float den_p = 1.0f + r1_p*r1_p*r2_p*r2_p + 2.0f*r1_p*r2_p*cos_phi;
    float R_p = num_p / den_p;
    
    // Return average unpolarized reflectance
    return 0.5f * (R_s + R_p);
}

__device__ bool scatter(
    const GpuRay& r_in, const GpuMaterial& mat, const GpuVec3& p, const GpuVec3& n, const GpuVec2& uv,
    const GpuSpectrum& current_throughput,
    GpuSpectrum& attenuation, GpuRay& scattered, StokesVector& stokes, unsigned int& seed,
    float dispersion_clamp,
    int sample_index,
    int pixel_index,
    int depth,
    int& spectral_channel // 0: None, 1: R, 2: G, 3: B
) {
    // LDS Sampling
    // Dimensions reserved for BSDF: 0, 1, 2 offset by depth
    int dim_offset = 4 + depth * 6;
    float r_bsdf_1 = sample_dimension(sample_index, pixel_index, dim_offset + 0);
    float r_bsdf_2 = sample_dimension(sample_index, pixel_index, dim_offset + 1);
    float r_bsdf_3 = sample_dimension(sample_index, pixel_index, dim_offset + 2);

    // Modulate thin film thickness based on UVs to simulate gravity/irregularity (Color Bands)
    float effective_thickness = mat.thin_film_thickness;
    if (effective_thickness > 0.0f) {
        // Simple gravity simulation: Thickness varies with V coordinate
        // Assuming V maps to vertical height in the scene (after rotation)
        // We create a variation from 1.5x (bottom) to 0.5x (top) 
        effective_thickness = effective_thickness * (1.5f - 1.0f * uv.v);
    }

    // Initialize interaction normal for consistent reference frame rotation
    // Removed: interaction_normal was causing issues with Metal reflection.
    // GpuVec3 interaction_normal = n;

    if (mat.type == MaterialType::Lambertian) {
        GpuVec3 scatter_direction = n + sample_unit_vector_lds(r_bsdf_1, r_bsdf_2);
        
        // Catch degenerate scatter direction (near zero)
        if (scatter_direction.length_sq() < 1e-16f)
            scatter_direction = n;
        
        scattered.direction = scatter_direction.normalize(); // Normalize for consistent t
        
        // Offset along normal. For Lambertian, always scatter OUT.
        // Use Robust Offset: push in direction of scatter to prevent self-intersection
        GpuVec3 offset = (scattered.direction.dot(n) > 0.0f) ? n : -n;
        scattered.origin = p + offset * 1e-3f; 
        
        scattered.t_min = 1e-3f; 
        scattered.t_max = FLT_MAX;
        attenuation = mat.albedo;
        
        // Phase 3: Lambertian reflection depolarizes light
        // I remains (handled by attenuation), Q=U=V=0
        stokes.Q = 0.0f;
        stokes.U = 0.0f;
        stokes.V = 0.0f;
        
        return true;
    } else if (mat.type == MaterialType::Metal) {
        GpuVec3 V = (-r_in.direction).normalize();
        GpuVec3 N = n;
        if (V.dot(N) < 0.0f) N = -N; // Ensure Normal faces View

        // GGX Importance Sampling (LDS)
        float r1 = r_bsdf_1;
        float r2 = r_bsdf_2;
        
        GpuVec3 H = ImportanceSampleGGXVisible(r1, r2, V, N, mat.roughness);
        // interaction_normal = H; // Removed
        GpuVec3 L = reflect(-V, H);
        
        scattered.direction = L.normalize();
        
        // Spectral Wavelength Selection for Metal (Phase 2 & 3 Integration)
        // Deterministic Stratified Sampling for Dispersion to reduce color noise
        // We use the 'channel' to determine the reference wavelength for polarization (Stokes),
        // but we calculate attenuation for ALL channels to prevent color noise.
        int channel = sample_index % 3;
        
        float n_val = mat.ior;
        
        // Calculate Extinction for all channels
        float k_r = mat.extinction.values.x;
        float k_g = mat.extinction.values.y;
        float k_b = mat.extinction.values.z;
        
        // Use the selected channel for Stokes/Geometric interactions
        float k_val = (channel == 0) ? k_r : ((channel == 1) ? k_g : k_b);
        
        // Phase 3: Polarization for Metal (Conductor)
        // Treat as reflection off microfacet H
        GpuVec3 ref_in = get_reference_frame(r_in.direction);
        
        // Fix Singularity: Check length before normalization
        GpuVec3 raw_s = r_in.direction.cross(H);
        float raw_len_sq = raw_s.length_sq();
        GpuVec3 s_axis;
        
        if (raw_len_sq < 1e-12f) {
            s_axis = get_reference_frame(H);
        } else {
            s_axis = raw_s * (1.0f / sqrtf(raw_len_sq));
        }

        // Rotate In
        float cos_phi_in = 1.0f;
        float sin_phi_in = 0.0f;
        
        // Robust check for singular s_axis (Parallel View/Normal)
        // If s_axis length is effectively zero, we are at normal incidence or singularity.
        // Rotation is undefined but also irrelevant as Rs=Rp.
        if (s_axis.length_sq() > 1e-6f) {
            cos_phi_in = ref_in.dot(s_axis);
            sin_phi_in = ref_in.cross(s_axis).dot(r_in.direction);
        }
        
        float phi_in = atan2f(sin_phi_in, cos_phi_in);
        rotate_stokes(stokes, 2.0f * phi_in);
        
        // Apply Physical Conductor Mueller Matrix
        float cos_theta_h = fmaxf(0.0f, V.dot(H));
        
        bool use_albedo_fresnel = (k_r * k_r + k_g * k_g + k_b * k_b) < 1e-8f;
        if (!use_albedo_fresnel) {
            apply_mueller_reflection_conductor(stokes, n_val, k_val, cos_theta_h);
        }
        
        float fresnel_r;
        float fresnel_g;
        float fresnel_b;
        if (use_albedo_fresnel) {
            float f0_r = fminf(0.999f, fmaxf(0.0f, mat.albedo.values.x));
            float f0_g = fminf(0.999f, fmaxf(0.0f, mat.albedo.values.y));
            float f0_b = fminf(0.999f, fmaxf(0.0f, mat.albedo.values.z));
            float one_minus = powf(1.0f - cos_theta_h, 5.0f);
            fresnel_r = f0_r + (1.0f - f0_r) * one_minus;
            fresnel_g = f0_g + (1.0f - f0_g) * one_minus;
            fresnel_b = f0_b + (1.0f - f0_b) * one_minus;
        } else {
            fresnel_r = conductor_fresnel_reflectance(n_val, k_r, cos_theta_h);
            fresnel_g = conductor_fresnel_reflectance(n_val, k_g, cos_theta_h);
            fresnel_b = conductor_fresnel_reflectance(n_val, k_b, cos_theta_h);
        }
        float NdotV = N.dot(V);
        float NdotL = N.dot(scattered.direction);
        float NdotH = N.dot(H);
        float VdotH = V.dot(H);
        
        if (NdotL <= 0.0f || NdotV <= 0.0f || NdotH <= 0.0f || VdotH <= 0.0f) {
            return false;
        }
        
        NdotV = fmaxf(1e-6f, NdotV);
        NdotL = fmaxf(1e-6f, NdotL);
        NdotH = fmaxf(1e-6f, NdotH);
        VdotH = fmaxf(1e-6f, VdotH);
        
        float rough = fmaxf(0.001f, mat.roughness);
        float k = (rough + 1.0f);
        k = (k * k) * 0.125f;
        
        float G1_L = smith_G1(NdotL, k);
        float microfacet_weight = (G1_L * VdotH) / fmaxf(1e-6f, NdotH * NdotV);
        
        float base_r = use_albedo_fresnel ? 1.0f : mat.albedo.values.x;
        float base_g = use_albedo_fresnel ? 1.0f : mat.albedo.values.y;
        float base_b = use_albedo_fresnel ? 1.0f : mat.albedo.values.z;
        
        float tf_boost = 1.0f;
        // Thin-film interference for metal
        if (effective_thickness > 0.0f) {
            // Calculate thin film for all channels
            float r_base_r = mat.albedo.values.x;
            float r_base_g = mat.albedo.values.y;
            float r_base_b = mat.albedo.values.z;
            
            float R_tf_r = get_thin_film_interference(650.0f, effective_thickness, mat.thin_film_ior, cos_theta_h, r_base_r);
            float R_tf_g = get_thin_film_interference(550.0f, effective_thickness, mat.thin_film_ior, cos_theta_h, r_base_g);
            float R_tf_b = get_thin_film_interference(450.0f, effective_thickness, mat.thin_film_ior, cos_theta_h, r_base_b);

            // Calculate per-channel boost
            float boost_r = R_tf_r / fmaxf(1e-6f, r_base_r);
            float boost_g = R_tf_g / fmaxf(1e-6f, r_base_g);
            float boost_b = R_tf_b / fmaxf(1e-6f, r_base_b);
            
            // Use average boost for Stokes intensity modulation (approximation)
            tf_boost = (boost_r + boost_g + boost_b) / 3.0f;
            
            stokes.I *= tf_boost;
            stokes.Q *= tf_boost;
            stokes.U *= tf_boost;
            stokes.V *= tf_boost;
            
            // Update albedo with thin film effect directly
            attenuation.values.x = base_r * boost_r * fresnel_r * microfacet_weight;
            attenuation.values.y = base_g * boost_g * fresnel_g * microfacet_weight;
            attenuation.values.z = base_b * boost_b * fresnel_b * microfacet_weight;
        } else {
            // Standard Metal
            attenuation.values.x = base_r * fresnel_r * microfacet_weight;
            attenuation.values.y = base_g * fresnel_g * microfacet_weight;
            attenuation.values.z = base_b * fresnel_b * microfacet_weight;
        }

        // Phase 3: Rotate Stokes to Outgoing Reference Frame (Missing Logic Fixed)
        // Use H (Microfacet Normal) for consistent reference frame with input
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
        
        // Final Setup for Metal
        // Use Robust Offset based on scatter direction and geometric normal N (not H)
        GpuVec3 offset = (scattered.direction.dot(N) > 0.0f) ? N : -N;
        scattered.origin = p + offset * 1e-3f; 
        scattered.t_min = 1e-3f;
        scattered.t_max = FLT_MAX;

        // Remove channel masking - return full RGB
        return (scattered.direction.dot(N) > 0);
    } else if (mat.type == MaterialType::Dielectric) {
        // Default to white for reflection/base
        attenuation = GpuSpectrum::from_rgb(GpuVec3(1.0f, 1.0f, 1.0f));
        float refraction_ratio = mat.ior; 

        // Dispersion and Thin-Film Wavelength Sampling Logic
        if (mat.dispersion > 0.0f || mat.thin_film_thickness > 0.0f) {
            // Deterministic Stratified Sampling
            // If we are already in a spectral channel, reuse it to prevent energy loss
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

            // Apply Dispersion if requested (affects Geometry/IOR)
            if (mat.dispersion > 0.0f) {
                float inv_lambda2 = 1.0f / (lambda * lambda);
                float inv_ref2 = 1.0f / (550.0f * 550.0f);
                float offset = (inv_lambda2 - inv_ref2) * 4e5f;
                refraction_ratio = mat.ior + mat.dispersion * offset;
                if (refraction_ratio < 1.01f) refraction_ratio = 1.01f;
            }

            // Mask attenuation and boost
            float b_val = 1.0f;
            if (b_val > dispersion_clamp) b_val = dispersion_clamp; 
            
            attenuation = GpuSpectrum(b_val);
        }

        bool front_face = r_in.direction.dot(n) < 0;
        GpuVec3 normal = front_face ? n : -n;

        // Micro-jitter normal to smooth out singular caustics (anti-firefly for perfect dielectrics)
        if (mat.type == MaterialType::Dielectric) {
            float jitter_scale = mat.roughness * 0.002f;
            if (jitter_scale > 0.0f) {
                GpuVec3 jitter = sample_unit_vector_lds(r_bsdf_1, r_bsdf_2) * jitter_scale; 
                normal = (normal + jitter).normalize();
            }
        }

        // Phase 3: Polarization-aware Dielectric Scattering
        
        // 1. Setup Incident Reference Frame Rotation
        GpuVec3 ref_in = get_reference_frame(r_in.direction);
        
        // Fix Singularity: Check length before normalization
        GpuVec3 raw_s = r_in.direction.cross(normal);
        float raw_len_sq = raw_s.length_sq();
        GpuVec3 s_axis;
        
        if (raw_len_sq < 1e-12f) {
            s_axis = get_reference_frame(normal);
        } else {
            s_axis = raw_s * (1.0f / sqrtf(raw_len_sq));
        }

        // Calculate rotation angle phi_in (from ref_in to s_axis)
        float cos_phi_in = ref_in.dot(s_axis);
        float sin_phi_in = ref_in.cross(s_axis).dot(r_in.direction);
        float phi_in = atan2f(sin_phi_in, cos_phi_in);
        
        // Rotate incoming Stokes to Incident Plane Frame
        rotate_stokes(stokes, 2.0f * phi_in);

        // 2. Calculate Fresnel Coefficients
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
            // Fresnel Amplitude Coefficients
            float n1c1 = eta_i * cos_theta_i;
            float n2c2 = eta_t * cos_theta_t;
            float n2c1 = eta_t * cos_theta_i;
            float n1c2 = eta_i * cos_theta_t;
            
            rs = (n1c1 - n2c2) / (n1c1 + n2c2);
            rp = (n2c1 - n1c2) / (n2c1 + n1c2);
            
            ts = (2.0f * n1c1) / (n1c1 + n2c2);
            tp = (2.0f * n1c1) / (n2c1 + n1c2);
        }

        // 3. Calculate Reflectance/Transmittance Probability based on Polarization
        float Is = 0.5f * (stokes.I - stokes.Q);
        float Ip = 0.5f * (stokes.I + stokes.Q);
        
        float Rs = rs * rs;
        float Rp = rp * rp;
        
        // Effective Reflectance (Probability)
        float reflect_prob = (Rs * Is + Rp * Ip) / (stokes.I + 1e-6f);
        if (is_tir) reflect_prob = 1.0f;

        // Thin-film interference for dielectric
        GpuVec3 R_spectral(1.0f, 1.0f, 1.0f); 
        GpuVec3 T_spectral(1.0f, 1.0f, 1.0f);
        bool has_thin_film = (!is_tir && effective_thickness > 0.0f);

        if (has_thin_film) {
            float R_r = get_dielectric_thin_film_reflectance(650.0f, effective_thickness, mat.thin_film_ior, eta_i, eta_t, cos_theta_i);
            float R_g = get_dielectric_thin_film_reflectance(550.0f, effective_thickness, mat.thin_film_ior, eta_i, eta_t, cos_theta_i);
            float R_b = get_dielectric_thin_film_reflectance(450.0f, effective_thickness, mat.thin_film_ior, eta_i, eta_t, cos_theta_i);
            
            R_spectral = GpuVec3(R_r, R_g, R_b);
            T_spectral = GpuVec3(1.0f - R_r, 1.0f - R_g, 1.0f - R_b);
            
            // Fix: Use Average Reflectance for Sampling Probability (Deterministic)
            // This reduces variance compared to random channel selection
            reflect_prob = (R_r + R_g + R_b) / 3.0f;
        }

        // 4. Sample and Update (LDS)
        GpuVec3 out_direction;
        float delta = 0.0f;

        if (r_bsdf_3 < reflect_prob) {
            // Reflection (White for Dielectric)
            out_direction = reflect(unit_direction, normal);
            
            if (is_tir) {
                // ... (no change to TIR logic)
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
            
            // Apply Spectral Modulation for Thin Film
            if (has_thin_film) {
                attenuation.values.x *= R_spectral.x;
                attenuation.values.y *= R_spectral.y;
                attenuation.values.z *= R_spectral.z;
            } else {
                // Apply Fresnel Reflectance (Energy Conservation)
                attenuation = attenuation * reflect_prob;
            }

            // Standard importance sampling normalization
            float pdf = fmaxf(1e-6f, reflect_prob);
            stokes = stokes * (1.0f / pdf);
            attenuation = attenuation * (1.0f / pdf);
        } else {
            // Refraction
            GpuSpectrum transmission_color = mat.albedo;
            
            if (has_thin_film) {
                 transmission_color.values.x *= T_spectral.x;
                 transmission_color.values.y *= T_spectral.y;
                 transmission_color.values.z *= T_spectral.z;
            } else {
                // Standard Dielectric Transmission
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

            // Normalize by transmit_prob
            float pdf = fmaxf(1e-6f, transmit_prob);
            stokes = stokes * (1.0f / pdf);
            attenuation = attenuation * (1.0f / pdf);
        }

        // 5. Rotate to Outgoing Reference Frame
        scattered.direction = out_direction.normalize();
        
        GpuVec3 ref_out = get_reference_frame(scattered.direction);
        
        // Fix Singularity: Check length before normalization
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
        
        // Rotate from Plane Frame to Output Frame
        rotate_stokes(stokes, 2.0f * phi_out);

        // Final Setup
        // Use Robust Offset based on scatter direction and original normal 'n'
        // This handles both Reflection (same side) and Refraction (opposite side) correctly
        GpuVec3 offset = (scattered.direction.dot(n) > 0.0f) ? n : -n;
        scattered.origin = p + offset * 1e-3f; 
        scattered.t_min = 1e-3f;
        scattered.t_max = FLT_MAX;
        
        return true;
    } else if (mat.type == MaterialType::Cloth) {
        // Procedural Weave Pattern
        // Use world position to modulate albedo
        float freq = 20.0f; // Lower frequency for visible texture (was 100.0f)
        float noise = sinf(p.x * freq) * sinf(p.z * freq);
        
        // Map [-1, 1] to [0.5, 1.0] for high contrast visibility
        float intensity = 0.75f + noise * 0.25f;
        
        attenuation = mat.albedo * intensity;
        
        // Treat as Lambertian scattering (LDS)
        GpuVec3 scatter_direction = n + sample_unit_vector_lds(r_bsdf_1, r_bsdf_2);
        if (scatter_direction.length_sq() < 1e-16f) scatter_direction = n;
        
        scattered.direction = scatter_direction.normalize();
        
        // Use Robust Offset
        GpuVec3 offset = (scattered.direction.dot(n) > 0.0f) ? n : -n;
        scattered.origin = p + offset * 1e-3f;
        scattered.t_min = 1e-3f;
        scattered.t_max = FLT_MAX;
        return true;
    }
    return false;
}

__device__ GpuVec3 path_trace(GpuRay& r, GpuScene scene, unsigned int& seed, int sample_index, int pixel_index) {
    // Initialize Throughput with Full Spectral Weight (Deterministic)
    // We trace one path (Hero Wavelength driven) but accumulate contribution for all RGB channels.
    // This eliminates color noise at the cost of slight spectral blurring (biased but consistent).
    GpuSpectrum accumulated_color = GpuSpectrum(1.0f);
    
    GpuSpectrum final_color = GpuSpectrum::from_rgb(GpuVec3(0.0f, 0.0f, 0.0f));
    
    // Phase 3: Polarization tracking
    StokesVector current_stokes(1.0f, 0.0f, 0.0f, 0.0f); // Start unpolarized
    
    int spectral_channel = 0; // 0: None, 1: R, 2: G, 3: B

    // Phase 3: Volume / SSS Tracking
    // -1: Global Medium (Air/Vacuum)
    // >=0: Inside Material (SSS)
    int current_medium_mat_idx = -1;

    int depth = 0;
    // Increase max_depth to prevent black artifacts in dielectrics (TIR trapping)
    int max_depth = 50; 
    
    while (depth < max_depth) {
        float t;
        GpuVec3 p, n, ng;
        GpuVec2 uv;
        int mat_idx;
        
        // Use a consistent epsilon for primary and secondary rays
        float current_t_min = (depth == 0) ? 1e-3f : r.t_min;
        
        // 1. Determine active medium properties
        float density_scale = 0.0f;
        GpuSpectrum sigma_s = GpuSpectrum(0.0f);
        GpuSpectrum sigma_a = GpuSpectrum(0.0f);
        
        if (current_medium_mat_idx == -1) {
            // Global Medium
            if (scene.medium_density > 0.0f) {
                density_scale = scene.medium_density;
                sigma_s = scene.medium_scattering * density_scale;
                sigma_a = scene.medium_absorption * density_scale;
            }
        } else {
            // Inside SSS Object
            GpuMaterial mat = scene.materials[current_medium_mat_idx];
            if (mat.medium_density > 0.0f) {
                density_scale = mat.medium_density;
                sigma_s = mat.medium_scattering * density_scale;
                sigma_a = mat.medium_absorption * density_scale;
            }
        }

        GpuSpectrum sigma_t = sigma_s + sigma_a;
        float max_sigma_t = fmaxf(sigma_t.values.x, fmaxf(sigma_t.values.y, sigma_t.values.z));

        // 2. Intersect Scene (Geometry)
        bool hit_surface = world_hit(scene, r, current_t_min, FLT_MAX, t, p, n, ng, uv, mat_idx);
        float hit_distance = hit_surface ? t : FLT_MAX;

        // 3. Sample Volume Interaction
        if (max_sigma_t > 1e-6f) {
            // Sample distance using the majorant (max_sigma_t)
            // PDF = max_sigma_t * exp(-max_sigma_t * s)
            float scatter_dist = -logf(rand_float(seed)) / max_sigma_t;
            
            if (scatter_dist < hit_distance) {
                // Volume Scatter Event
                
                // Throughput update:
                // Weight = (sigma_s * Transmittance_physical) / PDF_sampling
                // Weight = (sigma_s * exp(-sigma_t * s)) / (max_sigma_t * exp(-max_sigma_t * s))
                // Weight = (sigma_s / max_sigma_t) * exp((max_sigma_t - sigma_t) * s)
                
                GpuSpectrum weight_term = (sigma_s * (1.0f / max_sigma_t));
                GpuSpectrum trans_correction;
                trans_correction.values.x = expf((max_sigma_t - sigma_t.values.x) * scatter_dist);
                trans_correction.values.y = expf((max_sigma_t - sigma_t.values.y) * scatter_dist);
                trans_correction.values.z = expf((max_sigma_t - sigma_t.values.z) * scatter_dist);
                
                accumulated_color = accumulated_color * weight_term * trans_correction;
                
                // Update Ray
                r.origin = r.origin + r.direction * scatter_dist;
                r.direction = random_in_unit_sphere(seed); // Isotropic Phase Function
                
                // Depolarize in volume (multiple scattering)
                current_stokes = StokesVector(1.0f, 0.0f, 0.0f, 0.0f); 

                depth++;
                
                // Russian Roulette for Volume
                if (depth > 12) {
                     GpuVec3 rgb = accumulated_color.to_rgb();
                     float max_comp = fmaxf(rgb.x, fmaxf(rgb.y, rgb.z));
                     float probability = fminf(fmaxf(max_comp, 0.1f), 0.99f);
                     if (rand_float(seed) > probability) break;
                     accumulated_color = accumulated_color * (1.0f / probability);
                }

                continue; // Skip Surface Logic
            } else {
                // Surface Hit (Pass through volume)
                // We sampled a distance BEYOND the surface, so we treat this as no scatter.
                // Weight = Transmittance_physical(hit_dist) / Prob_no_scatter(hit_dist)
                // Prob_no_scatter(d) = P(s > d) = exp(-max_sigma_t * d)
                // Weight = exp(-sigma_t * d) / exp(-max_sigma_t * d)
                // Weight = exp((max_sigma_t - sigma_t) * d)
                
                GpuSpectrum trans_correction;
                trans_correction.values.x = expf((max_sigma_t - sigma_t.values.x) * hit_distance);
                trans_correction.values.y = expf((max_sigma_t - sigma_t.values.y) * hit_distance);
                trans_correction.values.z = expf((max_sigma_t - sigma_t.values.z) * hit_distance);
                
                accumulated_color = accumulated_color * trans_correction;
            }
        }

        if (hit_surface) {
            GpuMaterial mat = scene.materials[mat_idx];
            
            // Add emission from the material we just hit
            GpuSpectrum emitted = mat.emission;
            
            // Indirect Clamping (Firefly Removal) - Apply to Contribution, not Throughput
            GpuSpectrum contribution = accumulated_color * emitted;
            
            // Allow higher dynamic range for primary hit, clamp more aggressively for indirect
            float max_radiance = (depth == 0) ? 1000.0f : 20.0f; 
            
            contribution.values.x = fminf(contribution.values.x, max_radiance);
            contribution.values.y = fminf(contribution.values.y, max_radiance);
            contribution.values.z = fminf(contribution.values.z, max_radiance);
            
            final_color = final_color + contribution;

            GpuRay scattered;
            GpuSpectrum attenuation;
            
            // Pass default dispersion clamp (10.0f) for megakernel path
            // Reduced from 20.0f to reduce fireflies in caustics
            if (scatter(r, mat, p, n, uv, accumulated_color, attenuation, scattered, current_stokes, seed, 10.0f, sample_index, pixel_index, depth, spectral_channel)) {
                accumulated_color = accumulated_color * attenuation;
                
                // Track Medium Enter/Exit for SSS
                if (mat.type == MaterialType::Dielectric) {
                    float in_dot = r.direction.dot(ng);
                    float out_dot = scattered.direction.dot(ng);
                    
                    // If signs match, we transmitted through the surface (Refraction)
                    if ((in_dot * out_dot) > 0.0f) {
                        if (in_dot < 0.0f) {
                            // Entering
                            current_medium_mat_idx = mat_idx;
                        } else {
                            // Exiting
                            current_medium_mat_idx = -1;
                        }
                    }
                    // Else Reflected: Medium stays the same
                }

                // Robust NaN check (checking first value as proxy)
                if (accumulated_color.values.x != accumulated_color.values.x) {
                    accumulated_color = GpuSpectrum::from_rgb(GpuVec3(0,0,0));
                    break; 
                }
                
                r = scattered;
                depth++;

                // Russian Roulette
                // Delay RR for deep paths to allow dielectrics to escape
                if (depth > 12) {
                    GpuVec3 rgb = accumulated_color.to_rgb();
                    float max_comp = fmaxf(rgb.x, fmaxf(rgb.y, rgb.z));
                    // Clamp probability to avoid terminating bright paths too aggressively or infinite loops
                    // Also clamp minimum probability to prevent massive weight boosts (fireflies) for dark paths
                    float probability = fminf(fmaxf(max_comp, 0.1f), 0.99f); 
                    
                    if (rand_float(seed) > probability) {
                        break;
                    }
                    
                    // Compensation
                    accumulated_color = accumulated_color * (1.0f / probability);
                }

            } else {
                // Hit a light or non-scattering material, stop.
                break;
            }
        } else {
            // Missed everything (Sky)
            // Tilted parallel light (Sun) + Ambient
            GpuVec3 unit_direction = r.direction.normalize();
            
            // Sun direction (tilted, from top-right-back)
            // Using (1, 1, 1) normalized -> (0.577, 0.577, 0.577)
            GpuVec3 sun_dir = GpuVec3(1.0f, 1.0f, 1.0f).normalize();
            
            // Check if ray is pointing at the sun
            float sun_focus = 0.99f; 
            float sun_intensity = 30.0f; // Bright sun (adjusted for larger area)
            
            float alignment = unit_direction.dot(sun_dir);
            
            if (alignment > sun_focus) {
                // Hit the sun
                final_color = final_color + accumulated_color * GpuSpectrum::from_rgb(GpuVec3(sun_intensity, sun_intensity, sun_intensity));
            } else {
                // Sky Ambient (Blue-ish gradient)
                // Use a darker sky to emphasize the sun and point light
                float t_sky = 0.5f * (unit_direction.y + 1.0f);
                GpuVec3 sky_color = (1.0f - t_sky) * GpuVec3(0.05f, 0.05f, 0.05f) + t_sky * GpuVec3(0.2f, 0.2f, 0.4f);
                final_color = final_color + accumulated_color * GpuSpectrum::from_rgb(sky_color);
            }
            break;
        }
    }
    
    if (depth == max_depth) return GpuVec3(0,0,0);
    
    return final_color.to_rgb();
}

// ==========================================
// Wavefront Kernels
// ==========================================

__global__ void generate_rays_kernel(
    RayQueue queue,
    int width,
    int height,
    GpuCamera camera,
    int sample_index,
    int* sample_counts
) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    int j = blockIdx.y * blockDim.y + threadIdx.y;
    
    if (i >= width || j >= height) return;
    
    int pixel_index = j * width + i;
    int ray_index = pixel_index; 
    
    if (sample_counts) {
        sample_counts[pixel_index] += 1;
    }
    
    // Initialize Seed using Wang Hash
    // Combine sample_index and pixel_index
    unsigned int seed = wang_hash(1984 + pixel_index + sample_index * width * height);
    
    // Tent Filter for High Quality Anti-Aliasing (Strict Quality Mode)
    // Uses Halton Sequence (LDS) for faster convergence
    // Dimensions 0 and 1 for Pixel Jitter
    float r1 = sample_dimension(sample_index, pixel_index, 0);
    float r2 = sample_dimension(sample_index, pixel_index, 1);
    
    // Reserve Dimensions 2 and 3 for Lens Sampling (Stratified)
    // Even if camera is pinhole, we reserve these to align with scatter offsets
    // float r3 = sample_dimension(sample_index, pixel_index, 2);
    // float r4 = sample_dimension(sample_index, pixel_index, 3);
    
    float dx = (r1 < 0.5f) ? sqrtf(2.0f * r1) - 1.0f : 1.0f - sqrtf(2.0f * (1.0f - r1));
    float dy = (r2 < 0.5f) ? sqrtf(2.0f * r2) - 1.0f : 1.0f - sqrtf(2.0f * (1.0f - r2));

    float u = (float(i) + 0.5f + dx) / float(width);
    float v = (float(height - 1 - j) + 0.5f + dy) / float(height);
    
    GpuRay r;
    r.origin = camera.origin;
    r.direction = (camera.lower_left_corner + u * camera.horizontal + v * camera.vertical - camera.origin).normalize();
    
    queue.origins[ray_index] = r.origin;
    queue.directions[ray_index] = r.direction;

    // Initialize Throughput with Full Spectral Weight (Deterministic)
    // We trace one path (Hero Wavelength driven) but accumulate contribution for all RGB channels.
    // This eliminates color noise at the cost of slight spectral blurring (biased but consistent).
    GpuSpectrum initial_throughput = GpuSpectrum(1.0f); 
    
    queue.throughputs[ray_index] = initial_throughput;
    queue.stokes[ray_index] = StokesVector(1.0f, 0.0f, 0.0f, 0.0f); // Phase 3: Unpolarized
    queue.medium_indices[ray_index] = -1; // Phase 3: Start in Global Medium
    queue.seeds[ray_index] = seed;
    queue.pixel_indices[ray_index] = pixel_index;
    queue.depths[ray_index] = 0;
    queue.flags[ray_index] = 1; // Treat primary ray as "specular" so we see lights directly
    
    // We set count on host before launch
}

__global__ void extend_kernel(
    RayQueue ray_queue,
    HitQueue hit_queue,
    GpuScene scene
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= *ray_queue.count) return;
    
    GpuRay r;
    r.origin = ray_queue.origins[idx];
    r.direction = ray_queue.directions[idx];
    
    // Consistent epsilon with megakernel
    int depth = ray_queue.depths[idx];
    float t_min = (depth == 0) ? 1e-3f : 1e-3f; 
    
    float t;
    GpuVec3 p, n, ng;
    GpuVec2 uv;
    int mat_idx;
    
    if (world_hit(scene, r, t_min, FLT_MAX, t, p, n, ng, uv, mat_idx)) {
        hit_queue.t[idx] = t;
        hit_queue.p[idx] = p;
        hit_queue.n[idx] = n;
        hit_queue.ng[idx] = ng; 
        hit_queue.uv[idx] = uv;
        hit_queue.mat_ids[idx] = mat_idx;
    } else {
        hit_queue.mat_ids[idx] = -1; // Miss
    }
}

__device__ GpuSpectrum sample_texture(const GpuScene& scene, int tex_idx, float u, float v) {
    if (tex_idx < 0 || tex_idx >= scene.texture_count) return GpuSpectrum::from_rgb(GpuVec3(1,0,1)); // Error pink
    
    // Pointer arithmetic to get texture
    GpuTexture tex = scene.textures[tex_idx];

    // Hardware Texture Sampling
    if (tex.texObj) {
        float4 val = tex2D<float4>(tex.texObj, u, v);
        return GpuSpectrum::from_rgb(GpuVec3(val.x, val.y, val.z));
    }

    if (!tex.data) return GpuSpectrum::from_rgb(GpuVec3(0,0,0));
    
    // Wrap UVs
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
    
    // Bilinear Interpolation
    // Unified Memory Access here - this "pages in" data if not resident
    GpuSpectrum c00 = tex.data[y0 * tex.width + x0];
    GpuSpectrum c10 = tex.data[y0 * tex.width + x1];
    GpuSpectrum c01 = tex.data[y1 * tex.width + x0];
    GpuSpectrum c11 = tex.data[y1 * tex.width + x1];
    
    GpuSpectrum c0 = c00 * (1.0f - dx) + c10 * dx;
    GpuSpectrum c1 = c01 * (1.0f - dx) + c11 * dx;
    
    return c0 * (1.0f - dy) + c1 * dy;
}

__device__ bool any_hit_bvh(const GpuMesh& mesh, const GpuRay& r, float t_min, float t_max) {
    // Stack for traversal (Fixed size for GPU)
    int stack[64];
    int stack_ptr = 0;
    stack[stack_ptr++] = 0; // Push root node index

    while (stack_ptr > 0) {
        int node_idx = stack[--stack_ptr];
        
        // Fetch node from global memory
        const GpuBvhNode& node = mesh.bvh_nodes[node_idx];

        // Check AABB intersection
        if (!hit_aabb(r, node.min_pt, node.max_pt, t_min, t_max)) {
            continue;
        }

        // Leaf Node Check
        if (node.primitive_count > 0) {
            // Intersect triangles in this leaf
            int start_idx = node.child_or_primitive_index;
            int end_idx = start_idx + node.primitive_count;

            for (int i = start_idx; i < end_idx; ++i) {
                // Fetch triangle indices
                int i0 = mesh.indices[i * 3 + 0];
                int i1 = mesh.indices[i * 3 + 1];
                int i2 = mesh.indices[i * 3 + 2];

                GpuVec3 v0 = mesh.vertices[i0];
                GpuVec3 v1 = mesh.vertices[i1];
                GpuVec3 v2 = mesh.vertices[i2];

                // For any_hit, we don't need normals
                const GpuVec3* n0_ptr = nullptr;
                const GpuVec3* n1_ptr = nullptr;
                const GpuVec3* n2_ptr = nullptr;

                float t_tri;
                GpuVec3 ng_tri, ns_tri;
                float u_tri, v_tri;
                
                // Use a local max for hit_triangle to check against t_max
                float local_max = t_max;

                if (hit_triangle(r, v0, v1, v2, n0_ptr, n1_ptr, n2_ptr, t_min, local_max, t_tri, ng_tri, ns_tri, u_tri, v_tri)) {
                    return true;
                }
            }
        } else {
            // Internal Node
            int left_child = node_idx + 1;
            int right_child = node.child_or_primitive_index;
            
            stack[stack_ptr++] = right_child;
            stack[stack_ptr++] = left_child;
        }
    }
    return false;
}

__device__ bool any_hit(const GpuScene& scene, const GpuRay& r, float t_min, float t_max) {
    // Check Spheres
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

    // Check Meshes
    for (int i = 0; i < scene.mesh_count; ++i) {
        GpuMesh& mesh = scene.meshes[i];
        
        // AABB Check
        if (!hit_aabb(r, mesh.min_pt, mesh.max_pt, t_min, t_max)) {
            continue;
        }

        if (mesh.bvh_node_count > 0) {
            if (any_hit_bvh(mesh, r, t_min, t_max)) return true;
        } else {
            // Linear Fallback
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

// Shadow extension kernel
__global__ void extend_shadow_kernel(
    ShadowQueue shadow_queue,
    GpuVec3* accum_buffer,
    GpuScene scene
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= *shadow_queue.count) return;

    GpuVec3 origin = shadow_queue.origins[idx];
    GpuVec3 direction = shadow_queue.directions[idx];
    float max_dist = shadow_queue.max_dist[idx];
    int pixel_index = shadow_queue.pixel_indices[idx];
    GpuSpectrum radiance = shadow_queue.radiance[idx];

    GpuRay r;
    r.origin = origin;
    r.direction = direction;
    
    float remaining_dist = max_dist;
    for (int pass = 0; pass < 8; ++pass) {
        float t;
        GpuVec3 p, n, ng;
        GpuVec2 uv;
        int mat_idx;
        
        if (!world_hit(scene, r, 1e-3f, remaining_dist - 1e-3f, t, p, n, ng, uv, mat_idx)) {
            break;
        }
        
        GpuMaterial mat = scene.materials[mat_idx];
        GpuVec3 emission_rgb = mat.emission.to_rgb();
        if (emission_rgb.length_sq() > 0.0f) {
            break;
        }
        
        if (mat.type != MaterialType::Dielectric) {
            return;
        }
        
        bool front_face = r.direction.dot(n) < 0.0f;
        GpuVec3 normal = front_face ? n : -n;
        float eta_i = front_face ? 1.0f : mat.ior;
        float eta_t = front_face ? mat.ior : 1.0f;
        float eta = eta_i / eta_t;
        
        float cos_theta = fminf((-r.direction).dot(normal), 1.0f);
        float sin_theta2 = fmaxf(0.0f, 1.0f - cos_theta * cos_theta);
        bool cannot_refract = eta * eta * sin_theta2 > 1.0f;
        
        float r0 = (eta_t - eta_i) / (eta_t + eta_i);
        r0 *= r0;
        float fresnel = r0 + (1.0f - r0) * powf(1.0f - cos_theta, 5.0f);
        float transmission = 1.0f - fresnel;
        
        radiance = radiance * (mat.albedo * transmission);
        GpuVec3 radiance_rgb = radiance.to_rgb();
        if (!isfinite(radiance_rgb.x) || !isfinite(radiance_rgb.y) || !isfinite(radiance_rgb.z)) {
            return;
        }
        
        if (cannot_refract) {
            return;
        }
        
        GpuVec3 refracted;
        refract(r.direction, normal, eta, refracted);
        r.direction = refracted.normalize();
        
        float advance = t + 1e-3f;
        remaining_dist -= advance;
        if (remaining_dist <= 1e-3f) {
            return;
        }
        
        r.origin = p + r.direction * 1e-3f;
    }
    
    // Unoccluded - Add Contribution
    GpuVec3 rgb = radiance.to_rgb();
    
    // Clamping for fireflies (NEE can be bright)
    float max_val = 1000.0f;
    if (rgb.x > max_val) rgb.x = max_val;
    if (rgb.y > max_val) rgb.y = max_val;
    if (rgb.z > max_val) rgb.z = max_val;

    if (isfinite(rgb.x) && isfinite(rgb.y) && isfinite(rgb.z)) {
        atomicAdd(&accum_buffer[pixel_index].x, rgb.x);
        atomicAdd(&accum_buffer[pixel_index].y, rgb.y);
        atomicAdd(&accum_buffer[pixel_index].z, rgb.z);
    }
}

__global__ void shade_kernel(
    RayQueue current_queue,
    HitQueue hit_queue,
    RayQueue next_queue,
    ShadowQueue shadow_queue,
    GpuVec3* accum_buffer,
    GpuVec3* normal_buffer,
    GpuVec3* albedo_buffer,
    GpuScene scene,
    int sample_index,
    float dispersion_clamp,
    float rr_min_prob
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= *current_queue.count) return;
    
    int pixel_index = current_queue.pixel_indices[idx];
    int mat_idx = hit_queue.mat_ids[idx];
    GpuSpectrum throughput = current_queue.throughputs[idx];
    int depth = current_queue.depths[idx];
    unsigned int seed = current_queue.seeds[idx];
    int flag = current_queue.flags[idx];
    
    // Phase 3: Volume / SSS Integration
    int current_medium_idx = current_queue.medium_indices[idx];
    float t_hit = (mat_idx != -1) ? hit_queue.t[idx] : 1e30f;
    
    float density = 0.0f;
    GpuSpectrum sigma_s(0.0f);
    GpuSpectrum sigma_a(0.0f);
    
    if (current_medium_idx == -1) {
        // Global Medium
        density = scene.medium_density;
        sigma_s = scene.medium_scattering;
        sigma_a = scene.medium_absorption;
    } else {
        // Material Medium (SSS)
        GpuMaterial med_mat = scene.materials[current_medium_idx];
        density = med_mat.medium_density;
        sigma_s = med_mat.medium_scattering;
        sigma_a = med_mat.medium_absorption;
    }
    
    // Simple monochromatic approximation for distance sampling
    GpuSpectrum sigma_t = (sigma_s + sigma_a) * density;
    float sigma_t_avg = (sigma_t.values.x + sigma_t.values.y + sigma_t.values.z) / 3.0f;
    
    bool volume_event = false;
    
    if (sigma_t_avg > 1e-4f) {
        float r_dist = rand_float(seed);
        float t_medium = -logf(1.0f - r_dist) / sigma_t_avg;
        
        if (t_medium < t_hit) {
            volume_event = true;
            
            // Transmittance & PDF Weight
            // Weight = (sigma_s / sigma_t) * Phase(w) / PDF(w)
            // Isotropic: Phase = 1/4pi, PDF = 1/4pi -> Cancel out.
            // Result: Throughput *= sigma_s / sigma_t (Albedo)
            
            // Chromatic handling:
            // Tr = exp(-sigma_t * t)
            // pdf_t = sigma_t_avg * exp(-sigma_t_avg * t)
            // Weight = Tr / pdf_t
            
            GpuVec3 tr_vals;
            tr_vals.x = expf(-sigma_t.values.x * t_medium);
            tr_vals.y = expf(-sigma_t.values.y * t_medium);
            tr_vals.z = expf(-sigma_t.values.z * t_medium);
            
            float pdf_t = sigma_t_avg * expf(-sigma_t_avg * t_medium);
            
            GpuSpectrum tr_spectrum = GpuSpectrum::from_rgb(tr_vals);
            // throughput = throughput * tr_spectrum * (1.0f / pdf_t) * sigma_s * density; // Wait, sigma_s is per unit distance, need density?
            // sigma_s struct already includes density? No, multiplied above.
            
            // Correct Logic:
            // sigma_s_eff = sigma_s * density
            // sigma_t_eff = sigma_t * density
            // Throughput *= (Tr * sigma_s_eff) / pdf_t
            
            GpuSpectrum sigma_s_eff = sigma_s * density;
            throughput = throughput * tr_spectrum * sigma_s_eff * (1.0f / pdf_t);
            
            // Scatter Direction (Isotropic)
            GpuVec3 new_dir = random_unit_vector(seed);
            GpuVec3 new_origin = current_queue.origins[idx] + current_queue.directions[idx] * t_medium;
            
            // Enqueue
             int out_idx = atomicAdd(next_queue.count, 1);
             if (out_idx < next_queue.capacity) {
                next_queue.origins[out_idx] = new_origin;
                next_queue.directions[out_idx] = new_dir;
                next_queue.throughputs[out_idx] = throughput;
                next_queue.stokes[out_idx] = current_queue.stokes[idx]; // Unchanged for isotropic (approx)
                next_queue.medium_indices[out_idx] = current_medium_idx; // Stay in same medium
                next_queue.seeds[out_idx] = seed;
                next_queue.pixel_indices[out_idx] = pixel_index;
                next_queue.depths[out_idx] = depth + 1;
                
                // Preserve spectral channel, mark as Diffuse (bit 0 = 0)
                int spectral_channel = (flag >> 1) & 3;
                next_queue.flags[out_idx] = (spectral_channel << 1); 
             }
             return; // Skip surface shading
        } else {
             // Surface Hit - Apply Transmittance up to t_hit
            GpuVec3 tr_vals;
            tr_vals.x = expf(-sigma_t.values.x * t_hit);
            tr_vals.y = expf(-sigma_t.values.y * t_hit);
            tr_vals.z = expf(-sigma_t.values.z * t_hit);
            
            // Division by probability of NOT scattering?
            // prob_no_scatter = exp(-sigma_t_avg * t_hit)
            float prob_no_scatter = expf(-sigma_t_avg * t_hit);
            
            // Robustness: Avoid division by zero
            if (prob_no_scatter > 1e-6f) {
                throughput = throughput * GpuSpectrum::from_rgb(tr_vals) * (1.0f / prob_no_scatter);
            }
        }
    }

    if (mat_idx == -1) {
        // Miss: Sky color
        GpuVec3 unit_direction = current_queue.directions[idx].normalize();
        float t_sky = 0.5f * (unit_direction.y + 1.0f);
        GpuVec3 sky_color = (1.0f - t_sky) * GpuVec3(0.05f, 0.05f, 0.05f) + t_sky * GpuVec3(0.2f, 0.2f, 0.4f);
        
        GpuSpectrum contribution = throughput * GpuSpectrum::from_rgb(sky_color);
        GpuVec3 rgb = contribution.to_rgb();
        if (isfinite(rgb.x) && isfinite(rgb.y) && isfinite(rgb.z)) {
            atomicAdd(&accum_buffer[pixel_index].x, rgb.x);
            atomicAdd(&accum_buffer[pixel_index].y, rgb.y);
            atomicAdd(&accum_buffer[pixel_index].z, rgb.z);
        }

        // Feature Buffers (Sky)
        if (normal_buffer) normal_buffer[pixel_index] = GpuVec3(0, 0, 0);
        if (albedo_buffer) albedo_buffer[pixel_index] = sky_color;
        return;
    }
    
    // Hit
    GpuMaterial mat = scene.materials[mat_idx];

    // Texture Sampling (Out-of-Core / Unified Memory)
    if (mat.texture_index != -1) {
        GpuVec2 uv = hit_queue.uv[idx];
        GpuSpectrum tex_color = sample_texture(scene, mat.texture_index, uv.u, uv.v);
        mat.albedo = mat.albedo * tex_color;
    }
    
    GpuVec3 p = hit_queue.p[idx];
    GpuVec3 n = hit_queue.n[idx];
    GpuVec3 ng = hit_queue.ng[idx];

    // Feature Buffers (Hit)
    if (normal_buffer) normal_buffer[pixel_index] = n;
    if (albedo_buffer) albedo_buffer[pixel_index] = mat.albedo.to_rgb();
    
    // Emission
    GpuVec3 emission_rgb = mat.emission.to_rgb();
    if (emission_rgb.length_sq() > 0) {
        // Split MIS Logic:
        // 1. Primary rays (depth 0) always see lights.
        // 2. Specular bounces (flag=1) always see lights (NEE not possible).
        // 3. Diffuse bounces (flag=0) DO NOT see lights implicitly (handled by NEE), unless no lights exist.
        
        bool should_add = (depth == 0) || (flag & 1);
        
        if (should_add) {
            GpuSpectrum contribution = throughput * mat.emission;
            GpuVec3 rgb = contribution.to_rgb();
            
            if (depth > 0) {
                 // Relaxed clamp for Caustics
                 // High-quality caustics require high dynamic range.
                 // Increased to 1000.0f to allow bright caustics (e.g. through glass)
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
    
    // Max Depth Check
    if (depth >= 50) return;

    // Next Event Estimation (NEE)
    // Only for non-specular materials (Lambertian, Cloth)
    if (scene.light_count > 0 && (mat.type == MaterialType::Lambertian || mat.type == MaterialType::Cloth)) {
        // LDS for Light Sampling
        // Dimensions reserved for Light: 3, 4, 5 offset by depth
        // Base offset = 4 + depth * 6 (BSDF takes 0,1,2; Light takes 3,4,5)
        int dim_offset = 4 + depth * 6;
        
        float r_light_pick = sample_dimension(sample_index, pixel_index, dim_offset + 3);
        float r_light_1 = sample_dimension(sample_index, pixel_index, dim_offset + 4);
        float r_light_2 = sample_dimension(sample_index, pixel_index, dim_offset + 5);

        // Pick a random light
        int light_idx_idx = min(int(r_light_pick * scene.light_count), scene.light_count - 1);
        int light_idx = scene.light_indices[light_idx_idx];
        
        // Sample Point on Light (Sphere) using Cone Sampling (Solid Angle)
        GpuSphere light_sphere = scene.spheres[light_idx];
        GpuVec3 wc = light_sphere.center - p;
        float dist_sq = wc.length_sq();
        float radius = light_sphere.radius;
        float radius_sq = radius * radius;

        // Check if we are outside the light
        if (dist_sq > radius_sq) {
            float dist = sqrtf(dist_sq);
            float sin_theta_max2 = radius_sq / dist_sq;
            float cos_theta_max = sqrtf(fmaxf(0.0f, 1.0f - sin_theta_max2));
            
            // Uniform Sample Cone
            float r1 = r_light_1;
            float r2 = r_light_2;
            float cos_theta = 1.0f - r1 + r1 * cos_theta_max;
            float sin_theta = sqrtf(fmaxf(0.0f, 1.0f - cos_theta * cos_theta));
            float phi = 6.2831853f * r2;
            
            // Build Orthonormal Basis (w aligned to light center)
            GpuVec3 w = wc * (1.0f / dist);
            GpuVec3 u = (fabsf(w.x) > 0.9f) ? GpuVec3(0, 1, 0) : GpuVec3(1, 0, 0);
            u = u.cross(w).normalize();
            GpuVec3 v = w.cross(u);
            
            GpuVec3 l_dir = (u * cosf(phi) * sin_theta + v * sinf(phi) * sin_theta + w * cos_theta).normalize();
            
            // Visibility Check (Geometry)
            // Note: Cone sampling ensures we hit the light, so cos_light is implicitly handled in Solid Angle PDF
            float cos_surf = fmaxf(0.0f, n.dot(l_dir));
            
            // Shadow Terminator Fix: Ensure light is also visible from geometric normal
            if (cos_surf > 0.0f && ng.dot(l_dir) > 0.0f) {
                 // Solid Angle PDF
                 float solid_angle = 6.2831853f * (1.0f - cos_theta_max);
                 float pdf = 1.0f / solid_angle;
                 
                 // Selection PDF (1/N)
                 pdf *= (1.0f / scene.light_count);
                 
                 // Light Emission
                 GpuSpectrum L_e = scene.materials[light_sphere.material_index].emission;
                 
                 // BRDF (Lambertian = albedo / PI)
                 GpuSpectrum f_r = mat.albedo * (1.0f / 3.14159f);
                 
                 // Contribution = Le * fr * cos_surf / PDF
                 GpuSpectrum contribution = throughput * L_e * f_r * cos_surf * (1.0f / pdf);
                 
                 // Calculate exact distance to sphere surface for shadow ray
                 // Intersection t: t^2 + 2(M.D)t + (M.M - R^2) = 0 where M = P - C = -wc
                 float M_dot_D = -wc.dot(l_dir);
                 float c = dist_sq - radius_sq;
                 float discriminant = M_dot_D * M_dot_D - c;
                 
                 if (discriminant > 0.0f) {
                    float t_hit = -M_dot_D - sqrtf(discriminant);
                    
                    // Fix: Ensure t_hit is positive to avoid self-intersection or wrong direction
                    if (t_hit > 1e-4f) {
                        // Phase 3: Volumetric Shadow Attenuation
                        if (sigma_t_avg > 1e-4f) {
                             GpuVec3 tr_vals;
                             tr_vals.x = expf(-sigma_t.values.x * t_hit);
                             tr_vals.y = expf(-sigma_t.values.y * t_hit);
                             tr_vals.z = expf(-sigma_t.values.z * t_hit);
                             contribution = contribution * GpuSpectrum::from_rgb(tr_vals);
                        }

                        // Queue Shadow Ray
                        int s_idx = atomicAdd(shadow_queue.count, 1);
                        if (s_idx < shadow_queue.capacity) {
                            shadow_queue.origins[s_idx] = p + ng * 1e-4f;
                            shadow_queue.directions[s_idx] = l_dir;
                            shadow_queue.max_dist[s_idx] = t_hit - 1e-4f; 
                            shadow_queue.radiance[s_idx] = contribution;
                            shadow_queue.pixel_indices[s_idx] = pixel_index;
                        }
                    }
                }
            }
        }
    }
    
    // Scatter (Standard BSDF Sampling)
    GpuRay r_in;
    r_in.origin = current_queue.origins[idx];
    r_in.direction = current_queue.directions[idx];
    
    GpuRay scattered;
    GpuSpectrum attenuation;
    
    // Phase 3: Retrieve current Stokes vector
    StokesVector current_stokes = current_queue.stokes[idx];

    GpuVec2 uv = hit_queue.uv[idx];

            // Decode spectral channel (Bit 1-2)
            // 0: None, 1: R, 2: G, 3: B
            int spectral_channel = (flag >> 1) & 3;

            if (scatter(r_in, mat, p, n, uv, throughput, attenuation, scattered, current_stokes, seed, dispersion_clamp, sample_index, pixel_index, depth, spectral_channel)) {
                GpuSpectrum new_throughput = throughput * attenuation;
                
                // Robust NaN check
                if (!isfinite(new_throughput.values.x) || !isfinite(new_throughput.values.y) || 
                    !isfinite(new_throughput.values.z) || !isfinite(new_throughput.values.w)) {
                    return;
                }

                // Russian Roulette
                if (depth > 3) {
                    GpuVec3 rgb = new_throughput.to_rgb();
                    float max_comp = fmaxf(rgb.x, fmaxf(rgb.y, rgb.z));
                    
                    // Fix: Probability must be clamped to 1.0
                    // Also clamp minimum probability to prevent massive weight boosts (fireflies) for dark paths
                    float prob = fmaxf(rr_min_prob, fminf(1.0f, max_comp));
                    
                    if (rand_float(seed) > prob) {
                        return; // Terminate
                    }
                    new_throughput = new_throughput * (1.0f / prob);
                }
                
                // Determine next flag for MIS
                int next_flag = 0;
                bool is_specular = (mat.type == MaterialType::Metal || mat.type == MaterialType::Dielectric);
                if (is_specular || scene.light_count == 0) {
                    next_flag = 1; // Specular bounce or no lights -> Enable implicit emission
                }
                
                // Propagate spectral channel
                next_flag |= (spectral_channel << 1);

                // Enqueue to next_queue
        int out_idx = atomicAdd(next_queue.count, 1);
        if (out_idx < next_queue.capacity) {
            next_queue.origins[out_idx] = scattered.origin;
            next_queue.directions[out_idx] = scattered.direction;
            next_queue.throughputs[out_idx] = new_throughput;
            next_queue.stokes[out_idx] = current_stokes; // Phase 3: Propagate updated Stokes vector
            
            // Phase 3: Volume / SSS Medium Tracking
            int next_medium = current_medium_idx;
            if (mat.type == MaterialType::Dielectric) {
                // Check for Transmission (Refraction) vs Reflection
                // If dot(in, ng) and dot(out, ng) have SAME sign -> Transmission
                // Use geometric normal ng for robust inside/outside check
                float in_dot_ng = r_in.direction.dot(ng);
                float out_dot_ng = scattered.direction.dot(ng);
                
                if (in_dot_ng * out_dot_ng > 0.0f) {
                    // Transmission: Enter/Exit Medium
                    if (current_medium_idx == -1) {
                        next_medium = mat_idx; // Enter
                    } else if (current_medium_idx == mat_idx) {
                        next_medium = -1; // Exit
                    } else {
                        // Enter new nested medium
                        // For now, simple override. Proper handling requires a stack.
                        next_medium = mat_idx; 
                    }
                }
            }
            next_queue.medium_indices[out_idx] = next_medium;

            next_queue.seeds[out_idx] = seed;
            next_queue.pixel_indices[out_idx] = pixel_index;
            next_queue.depths[out_idx] = depth + 1;
            next_queue.flags[out_idx] = next_flag;
        }
    }
}

// Helper functions
void alloc_ray_queue(RayQueue& q, int capacity) {
    q.capacity = capacity;
    cudaMalloc(&q.origins, capacity * sizeof(GpuVec3));
    cudaMalloc(&q.directions, capacity * sizeof(GpuVec3));
    cudaMalloc(&q.throughputs, capacity * sizeof(GpuSpectrum));
    cudaMalloc(&q.stokes, capacity * sizeof(StokesVector)); // Phase 3
    cudaMalloc(&q.medium_indices, capacity * sizeof(int)); // Phase 3: Volume / SSS
    cudaMalloc(&q.seeds, capacity * sizeof(unsigned int));
    cudaMalloc(&q.pixel_indices, capacity * sizeof(int));
    cudaMalloc(&q.depths, capacity * sizeof(int));
    cudaMalloc(&q.flags, capacity * sizeof(int));
    cudaMalloc(&q.count, sizeof(int));
}

void free_ray_queue(RayQueue& q) {
    cudaFree(q.origins);
    cudaFree(q.directions);
    cudaFree(q.throughputs);
    cudaFree(q.stokes); // Phase 3
    cudaFree(q.medium_indices); // Phase 3
    cudaFree(q.seeds);
    cudaFree(q.pixel_indices);
    cudaFree(q.depths);
    cudaFree(q.flags);
    cudaFree(q.count);
}

void alloc_hit_queue(HitQueue& q, int capacity) {
    cudaMalloc(&q.t, capacity * sizeof(float));
    cudaMalloc(&q.p, capacity * sizeof(GpuVec3));
    cudaMalloc(&q.n, capacity * sizeof(GpuVec3));
    cudaMalloc(&q.ng, capacity * sizeof(GpuVec3));
    cudaMalloc(&q.uv, capacity * sizeof(GpuVec2));
    cudaMalloc(&q.mat_ids, capacity * sizeof(int));
}

void free_hit_queue(HitQueue& q) {
    cudaFree(q.t);
    cudaFree(q.p);
    cudaFree(q.n);
    cudaFree(q.ng);
    cudaFree(q.uv);
    cudaFree(q.mat_ids);
}

void alloc_shadow_queue(ShadowQueue& q, int capacity) {
    q.capacity = capacity;
    cudaMalloc(&q.origins, capacity * sizeof(GpuVec3));
    cudaMalloc(&q.directions, capacity * sizeof(GpuVec3));
    cudaMalloc(&q.max_dist, capacity * sizeof(float));
    cudaMalloc(&q.radiance, capacity * sizeof(GpuSpectrum));
    cudaMalloc(&q.pixel_indices, capacity * sizeof(int));
    cudaMalloc(&q.count, sizeof(int));
}

void free_shadow_queue(ShadowQueue& q) {
    cudaFree(q.origins);
    cudaFree(q.directions);
    cudaFree(q.max_dist);
    cudaFree(q.radiance);
    cudaFree(q.pixel_indices);
    cudaFree(q.count);
}

// Adaptive rendering kernels
__global__ void adaptive_render_kernel(
    GpuVec3* accum_buffer,
    GpuVec3* accum_sq_buffer,
    int* sample_counts,
    int width,
    int height,
    GpuCamera camera,
    GpuScene scene,
    int batch_samples,
    int seed_offset,
    float variance_threshold
) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    int j = blockIdx.y * blockDim.y + threadIdx.y;

    if (i >= width || j >= height) return;

    int pixel_index = j * width + i;
    
    // Check convergence if we have existing samples
    int current_samples = sample_counts[pixel_index];
    if (current_samples > 0 && variance_threshold > 0.0f) {
        GpuVec3 sum = accum_buffer[pixel_index];
        GpuVec3 sum_sq = accum_sq_buffer[pixel_index];
        float N = (float)current_samples;
        
        // Variance = E[x^2] - (E[x])^2
        // We use RGB magnitude or just max component
        GpuVec3 mean = sum * (1.0f / N);
        GpuVec3 mean_sq = mean * mean;
        GpuVec3 var_vec = (sum_sq * (1.0f / N)) - mean_sq;
        
        // Fix: Ensure all channels are converged and handle potential precision issues
        float threshold_sq_N = variance_threshold * variance_threshold * N;
        
        if (var_vec.x < threshold_sq_N && var_vec.y < threshold_sq_N && var_vec.z < threshold_sq_N) {
            // Converged
            return;
        }
    }
    
    unsigned int seed = wang_hash(1984 + seed_offset + pixel_index);
    
    GpuVec3 batch_color(0, 0, 0);
    GpuVec3 batch_sq(0, 0, 0);
    
    for (int s = 0; s < batch_samples; ++s) {
        // Tent Filter Implementation
        // Maps uniform random [0,1] to triangle distribution [-1, 1]
        // This provides better anti-aliasing than a Box filter.
        float r_x = rand_float(seed);
        float r_y = rand_float(seed);
        
        float dx, dy;
        if (r_x < 0.5f) dx = sqrtf(2.0f * r_x) - 1.0f;
        else dx = 1.0f - sqrtf(2.0f * (1.0f - r_x));
        
        if (r_y < 0.5f) dy = sqrtf(2.0f * r_y) - 1.0f;
        else dy = 1.0f - sqrtf(2.0f * (1.0f - r_y));

        // Use 'width' and 'height' for correct UV mapping (not -1)
        float u = (float(i) + 0.5f + dx) / float(width);
        float v = (float(height - 1 - j) + 0.5f + dy) / float(height); 

        GpuRay ray;
        ray.origin = camera.origin;
        ray.direction = (camera.lower_left_corner + camera.horizontal * u + camera.vertical * v - camera.origin).normalize();
        ray.t_min = 1e-4f;
        ray.t_max = FLT_MAX;
        
        GpuVec3 sample_radiance = path_trace(ray, scene, seed, current_samples + s, pixel_index);
        batch_color = batch_color + sample_radiance;
        batch_sq = batch_sq + (sample_radiance * sample_radiance);
    }
    
    // Accumulate results
    accum_buffer[pixel_index] = accum_buffer[pixel_index] + batch_color;
    accum_sq_buffer[pixel_index] = accum_sq_buffer[pixel_index] + batch_sq;
    sample_counts[pixel_index] += batch_samples;
}

void render_frame_gpu(float* output_buffer, int width, int height, int samples_per_pixel,
                      const std::vector<ure::gpu::RenderMesh>& meshes,
                      const std::vector<ure::gpu::GpuSphere>& spheres,
                      const std::vector<ure::gpu::GpuMaterial>& materials,
                      const float* cam_pos,
                      const float* cam_look,
                      float fov,
                      float medium_density,
                      GpuSpectrum medium_scattering,
                      GpuSpectrum medium_absorption,
                      float medium_max_distance) {
    std::cout << "[GPU] Allocating memory for " << width << "x" << height << " image..." << std::endl;

    size_t framebuffer_size = width * height * sizeof(GpuVec3);
    GpuVec3* d_output;
    cudaMalloc(&d_output, framebuffer_size);

    // Setup Materials
    // Only load default geometry if no input geometry is provided
    bool use_default_geometry = spheres.empty() && meshes.empty();
    GpuHostScene host_scene = load_default_scene(!use_default_geometry);
    
    std::vector<GpuMaterial>& host_materials = host_scene.materials;
    // Append passed materials if any
    if (!materials.empty()) {
        host_materials.insert(host_materials.end(), materials.begin(), materials.end());
    }

    GpuMaterial* d_materials;
    cudaMalloc(&d_materials, host_materials.size() * sizeof(GpuMaterial));
    cudaMemcpy(d_materials, host_materials.data(), host_materials.size() * sizeof(GpuMaterial), cudaMemcpyHostToDevice);

    // Setup Spheres
    std::vector<GpuSphere> host_spheres = spheres;
    
    // Fallback: If no input geometry, use default scene spheres
    if (spheres.empty() && meshes.empty()) {
        host_spheres = host_scene.spheres;
    }

    GpuSphere* d_spheres;
    cudaMalloc(&d_spheres, host_spheres.size() * sizeof(GpuSphere));
    cudaMemcpy(d_spheres, host_spheres.data(), host_spheres.size() * sizeof(GpuSphere), cudaMemcpyHostToDevice);

    // Setup Meshes
    std::vector<GpuMesh> host_gpu_meshes;
    std::vector<void*> pointers_to_free;

    // Helper to compute AABB
    auto compute_aabb = [](const std::vector<float>& vertices, GpuVec3& min_pt, GpuVec3& max_pt) {
        min_pt = GpuVec3(1e30f, 1e30f, 1e30f);
        max_pt = GpuVec3(-1e30f, -1e30f, -1e30f);
        if (vertices.empty()) return;

        for (size_t i = 0; i < vertices.size(); i += 3) {
            float x = vertices[i];
            float y = vertices[i+1];
            float z = vertices[i+2];
            if (x < min_pt.x) min_pt.x = x;
            if (y < min_pt.y) min_pt.y = y;
            if (z < min_pt.z) min_pt.z = z;
            if (x > max_pt.x) max_pt.x = x;
            if (y > max_pt.y) max_pt.y = y;
            if (z > max_pt.z) max_pt.z = z;
        }
        // Add small epsilon padding
        float padding = 1e-3f;
        min_pt = min_pt - GpuVec3(padding, padding, padding);
        max_pt = max_pt + GpuVec3(padding, padding, padding);
    };

    // 1. Handle Input Meshes (from argument)
    for (const auto& input_mesh : meshes) {
        GpuMesh mesh;
        mesh.triangle_count = (int)input_mesh.indices.size() / 3;
        mesh.material_index = input_mesh.material_index;
        
        if (!input_mesh.uvs.empty()) {
             size_t uv_size = input_mesh.uvs.size() * sizeof(float);
             GpuVec2* d_uv;
             cudaMalloc(&d_uv, uv_size);
             cudaMemcpy(d_uv, input_mesh.uvs.data(), uv_size, cudaMemcpyHostToDevice);
             mesh.uvs = d_uv;
             pointers_to_free.push_back(d_uv);
        } else {
             mesh.uvs = nullptr;
        }

        if (!input_mesh.normals.empty()) {
             size_t n_size = input_mesh.normals.size() * sizeof(float);
             GpuVec3* d_n;
             cudaMalloc(&d_n, n_size);
             cudaMemcpy(d_n, input_mesh.normals.data(), n_size, cudaMemcpyHostToDevice);
             mesh.normals = d_n;
             pointers_to_free.push_back(d_n);
        } else {
             mesh.normals = nullptr;
        }

        compute_aabb(input_mesh.vertices, mesh.min_pt, mesh.max_pt);

        // Build BVH
        std::vector<int> temp_indices = input_mesh.indices; // Copy for reordering
        std::vector<GpuBvhNode> build_nodes;
        MeshBvhBuilder::build(input_mesh.vertices, temp_indices, build_nodes);

        if (!build_nodes.empty()) {
             size_t bvh_size = build_nodes.size() * sizeof(GpuBvhNode);
             GpuBvhNode* d_nodes;
             cudaMalloc(&d_nodes, bvh_size);
             cudaMemcpy(d_nodes, build_nodes.data(), bvh_size, cudaMemcpyHostToDevice);
             mesh.bvh_nodes = d_nodes;
             mesh.bvh_node_count = (int)build_nodes.size();
             pointers_to_free.push_back(d_nodes);
             std::cout << "[GPU] Built BVH for mesh: " << mesh.bvh_node_count << " nodes." << std::endl;
        } else {
             mesh.bvh_nodes = nullptr;
             mesh.bvh_node_count = 0;
        }

        size_t v_size = input_mesh.vertices.size() * sizeof(float);
        GpuVec3* d_v;
        cudaMalloc(&d_v, v_size);
        cudaMemcpy(d_v, input_mesh.vertices.data(), v_size, cudaMemcpyHostToDevice);
        mesh.vertices = d_v;
        pointers_to_free.push_back(d_v);

        size_t i_size = temp_indices.size() * sizeof(int);
        int* d_i;
        cudaMalloc(&d_i, i_size);
        cudaMemcpy(d_i, temp_indices.data(), i_size, cudaMemcpyHostToDevice); // Upload reordered indices
        mesh.indices = d_i;
        pointers_to_free.push_back(d_i);
        
        host_gpu_meshes.push_back(mesh);
        std::cout << "[GPU] Uploaded Input mesh: " << mesh.triangle_count << " triangles." << std::endl;
    }

    // 2. Handle Scene Meshes (e.g. Blue Cube)
    for (auto& hm : host_scene.meshes) {
        GpuMesh mesh;
        mesh.triangle_count = (int)hm.indices.size() / 3;
        mesh.material_index = hm.material_index;

        compute_aabb(hm.vertices, mesh.min_pt, mesh.max_pt);

        // Build BVH
        std::vector<GpuBvhNode> build_nodes;
        MeshBvhBuilder::build(hm.vertices, hm.indices, build_nodes);
        
        if (!build_nodes.empty()) {
             size_t bvh_size = build_nodes.size() * sizeof(GpuBvhNode);
             GpuBvhNode* d_nodes;
             cudaMalloc(&d_nodes, bvh_size);
             cudaMemcpy(d_nodes, build_nodes.data(), bvh_size, cudaMemcpyHostToDevice);
             mesh.bvh_nodes = d_nodes;
             mesh.bvh_node_count = (int)build_nodes.size();
             pointers_to_free.push_back(d_nodes);
        } else {
             mesh.bvh_nodes = nullptr;
             mesh.bvh_node_count = 0;
        }

        size_t v_size = hm.vertices.size() * sizeof(float);
        GpuVec3* d_v;
        cudaMalloc(&d_v, v_size);
        cudaMemcpy(d_v, hm.vertices.data(), v_size, cudaMemcpyHostToDevice);
        mesh.vertices = d_v;
        pointers_to_free.push_back(d_v);

        if (!hm.normals.empty()) {
            size_t n_size = hm.normals.size() * sizeof(float);
            GpuVec3* d_n;
            cudaMalloc(&d_n, n_size);
            cudaMemcpy(d_n, hm.normals.data(), n_size, cudaMemcpyHostToDevice);
            mesh.normals = d_n;
            pointers_to_free.push_back(d_n);
        } else {
            mesh.normals = nullptr;
        }

        // Handle UVs
        if (!hm.uvs.empty()) {
            size_t uv_size = hm.uvs.size() * sizeof(float);
            GpuVec2* d_uv;
            cudaMalloc(&d_uv, uv_size);
            cudaMemcpy(d_uv, hm.uvs.data(), uv_size, cudaMemcpyHostToDevice);
            mesh.uvs = d_uv;
            pointers_to_free.push_back(d_uv);
        } else {
            mesh.uvs = nullptr;
        }

        size_t i_size = hm.indices.size() * sizeof(int);
        int* d_i;
        cudaMalloc(&d_i, i_size);
        cudaMemcpy(d_i, hm.indices.data(), i_size, cudaMemcpyHostToDevice);
        mesh.indices = d_i;
        pointers_to_free.push_back(d_i);

        host_gpu_meshes.push_back(mesh);
        std::cout << "[GPU] Uploaded Scene mesh: " << mesh.triangle_count << " triangles." << std::endl;
        if (mesh.bvh_node_count > 0) {
            std::cout << "      BVH Nodes: " << mesh.bvh_node_count << std::endl;
        } else {
            std::cout << "      BVH: None (Linear Scan)" << std::endl;
        }
    }

    GpuMesh* d_meshes;
    cudaMalloc(&d_meshes, host_gpu_meshes.size() * sizeof(GpuMesh));
    cudaMemcpy(d_meshes, host_gpu_meshes.data(), host_gpu_meshes.size() * sizeof(GpuMesh), cudaMemcpyHostToDevice);

    // Setup Textures
    std::vector<GpuTexture> host_gpu_textures;
    std::vector<cudaArray_t> arrays_to_free;
    std::vector<cudaTextureObject_t> tex_objs_to_free;

    for (const auto& h_tex : host_scene.textures) {
        GpuTexture d_tex;
        d_tex.width = h_tex.width;
        d_tex.height = h_tex.height;
        
        // 1. Unified Memory (Legacy/Linear)
        size_t size_bytes = h_tex.width * h_tex.height * sizeof(GpuSpectrum);
        cudaMallocManaged(&d_tex.data, size_bytes);
        
        std::vector<GpuSpectrum> temp_spec(h_tex.width * h_tex.height);
        std::vector<float4> temp_float4(h_tex.width * h_tex.height);

        for(size_t i=0; i < temp_spec.size(); ++i) {
             float r = h_tex.data[i*3+0];
             float g = h_tex.data[i*3+1];
             float b = h_tex.data[i*3+2];
             
             // For Linear Buffer
             temp_spec[i] = GpuSpectrum::from_rgb(GpuVec3(r, g, b));
             
             // For Texture Object
             temp_float4[i] = make_float4(r, g, b, 1.0f);
        }
        
        cudaMemcpy(d_tex.data, temp_spec.data(), size_bytes, cudaMemcpyHostToDevice);
        pointers_to_free.push_back(d_tex.data);

        // 2. Hardware Texture Object
        cudaChannelFormatDesc channelDesc = cudaCreateChannelDesc<float4>();
        cudaArray_t cuArray;
        checkCudaErrors(cudaMallocArray(&cuArray, &channelDesc, d_tex.width, d_tex.height));
        arrays_to_free.push_back(cuArray);

        checkCudaErrors(cudaMemcpy2DToArray(cuArray, 0, 0, temp_float4.data(), d_tex.width * sizeof(float4), d_tex.width * sizeof(float4), d_tex.height, cudaMemcpyHostToDevice));

        struct cudaResourceDesc resDesc;
        memset(&resDesc, 0, sizeof(resDesc));
        resDesc.resType = cudaResourceTypeArray;
        resDesc.res.array.array = cuArray;

        struct cudaTextureDesc texDesc;
        memset(&texDesc, 0, sizeof(texDesc));
        texDesc.addressMode[0] = cudaAddressModeWrap;
        texDesc.addressMode[1] = cudaAddressModeWrap;
        texDesc.filterMode = cudaFilterModeLinear;
        texDesc.readMode = cudaReadModeElementType;
        texDesc.normalizedCoords = 1;

        checkCudaErrors(cudaCreateTextureObject(&d_tex.texObj, &resDesc, &texDesc, NULL));
        tex_objs_to_free.push_back(d_tex.texObj);

        host_gpu_textures.push_back(d_tex);
        std::cout << "[GPU] Uploaded Spectral Texture: " << d_tex.width << "x" << d_tex.height << std::endl;
    }

    GpuTexture* d_textures = nullptr;
    if (!host_gpu_textures.empty()) {
        cudaMalloc(&d_textures, host_gpu_textures.size() * sizeof(GpuTexture));
        cudaMemcpy(d_textures, host_gpu_textures.data(), host_gpu_textures.size() * sizeof(GpuTexture), cudaMemcpyHostToDevice);
        pointers_to_free.push_back(d_textures);
    }

    // Setup Light Indices (Spheres Only for now)
    std::vector<int> host_light_indices;
    for (int i = 0; i < host_spheres.size(); ++i) {
        int mat_idx = host_spheres[i].material_index;
        if (mat_idx >= 0 && mat_idx < host_materials.size()) {
            const auto& mat = host_materials[mat_idx];
            // Check if emission is non-zero (using a small epsilon or just > 0)
            if (mat.emission.values.x > 1e-4f || mat.emission.values.y > 1e-4f || mat.emission.values.z > 1e-4f) {
                host_light_indices.push_back(i);
            }
        }
    }

    int* d_light_indices = nullptr;
    if (!host_light_indices.empty()) {
        cudaMalloc(&d_light_indices, host_light_indices.size() * sizeof(int));
        cudaMemcpy(d_light_indices, host_light_indices.data(), host_light_indices.size() * sizeof(int), cudaMemcpyHostToDevice);
        // pointers_to_free.push_back(d_light_indices); // We will free it manually or add to list
    }

    GpuScene scene;
    scene.spheres = d_spheres;
    scene.sphere_count = (int)host_spheres.size();
    scene.meshes = d_meshes;
    scene.mesh_count = (int)host_gpu_meshes.size();
    scene.materials = d_materials;
    scene.material_count = (int)host_materials.size();
    scene.textures = d_textures;
    scene.texture_count = (int)host_gpu_textures.size();
    scene.light_indices = d_light_indices;
    scene.light_count = (int)host_light_indices.size();
    
    // Medium Setup
    scene.medium_density = medium_density;
    scene.medium_scattering = medium_scattering;
    scene.medium_absorption = medium_absorption;
    scene.medium_max_distance = medium_max_distance;

    std::cout << "[GPU] Found " << scene.light_count << " emissive spheres for NEE." << std::endl;


    // Setup Camera
    GpuCamera h_camera;
    
    GpuVec3 lookfrom(0, 3, 12);
    if (cam_pos) lookfrom = GpuVec3(cam_pos[0], cam_pos[1], cam_pos[2]);

    GpuVec3 lookat(0, 1, 0);
    if (cam_look) lookat = GpuVec3(cam_look[0], cam_look[1], cam_look[2]);

    float vfov = (fov > 0) ? fov : 40.0f;
    float theta = vfov * 3.14159265358979323846f / 180.0f;
    float h = tan(theta / 2.0f);
    float aspect_ratio = float(width) / float(height);
    float viewport_height = 2.0f * h;
    float viewport_width = aspect_ratio * viewport_height;
    
    GpuVec3 vup(0, 1, 0);
    GpuVec3 w = (lookfrom - lookat).normalize();
    GpuVec3 u = vup.cross(w).normalize();
    GpuVec3 v = w.cross(u);
    float focus_dist = 18.0f;

    h_camera.origin = lookfrom;
    h_camera.horizontal = u * viewport_width * focus_dist;
    h_camera.vertical = v * viewport_height * focus_dist;
    h_camera.lower_left_corner = h_camera.origin - h_camera.horizontal * 0.5f - h_camera.vertical * 0.5f - w * focus_dist;

    // Allocate Accumulation Buffers
    GpuVec3* d_accum_buffer = nullptr;
    int* d_sample_counts = nullptr;
    
    // Correct allocation size
    cudaMalloc(&d_accum_buffer, framebuffer_size);
    cudaMemset(d_accum_buffer, 0, framebuffer_size);
    
    cudaMalloc(&d_sample_counts, width * height * sizeof(int));
    cudaMemset(d_sample_counts, 0, width * height * sizeof(int));

    // Allocate Feature Buffers for Denoising
    GpuVec3* d_normal_buffer = nullptr;
    GpuVec3* d_albedo_buffer = nullptr;
    cudaMalloc(&d_normal_buffer, framebuffer_size);
    cudaMemset(d_normal_buffer, 0, framebuffer_size);
    cudaMalloc(&d_albedo_buffer, framebuffer_size);
    cudaMemset(d_albedo_buffer, 0, framebuffer_size);

    dim3 threadsPerBlock(16, 16);
    dim3 numBlocks((width + threadsPerBlock.x - 1) / threadsPerBlock.x,
                   (height + threadsPerBlock.y - 1) / threadsPerBlock.y);

    std::cout << "[GPU] Starting Wavefront Render Loop..." << std::endl;
    
    // Wavefront setup
    RayQueue queueA, queueB;
    HitQueue hitQueue;
    ShadowQueue shadowQueue;
    int max_rays = width * height;
    
    alloc_ray_queue(queueA, max_rays);
    alloc_ray_queue(queueB, max_rays);
    alloc_hit_queue(hitQueue, max_rays);
    alloc_shadow_queue(shadowQueue, max_rays);
    
    int total_samples = samples_per_pixel;
    
    // Timing and Progress
    auto start_time = std::chrono::high_resolution_clock::now();
    int total_bars = 40;

    for (int s = 0; s < total_samples; ++s) {
        // 1. Generate Rays
        int initial_count = width * height;
        cudaMemcpy(queueA.count, &initial_count, sizeof(int), cudaMemcpyHostToDevice);
        
        generate_rays_kernel<<<numBlocks, threadsPerBlock>>>(
            queueA, width, height, h_camera, s, d_sample_counts
        );
        
        checkCudaErrors(cudaGetLastError());
        checkCudaErrors(cudaDeviceSynchronize()); // Ensure generation finishes for debug
        
        RayQueue* current_q = &queueA;
        RayQueue* next_q = &queueB;
        
        for (int depth = 0; depth < 50; ++depth) {
             // 2. Extend
             int ray_count = 0;
             cudaMemcpy(&ray_count, current_q->count, sizeof(int), cudaMemcpyDeviceToHost);
             
             if (ray_count == 0) break;
             
             int num_threads = 256;
             int num_blocks = (ray_count + num_threads - 1) / num_threads;
             
             extend_kernel<<<num_blocks, num_threads>>>(*current_q, hitQueue, scene);
             
             // 3. Shade
             cudaMemset(next_q->count, 0, sizeof(int));
             cudaMemset(shadowQueue.count, 0, sizeof(int));
             
             // Dynamic parameters based on SPP (passed from host)
             float current_dispersion_clamp = (total_samples < 100) ? 5.0f : 20.0f;
             float current_rr_min_prob = (total_samples < 100) ? 0.1f : 0.05f;
             
             shade_kernel<<<num_blocks, num_threads>>>(*current_q, hitQueue, *next_q, shadowQueue, d_accum_buffer, d_normal_buffer, d_albedo_buffer, scene, s, current_dispersion_clamp, current_rr_min_prob);
             
             // 4. Shadow Rays (NEE)
             int shadow_count = 0;
             cudaMemcpy(&shadow_count, shadowQueue.count, sizeof(int), cudaMemcpyDeviceToHost);
             
             if (shadow_count > 0) {
                 int s_blocks = (shadow_count + num_threads - 1) / num_threads;
                 extend_shadow_kernel<<<s_blocks, num_threads>>>(shadowQueue, d_accum_buffer, scene);
                 
                 cudaError_t err_shadow = cudaGetLastError();
                 if (err_shadow != cudaSuccess) {
                     std::cerr << "[GPU] Shadow Kernel Error: " << cudaGetErrorString(err_shadow) << std::endl;
                 }
             }
             
             cudaError_t err_loop = cudaGetLastError();
             if (err_loop != cudaSuccess) {
                 std::cerr << "[GPU] Render Loop Error at depth " << depth << ": " << cudaGetErrorString(err_loop) << std::endl;
                 break;
             }
             
             // Swap queues
             RayQueue* temp = current_q;
             current_q = next_q;
             next_q = temp;
        }
        
        // Progress Bar Update
        if (s % 1 == 0 || s == total_samples - 1) { 
             float percent = (float)(s + 1) / total_samples;
             
             auto current_time = std::chrono::high_resolution_clock::now();
             auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time).count();
             float elapsed_sec = elapsed_ms / 1000.0f;
             
             float samples_per_sec = (s + 1) / std::max(0.001f, elapsed_sec);
             float remaining_sec = (total_samples - (s + 1)) / samples_per_sec;
             
             std::cout << "\r[GPU] Progress: [";
             int bars = (int)(percent * total_bars);
             for (int b = 0; b < total_bars; ++b) {
                 if (b < bars) std::cout << "=";
                 else if (b == bars) std::cout << ">";
                 else std::cout << " ";
             }
             std::cout << "] " << int(percent * 100) << "% " 
                       << std::fixed << std::setprecision(1) << elapsed_sec << "s / " << int(remaining_sec) << "s ETA  " << std::flush;
        }
    }
    std::cout << std::endl;

    std::cout << "[GPU] Resolving Framebuffer..." << std::endl;
    resolve_framebuffer_kernel<<<numBlocks, threadsPerBlock>>>(
        d_accum_buffer,
        d_sample_counts,
        d_output, 
        width,
        height
    );
    checkCudaErrors(cudaDeviceSynchronize());

    // Denoising Pass
    // Logic: For high SPP (>= 400), denoising causes unnecessary blur.
    // We disable it to preserve sharpness and detail.
    bool enable_denoiser = (samples_per_pixel < 400);
    
    GpuVec3* final_denoised = nullptr;
    GpuVec3* d_ping = nullptr;
    GpuVec3* d_pong = nullptr;

    if (enable_denoiser) {
        std::cout << "[GPU] Denoising (A-Trous Wavelet)..." << std::endl;
        
        // We need two buffers for ping-pong
        cudaMalloc(&d_ping, framebuffer_size);
        cudaMalloc(&d_pong, framebuffer_size);
        
        // Copy initial noisy image to ping buffer
        cudaMemcpy(d_ping, d_output, framebuffer_size, cudaMemcpyDeviceToDevice);
        
        int iterations = 5;
        
        // Adaptive Denoiser Parameters
        // As SPP increases, we reduce the color tolerance (c_phi) to preserve fine detail.
        // Base c_phi = 1.0 at low SPP.
        // Formula: c_phi = 10.0 / sqrt(SPP) is too high. 
        // Let's use: c_phi = 1.0 for SPP < 100, then drop.
        // Better: c_phi = max(0.05f, 5.0f / samples_per_pixel) ? No, linear drop is too fast.
        // Use inverse square root: c_phi = 2.0f / sqrt(SPP).
        // SPP 100 -> 0.2
        // SPP 400 -> 0.1
        // SPP 2500 -> 0.04
        float c_phi = 1.0f;
        if (samples_per_pixel > 0) {
            c_phi = fmaxf(0.03f, 4.0f / sqrtf((float)samples_per_pixel));
        } 
        
        // If SPP is very high, reduce iterations to avoid over-smoothing
        if (samples_per_pixel > 1000) iterations = 2;

        float n_phi = 0.15f;
        float p_phi = 0.1f;
        
        suppress_dark_outliers_kernel<<<numBlocks, threadsPerBlock>>>(
            d_pong,
            d_ping,
            d_normal_buffer,
            d_albedo_buffer,
            width,
            height,
            1.2f,
            0.03f,
            0.2f,
            0.15f
        );
        checkCudaErrors(cudaGetLastError());
        checkCudaErrors(cudaDeviceSynchronize());
        
        {
            GpuVec3* temp = d_ping;
            d_ping = d_pong;
            d_pong = temp;
        }
        
        for (int i = 0; i < iterations; ++i) {
            int step_width = 1 << i;
            
            GpuVec3* input = (i % 2 == 0) ? d_ping : d_pong;
            GpuVec3* output = (i % 2 == 0) ? d_pong : d_ping;
            
            atrous_filter_kernel<<<numBlocks, threadsPerBlock>>>(
                output,
                input,
                d_normal_buffer,
                d_albedo_buffer,
                width,
                height,
                step_width,
                c_phi,
                n_phi,
                p_phi
            );
            checkCudaErrors(cudaGetLastError());
            checkCudaErrors(cudaDeviceSynchronize());
        }
        
        final_denoised = (iterations % 2 == 0) ? d_ping : d_pong;

        {
            GpuVec3* input = final_denoised;
            GpuVec3* output = (final_denoised == d_ping) ? d_pong : d_ping;

            suppress_dark_outliers_kernel<<<numBlocks, threadsPerBlock>>>(
                output,
                input,
                d_normal_buffer,
                d_albedo_buffer,
                width,
                height,
                1.0f,
                0.03f,
                0.2f,
                0.15f
            );
            checkCudaErrors(cudaGetLastError());
            checkCudaErrors(cudaDeviceSynchronize());
            final_denoised = output;
        }
    } else {
        std::cout << "[GPU] High SPP detected (" << samples_per_pixel << "), skipping denoiser for sharpness." << std::endl;
        final_denoised = d_output;
    }

    // FXAA Pass
    // Only apply if we have a valid pointer (always true here)
    // Update: Disable FXAA for high SPP (>= 400) to avoid double-blurring with Tent filter
    bool enable_fxaa = (samples_per_pixel < 400); 

    GpuVec3* d_fxaa_out = nullptr;

    if (enable_fxaa) {
        std::cout << "[GPU] Anti-Aliasing (FXAA)..." << std::endl;
        
        if (enable_denoiser) {
            d_fxaa_out = (final_denoised == d_ping) ? d_pong : d_ping;
        } else {
            // Need a temp buffer if denoiser wasn't run but FXAA is enabled
            cudaMalloc(&d_fxaa_out, framebuffer_size);
        }

        fxaa_kernel<<<numBlocks, threadsPerBlock>>>(
            d_fxaa_out,
            final_denoised,
            width,
            height
        );
        checkCudaErrors(cudaGetLastError());
        checkCudaErrors(cudaDeviceSynchronize());
        
        std::cout << "[GPU] Copying results to host..." << std::endl;
        cudaMemcpy(output_buffer, d_fxaa_out, framebuffer_size, cudaMemcpyDeviceToHost);
    } else {
        std::cout << "[GPU] High SPP detected, skipping FXAA for maximum sharpness." << std::endl;
        std::cout << "[GPU] Copying results to host..." << std::endl;
        cudaMemcpy(output_buffer, final_denoised, framebuffer_size, cudaMemcpyDeviceToHost);
    }

    std::cout << "[GPU] Render Complete." << std::endl;

    if (enable_denoiser) {
        cudaFree(d_ping);
        cudaFree(d_pong);
    } else {
        if (d_fxaa_out) cudaFree(d_fxaa_out);
    }
    cudaFree(d_normal_buffer);
    cudaFree(d_albedo_buffer);

    free_ray_queue(queueA);
    free_ray_queue(queueB);
    free_hit_queue(hitQueue);
    free_shadow_queue(shadowQueue);

    for(auto t : tex_objs_to_free) cudaDestroyTextureObject(t);
    for(auto a : arrays_to_free) cudaFreeArray(a);

    cudaFree(d_accum_buffer);
    cudaFree(d_sample_counts);
    cudaFree(d_output);
    cudaFree(d_spheres);
    cudaFree(d_meshes);
    cudaFree(d_materials);
    cudaFree(d_light_indices);
    for (void* ptr : pointers_to_free) {
        cudaFree(ptr);
    }
}

} // namespace ure::gpu
