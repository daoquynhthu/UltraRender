#pragma once

#include <cuda_runtime.h>
#include <cmath>
#include <cstdint>
#include <vector>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4324)
#endif

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

struct GpuMat4 {
    float m[4][4];

    __host__ __device__ GpuMat4() {
        for(int i=0; i<4; ++i) for(int j=0; j<4; ++j) m[i][j] = (i==j)?1.0f:0.0f;
    }

    __host__ __device__ static GpuMat4 identity() { return GpuMat4(); }

    __host__ __device__ GpuVec3 transform_point(const GpuVec3& p) const {
        float x = m[0][0]*p.x + m[0][1]*p.y + m[0][2]*p.z + m[0][3];
        float y = m[1][0]*p.x + m[1][1]*p.y + m[1][2]*p.z + m[1][3];
        float z = m[2][0]*p.x + m[2][1]*p.y + m[2][2]*p.z + m[2][3];
        float w = m[3][0]*p.x + m[3][1]*p.y + m[3][2]*p.z + m[3][3];
        if (fabsf(w) > 1e-6f) {
            float invW = 1.0f / w;
            x *= invW; y *= invW; z *= invW;
        }
        return GpuVec3(x, y, z);
    }

    __host__ __device__ GpuVec3 transform_vector(const GpuVec3& v) const {
        // Vectors ignore translation (w=0)
        float x = m[0][0]*v.x + m[0][1]*v.y + m[0][2]*v.z;
        float y = m[1][0]*v.x + m[1][1]*v.y + m[1][2]*v.z;
        float z = m[2][0]*v.x + m[2][1]*v.y + m[2][2]*v.z;
        return GpuVec3(x, y, z);
    }

    // Transform normal using Transpose of this matrix
    // Use this when 'this' is the Inverse Transform Matrix to get World Normal from Object Normal
    __host__ __device__ GpuVec3 transform_normal(const GpuVec3& n) const {
         float x = m[0][0]*n.x + m[1][0]*n.y + m[2][0]*n.z;
         float y = m[0][1]*n.x + m[1][1]*n.y + m[2][1]*n.z;
         float z = m[0][2]*n.x + m[1][2]*n.y + m[2][2]*n.z;
         return GpuVec3(x, y, z);
    }
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

// Spectral range constants.
constexpr float kSpectralLambdaMin = 360.0f;
constexpr float kSpectralLambdaMax = 830.0f;
constexpr int kMinPacketLanes = 1;
constexpr int kMaxPacketLanes = 32;

__host__ __device__ inline bool valid_packet_lane_count(int lanes) {
    return lanes == 1 || (lanes >= 8 && lanes <= kMaxPacketLanes);
}

struct SpectralSample {
    float value = 0.0f;
    float wavelength = 0.0f;
    float pdf = 1.0f;
    int domain_bin = -1;
};

enum class SpectralResourceKind : int {
    None = 0,
    Constant = 1,
    RgbReflectance = 2,
    RgbEmission = 3,
    SampledTable = 4
};

enum class SpectralTextureResourceKind : int {
    None = 0,
    SourceSampleGrid = 1
};

enum class SpectralExpressionNodeKind : int {
    None = 0,
    Resource = 1,
    Texture = 2,
    Add = 3,
    Multiply = 4,
    Mix = 5,
    Checker2D = 6,
    Noise2D = 7
};

enum class SpectralExpressionSemantic : int {
    Reflectance = 0,
    Emission = 1,
    OpticalConstant = 2,
    Scalar = 3
};

constexpr int kMaxMaterialExpressionNodes = 32;

struct SpectralResource {
    SpectralResourceKind kind = SpectralResourceKind::None;
    float constant = 0.0f;
    GpuVec3 rgb = GpuVec3(0.0f, 0.0f, 0.0f);
    const float* wavelengths = nullptr;
    const float* values = nullptr;
    int sample_count = 0;
};

struct HostSpectralResource {
    SpectralResourceKind kind = SpectralResourceKind::None;
    float constant = 0.0f;
    GpuVec3 rgb = GpuVec3(0.0f, 0.0f, 0.0f);
    std::vector<float> wavelengths;
    std::vector<float> values;
};

struct GpuMiePhaseResource {
    int wavelength_offset = 0;
    int wavelength_count = 0;
    int angle_offset = 0;
    int angle_count = 0;
    int phase_offset = 0;
    int cross_section_offset = 0;
};

struct SpectralExpressionNode {
    SpectralExpressionNodeKind kind = SpectralExpressionNodeKind::None;
    SpectralExpressionSemantic semantic = SpectralExpressionSemantic::Reflectance;
    SpectralResource resource;
    int texture_index = -1;
    int input_a = -1;
    int input_b = -1;
    int input_factor = -1;
};

struct HostSpectralExpressionNode {
    SpectralExpressionNodeKind kind = SpectralExpressionNodeKind::None;
    SpectralExpressionSemantic semantic = SpectralExpressionSemantic::Reflectance;
    HostSpectralResource resource;
    int texture_index = -1;
    int input_a = -1;
    int input_b = -1;
    int input_factor = -1;
};

struct alignas(16) SpectralPacket {
    float values[kMaxPacketLanes];
    float wavelengths[kMaxPacketLanes];

