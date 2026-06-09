#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <stdio.h>
#include <assert.h>
#include <float.h>
#include <vector>
#include <iostream>
#include <iomanip>
#include <chrono>

#include "ure/gpu_driver.hpp"
#include "ure/gpu_structs.hpp"
#include "ure/gpu_spectrum_utils.cuh"
#include "ure/path_tracer_sampling.cuh"
#include "ure/gpu_scene_loader.hpp"
#include "ure/bvh_builder.hpp"

#define checkCudaErrors(val) check_cuda( (val), #val, __FILE__, __LINE__ )

// ===== Diagnostic Logging Pipeline =====
#define DEBUG_ENABLED 0

#if DEBUG_ENABLED
#define MAX_DEBUG_ENTRIES 4096
struct DebugEntry {
    int thread_id;
    int block_id;
    int msg_code;
    int ival;
    unsigned long long pval1;
    unsigned long long pval2;
    float fval;
};
// Global device pointer for easy kernel access
__device__ DebugEntry* g_debug_log = nullptr;
__device__ int* g_debug_count = nullptr;

#define DEVICE_LOG(code_, ival_, p1_, p2_, fv_) do { \
    if (g_debug_count) { \
        int _i_ = atomicAdd(g_debug_count, 1); \
        if (_i_ < MAX_DEBUG_ENTRIES) { \
            g_debug_log[_i_].thread_id = threadIdx.x + blockIdx.x * blockDim.x; \
            g_debug_log[_i_].block_id = blockIdx.x; \
            g_debug_log[_i_].msg_code = (int)(code_); \
            g_debug_log[_i_].ival = (int)(ival_); \
            g_debug_log[_i_].pval1 = (unsigned long long)(p1_); \
            g_debug_log[_i_].pval2 = (unsigned long long)(p2_); \
            g_debug_log[_i_].fval = (float)(fv_); \
        } \
    } \
} while(0)

void init_debug_log() {
    DebugEntry* h_entries = nullptr;
    int* h_count = nullptr;
    cudaMallocHost(&h_entries, MAX_DEBUG_ENTRIES * sizeof(DebugEntry)); // pinned = survives GPU faults
    cudaMallocHost(&h_count, sizeof(int));
    memset(h_entries, 0, MAX_DEBUG_ENTRIES * sizeof(DebugEntry));
    memset(h_count, 0, sizeof(int));
    cudaMemcpyToSymbol(g_debug_log, &h_entries, sizeof(DebugEntry*));
    cudaMemcpyToSymbol(g_debug_count, &h_count, sizeof(int*));
}

void flush_debug_log() {
    DebugEntry* h_entries = nullptr;
    int* h_count = nullptr;
    cudaMemcpyFromSymbol(&h_entries, g_debug_log, sizeof(DebugEntry*));
    cudaMemcpyFromSymbol(&h_count, g_debug_count, sizeof(int*));
    if (!h_entries || !h_count) { printf("DEBUG LOG: not initialized\n"); return; }
    int count = *h_count;
    if (count == 0) { printf("DEBUG LOG: empty\n"); return; }
    if (count > MAX_DEBUG_ENTRIES) count = MAX_DEBUG_ENTRIES;
    int show = (count > 200) ? 200 : count;
    printf("\n===== DEBUG LOG (%d total, showing %d) =====\n", count, show);
    for (int i = 0; i < show; ++i) {
        auto& e = h_entries[i];
        if (e.msg_code == 0) break; // uninitialized entry
        const char* codes[] = {"ENTRY","SCENE","SPHERE","INST","MESH","HIT","MISS","QUEUE","PTR"};
        const char* c = (e.msg_code >= 0 && e.msg_code < 9) ? codes[e.msg_code] : "???";
        printf("[%s][T%d/B%d] iv=%d p1=0x%llx p2=0x%llx fv=%.3f\n",
               c, e.thread_id, e.block_id, e.ival, e.pval1, e.pval2, e.fval);
    }
    memset(h_count, 0, sizeof(int));
}

void free_debug_log() {
    DebugEntry* h_entries = nullptr;
    int* h_count = nullptr;
    cudaMemcpyFromSymbol(&h_entries, g_debug_log, sizeof(DebugEntry*));
    cudaMemcpyFromSymbol(&h_count, g_debug_count, sizeof(int*));
    if (h_entries) cudaFreeHost(h_entries);
    if (h_count) cudaFreeHost(h_count);
    h_entries = nullptr; h_count = nullptr;
    cudaMemcpyToSymbol(g_debug_log, &h_entries, sizeof(DebugEntry*));
    cudaMemcpyToSymbol(g_debug_count, &h_count, sizeof(int*));
}

#else
#define DEVICE_LOG(code, ival, p1, p2, fv) do {} while(0)
#define init_debug_log() do {} while(0)
#define flush_debug_log() do {} while(0)
#define free_debug_log() do {} while(0)
#endif
// ===== End Diagnostic Logging =====

void check_cuda(cudaError_t result, char const *const func, const char *const file, int const line) {
    if (result) {
        std::cerr << "CUDA error = " << static_cast<unsigned int>(result) << " at " <<
            file << ":" << line << " '" << func << "' \n";
        cudaDeviceReset();
        exit(99);
    }
}

