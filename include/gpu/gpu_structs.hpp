#pragma once

#include <cuda_runtime.h>

namespace ure::gpu {

// Use 16-byte alignment for float3 to match CUDA float4 alignment if needed,
// but for now we'll stick to simple structs.

struct GpuVec2 {
    float u, v;
    __host__ __device__ GpuVec2() : u(0), v(0) {}
    __host__ __device__ GpuVec2(float u, float v) : u(u), v(v) {}
    __host__ __device__ GpuVec2 operator+(const GpuVec2& other) const { return {u + other.u, v + other.v}; }
    __host__ __device__ GpuVec2 operator*(float s) const { return {u * s, v * s}; }
};

// SIMD Helper Operators for float4
__host__ __device__ inline float4 operator+(const float4& a, const float4& b) {
    return make_float4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w);
}

__host__ __device__ inline float4 operator-(const float4& a, const float4& b) {
    return make_float4(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w);
}

__host__ __device__ inline float4 operator*(const float4& a, const float4& b) {
    return make_float4(a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w);
}

__host__ __device__ inline float4 operator*(const float4& a, float s) {
    return make_float4(a.x * s, a.y * s, a.z * s, a.w * s);
}

struct GpuVec3 {
    float x, y, z;
    
    __host__ __device__ GpuVec3() : x(0), y(0), z(0) {}
    __host__ __device__ GpuVec3(float x, float y, float z) : x(x), y(y), z(z) {}
    
    __host__ __device__ GpuVec3 operator+(const GpuVec3& v) const { return {x + v.x, y + v.y, z + v.z}; }
    __host__ __device__ GpuVec3 operator-(const GpuVec3& v) const { return {x - v.x, y - v.y, z - v.z}; }
    __host__ __device__ GpuVec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    __host__ __device__ GpuVec3 operator*(const GpuVec3& v) const { return {x * v.x, y * v.y, z * v.z}; }
    
    __host__ __device__ float dot(const GpuVec3& v) const { return x * v.x + y * v.y + z * v.z; }
    
    __host__ __device__ GpuVec3 cross(const GpuVec3& v) const {
        return {y * v.z - z * v.y, z * v.x - x * v.z, x * v.y - y * v.x};
    }
    
    __host__ __device__ GpuVec3 normalize() const {
        float len = sqrtf(x * x + y * y + z * z);
        return len > 0 ? *this * (1.0f / len) : *this;
    }

    __host__ __device__ float length_sq() const { return x * x + y * y + z * z; }
    __host__ __device__ float length() const { return sqrtf(length_sq()); }
    __host__ __device__ GpuVec3 operator-() const { return {-x, -y, -z}; }
    __host__ __device__ friend GpuVec3 operator*(float s, const GpuVec3& v) { return v * s; }
};

// Phase 3: Polarization Support (Beyond Ray Optics)
struct StokesVector {
    // S0, S1, S2, S3 (or I, Q, U, V)
    // I: Intensity
    // Q: Linear polarization (horizontal/vertical)
    // U: Linear polarization (diagonal)
    // V: Circular polarization
    float I, Q, U, V;

    __host__ __device__ StokesVector() : I(0), Q(0), U(0), V(0) {}
    __host__ __device__ StokesVector(float i, float q, float u, float v) : I(i), Q(q), U(u), V(v) {}

    __host__ __device__ StokesVector operator+(const StokesVector& other) const {
        return {I + other.I, Q + other.Q, U + other.U, V + other.V};
    }
    
    __host__ __device__ StokesVector operator*(float s) const {
        return {I * s, Q * s, U * s, V * s};
    }
};

struct MuellerMatrix {
    // 4x4 Matrix for polarization transformation
    float m[4][4];

    __host__ __device__ MuellerMatrix() {
        for(int i=0; i<4; ++i)
            for(int j=0; j<4; ++j)
                m[i][j] = (i==j) ? 1.0f : 0.0f;
    }