    __host__ __device__ SpectralPacket() {
        for (int c = 0; c < kMaxPacketLanes; ++c) {
            values[c] = 0.0f;
            wavelengths[c] = 0.0f;
        }
    }

    __host__ __device__ SpectralPacket(float v) {
        for (int c = 0; c < kMaxPacketLanes; ++c) {
            values[c] = v;
            wavelengths[c] = 0.0f;
        }
    }

    __host__ __device__ SpectralPacket(float r, float g, float b) {
        float avg = (r + g + b) / 3.0f;
        for (int c = 0; c < kMaxPacketLanes; ++c) {
            values[c] = avg;
            wavelengths[c] = 0.0f;
        }
        values[0] = r;
        values[1] = g;
        values[2] = b;
    }

    __host__ __device__ SpectralPacket operator+(const SpectralPacket& other) const {
        SpectralPacket res;
        for (int c = 0; c < kMaxPacketLanes; ++c) {
            res.values[c] = values[c] + other.values[c];
            res.wavelengths[c] = wavelengths[c];
        }
        return res;
    }

    __host__ __device__ SpectralPacket operator-(const SpectralPacket& other) const {
        SpectralPacket res;
        for (int c = 0; c < kMaxPacketLanes; ++c) {
            res.values[c] = values[c] - other.values[c];
            res.wavelengths[c] = wavelengths[c];
        }
        return res;
    }

    __host__ __device__ SpectralPacket operator*(float s) const {
        SpectralPacket res;
        for (int c = 0; c < kMaxPacketLanes; ++c) {
            res.values[c] = values[c] * s;
            res.wavelengths[c] = wavelengths[c];
        }
        return res;
    }

    __host__ __device__ SpectralPacket operator*(const SpectralPacket& other) const {
        SpectralPacket res;
        for (int c = 0; c < kMaxPacketLanes; ++c) {
            res.values[c] = values[c] * other.values[c];
            res.wavelengths[c] = wavelengths[c];
        }
        return res;
    }