namespace ure::gpu {

using namespace ure::gpu;

// Forward declared from path_tracer_raygen.cu
__global__ void generate_rays_kernel(RayQueue queue, int width, int height, GpuCamera camera, int sample_index, int* sample_counts);

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

// scatter() is defined in path_tracer_material.cu (included at end of file)
__device__ inline bool scatter(
    const GpuRay& r_in, const GpuMaterial& mat, const GpuVec3& p, const GpuVec3& n, const GpuVec2& uv,
    const GpuSpectrum& current_throughput,
    GpuSpectrum& attenuation, GpuRay& scattered, StokesVector& stokes, unsigned int& seed,
    float& out_pdf,
    float dispersion_clamp,
    int sample_index,
    int pixel_index,
    int depth,
    int& spectral_channel,
    float ior_outside = 1.0f,
    float ior_inside = 1.0f
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

#include "ure/gpu_math_functions.cuh"

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

__device__ bool world_hit(const GpuScene& scene, const GpuRay& r, float t_min, float t_max, float& t_out, GpuVec3& p_out, GpuVec3& n_out, GpuVec3& ng_out, GpuVec2& uv_out, int& mat_idx_out, int& type_out, int& index_out, bool ignore_lights = false) {
    float t_closest = t_max;
    bool hit_anything = false;
    float t_temp;
    GpuVec3 p_temp, n_temp;
    int mat_idx_temp;

    // Check Spheres
    for (int i = 0; i < scene.sphere_count; ++i) {
        if (ignore_lights && scene.materials[scene.spheres[i].material_index].type == MaterialType::Light) continue;
        
        if (hit_sphere(scene.spheres[i], r, t_min, t_closest, t_temp, p_temp, n_temp, mat_idx_temp)) {
            hit_anything = true;
            t_closest = t_temp;
            t_out = t_temp;
            p_out = p_temp;
            n_out = n_temp;
            ng_out = n_temp; // Spheres have perfect geometry
            mat_idx_out = mat_idx_temp;
            type_out = 0; // Sphere
            index_out = i;
            
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

    // Check Instances
    for (int i = 0; i < scene.instance_count; ++i) {
        const GpuInstanceDesc& desc = scene.instance_descs[i];
        const GpuInstanceTransform& xform = scene.instance_transforms[i];
        
        // AABB Check in World Space
        if (!hit_aabb(r, xform.min_pt, xform.max_pt, t_min, t_closest)) {
            continue;
        }

        // Transform Ray to Object Space
        GpuRay r_obj = r;
        r_obj.origin = xform.inverse_transform.transform_point(r.origin);
        r_obj.direction = xform.inverse_transform.transform_vector(r.direction);
        
        // Scale t_min/t_closest?
        // t is invariant under linear transformation if we assume P(t) = O + t*D mapping holds.
        // P_world = M * P_obj => O_w + t*D_w = M(O_o + t*D_o) = M*O_o + t*M*D_o.
        // Matches O_w = M*O_o and D_w = M*D_o.
        // So t is the same.
        
        const GpuMesh& mesh = scene.meshes[desc.mesh_index];
        
        float t_mesh;
        GpuVec3 ng_mesh, ns_mesh;
        GpuVec2 uv_mesh;
        bool hit_mesh = false;

        if (mesh.bvh_node_count > 0) {
             hit_mesh = hit_bvh(mesh, r_obj, t_min, t_closest, t_mesh, ng_mesh, ns_mesh, uv_mesh);
        } else {
             // Fallback for small meshes without BVH (if any)
             // Simple linear scan over triangles
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
                    t_closest = t_tri; // Update local closest for this mesh search
                    t_mesh = t_tri;
                    ng_mesh = ng_tri;
                    ns_mesh = ns_tri;
                    
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
            t_closest = t_mesh; // Update global closest
            t_out = t_mesh;
            
            // P_out = world ray at t
            p_out = r.at(t_mesh);
            
            // Transform Normals to World Space
            // N_world = Transpose(InvM) * N_obj
            // We use the inverse transform matrix (InvM) and apply its transpose
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
            type_out = 2; // Instance
            index_out = i;
        }
    }

    // Check Meshes (AABB + BVH Optimized)
    DEVICE_LOG(4, scene.mesh_count, (unsigned long long)scene.meshes, 0, 0);
    for (int i = 0; i < scene.mesh_count; ++i) {
        GpuMesh& mesh = scene.meshes[i];
        
        // Skip hidden meshes (e.g. instance prototypes with material_index -1)
        if (mesh.material_index < 0) continue;

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
                type_out = 1; // Mesh
                index_out = i;
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

__device__ inline float get_cloth_intensity(const GpuVec3& p) {
    float freq = 20.0f; // Lower frequency for visible texture
    float noise = sinf(p.x * freq) * sinf(p.z * freq);
    // Map [-1, 1] to [0.5, 1.0] for high contrast visibility
    return 0.75f + noise * 0.25f;
}

__device__ float pdf_bsdf(const GpuMaterial& mat, const GpuVec3& n, const GpuVec3& wo, const GpuVec3& wi) {
    if (mat.type == MaterialType::Lambertian || mat.type == MaterialType::Cloth) {
        float cosine = n.dot(wi);
        return (cosine > 0.0f) ? cosine * 0.318309886f : 0.0f;
    } else if (mat.type == MaterialType::Metal) {
        GpuVec3 V = wo;
        GpuVec3 L = wi;
        GpuVec3 N = n;
        if (V.dot(N) < 0.0f) N = -N;
        
        float NdotV = N.dot(V);
        float NdotL = N.dot(L);
        if (NdotV <= 1e-6f || NdotL <= 1e-6f) return 0.0f;
        
        GpuVec3 H = (V + L).normalize();
        float NdotH = N.dot(H);
        if (NdotH <= 0.0f) return 0.0f;
        
        float D = ggx_D(NdotH, mat.roughness);
        
        float rough = fmaxf(0.001f, mat.roughness);
        float k = (rough + 1.0f);
        k = (k * k) * 0.125f;
        float G1_V = smith_G1(NdotV, k);
        
        // PDF(L) = G1(V) * D / (4 * n.v) for Visible Normal Sampling
        return (G1_V * D) / (4.0f * NdotV);
    } else if (mat.type == MaterialType::Dielectric) {
        // Delta distribution (Perfect Specular) -> PDF is Dirac Delta (effectively 0 for solid angle sampling)
        return 0.0f;
    }
    return 0.0f;
}

__device__ GpuSpectrum eval_bsdf(const GpuMaterial& mat, const GpuVec3& p, const GpuVec3& n, const GpuVec3& wo, const GpuVec3& wi, float4 wavelengths) {
    GpuSpectrum albedo_spec = rgb_to_spectrum(mat.albedo.to_rgb(), wavelengths);
    
    if (mat.type == MaterialType::Lambertian) {
        float cosine = n.dot(wi);
        if (cosine > 0.0f) {
            return albedo_spec * 0.318309886f;
        }
    } else if (mat.type == MaterialType::Cloth) {
        float cosine = n.dot(wi);
        if (cosine > 0.0f) {
            float intensity = get_cloth_intensity(p);
            return albedo_spec * intensity * 0.318309886f;
        }
    } else if (mat.type == MaterialType::Metal) {
        GpuVec3 V = wo;
        GpuVec3 L = wi;
        GpuVec3 N = n;
        if (V.dot(N) < 0.0f) N = -N;
        
        float NdotV = N.dot(V);
        float NdotL = N.dot(L);
        if (NdotV <= 1e-6f || NdotL <= 1e-6f) return GpuSpectrum(0.0f);
        
        GpuVec3 H = (V + L).normalize();
        float NdotH = N.dot(H);
        float VdotH = V.dot(H);
        
        float D = ggx_D(NdotH, mat.roughness);
        
        float rough = fmaxf(0.001f, mat.roughness);
        float k_val = (rough + 1.0f);
        k_val = (k_val * k_val) * 0.125f;
        float G = smith_G(NdotV, NdotL, k_val);
        
        // Correct Spectral Fresnel for eval_bsdf
        GpuSpectrum fresnel_spec;
        fresnel_spec.wavelengths = wavelengths;
        
        bool use_albedo_fresnel = (mat.extinction.values.x * mat.extinction.values.x + mat.extinction.values.y * mat.extinction.values.y + mat.extinction.values.z * mat.extinction.values.z) < 1e-8f;
        
        if (use_albedo_fresnel) {
            GpuSpectrum F0_spec = rgb_to_spectrum(mat.albedo.to_rgb(), wavelengths);
            float one_minus = powf(1.0f - fmaxf(0.0f, VdotH), 5.0f);
            GpuSpectrum one_spec(1.0f);
            one_spec.wavelengths = wavelengths;
            fresnel_spec = F0_spec + (one_spec - F0_spec) * one_minus;
        } else {
            GpuSpectrum K_spec = rgb_coeff_to_spectrum(mat.extinction.to_rgb(), wavelengths);
            bool use_spectral_eta = (mat.metal_eta.values.x * mat.metal_eta.values.x +
                                     mat.metal_eta.values.y * mat.metal_eta.values.y +
                                     mat.metal_eta.values.z * mat.metal_eta.values.z) > 1e-8f;
            GpuSpectrum eta_spec = use_spectral_eta
                ? rgb_coeff_to_spectrum(mat.metal_eta.to_rgb(), wavelengths)
                : GpuSpectrum(mat.ior);
            eta_spec.wavelengths = wavelengths;
            fresnel_spec.values.x = conductor_fresnel_reflectance(eta_spec.values.x, K_spec.values.x, VdotH);
            fresnel_spec.values.y = conductor_fresnel_reflectance(eta_spec.values.y, K_spec.values.y, VdotH);
            fresnel_spec.values.z = conductor_fresnel_reflectance(eta_spec.values.z, K_spec.values.z, VdotH);
            fresnel_spec.values.w = conductor_fresnel_reflectance(eta_spec.values.w, K_spec.values.w, VdotH);
        }
        
        return fresnel_spec * (D * G / (4.0f * NdotV * NdotL));
    } else if (mat.type == MaterialType::Dielectric) {
        // Delta distribution -> BSDF is Dirac Delta (cannot evaluate as function)
        return GpuSpectrum(0.0f);
    }
    return GpuSpectrum(0.0f);
}

// scatter() is now provided by path_tracer_material.cu (included below)

__device__ GpuVec3 sample_henyey_greenstein(const GpuVec3& w_in, float g, unsigned int& seed) {
    if (fabsf(g) < 1e-3f) {
        return random_unit_vector(seed);
    }

    float r1 = rand_float(seed);
    float r2 = rand_float(seed);

    // Sample cos(theta)
    float sqr_term = (1.0f - g * g) / (1.0f - g + 2.0f * g * r1);
    float cos_theta = (1.0f + g * g - sqr_term * sqr_term) / (2.0f * g);

    // Clamp for numerical stability
    if (cos_theta > 1.0f) cos_theta = 1.0f;
    if (cos_theta < -1.0f) cos_theta = -1.0f;

    float sin_theta = sqrtf(fmaxf(0.0f, 1.0f - cos_theta * cos_theta));
    float phi = 2.0f * 3.14159265359f * r2;

    // Coordinate system from w_in (forward direction)
    GpuVec3 forward = w_in.normalize();
    GpuVec3 v1;
    if (fabsf(forward.x) > 0.9f) {
        v1 = GpuVec3(0.0f, 1.0f, 0.0f);
    } else {
        v1 = GpuVec3(1.0f, 0.0f, 0.0f);
    }
    GpuVec3 v2 = forward.cross(v1).normalize();
    v1 = forward.cross(v2).normalize();

    return (v1 * (cosf(phi) * sin_theta) +
            v2 * (sinf(phi) * sin_theta) +
            forward * cos_theta).normalize();
}

__device__ float eval_henyey_greenstein(float cos_theta, float g) {
    if (fabsf(g) < 1e-3f) return 1.0f / (4.0f * 3.14159265359f);
    
    float denom = 1.0f + g * g - 2.0f * g * cos_theta;
    return (1.0f - g * g) / (4.0f * 3.14159265359f * denom * sqrtf(fmaxf(0.0f, denom)));
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
    // Stack-based medium tracking for nested dielectrics
    int medium_stack[8];
    int stack_ptr = 0; // Points to next free slot. 0 means empty (Global Medium)

    int current_medium_mat_idx = -1;

    int depth = 0;
    // Increase max_depth to prevent black artifacts in dielectrics (TIR trapping)
    int max_depth = 50; 
    
    float last_bsdf_pdf = 0.0f;
    bool last_bounce_specular = true;

    while (depth < max_depth) {
        // Update active medium from stack
        current_medium_mat_idx = (stack_ptr > 0) ? medium_stack[stack_ptr - 1] : -1;

        float t;
        GpuVec3 p, n, ng;
        GpuVec2 uv;
        int mat_idx;
        
        // Use a consistent epsilon for primary and secondary rays
        float current_t_min = (depth == 0) ? 1e-3f : r.t_min;
        
        // 1. Determine active medium properties
        float density_scale = 0.0f;
        float anisotropy = 0.0f;
        GpuSpectrum sigma_s = GpuSpectrum(0.0f);
        GpuSpectrum sigma_a = GpuSpectrum(0.0f);
        
        if (current_medium_mat_idx == -1) {
            // Global Medium
            if (scene.medium_density > 0.0f) {
                density_scale = scene.medium_density;
                anisotropy = scene.medium_anisotropy;
                sigma_s = scene.medium_scattering * density_scale;
                sigma_a = scene.medium_absorption * density_scale;
            }
        } else {
            // Inside SSS Object
            GpuMaterial mat = scene.materials[current_medium_mat_idx];
            if (mat.medium_density > 0.0f) {
                density_scale = mat.medium_density;
                anisotropy = mat.medium_anisotropy;
                sigma_s = mat.medium_scattering * density_scale;
                sigma_a = mat.medium_absorption * density_scale;
            }
        }

        GpuSpectrum sigma_t = sigma_s + sigma_a;
        float max_sigma_t = fmaxf(sigma_t.values.x, fmaxf(sigma_t.values.y, sigma_t.values.z));

        // 2. Intersect Scene (Geometry)
        int hit_type; int hit_index;
        bool hit_surface = world_hit(scene, r, current_t_min, FLT_MAX, t, p, n, ng, uv, mat_idx, hit_type, hit_index);
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

                // Volume NEE (Next Event Estimation)
                if (scene.light_count > 0) {
                    // Pick a random light
                    int light_idx_idx = min(int(rand_float(seed) * scene.light_count), scene.light_count - 1);
                    int light_idx = scene.light_indices[light_idx_idx];
                    GpuSphere light_sphere = scene.spheres[light_idx];
                    
                    GpuVec3 p_vol = r.origin + r.direction * scatter_dist;
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
                        
                        float phase_val = eval_henyey_greenstein(r.direction.dot(l_dir), anisotropy);
                        float solid_angle = 6.2831853f * (1.0f - cos_theta_max);
                        float pdf = 1.0f / (solid_angle * scene.light_count);
                        
                        // Visibility Check
                        float M_dot_D = -wc.dot(l_dir);
                        float c_val = dist_sq - radius_sq;
                        float t_to_light = -M_dot_D - sqrtf(fmaxf(0.0f, M_dot_D * M_dot_D - c_val));
                        
                        if (t_to_light > 1e-4f) {
                            float t_dummy;
                            GpuVec3 p_dummy, n_dummy, ng_dummy;
                            GpuVec2 uv_dummy;
                            int mat_dummy;
                            int type_dummy, index_dummy;
                            bool occluded = world_hit(scene, GpuRay(p_vol, l_dir, 1e-4f, t_to_light - 1e-4f), 1e-4f, t_to_light - 1e-4f, t_dummy, p_dummy, n_dummy, ng_dummy, uv_dummy, mat_dummy, type_dummy, index_dummy, true);
                            
                            if (!occluded) {
                                GpuSpectrum tr_light;
                                tr_light.values.x = expf(-sigma_t.values.x * t_to_light);
                                tr_light.values.y = expf(-sigma_t.values.y * t_to_light);
                                tr_light.values.z = expf(-sigma_t.values.z * t_to_light);
                                
                                GpuSpectrum L_e = scene.materials[light_sphere.material_index].emission;
                                final_color = final_color + accumulated_color * L_e * phase_val * tr_light * (1.0f / pdf);
                            }
                        }
                    }
                }
                
                // Update Ray
                r.origin = r.origin + r.direction * scatter_dist;
                r.direction = sample_henyey_greenstein(r.direction, anisotropy, seed); // Isotropic/Anisotropic Phase Function
                
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
            
            // ---------------------------------------------------------
            // 1. Emission (Implicit Hit) with MIS
            // ---------------------------------------------------------
            GpuSpectrum emitted = mat.emission;
            if (emitted.to_rgb().length_sq() > 0.0f) {
                float mis_weight = 1.0f;
                
                // If not the first bounce and previous bounce was non-specular, use MIS
                if (depth > 0 && !last_bounce_specular && scene.light_count > 0) {
                    float light_pdf = 0.0f;
                    
                    // Check if the hit object is a known light source
                    // For now, we only support Sphere lights in NEE, so we check if hit is a Sphere
                    if (hit_type == 0) { 
                        GpuSphere sph = scene.spheres[hit_index];
                        float area = 4.0f * 3.14159265f * sph.radius * sph.radius;
                        float dist_sq = hit_distance * hit_distance;
                        float cos_theta = fmaxf(0.0f, n.dot(-r.direction));
                        
                        if (cos_theta > 1e-6f) {
                            // PDF conversion from Area to Solid Angle
                            light_pdf = (1.0f / scene.light_count) * (1.0f / area) * (dist_sq / cos_theta);
                        }
                    }
                    
                    mis_weight = power_heuristic(last_bsdf_pdf, light_pdf);
                }
                
                GpuSpectrum contribution = accumulated_color * emitted * mis_weight;
                
                float max_radiance = (depth == 0) ? 1000.0f : 20.0f; 
                contribution.values.x = fminf(contribution.values.x, max_radiance);
                contribution.values.y = fminf(contribution.values.y, max_radiance);
                contribution.values.z = fminf(contribution.values.z, max_radiance);
                
                final_color = final_color + contribution;
            }

            // ---------------------------------------------------------
            // 2. Next Event Estimation (Direct Light Sampling)
            // ---------------------------------------------------------
            // Only for non-specular materials
            bool is_specular = (mat.type == MaterialType::Metal && mat.roughness <= 0.02f) || mat.type == MaterialType::Dielectric;
            
            if (scene.light_count > 0 && !is_specular) {
                // Pick a random light
                int light_idx_idx = min(int(rand_float(seed) * scene.light_count), scene.light_count - 1);
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
                    
                    float cos_surf = fmaxf(0.0f, n.dot(l_dir));
                    if (cos_surf > 0.0f) {
                        float solid_angle = 6.2831853f * (1.0f - cos_theta_max);
                        float light_pdf = 1.0f / (solid_angle * scene.light_count);
                        
                        // Visibility Check
                        float M_dot_D = -wc.dot(l_dir);
                        float c_val = dist_sq - radius_sq;
                        float t_to_light = -M_dot_D - sqrtf(fmaxf(0.0f, M_dot_D * M_dot_D - c_val));

                        if (t_to_light > 1e-4f) {
                            float t_dummy;
                            GpuVec3 p_dummy, n_dummy, ng_dummy;
                            GpuVec2 uv_dummy;
                            int mat_dummy, type_dummy, index_dummy;
                            
                            bool occluded = world_hit(scene, GpuRay(p + n * 1e-4f, l_dir, 1e-4f, t_to_light - 1e-4f), 1e-4f, t_to_light - 1e-4f, t_dummy, p_dummy, n_dummy, ng_dummy, uv_dummy, mat_dummy, type_dummy, index_dummy, true);
                            
                            if (!occluded) {
                            GpuSpectrum L_e = emission_to_spectrum(scene.materials[light_sphere.material_index].emission.to_rgb(), accumulated_color.wavelengths);
                                GpuSpectrum f_r = eval_bsdf(mat, p, n, -r.direction, l_dir, accumulated_color.wavelengths);
                                float bsdf_pdf = pdf_bsdf(mat, n, -r.direction, l_dir);
                                float mis_weight = power_heuristic(light_pdf, bsdf_pdf);
                                
                                GpuSpectrum ne_contrib = accumulated_color * L_e * f_r * cos_surf * (mis_weight / light_pdf);
                                final_color = final_color + ne_contrib;
                            }
                        }
                    }
                }
            }

            GpuRay scattered;
            GpuSpectrum attenuation;
            
            // Pass default dispersion clamp (10.0f) for megakernel path
            // Reduced from 20.0f to reduce fireflies in caustics
            float pdf_val = 0.0f;

            // Determine IOR of the surrounding medium (ior_outside parameter)
            // If Entering (front_face): ior_outside is the medium we are IN (Stack Top)
            // If Exiting (!front_face): ior_outside is the medium we are GOING TO (Stack Top - 1)
            
            bool is_entering = r.direction.dot(ng) < 0.0f; // Geometric normal determines entry/exit
            
            float ior_surrounding = 1.0f;
            int surrounding_mat_idx = -1;
            
            if (is_entering) {
                surrounding_mat_idx = (stack_ptr > 0) ? medium_stack[stack_ptr - 1] : -1;
            } else {
                // Exiting: We expect to pop the current material, so look at what's below it
                surrounding_mat_idx = (stack_ptr > 1) ? medium_stack[stack_ptr - 2] : -1;
            }
            
            if (surrounding_mat_idx >= 0) {
                GpuMaterial surr_mat = scene.materials[surrounding_mat_idx];
                ior_surrounding = surr_mat.ior;
                
                // Apply Dispersion to surrounding IOR if needed
                if (surr_mat.dispersion > 0.0f && spectral_channel > 0) {
                     float lambda = 550.0f; // Default Green
                     if (spectral_channel == 1) lambda = 650.0f; // Red
                     else if (spectral_channel == 2) lambda = 550.0f; // Green
                     else if (spectral_channel == 3) lambda = 450.0f; // Blue
                     
                     float inv_lambda2 = 1.0f / (lambda * lambda);
                     float inv_ref2 = 1.0f / (550.0f * 550.0f);
                     float offset = (inv_lambda2 - inv_ref2) * 4e5f;
                     ior_surrounding += surr_mat.dispersion * offset;
                }
            }

            if (scatter(r, mat, p, n, uv, accumulated_color, attenuation, scattered, current_stokes, seed, pdf_val, 10.0f, sample_index, pixel_index, depth, spectral_channel, ior_surrounding)) {
                accumulated_color = accumulated_color * attenuation;
                
                // Track Medium Enter/Exit for Dielectrics
                if (mat.type == MaterialType::Dielectric) {
                    // Check if we transmitted (Refracted)
                    float in_dot = r.direction.dot(ng);
                    float out_dot = scattered.direction.dot(ng);
                    
                    if ((in_dot * out_dot) > 0.0f) {
                        // Transmitted
                        if (in_dot < 0.0f) {
                            // Entering: Push to stack
                            if (stack_ptr < 8) {
                                medium_stack[stack_ptr++] = mat_idx;
                            }
                        } else {
                            // Exiting: Pop from stack if it matches
                            if (stack_ptr > 0) {
                                // Ideal: Pop the specific material. 
                                // Simple: Pop top if it matches current mat_idx.
                                // If top doesn't match (e.g. overlapping geometry issue), we should probably scan or just pop top.
                                // For robust nested dielectrics, usually we assume strict nesting.
                                if (medium_stack[stack_ptr - 1] == mat_idx) {
                                    stack_ptr--;
                                }
                            }
                        }
                    }
                    // Else Reflected: Stack stays the same
                }

                // Robust NaN check (checking first value as proxy)
                if (accumulated_color.values.x != accumulated_color.values.x) {
                    accumulated_color = GpuSpectrum::from_rgb(GpuVec3(0,0,0));
                    break; 
                }
                
                // Update MIS state for next bounce
                last_bsdf_pdf = pdf_val;
                
                // Specular threshold should match NEE check (Roughness <= 0.02 is treated as specular)
                last_bounce_specular = (mat.type == MaterialType::Dielectric) || (mat.type == MaterialType::Metal && mat.roughness <= 0.02f);

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
                GpuSpectrum sun_spec = emission_to_spectrum(GpuVec3(sun_intensity, sun_intensity, sun_intensity), accumulated_color.wavelengths);
                final_color = final_color + accumulated_color * sun_spec;
            } else {
                float t_sky = 0.5f * (unit_direction.y + 1.0f);
                GpuVec3 sky_color;
                if (scene.medium_density > 1e-6f) {
                    float sky_luma = 0.05f + 0.15f * t_sky;
                    sky_color = GpuVec3(sky_luma, sky_luma, sky_luma);
                } else {
                    sky_color = (1.0f - t_sky) * GpuVec3(0.05f, 0.05f, 0.05f) + t_sky * GpuVec3(0.2f, 0.2f, 0.4f);
                }
                GpuSpectrum sky_spec = emission_to_spectrum(sky_color, accumulated_color.wavelengths);
                final_color = final_color + accumulated_color * sky_spec;
            }
            break;
        }
    }
    
    if (depth == max_depth) return GpuVec3(0,0,0);
    
    GpuVec3 xyz = spectrum_to_xyz(final_color);
    return xyz_to_rgb(xyz);
}

// ==========================================
// Wavefront Kernels
// ==========================================

__global__ void extend_kernel(
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
    
    // Consistent epsilon with megakernel
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
        hit_queue.mat_ids[idx] = -1; // Miss
    }
}

__device__ GpuSpectrum sample_texture(const GpuScene& scene, int tex_idx, float u, float v, float4 wavelengths) {
    if (tex_idx < 0 || tex_idx >= scene.texture_count) return rgb_to_spectrum(GpuVec3(1,0,1), wavelengths); // Error pink
    
    // Pointer arithmetic to get texture
    GpuTexture tex = scene.textures[tex_idx];

    // Hardware Texture Sampling
    if (tex.texObj) {
        float4 val = tex2D<float4>(tex.texObj, u, v);
        return rgb_to_spectrum(GpuVec3(val.x, val.y, val.z), wavelengths);
    }

    if (!tex.data) return rgb_to_spectrum(GpuVec3(0,0,0), wavelengths);
    
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
    GpuSpectrum c00 = rgb_to_spectrum(tex.data[y0 * tex.width + x0].to_rgb(), wavelengths);
    GpuSpectrum c10 = rgb_to_spectrum(tex.data[y0 * tex.width + x1].to_rgb(), wavelengths);
    GpuSpectrum c01 = rgb_to_spectrum(tex.data[y1 * tex.width + x0].to_rgb(), wavelengths);
    GpuSpectrum c11 = rgb_to_spectrum(tex.data[y1 * tex.width + x1].to_rgb(), wavelengths);
    
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

// Shadow extension kernel (Visibility & Transmission)
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

    GpuRay r(origin, direction, 1e-4f, max_dist);
    
    for (int pass = 0; pass < 8; ++pass) {
        float t;
        GpuVec3 p, n, ng;
        GpuVec2 uv;
        int mat_idx;
        int type_dummy; int index_dummy;
        
        if (!world_hit(scene, r, 1e-4f, r.t_max, t, p, n, ng, uv, mat_idx, type_dummy, index_dummy, true)) {
            // Unoccluded visibility!
            break;
        }
        
        GpuMaterial mat = scene.materials[mat_idx];
        
        // If we hit a light, the shadow ray is not blocked (it reached its destination)
        if (mat.type == MaterialType::Light) {
            r.origin = p + r.direction * 1e-4f;
            r.t_max -= (t + 1e-4f);
            if (r.t_max <= 1e-4f) break;
            continue;
        }
        
        // If we hit an opaque object, the shadow ray is blocked
        if (mat.type != MaterialType::Dielectric) {
            return;
        }
        
        // Handle transparency (Dielectrics)
        float cos_theta = fminf(fabsf(r.direction.dot(n)), 1.0f);
        float fresnel = schlick(cos_theta, mat.ior);
        float transmission = 1.0f - fresnel;
        
        // Apply attenuation
        GpuSpectrum albedo_spec = rgb_coeff_to_spectrum(mat.albedo.to_rgb(), radiance.wavelengths);
        radiance = radiance * (albedo_spec * transmission);
        
        // Check for total energy loss
        GpuVec3 rgb_check = xyz_to_rgb(spectrum_to_xyz(radiance));
        if (fmaxf(rgb_check.x, fmaxf(rgb_check.y, rgb_check.z)) < 1e-5f) return;
        
        // Continue through the object in a STRAIGHT line
        r.origin = p + r.direction * 1e-4f;
        r.t_max -= (t + 1e-4f);
        if (r.t_max <= 1e-4f) break;
    }
    
    // Add contribution
    GpuVec3 xyz = spectrum_to_xyz(radiance);
    GpuVec3 rgb = xyz_to_rgb(xyz);
    
    // Safety clamp
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

// ============================= SHADE KERNEL =============================

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
    
    // --- Volume / Medium Setup ---
    int current_medium_idx = current_queue.medium_indices[idx];
    float t_hit = (mat_idx != -1) ? hit_queue.t[idx] : 1e30f;
    
    float density = 0.0f;
    float anisotropy = 0.0f;
    GpuSpectrum sigma_s(0.0f);
    GpuSpectrum sigma_a(0.0f);
    
    if (current_medium_idx == -1) {
        // Global Medium
        density = scene.medium_density;
        anisotropy = scene.medium_anisotropy;
        sigma_s = scene.medium_scattering;
        sigma_a = scene.medium_absorption;
    } else {
        // Material Medium (SSS)
        GpuMaterial med_mat = scene.materials[current_medium_idx];
        density = med_mat.medium_density;
        anisotropy = med_mat.medium_anisotropy;
        sigma_s = med_mat.medium_scattering;
        sigma_a = med_mat.medium_absorption;
    }
    
    // Simple monochromatic approximation for distance sampling
    GpuSpectrum sigma_t = (sigma_s + sigma_a) * density;
    GpuVec3 sigma_t_rgb = sigma_t.to_rgb();
    float sigma_t_avg = (sigma_t_rgb.x + sigma_t_rgb.y + sigma_t_rgb.z) / 3.0f;
    
    if (sigma_t_avg > 1e-4f) {
        float r_dist = rand_float(seed);
        float t_medium = -logf(1.0f - r_dist) / sigma_t_avg;
        
        if (t_medium < t_hit) {
            
            // --- Volume Scatter Path ---
            // Transmittance & PDF Weight
            // Weight = (sigma_s / sigma_t) * Phase(w) / PDF(w)
            // Isotropic: Phase = 1/4pi, PDF = 1/4pi -> Cancel out.
            // Result: Throughput *= sigma_s / sigma_t (Albedo)
            
            // Chromatic handling:
            // Tr = exp(-sigma_t * t)
            // pdf_t = sigma_t_avg * exp(-sigma_t_avg * t)
            // Weight = Tr / pdf_t
            
            GpuVec3 tr_vals;
            tr_vals.x = expf(-sigma_t_rgb.x * t_medium);
            tr_vals.y = expf(-sigma_t_rgb.y * t_medium);
            tr_vals.z = expf(-sigma_t_rgb.z * t_medium);
            
            float pdf_t = sigma_t_avg * expf(-sigma_t_avg * t_medium);
            
            GpuSpectrum tr_spectrum = rgb_coeff_to_spectrum(tr_vals, throughput.wavelengths);
            
            GpuSpectrum sigma_s_eff = rgb_coeff_to_spectrum(sigma_s.to_rgb(), throughput.wavelengths) * density;
            throughput = throughput * tr_spectrum * sigma_s_eff * (1.0f / pdf_t);
            
            // Volume NEE (Next Event Estimation)
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
                         GpuVec3 tr_light_vals;
                         tr_light_vals.x = expf(-sigma_t_rgb.x * t_to_light);
                         tr_light_vals.y = expf(-sigma_t_rgb.y * t_to_light);
                         tr_light_vals.z = expf(-sigma_t_rgb.z * t_to_light);
                         
                         GpuSpectrum tr_light = rgb_coeff_to_spectrum(tr_light_vals, throughput.wavelengths);
                         GpuSpectrum L_e = emission_to_spectrum(scene.materials[light_sphere.material_index].emission.to_rgb(), throughput.wavelengths);
                         GpuSpectrum contribution = throughput * L_e * phase_val * tr_light * (1.0f / pdf);
                         
                         int s_idx = atomicAdd(shadow_queue.count, 1);
                         if (s_idx < shadow_queue.capacity) {
                             shadow_queue.origins[s_idx] = p_vol;
                             shadow_queue.directions[s_idx] = l_dir;
                             shadow_queue.max_dist[s_idx] = t_to_light - 1e-4f;
                             shadow_queue.radiance[s_idx] = contribution;
                             shadow_queue.pixel_indices[s_idx] = pixel_index;
                         }
                    }
                }
            }
            
            // Scatter Direction (Isotropic/Anisotropic)
            GpuVec3 new_dir = sample_henyey_greenstein(current_queue.directions[idx], anisotropy, seed);
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
             // --- Surface Hit Path 鈥?Apply Transmittance up to t_hit ---
            GpuVec3 tr_vals;
            tr_vals.x = expf(-sigma_t.values.x * t_hit);
            tr_vals.y = expf(-sigma_t.values.y * t_hit);
            tr_vals.z = expf(-sigma_t.values.z * t_hit);
            
            // Probability of NOT scattering (t_medium > t_hit)
            // P(t > t_hit) = (1/3) * sum( exp(-sigma_t[i] * t_hit) )
            float prob_no_scatter = (tr_vals.x + tr_vals.y + tr_vals.z) / 3.0f;
            
            // Robustness: Avoid division by zero
            if (prob_no_scatter > 1e-6f) {
                GpuSpectrum tr_spec = rgb_coeff_to_spectrum(tr_vals, throughput.wavelengths);
                throughput = throughput * tr_spec * (1.0f / prob_no_scatter);
            } else {
                throughput = GpuSpectrum(0.0f);
            }
        }
    }

    if (mat_idx == -1) {
        // Miss: Sky color
        GpuVec3 unit_direction = current_queue.directions[idx].normalize();
        float t_sky = 0.5f * (unit_direction.y + 1.0f);
        GpuVec3 sky_color;
        if (scene.medium_density > 1e-6f || current_medium_idx != -1) {
            float sky_luma = 0.05f + 0.15f * t_sky;
            sky_color = GpuVec3(sky_luma, sky_luma, sky_luma);
        } else {
            sky_color = (1.0f - t_sky) * GpuVec3(0.05f, 0.05f, 0.05f) + t_sky * GpuVec3(0.2f, 0.2f, 0.4f);
        }
        
        // Use full spectral pipeline for sky
        GpuSpectrum sky_spectrum = emission_to_spectrum(sky_color, throughput.wavelengths);
        GpuSpectrum contribution = throughput * sky_spectrum;
        
        GpuVec3 xyz = spectrum_to_xyz(contribution);
        GpuVec3 rgb = xyz_to_rgb(xyz);

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
    GpuVec2 hit_uv = hit_queue.uv[idx];
    if (mat.texture_index != -1) {
        GpuSpectrum tex_color = sample_texture(scene, mat.texture_index, hit_uv.u, hit_uv.v, throughput.wavelengths);
        mat.albedo = mat.albedo * tex_color;
    }
    if (mat.roughness_texture_index != -1) {
        GpuSpectrum tex_roughness = sample_texture(scene, mat.roughness_texture_index, hit_uv.u, hit_uv.v, throughput.wavelengths);
        GpuVec3 tex_rgb = tex_roughness.to_rgb();
        float tex_value = fminf(1.0f, fmaxf(0.0f, (tex_rgb.x + tex_rgb.y + tex_rgb.z) / 3.0f));
        mat.roughness = fminf(1.0f, fmaxf(0.001f, mat.roughness * tex_value));
    }
    if (mat.emission_texture_index != -1) {
        GpuSpectrum tex_emission = sample_texture(scene, mat.emission_texture_index, hit_uv.u, hit_uv.v, throughput.wavelengths);
        mat.emission = mat.emission * tex_emission;
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
        // MIS Logic for Implicit Light Hits
        float mis_weight = 1.0f;
        
        // If previous bounce was diffuse (flag & 1 == 0) and we have lights, 
        // we need to balance against NEE.
        if (depth > 0 && !(flag & 1) && scene.light_count > 0) {
             float light_area = 0.0f;
             
             // Identify the light source (Sphere only for now)
             for(int k=0; k<scene.light_count; ++k) {
                 int l_idx = scene.light_indices[k];
                 GpuSphere sph = scene.spheres[l_idx];
                 // Simple check: Material match + Radius check
                 if (sph.material_index == mat_idx) {
                      light_area = 4.0f * 3.14159f * sph.radius * sph.radius;
                      break;
                 }
             }
             
             if (light_area > 0.0f) {
                 float dist_sq = (p - current_queue.origins[idx]).length_sq();
                 float cos_theta = fmaxf(0.0f, (-current_queue.directions[idx]).dot(n));
                 
                 if (cos_theta > 1e-6f) {
                     // PDF_nee (Solid Angle) = (1/N) * (1/Area) * (dist^2 / cos_theta)
                     float pdf_nee = (1.0f / scene.light_count) * (1.0f / light_area) * (dist_sq / cos_theta);
                     float last_pdf = current_queue.last_pdf[idx];
                     
                     // Power Heuristic
                     mis_weight = (last_pdf * last_pdf) / (last_pdf * last_pdf + pdf_nee * pdf_nee);
                 } else {
                     mis_weight = 0.0f;
                 }
             }
        }
        
        if (mis_weight > 0.0f) {
            GpuSpectrum emission_spectrum = emission_to_spectrum(mat.emission.to_rgb(), throughput.wavelengths);
            GpuSpectrum contribution = throughput * emission_spectrum * mis_weight;
            
            GpuVec3 xyz = spectrum_to_xyz(contribution);
            GpuVec3 rgb = xyz_to_rgb(xyz);
            
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

    // --- NEE: Next Event Estimation ---
    // Only for non-specular materials (Lambertian, Cloth, Rough Metal)
    if (scene.light_count > 0 && (mat.type == MaterialType::Lambertian || mat.type == MaterialType::Cloth || (mat.type == MaterialType::Metal && mat.roughness > 0.02f))) {
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
                 GpuSpectrum L_e = emission_to_spectrum(scene.materials[light_sphere.material_index].emission.to_rgb(), throughput.wavelengths);
                 
                 // BRDF (Lambertian = albedo / PI)
                 GpuSpectrum f_r = eval_bsdf(mat, p, n, -current_queue.directions[idx], l_dir, throughput.wavelengths);
                 
                 // MIS Weight (Power Heuristic)
                 float pdf_mat = pdf_bsdf(mat, n, -current_queue.directions[idx], l_dir);
                 float mis_weight = (pdf * pdf) / (pdf * pdf + pdf_mat * pdf_mat);
                 
                 // Contribution = Le * fr * cos_surf / PDF * MIS_Weight
                 GpuSpectrum contribution = throughput * L_e * f_r * cos_surf * (1.0f / pdf) * mis_weight;
                 
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
                             tr_vals.x = expf(-sigma_t_rgb.x * t_hit);
                             tr_vals.y = expf(-sigma_t_rgb.y * t_hit);
                             tr_vals.z = expf(-sigma_t_rgb.z * t_hit);
                             
                             GpuSpectrum tr_spec = rgb_coeff_to_spectrum(tr_vals, throughput.wavelengths);
                             contribution = contribution * tr_spec;
                        }

                        // Queue Shadow Ray
                        int s_idx = atomicAdd(shadow_queue.count, 1);
                        if (s_idx < shadow_queue.capacity) {
                            // Robust Adaptive Offset
                            float adaptive_eps = 1e-4f / fmaxf(0.01f, ng.dot(l_dir));
                            shadow_queue.origins[s_idx] = p + ng * adaptive_eps;
                            shadow_queue.directions[s_idx] = l_dir;
                            shadow_queue.max_dist[s_idx] = t_hit - adaptive_eps; 
                            shadow_queue.radiance[s_idx] = contribution;
                            shadow_queue.pixel_indices[s_idx] = pixel_index;
                        }
                    }
                }
            }
        }
    }
    
    // --- BSDF Scatter ---
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

            float pdf_val = 0.0f;
            float ior_outside = 1.0f;
            bool front_face = r_in.direction.dot(ng) < 0.0f;
            if (front_face && current_medium_idx >= 0) {
                ior_outside = scene.materials[current_medium_idx].ior;
            }
            if (scatter(r_in, mat, p, n, uv, throughput, attenuation, scattered, current_stokes, seed, pdf_val, dispersion_clamp, sample_index, pixel_index, depth, spectral_channel, ior_outside)) {
                GpuSpectrum new_throughput = throughput * attenuation;
                
                // Robust NaN check
                if (!isfinite(new_throughput.values.x) || !isfinite(new_throughput.values.y) || 
                    !isfinite(new_throughput.values.z) || !isfinite(new_throughput.values.w)) {
                    return;
                }

                // Russian Roulette
                if (depth > 3) {
                    GpuVec3 xyz = spectrum_to_xyz(new_throughput);
                    GpuVec3 rgb = xyz_to_rgb(xyz);
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
            next_queue.last_pdf[out_idx] = pdf_val;
            
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

// ============================= END SHADE KERNEL =============================

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
    cudaMalloc(&q.last_pdf, capacity * sizeof(float)); // MIS
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
    cudaFree(q.last_pdf); // MIS
    cudaFree(q.count);
}

void alloc_hit_queue(HitQueue& q, int capacity) {
    cudaMalloc(&q.t, capacity * sizeof(float));
    cudaMalloc(&q.p, capacity * sizeof(GpuVec3));
    cudaMalloc(&q.n, capacity * sizeof(GpuVec3));
    cudaMalloc(&q.ng, capacity * sizeof(GpuVec3));
    cudaMalloc(&q.uv, capacity * sizeof(GpuVec2));
    cudaMalloc(&q.mat_ids, capacity * sizeof(int));
    cudaMalloc(&q.hit_types, capacity * sizeof(int));
    cudaMalloc(&q.hit_indices, capacity * sizeof(int));
}

void free_hit_queue(HitQueue& q) {
    cudaFree(q.t);
    cudaFree(q.p);
    cudaFree(q.n);
    cudaFree(q.ng);
    cudaFree(q.uv);
    cudaFree(q.mat_ids);
    cudaFree(q.hit_types);
    cudaFree(q.hit_indices);
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

// --- Interactive API Implementation ---

static void compute_aabb(const std::vector<float>& vertices, GpuVec3& min_pt, GpuVec3& max_pt) {
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
}

struct GpuContext {
    int width;
    int height;
    int current_spp;
    
    // Buffers
    GpuVec3* d_output;          // Final Resolve Buffer (LDR/HDR)
    GpuVec3* d_accum_buffer;    // Accumulation Buffer (HDR)
    GpuVec3* d_accum_sq_buffer; // Variance Buffer
    int* d_sample_counts;       // Per-pixel counts
    
    GpuVec3* d_normal_buffer;   // Albedo/Normal for denoising
    GpuVec3* d_albedo_buffer;
    
    // Wavefront Queues
    RayQueue queueA, queueB;
    HitQueue hitQueue;
    ShadowQueue shadowQueue;
    
    // Scene Data Pointers
    GpuMaterial* d_materials;
    GpuSphere* d_spheres;
    GpuMesh* d_meshes;
    GpuInstance* d_instances;            // Legacy (desc + xform combined)
    GpuInstanceDesc* d_instance_descs;   // Phase P.7: separate desc array (proper 8B stride)
    GpuInstanceTransform* d_instance_transforms; // Phase P.1: dynamic transforms
    GpuTexture* d_textures;
    int* d_light_indices;
    
    // Scene Counts
    int material_count;
    int sphere_count;
    int mesh_count;
    int instance_count;
    int texture_count;
    int light_count;
    
    // Camera
    GpuCamera camera;
    
    // Medium
    float medium_density;
    float medium_anisotropy;
    GpuSpectrum medium_scattering;
    GpuSpectrum medium_absorption;
    float medium_max_distance;
    
    // Cleanup Lists
    std::vector<void*> pointers_to_free;
    std::vector<cudaArray_t> arrays_to_free;
    std::vector<cudaTextureObject_t> tex_objs_to_free;
};

GpuContext* init_gpu_renderer(int width, int height,
                              const std::vector<ure::gpu::RenderMesh>& meshes,
                              const std::vector<ure::gpu::GpuInstance>& instances,
                              const std::vector<ure::gpu::GpuSphere>& spheres,
                              const std::vector<ure::gpu::GpuMaterial>& materials,
                              const std::vector<ure::gpu::HostTexture>& textures) {
    GpuContext* ctx = new GpuContext();
    ctx->width = width;
    ctx->height = height;
    ctx->current_spp = 0;
    
    // Initialize default medium (vacuum)
    ctx->medium_density = 0.0f;
    ctx->medium_anisotropy = 0.0f;
    ctx->medium_scattering = GpuSpectrum(0.0f);
    ctx->medium_absorption = GpuSpectrum(0.0f);
    ctx->medium_max_distance = 1e6f;
    
    std::cout << "[GPU] Allocating memory for " << width << "x" << height << " interactive session..." << std::endl;

    size_t framebuffer_size = width * height * sizeof(GpuVec3);
    cudaMalloc(&ctx->d_output, framebuffer_size);
    cudaMalloc(&ctx->d_accum_buffer, framebuffer_size);
    cudaMalloc(&ctx->d_accum_sq_buffer, framebuffer_size); // Not used yet but allocated
    cudaMalloc(&ctx->d_sample_counts, width * height * sizeof(int));
    
    cudaMemset(ctx->d_accum_buffer, 0, framebuffer_size);
    cudaMemset(ctx->d_accum_sq_buffer, 0, framebuffer_size);
    cudaMemset(ctx->d_sample_counts, 0, width * height * sizeof(int));

    // Allocate Feature Buffers for Denoising
    cudaMalloc(&ctx->d_normal_buffer, framebuffer_size);
    cudaMemset(ctx->d_normal_buffer, 0, framebuffer_size);
    cudaMalloc(&ctx->d_albedo_buffer, framebuffer_size);
    cudaMemset(ctx->d_albedo_buffer, 0, framebuffer_size);
    
    init_debug_log();
    
    // Queues
    int max_rays = width * height;
    alloc_ray_queue(ctx->queueA, max_rays);
    alloc_ray_queue(ctx->queueB, max_rays);
    alloc_hit_queue(ctx->hitQueue, max_rays);
    alloc_shadow_queue(ctx->shadowQueue, max_rays);
    
    // Scene Setup
    bool use_default_geometry = spheres.empty() && meshes.empty() && instances.empty();
    GpuHostScene host_scene = load_default_scene(!use_default_geometry);
    if (!textures.empty()) {
        host_scene.textures.insert(host_scene.textures.end(), textures.begin(), textures.end());
    }
    
    std::vector<GpuMaterial>& host_materials = host_scene.materials;
    if (!materials.empty()) {
        host_materials.insert(host_materials.end(), materials.begin(), materials.end());
    }
    
    cudaMalloc(&ctx->d_materials, host_materials.size() * sizeof(GpuMaterial));
    cudaMemcpy(ctx->d_materials, host_materials.data(), host_materials.size() * sizeof(GpuMaterial), cudaMemcpyHostToDevice);
    ctx->material_count = (int)host_materials.size();
    
    // Spheres
    std::vector<GpuSphere> host_spheres = spheres;
    if (spheres.empty() && meshes.empty()) {
        host_spheres = host_scene.spheres;
    }
    cudaMalloc(&ctx->d_spheres, host_spheres.size() * sizeof(GpuSphere));
    cudaMemcpy(ctx->d_spheres, host_spheres.data(), host_spheres.size() * sizeof(GpuSphere), cudaMemcpyHostToDevice);
    ctx->sphere_count = (int)host_spheres.size();
    
    // Meshes
    std::vector<GpuMesh> host_gpu_meshes;
    
    // Input Meshes
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
             ctx->pointers_to_free.push_back(d_uv);
        } else { mesh.uvs = nullptr; }

        if (!input_mesh.normals.empty()) {
             size_t n_size = input_mesh.normals.size() * sizeof(float);
             GpuVec3* d_n;
             cudaMalloc(&d_n, n_size);
             cudaMemcpy(d_n, input_mesh.normals.data(), n_size, cudaMemcpyHostToDevice);
             mesh.normals = d_n;
             ctx->pointers_to_free.push_back(d_n);
        } else { mesh.normals = nullptr; }

        compute_aabb(input_mesh.vertices, mesh.min_pt, mesh.max_pt);

        std::vector<int> temp_indices = input_mesh.indices;
        std::vector<GpuBvhNode> build_nodes;
        MeshBvhBuilder::build(input_mesh.vertices, temp_indices, build_nodes);

        if (!build_nodes.empty()) {
             size_t bvh_size = build_nodes.size() * sizeof(GpuBvhNode);
             GpuBvhNode* d_nodes;
             cudaMalloc(&d_nodes, bvh_size);
             cudaMemcpy(d_nodes, build_nodes.data(), bvh_size, cudaMemcpyHostToDevice);
             mesh.bvh_nodes = d_nodes;
             mesh.bvh_node_count = (int)build_nodes.size();
             ctx->pointers_to_free.push_back(d_nodes);
        } else { mesh.bvh_nodes = nullptr; mesh.bvh_node_count = 0; }

        size_t v_size = input_mesh.vertices.size() * sizeof(float);
        GpuVec3* d_v;
        cudaMalloc(&d_v, v_size);
        cudaMemcpy(d_v, input_mesh.vertices.data(), v_size, cudaMemcpyHostToDevice);
        mesh.vertices = d_v;
        ctx->pointers_to_free.push_back(d_v);

        size_t i_size = temp_indices.size() * sizeof(int);
        int* d_i;
        cudaMalloc(&d_i, i_size);
        cudaMemcpy(d_i, temp_indices.data(), i_size, cudaMemcpyHostToDevice);
        mesh.indices = d_i;
        ctx->pointers_to_free.push_back(d_i);
        
        host_gpu_meshes.push_back(mesh);
    }
    
    // Scene Meshes
    for (auto& hm : host_scene.meshes) {
        GpuMesh mesh;
        mesh.triangle_count = (int)hm.indices.size() / 3;
        mesh.material_index = hm.material_index;
        compute_aabb(hm.vertices, mesh.min_pt, mesh.max_pt);
        std::vector<GpuBvhNode> build_nodes;
        MeshBvhBuilder::build(hm.vertices, hm.indices, build_nodes);
        if (!build_nodes.empty()) {
             size_t bvh_size = build_nodes.size() * sizeof(GpuBvhNode);
             GpuBvhNode* d_nodes;
             cudaMalloc(&d_nodes, bvh_size);
             cudaMemcpy(d_nodes, build_nodes.data(), bvh_size, cudaMemcpyHostToDevice);
             mesh.bvh_nodes = d_nodes;
             mesh.bvh_node_count = (int)build_nodes.size();
             ctx->pointers_to_free.push_back(d_nodes);
        } else { mesh.bvh_nodes = nullptr; mesh.bvh_node_count = 0; }
        size_t v_size = hm.vertices.size() * sizeof(float);
        GpuVec3* d_v;
        cudaMalloc(&d_v, v_size);
        cudaMemcpy(d_v, hm.vertices.data(), v_size, cudaMemcpyHostToDevice);
        mesh.vertices = d_v;
        ctx->pointers_to_free.push_back(d_v);
        if (!hm.normals.empty()) {
            size_t n_size = hm.normals.size() * sizeof(float);
            GpuVec3* d_n;
            cudaMalloc(&d_n, n_size);
            cudaMemcpy(d_n, hm.normals.data(), n_size, cudaMemcpyHostToDevice);
            mesh.normals = d_n;
            ctx->pointers_to_free.push_back(d_n);
        } else { mesh.normals = nullptr; }
        if (!hm.uvs.empty()) {
            size_t uv_size = hm.uvs.size() * sizeof(float);
            GpuVec2* d_uv;
            cudaMalloc(&d_uv, uv_size);
            cudaMemcpy(d_uv, hm.uvs.data(), uv_size, cudaMemcpyHostToDevice);
            mesh.uvs = d_uv;
            ctx->pointers_to_free.push_back(d_uv);
        } else { mesh.uvs = nullptr; }
        size_t i_size = hm.indices.size() * sizeof(int);
        int* d_i;
        cudaMalloc(&d_i, i_size);
        cudaMemcpy(d_i, hm.indices.data(), i_size, cudaMemcpyHostToDevice);
        mesh.indices = d_i;
        ctx->pointers_to_free.push_back(d_i);
        host_gpu_meshes.push_back(mesh);
    }
    
    {
        size_t mesh_bytes = host_gpu_meshes.size() * sizeof(GpuMesh);
        if (mesh_bytes == 0) mesh_bytes = sizeof(GpuMesh); // avoid nullptr from zero-size cudaMalloc
        cudaMalloc(&ctx->d_meshes, mesh_bytes);
        if (!host_gpu_meshes.empty())
            cudaMemcpy(ctx->d_meshes, host_gpu_meshes.data(), host_gpu_meshes.size() * sizeof(GpuMesh), cudaMemcpyHostToDevice);
    }
    ctx->mesh_count = (int)host_gpu_meshes.size();
    
    // Instances
    std::vector<GpuInstance> host_instances = instances;
    // If we have instances in the host scene (future proofing), we might merge them here
    // For now, just use the passed instances
    
    {
        size_t inst_bytes = host_instances.size() * sizeof(GpuInstance);
        if (inst_bytes == 0) inst_bytes = sizeof(GpuInstance);
        cudaMalloc(&ctx->d_instances, inst_bytes);
        if (!host_instances.empty())
            cudaMemcpy(ctx->d_instances, host_instances.data(), host_instances.size() * sizeof(GpuInstance), cudaMemcpyHostToDevice);
    }
    {
        // Phase P.7: allocate separate GpuInstanceDesc array (8B stride instead of 160B)
        size_t desc_bytes = host_instances.size() * sizeof(GpuInstanceDesc);
        if (desc_bytes == 0) desc_bytes = sizeof(GpuInstanceDesc);
        cudaMalloc(&ctx->d_instance_descs, desc_bytes);
        if (!host_instances.empty()) {
            std::vector<GpuInstanceDesc> host_descs(host_instances.size());
            for (size_t i = 0; i < host_instances.size(); ++i) {
                host_descs[i].mesh_index = host_instances[i].mesh_index;
                host_descs[i].material_index = host_instances[i].material_index;
            }
            cudaMemcpy(ctx->d_instance_descs, host_descs.data(), desc_bytes, cudaMemcpyHostToDevice);
        }
        ctx->pointers_to_free.push_back(ctx->d_instance_descs);
    }
    {
        std::vector<GpuInstanceTransform> host_transforms(host_instances.size());
        for (size_t i = 0; i < host_instances.size(); ++i) {
            host_transforms[i].transform = host_instances[i].transform;
            host_transforms[i].inverse_transform = host_instances[i].inverse_transform;
            host_transforms[i].min_pt = host_instances[i].min_pt;
            host_transforms[i].max_pt = host_instances[i].max_pt;
        }
        size_t xform_bytes = host_transforms.size() * sizeof(GpuInstanceTransform);
        if (xform_bytes == 0) xform_bytes = sizeof(GpuInstanceTransform);
        cudaMalloc(&ctx->d_instance_transforms, xform_bytes);
        if (!host_transforms.empty())
            cudaMemcpy(ctx->d_instance_transforms, host_transforms.data(), xform_bytes, cudaMemcpyHostToDevice);
        ctx->pointers_to_free.push_back(ctx->d_instance_transforms);
    }
    ctx->instance_count = (int)host_instances.size();
    
    // Textures
    std::vector<GpuTexture> host_gpu_textures;
    for (const auto& h_tex : host_scene.textures) {
        GpuTexture d_tex;
        d_tex.width = h_tex.width;
        d_tex.height = h_tex.height;
        size_t size_bytes = h_tex.width * h_tex.height * sizeof(GpuSpectrum);
        cudaMallocManaged(&d_tex.data, size_bytes);
        std::vector<GpuSpectrum> temp_spec(h_tex.width * h_tex.height);
        std::vector<float4> temp_float4(h_tex.width * h_tex.height);
        for(size_t i=0; i < temp_spec.size(); ++i) {
             float r = h_tex.data[i*3+0];
             float g = h_tex.data[i*3+1];
             float b = h_tex.data[i*3+2];
             temp_spec[i] = GpuSpectrum::from_rgb(GpuVec3(r, g, b));
             temp_float4[i] = make_float4(r, g, b, 1.0f);
        }
        cudaMemcpy(d_tex.data, temp_spec.data(), size_bytes, cudaMemcpyHostToDevice);
        ctx->pointers_to_free.push_back(d_tex.data);

        cudaChannelFormatDesc channelDesc = cudaCreateChannelDesc<float4>();
        cudaArray_t cuArray;
        checkCudaErrors(cudaMallocArray(&cuArray, &channelDesc, d_tex.width, d_tex.height));
        ctx->arrays_to_free.push_back(cuArray);
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
        ctx->tex_objs_to_free.push_back(d_tex.texObj);
        host_gpu_textures.push_back(d_tex);
    }
    
    {
        size_t tex_bytes = host_gpu_textures.size() * sizeof(GpuTexture);
        if (tex_bytes == 0) tex_bytes = sizeof(GpuTexture);
        cudaMalloc(&ctx->d_textures, tex_bytes);
        if (!host_gpu_textures.empty())
            cudaMemcpy(ctx->d_textures, host_gpu_textures.data(), host_gpu_textures.size() * sizeof(GpuTexture), cudaMemcpyHostToDevice);
        ctx->pointers_to_free.push_back(ctx->d_textures);
    }
    ctx->texture_count = (int)host_gpu_textures.size();
    
    // Light Indices
    std::vector<int> host_light_indices;
    for (int i = 0; i < host_spheres.size(); ++i) {
        int mat_idx = host_spheres[i].material_index;
        if (mat_idx >= 0 && mat_idx < host_materials.size()) {
            const auto& mat = host_materials[mat_idx];
            if (mat.emission.values.x > 1e-4f || mat.emission.values.y > 1e-4f || mat.emission.values.z > 1e-4f) {
                host_light_indices.push_back(i);
            }
        }
    }
    if (!host_light_indices.empty()) {
        cudaMalloc(&ctx->d_light_indices, host_light_indices.size() * sizeof(int));
        cudaMemcpy(ctx->d_light_indices, host_light_indices.data(), host_light_indices.size() * sizeof(int), cudaMemcpyHostToDevice);
    } else { ctx->d_light_indices = nullptr; }
    ctx->light_count = (int)host_light_indices.size();
    
    return ctx;
}