    __host__ __device__ static MuellerMatrix polarizer(float theta) {
        // Linear polarizer at angle theta
        float c = cosf(2*theta);
        float s = sinf(2*theta);
        MuellerMatrix mat;
        mat.m[0][0] = 1; mat.m[0][1] = c; mat.m[0][2] = s; mat.m[0][3] = 0;
        mat.m[1][0] = c; mat.m[1][1] = c*c; mat.m[1][2] = c*s; mat.m[1][3] = 0;
        mat.m[2][0] = s; mat.m[2][1] = s*c; mat.m[2][2] = s*s; mat.m[2][3] = 0;
        mat.m[3][0] = 0; mat.m[3][1] = 0; mat.m[3][2] = 0; mat.m[3][3] = 0;
        
        // Multiply by 0.5 transmission for unpolarized light
        for(int i=0; i<4; ++i)
            for(int j=0; j<4; ++j)
                mat.m[i][j] *= 0.5f;
        return mat;
    }
    
    __host__ __device__ StokesVector apply(const StokesVector& s) const {
        float res[4] = {0,0,0,0};
        float in[4] = {s.I, s.Q, s.U, s.V};
        for(int i=0; i<4; ++i) {
            for(int j=0; j<4; ++j) {
                res[i] += m[i][j] * in[j];
            }
        }
        return StokesVector(res[0], res[1], res[2], res[3]);
    }
};

// Phase 2: Spectral Parallelization Support
// Wavelength packet for SIMT execution (Vectorized)
struct alignas(16) GpuSpectrum {
    static constexpr int kNumWavelengths = 4;
    static constexpr float kLambdaMin = 360.0f;
    static constexpr float kLambdaMax = 830.0f;

    // Vectorized storage using float4 (128-bit alignment)
    float4 values;
    float4 wavelengths;