    __host__ __device__ float sample(int i) const { return values[i]; }
    __host__ __device__ void set_sample(int i, float v) { values[i] = v; }
    __host__ __device__ float wavelength(int i) const { return wavelengths[i]; }
    __host__ __device__ void set_wavelength(int i, float v) { wavelengths[i] = v; }
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
    Cloth,
    Composite,
    Layered
};

constexpr int kMaxMaterialBsdfLobes = 4;

struct GpuMaterialBsdfLobe {
    MaterialType type = MaterialType::Lambertian;
    float roughness = 0.5f;
    float ior = 1.45f;
    float dispersion = 0.0f;
    float thin_film_thickness = 0.0f;
    float thin_film_ior = 1.0f;
    int albedo_expression_root = -1;
    int roughness_expression_root = -1;
    int metal_eta_expression_root = -1;
    int extinction_expression_root = -1;
    int ior_expression_root = -1;
};

struct GpuTexture {
    int width;
    int height;
    int channels;
    cudaTextureObject_t texObj; // Texture Object (Hardware Filtering)
    SpectralTextureResourceKind spectral_kind = SpectralTextureResourceKind::None;
    const float* spectral_source_values = nullptr;
    int spectral_sample_count = 0;
    float spectral_lambda_min = kSpectralLambdaMin;
    float spectral_lambda_max = kSpectralLambdaMax;
};

enum class GpuLightKind : int {
    Sphere = 0,
    MeshTriangle = 1,
    InstanceTriangle = 2,
    Environment = 3
};

struct GpuLightRecord {
    GpuLightKind kind = GpuLightKind::Sphere;
    int primitive_index = -1;
    int secondary_index = -1;
    int material_index = -1;
    float area = 0.0f;
    GpuVec3 centroid = {};
    GpuVec3 bounds_min = {};
    GpuVec3 bounds_max = {};
};

struct GpuLightTreeNode {
    int parent = -1;
    int left = -1;
    int right = -1;
    int light_index = -1;
    float weight = 0.0f;
    GpuVec3 bounds_min = {};
    GpuVec3 bounds_max = {};
};

enum class GpuRestirDomain : int {
    Surface = 0,
    Volume = 1
};

struct GpuRestirDISample {
    GpuVec3 source_position = {};
    GpuVec3 source_normal = {};
    GpuVec3 direction = {};
    float max_distance = 0.0f;
    float light_u = 0.0f;
    float light_v = 0.0f;
    float wavelength_pdf = 0.0f;
    float lobe_pdf = 0.0f;
    float stokes_i = 1.0f;
    float stokes_q = 0.0f;
    float stokes_u = 0.0f;
    float stokes_v = 0.0f;
    int light_list_index = -1;
    int light_primitive_index = -1;
    int light_secondary_index = -1;
    int material_index = -1;
    int medium_index = -1;
    int spectral_mode = 0;
    int active_channel = 0;
    int source_pixel = -1;
    int source_depth = 0;
    GpuRestirDomain domain = GpuRestirDomain::Surface;
    std::uint32_t scene_epoch = 0;
};

struct GpuRestirDIReservoir {
    GpuRestirDISample sample = {};
    double weight_sum = 0.0;
    float selected_target = 0.0f;
    float normalization_weight = 0.0f;
    std::uint32_t candidate_count = 0;
    int valid = 0;
};

// Phase E: Scalar-only material header. Spectral data stored as SoA in GpuScene.
struct GpuMaterial {
    MaterialType type;
    float roughness;
    float ior;
    float dispersion;
    float thin_film_thickness;
    float thin_film_ior;
    float medium_density;
    float medium_anisotropy;
    int medium_phase = 0;
    int medium_phase_resource_index = -1;
    int texture_index = -1;
    int roughness_texture_index = -1;
    int emission_texture_index = -1;
    int expression_node_start = -1;
    int expression_node_count = 0;
    int albedo_expression_root = -1;
    int roughness_expression_root = -1;
    int emission_expression_root = -1;
    int metal_eta_expression_root = -1;
    int extinction_expression_root = -1;
    int ior_expression_root = -1;
    int bsdf_lobe_count = 0;
    int bsdf_lobe_start = -1;
    int bsdf_mix_expression_root = -1;
    int layer_thickness_expression_root = -1;
    int layer_absorption_expression_root = -1;
};

// Host-side companion holding spectral data alongside the GPU header.
struct GpuMaterialData {
    GpuMaterial header;
    SpectralPacket albedo;
    SpectralPacket metal_eta;
    SpectralPacket extinction;
    SpectralPacket medium_scattering;
    SpectralPacket medium_absorption;
    SpectralPacket emission;
    HostSpectralResource albedo_resource;
    HostSpectralResource metal_eta_resource;
    HostSpectralResource extinction_resource;
    HostSpectralResource medium_scattering_resource;
    HostSpectralResource medium_absorption_resource;
    HostSpectralResource emission_resource;
    std::vector<HostSpectralExpressionNode> expression_nodes;
    std::vector<GpuMaterialBsdfLobe> bsdf_lobes;
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
    GpuVec3* tangents; // Vertex Tangents (normal mapping)
    int* indices;
    int triangle_count;
    int material_index; // Default material

    // AABB Bounds (Object Space)
    GpuVec3 min_pt;
    GpuVec3 max_pt;