void update_camera_gpu(GpuContext* ctx, const float* cam_pos, const float* cam_look, float fov) {
    GpuVec3 lookfrom(0, 3, 12);
    if (cam_pos) lookfrom = GpuVec3(cam_pos[0], cam_pos[1], cam_pos[2]);

    GpuVec3 lookat(0, 1, 0);
    if (cam_look) lookat = GpuVec3(cam_look[0], cam_look[1], cam_look[2]);

    float vfov = (fov > 0) ? fov : 40.0f;
    float theta = vfov * 3.14159265358979323846f / 180.0f;
    float h = tan(theta / 2.0f);
    float aspect_ratio = float(ctx->width) / float(ctx->height);
    float viewport_height = 2.0f * h;
    float viewport_width = aspect_ratio * viewport_height;
    
    GpuVec3 vup(0, 1, 0);
    GpuVec3 w = (lookfrom - lookat).normalize();
    GpuVec3 u = vup.cross(w).normalize();
    GpuVec3 v = w.cross(u);
    float focus_dist = 18.0f;

    ctx->camera.origin = lookfrom;
    ctx->camera.horizontal = u * viewport_width * focus_dist;
    ctx->camera.vertical = v * viewport_height * focus_dist;
    ctx->camera.lower_left_corner = ctx->camera.origin - ctx->camera.horizontal * 0.5f - ctx->camera.vertical * 0.5f - w * focus_dist;
    
    reset_accumulation_gpu(ctx);
}