    __host__ __device__ GpuSpectrum() {
        values = make_float4(0.0f, 0.0f, 0.0f, 0.0f);
        wavelengths = make_float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    __host__ __device__ GpuSpectrum(float v) {
        values = make_float4(v, v, v, v);
        wavelengths = make_float4(0.0f, 0.0f, 0.0f, 0.0f);
    }
    
    __host__ __device__ GpuSpectrum(float r, float g, float b) {
        values = make_float4(r, g, b, (r+g+b)/3.0f);
        wavelengths = make_float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    __host__ __device__ GpuSpectrum operator*(const GpuSpectrum& other) const {
        GpuSpectrum res;
        res.values = values * other.values;
        res.wavelengths = wavelengths;
        return res;
    }

    __host__ __device__ GpuSpectrum operator+(const GpuSpectrum& other) const {
        GpuSpectrum res;
        res.values = values + other.values;
        res.wavelengths = wavelengths;
        return res;
    }

    __host__ __device__ GpuSpectrum operator-(const GpuSpectrum& other) const {
        GpuSpectrum res;
        res.values = values - other.values;
        res.wavelengths = wavelengths;
        return res;
    }
    
    __host__ __device__ GpuSpectrum operator*(float s) const {
        GpuSpectrum res;
        res.values = values * s;
        res.wavelengths = wavelengths;
        return res;
    }

    __host__ __device__ static GpuSpectrum from_rgb(const GpuVec3& rgb) {
        GpuSpectrum s;
        // Simple mapping for now: R, G, B, Lum
        s.values.x = rgb.x;
        s.values.y = rgb.y;
        s.values.z = rgb.z;
        s.values.w = (rgb.x + rgb.y + rgb.z) / 3.0f;
        return s;
    }

    __host__ __device__ GpuVec3 to_rgb() const {
        // Simple mapping back
        return GpuVec3(values.x, values.y, values.z);
    }
};

struct GpuRay {
    GpuVec3 origin;
    GpuVec3 direction;
    float t_min;
    float t_max;
    StokesVector stokes; // Phase 3: Polarization State
    
    __host__ __device__ GpuVec3 at(float t) const {
        return origin + direction * t;
    }
};

struct GpuCamera {
    GpuVec3 lower_left_corner;
    GpuVec3 horizontal;
    GpuVec3 vertical;
    GpuVec3 origin;
};



// Material types
enum class MaterialType {
    Lambertian,
    Metal,
    Dielectric,
    Light,
    Cloth
};

struct GpuTexture {
    int width;
    int height;
    GpuSpectrum* data; // Unified Memory / Linear
    cudaTextureObject_t texObj; // Texture Object (Hardware Filtering)
};

struct GpuMaterial {
    MaterialType type;
    GpuSpectrum albedo;
    float roughness;
    float ior;
    GpuSpectrum extinction; // For metals (Conductor)
    float dispersion; // 0.0 = no dispersion. High values (~0.05) = strong dispersion.
    float thin_film_thickness;
    float thin_film_ior;
    
    // Phase 3: Volume / SSS
    float medium_density;     // 0.0 = Surface only, > 0.0 = Volumetric/SSS
    float medium_anisotropy;  // g factor for Henyey-Greenstein
    GpuSpectrum medium_scattering; // Color of the medium (sigma_s)
    GpuSpectrum medium_absorption; // Absorption of the medium (sigma_a)
    
    GpuSpectrum emission;
    int texture_index = -1; // -1 means no texture
};

struct GpuSphere {
    GpuVec3 center;
    float radius;
    int material_index;
};

// Compact BVH Node for GPU (32 bytes)
struct GpuBvhNode {
    GpuVec3 min_pt;
    // If primitive_count == 0 (Internal): stores Right Child Index.
    // If primitive_count > 0 (Leaf): stores First Primitive Index.
    int child_or_primitive_index; 
    
    GpuVec3 max_pt;
    // If 0, Internal Node. If > 0, Leaf Node with this many primitives.
    int primitive_count; 
};

struct GpuMesh {
    GpuVec3* vertices;
    GpuVec3* normals; // Vertex Normals
    GpuVec2* uvs;
    int* indices;
    int triangle_count;
    int material_index;
    
    // AABB Bounds
    GpuVec3 min_pt;
    GpuVec3 max_pt;
    
    // BVH Data
    GpuBvhNode* bvh_nodes;
    int bvh_node_count;
};

struct GpuScene {
    GpuSphere* spheres;
    int sphere_count;
    GpuMesh* meshes;
    int mesh_count;
    GpuMaterial* materials;
    int material_count;
    GpuTexture* textures;
    int texture_count;
    int* light_indices; // Indices of emissive spheres/meshes
    int light_count;
    
    // Global Homogeneous Medium (Volumetric Fog)
    float medium_density = 0.0f;
    float medium_anisotropy = 0.0f; // 0.0 = Isotropic
    GpuSpectrum medium_scattering;
    GpuSpectrum medium_absorption;
    float medium_max_distance = 0.0f;
};

// Wavefront Path Tracing Structures
struct RayQueue {
    GpuVec3* origins;
    GpuVec3* directions;
    GpuSpectrum* throughputs;
    StokesVector* stokes; // Phase 3: Polarization
    int* medium_indices; // Phase 3: Volume / SSS (Current medium index, -1 = Global)
    unsigned int* seeds; // Lightweight RNG state
    int* pixel_indices;
    int* depths;
    int* flags; // Bitmask for ray state (e.g. 0x1 = Specular Bounce)
    float* last_pdf; // For MIS: PDF of the last sampled direction
    
    // Using pointer for atomic operations on device
    int* count; 
    int capacity;
};

struct ShadowQueue {
    GpuVec3* origins;
    GpuVec3* directions;
    float* max_dist;
    GpuSpectrum* radiance; // Potential contribution
    int* pixel_indices;
    
    int* count;
    int capacity;
};

struct HitQueue {
    float* t;
    GpuVec3* p;
    GpuVec3* n;
    GpuVec3* ng; // Geometric Normal
    GpuVec2* uv; // Texture coordinates
    int* mat_ids;
    int* hit_types; // 0 = Sphere, 1 = Mesh
    int* hit_indices; // Index in spheres[] or meshes[]
};

} // namespace ure::gpu