    // BVH Data (Object Space)
    GpuBvhNode* bvh_nodes;
    int bvh_node_count;
};

// Phase P.1: Independent desc and transform files (referenced from GpuScene below)
#include "ure/instance_desc.hpp"
#include "ure/instance_transform.hpp"

// Packed instance layout retained for existing GPU kernels.
// Layout: GpuInstanceDesc (8B) + GpuInstanceTransform (152B) = 160B total
// This ordering ensures reinterpret_cast<GpuInstanceDesc*>(ptr) works correctly
struct GpuInstance {
    int mesh_index;            // offset 0    <- matches GpuInstanceDesc
    int material_index;        // offset 4    <- matches GpuInstanceDesc
    GpuMat4 transform;         // offset 8 (or 16 with padding) <- matches GpuInstanceTransform
    GpuMat4 inverse_transform; // offset 72 (or 80)
    GpuVec3 min_pt;            // offset 136 (or 144)
    GpuVec3 max_pt;            // offset 148 (or 156)
};

struct GpuScene {
    GpuSphere* spheres;
    int sphere_count;

    GpuMesh* meshes;
    int mesh_count;

    GpuInstance* instances;          // Packed data, same content as descs + xforms
    GpuInstanceDesc* instance_descs; // Phase P.1: static descriptors
    GpuInstanceTransform* instance_transforms; // Phase P.1: dynamic transforms (updated per frame)
    GpuInstanceTransform* previous_instance_transforms;
    int instance_count;

    GpuMaterial* materials;
    int material_count;

    // Phase E: SoA spectral data (num_spectral_channels × material_count)
    float* mat_albedo_vals;
    float* mat_metal_eta_vals;
    float* mat_extinction_vals;
    float* mat_medium_scattering_vals;
    float* mat_medium_absorption_vals;
    float* mat_emission_vals;
    SpectralResource* mat_albedo_resources;
    SpectralResource* mat_metal_eta_resources;
    SpectralResource* mat_extinction_resources;
    SpectralResource* mat_medium_scattering_resources;
    SpectralResource* mat_medium_absorption_resources;
    SpectralResource* mat_emission_resources;
    SpectralExpressionNode* material_expression_nodes;
    int material_expression_node_count;
    GpuMaterialBsdfLobe* material_bsdf_lobes;
    int material_bsdf_lobe_count;
    int num_spectral_channels;

    GpuMiePhaseResource* mie_phase_resources;
    int mie_phase_resource_count;
    float* mie_wavelengths;
    float* mie_cos_theta;
    float* mie_phase_values;
    float* mie_cdf_values;
    float* mie_scattering_cross_sections;
    float* mie_extinction_cross_sections;
    float* mie_absorption_cross_sections;
    float* mie_asymmetry;
    int mie_wavelength_count;
    int mie_angle_count;
    int mie_phase_value_count;
    int mie_cdf_value_count;
    int mie_cross_section_count;

    GpuTexture* textures;
    int texture_count;
    GpuLightRecord* lights;
    int* light_indices;
    float* light_selection_pmf;
    float* light_selection_cdf;
    float* light_alias_prob;
    int* light_alias_index;
    GpuLightTreeNode* light_tree_nodes;
    int* light_tree_leaf_nodes;
    int light_tree_node_count;
    int light_tree_root;
    float* path_guiding_light_weights;
    int path_guiding_light_count;
    float path_guiding_light_mixture;
    float path_guiding_learning_rate;
    float path_guiding_min_weight;
    float* path_guiding_spatial_directional_weights;
    int path_guiding_spatial_cell_count;
    int path_guiding_directional_bin_count;
    GpuVec3 path_guiding_bounds_min;
    GpuVec3 path_guiding_bounds_max;
    std::uint32_t path_guiding_epoch;
    int environment_light_direct_sampling;
    float environment_light_intensity;
    GpuVec3* restir_di_origins;
    GpuVec3* restir_di_directions;
    float* restir_di_max_dist;
    float* restir_di_radiance_vals;
    float* restir_di_radiance_wavelengths;
    float* restir_di_target_luminance;
    float* restir_di_lobe_pdfs;
    float* restir_di_wavelength_pdfs;
    float* restir_di_stokes_i;
    float* restir_di_stokes_q;
    float* restir_di_stokes_u;
    float* restir_di_stokes_v;
    int* restir_di_light_list_indices;
    int* restir_di_spectral_modes;
    int* restir_di_active_channels;
    int* restir_di_history_lengths;
    int* restir_di_valid;
    int restir_di_pixel_count;
    int restir_di_enabled;
    int restir_di_temporal_reuse;
    int restir_di_unbiased;
    int restir_di_max_history;
    float restir_di_min_target;
    GpuRestirDIReservoir* restir_di_input_reservoirs;
    GpuRestirDIReservoir* restir_di_output_reservoirs;
    float* restir_di_input_spectral_values;
    float* restir_di_input_spectral_wavelengths;
    float* restir_di_output_spectral_values;
    float* restir_di_output_spectral_wavelengths;
    std::uint32_t restir_di_scene_epoch;
    int restir_di_spatial_candidate_count;
    int restir_di_spatial_radius;
    int restir_di_width;
    int restir_di_height;
    int light_count;