void update_medium_gpu(GpuContext* ctx, float medium_density, float medium_anisotropy, GpuSpectrum medium_scattering, GpuSpectrum medium_absorption, float medium_max_distance) {
    ctx->medium_density = medium_density;
    ctx->medium_anisotropy = medium_anisotropy;
    ctx->medium_scattering = medium_scattering;
    ctx->medium_absorption = medium_absorption;
    ctx->medium_max_distance = medium_max_distance;
    reset_accumulation_gpu(ctx);
}

void reset_accumulation_gpu(GpuContext* ctx) {
    size_t framebuffer_size = ctx->width * ctx->height * sizeof(GpuVec3);
    cudaMemset(ctx->d_accum_buffer, 0, framebuffer_size);
    cudaMemset(ctx->d_sample_counts, 0, ctx->width * ctx->height * sizeof(int));
    ctx->current_spp = 0;
}

void free_gpu_renderer(GpuContext* ctx) {
    if (!ctx) return;
    
    cudaFree(ctx->d_output);
    cudaFree(ctx->d_accum_buffer);
    cudaFree(ctx->d_accum_sq_buffer);
    cudaFree(ctx->d_sample_counts);
    cudaFree(ctx->d_normal_buffer);
    cudaFree(ctx->d_albedo_buffer);
    
    cudaFree(ctx->d_materials);
    cudaFree(ctx->d_spheres);
    cudaFree(ctx->d_meshes);
    cudaFree(ctx->d_light_indices);
    
    free_ray_queue(ctx->queueA);
    free_ray_queue(ctx->queueB);
    free_hit_queue(ctx->hitQueue);
    free_shadow_queue(ctx->shadowQueue);
    
    for (void* ptr : ctx->pointers_to_free) cudaFree(ptr);
    for (auto a : ctx->arrays_to_free) cudaFreeArray(a);
    for (auto t : ctx->tex_objs_to_free) cudaDestroyTextureObject(t);
    
    free_debug_log();
    delete ctx;
}