    // Global Homogeneous Medium (Volumetric Fog)
    float medium_density = 0.0f;
    float medium_anisotropy = 0.0f; // 0.0 = Isotropic
    int medium_phase = 0;
    int medium_phase_resource_index = -1;
    SpectralPacket medium_scattering;
    SpectralPacket medium_absorption;
    float medium_max_distance = 0.0f;
};

// Wavefront Path Tracing Structures
enum SpectralRayMode : int {
    SpectralRayModePacket = 0,
    SpectralRayModeLane = 1,
    SpectralRayModeSampled = 2
};

enum SpectralWavelengthSamplingStrategy : int {
    SpectralWavelengthSamplingUniform = 0,
    SpectralWavelengthSamplingCieYImportance = 1,
    SpectralWavelengthSamplingSceneSpectralPower = 2
};

__host__ __device__ inline bool spectral_mode_is_sampled(int mode) {
    return mode == SpectralRayModeLane || mode == SpectralRayModeSampled;
}

struct RayQueue {
    GpuVec3* origins = nullptr;
    GpuVec3* directions = nullptr;
    float* throughput_vals = nullptr;
    float* throughput_wavelengths = nullptr;
    int num_spectral_channels = 0;
    float* stokes_i = nullptr;
    float* stokes_q = nullptr;
    float* stokes_u = nullptr;
    float* stokes_v = nullptr;
    int* medium_indices = nullptr;
    unsigned int* seeds = nullptr;
    int* pixel_indices = nullptr;
    int* depths = nullptr;
    int* flags = nullptr;
    float* last_pdf = nullptr;
    int* spectral_modes = nullptr;
    int* active_channels = nullptr;
    float* wavelength_pdfs = nullptr;
    int initial_spectral_mode = SpectralRayModePacket;
    int wavelength_sampling_strategy = SpectralWavelengthSamplingUniform;
    float* wavelength_proposal_cdf = nullptr;
    float* wavelength_proposal_pdf = nullptr;
    int wavelength_proposal_count = 0;
    float wavelength_proposal_lambda_min = kSpectralLambdaMin;
    float wavelength_proposal_lambda_max = kSpectralLambdaMax;

    int* count = nullptr;
    int* overflow_count = nullptr;
    int capacity = 0;
};

struct ShadowQueue {
    GpuVec3* origins = nullptr;
    GpuVec3* directions = nullptr;
    float* max_dist = nullptr;
    float* radiance_vals = nullptr;
    float* radiance_wavelengths = nullptr;
    int num_spectral_channels = 0;
    int* spectral_modes = nullptr;
    int* active_channels = nullptr;
    float* wavelength_pdfs = nullptr;
    int* pixel_indices = nullptr;
    int* light_list_indices = nullptr;
    float* bsdf_lobe_pdfs = nullptr;
    float* guiding_product_luminance = nullptr;
    float* guiding_wavelength_nm = nullptr;
    std::uint32_t* guiding_epochs = nullptr;
    float* stokes_i = nullptr;
    float* stokes_q = nullptr;
    float* stokes_u = nullptr;
    float* stokes_v = nullptr;
    int* restir_replay_flags = nullptr;

    int* count = nullptr;
    int* overflow_count = nullptr;
    int capacity = 0;
};

struct HitQueue {
    float* t = nullptr;
    GpuVec3* p = nullptr;
    GpuVec3* n = nullptr;
    GpuVec3* ng = nullptr;
    GpuVec2* uv = nullptr;
    int* mat_ids = nullptr;
    int* hit_types = nullptr;
    int* hit_indices = nullptr;
    int* hit_primitive_indices = nullptr;
};

} // namespace ure::gpu

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