int render_pass_gpu(GpuContext* ctx, int samples_per_pass) {
    GpuScene scene;
    scene.spheres = ctx->d_spheres;
    scene.sphere_count = ctx->sphere_count;
    scene.meshes = ctx->d_meshes;
    scene.mesh_count = ctx->mesh_count;
    scene.instances = ctx->d_instances;
    scene.instance_descs = ctx->d_instance_descs;
    scene.instance_transforms = ctx->d_instance_transforms;
    scene.instance_count = ctx->instance_count;
    scene.materials = ctx->d_materials;
    scene.material_count = ctx->material_count;
    scene.textures = ctx->d_textures;
    scene.texture_count = ctx->texture_count;
    scene.light_indices = ctx->d_light_indices;
    scene.light_count = ctx->light_count;
    
    scene.medium_density = ctx->medium_density;
    scene.medium_anisotropy = ctx->medium_anisotropy;
    scene.medium_scattering = ctx->medium_scattering;
    scene.medium_absorption = ctx->medium_absorption;
    scene.medium_max_distance = ctx->medium_max_distance;
    
    dim3 threadsPerBlock(16, 16);
    dim3 numBlocks((ctx->width + threadsPerBlock.x - 1) / threadsPerBlock.x,
                   (ctx->height + threadsPerBlock.y - 1) / threadsPerBlock.y);

    for (int s = 0; s < samples_per_pass; ++s) {
        int current_global_sample = ctx->current_spp + s;
        
        // 1. Generate Rays
        int initial_count = ctx->width * ctx->height;
        checkCudaErrors(cudaMemcpy(ctx->queueA.count, &initial_count, sizeof(int), cudaMemcpyHostToDevice));
        
        generate_rays_kernel<<<numBlocks, threadsPerBlock>>>(
            ctx->queueA, ctx->width, ctx->height, ctx->camera, current_global_sample, ctx->d_sample_counts
        );
        checkCudaErrors(cudaGetLastError());
        checkCudaErrors(cudaDeviceSynchronize());
        
        RayQueue* current_q = &ctx->queueA;
        RayQueue* next_q = &ctx->queueB;
        
        for (int depth = 0; depth < 50; ++depth) {
             int ray_count = 0;
             checkCudaErrors(cudaMemcpy(&ray_count, current_q->count, sizeof(int), cudaMemcpyDeviceToHost));
             if (ray_count == 0) break;
             
             int num_threads = 256;
             int num_blocks = (ray_count + num_threads - 1) / num_threads;
             
              extend_kernel<<<num_blocks, num_threads>>>(*current_q, ctx->hitQueue, scene);
             checkCudaErrors(cudaGetLastError());
             checkCudaErrors(cudaDeviceSynchronize());
             
             checkCudaErrors(cudaMemset(next_q->count, 0, sizeof(int)));
             checkCudaErrors(cudaMemset(ctx->shadowQueue.count, 0, sizeof(int)));
             
             float current_dispersion_clamp = (current_global_sample < 100) ? 5.0f : 20.0f;
             float current_rr_min_prob = (current_global_sample < 100) ? 0.1f : 0.05f;
             
             shade_kernel<<<num_blocks, num_threads>>>(*current_q, ctx->hitQueue, *next_q, ctx->shadowQueue, ctx->d_accum_buffer, ctx->d_normal_buffer, ctx->d_albedo_buffer, scene, current_global_sample, current_dispersion_clamp, current_rr_min_prob);
             checkCudaErrors(cudaGetLastError());
             checkCudaErrors(cudaDeviceSynchronize());
             
             int shadow_count = 0;
             checkCudaErrors(cudaMemcpy(&shadow_count, ctx->shadowQueue.count, sizeof(int), cudaMemcpyDeviceToHost));
             if (shadow_count > 0) {
                 int s_blocks = (shadow_count + num_threads - 1) / num_threads;
                 extend_shadow_kernel<<<s_blocks, num_threads>>>(ctx->shadowQueue, ctx->d_accum_buffer, scene);
                 checkCudaErrors(cudaGetLastError());
             }
             
             RayQueue* temp = current_q;
             current_q = next_q;
             next_q = temp;
        }
    }
    
    checkCudaErrors(cudaDeviceSynchronize());
    ctx->current_spp += samples_per_pass;
    return ctx->current_spp;
}

void copy_frame_buffer_gpu(GpuContext* ctx, float* host_buffer) {
    dim3 threadsPerBlock(16, 16);
    dim3 numBlocks((ctx->width + threadsPerBlock.x - 1) / threadsPerBlock.x,
                   (ctx->height + threadsPerBlock.y - 1) / threadsPerBlock.y);
                   
    // Resolve
    resolve_framebuffer_kernel<<<numBlocks, threadsPerBlock>>>(
        ctx->d_accum_buffer,
        ctx->d_sample_counts,
        ctx->d_output, 
        ctx->width,
        ctx->height
    );
    checkCudaErrors(cudaDeviceSynchronize());
    
    // Simple copy for now (Denoiser/FXAA can be added here)
    size_t framebuffer_size = ctx->width * ctx->height * sizeof(GpuVec3);
    cudaMemcpy(host_buffer, ctx->d_output, framebuffer_size, cudaMemcpyDeviceToHost);
}

void update_instance_transforms_gpu(GpuContext* ctx,
                                    const GpuInstanceTransform* transforms,
                                    int count) {
    if (!ctx || count <= 0) return;
    // NOTE: count may differ from ctx->instance_count during development; assert the common case
    assert(ctx->d_instance_transforms != nullptr && "update_instance_transforms: no GPU transform buffer -- did you call init with empty instances?");
    assert(count == ctx->instance_count && "update_instance_transforms: count must match scene instance_count");
    assert(transforms != nullptr && "update_instance_transforms: null transforms pointer");
    size_t bytes = count * sizeof(GpuInstanceTransform);
    cudaMemcpy(ctx->d_instance_transforms, transforms, bytes, cudaMemcpyHostToDevice);
    cudaError_t err = cudaGetLastError();
    assert(err == cudaSuccess && "update_instance_transforms: cudaMemcpy failed");
}

void render_frame_gpu(float* output_buffer, int width, int height, int samples_per_pixel,
                      const std::vector<ure::gpu::RenderMesh>& meshes,
                      const std::vector<ure::gpu::GpuInstance>& instances,
                      const std::vector<ure::gpu::GpuSphere>& spheres,
                      const std::vector<ure::gpu::GpuMaterial>& materials,
                      const float* cam_pos,
                      const float* cam_look,
                      float fov,
                      float medium_density,
                      float medium_anisotropy,
                      GpuSpectrum medium_scattering,
                      GpuSpectrum medium_absorption,
                      float medium_max_distance) {
    std::cout << "[GPU] Allocating memory for " << width << "x" << height << " image..." << std::endl;

    size_t framebuffer_size = width * height * sizeof(GpuVec3);
    GpuVec3* d_output;
    cudaMalloc(&d_output, framebuffer_size);

    // Setup Materials
    // Only load default geometry if no input geometry is provided
    bool use_default_geometry = spheres.empty() && meshes.empty() && instances.empty();
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
    if (spheres.empty() && meshes.empty() && instances.empty()) {
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

    // Setup Instances
    GpuInstance* d_instances = nullptr;
    GpuInstanceTransform* d_instance_transforms = nullptr;
    GpuInstanceDesc* d_instance_descs = nullptr;
    if (!instances.empty()) {
        size_t inst_size = instances.size() * sizeof(GpuInstance);
        cudaMalloc(&d_instances, inst_size);
        cudaMemcpy(d_instances, instances.data(), inst_size, cudaMemcpyHostToDevice);
        pointers_to_free.push_back(d_instances);
        // Phase P.7: separate instance_descs array (8B stride, not 160B)
        {
            size_t desc_size = instances.size() * sizeof(GpuInstanceDesc);
            cudaMalloc(&d_instance_descs, desc_size);
            std::vector<GpuInstanceDesc> descs(instances.size());
            for (size_t i = 0; i < instances.size(); ++i) {
                descs[i].mesh_index = instances[i].mesh_index;
                descs[i].material_index = instances[i].material_index;
            }
            cudaMemcpy(d_instance_descs, descs.data(), desc_size, cudaMemcpyHostToDevice);
            pointers_to_free.push_back(d_instance_descs);
        }
        // Phase P.1: separate transform buffer (field-by-field copy avoids layout dependency)
        std::vector<GpuInstanceTransform> xforms(instances.size());
        for (size_t i = 0; i < instances.size(); ++i) {
            xforms[i].transform = instances[i].transform;
            xforms[i].inverse_transform = instances[i].inverse_transform;
            xforms[i].min_pt = instances[i].min_pt;
            xforms[i].max_pt = instances[i].max_pt;
        }
        cudaMalloc(&d_instance_transforms, xforms.size() * sizeof(GpuInstanceTransform));
        cudaMemcpy(d_instance_transforms, xforms.data(), xforms.size() * sizeof(GpuInstanceTransform), cudaMemcpyHostToDevice);
        pointers_to_free.push_back(d_instance_transforms);
    }

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
    scene.instances = d_instances;
    scene.instance_descs = d_instance_descs;
    scene.instance_transforms = d_instance_transforms;
    scene.instance_count = (int)instances.size();
    scene.materials = d_materials;
    scene.material_count = (int)host_materials.size();
    scene.textures = d_textures;
    scene.texture_count = (int)host_gpu_textures.size();
    scene.light_indices = d_light_indices;
    scene.light_count = (int)host_light_indices.size();
    
    // Scene Medium
    scene.medium_density = medium_density;
    scene.medium_anisotropy = medium_anisotropy;
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

// Include material scatter BSDF (replaces the legacy scatter() above)
#include "path_tracer_material.cu"

} // namespace ure::gpu
