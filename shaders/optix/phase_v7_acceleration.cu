#include <optix.h>
#include <optix_device.h>

struct Float4 {
    float x;
    float y;
    float z;
    float w;
};

struct UInt4 {
    unsigned int x;
    unsigned int y;
    unsigned int z;
    unsigned int w;
};

struct AccelerationRay {
    Float4 origin_tmin;
    Float4 direction_tmax;
    UInt4 mask_flags;
};

struct AccelerationHit {
    Float4 position_t;
    Float4 shading_normal;
    Float4 geometric_normal;
    Float4 tangent_handedness;
    Float4 uv_barycentrics;
    UInt4 ids;
};

struct GeometryData {
    const unsigned char* vertices;
    unsigned long long vertex_stride;
    const Float4* normals;
    const Float4* texcoords;
    const Float4* tangents;
    const unsigned int* indices;
};

struct InstanceData {
    unsigned int stable_index;
    unsigned int material_index;
};

struct LaunchParams {
    OptixTraversableHandle scene;
    const AccelerationRay* rays;
    AccelerationHit* hits;
    Float4* framebuffer;
    const InstanceData* instances;
    unsigned int ray_count;
};

extern "C" {
__constant__ LaunchParams params;
}

static __forceinline__ __device__ float3 to_float3(
    const Float4& value) {
    return make_float3(value.x, value.y, value.z);
}

static __forceinline__ __device__ Float4 to_float4(
    const float3& value,
    float w) {
    return {value.x, value.y, value.z, w};
}

static __forceinline__ __device__ float3 add(
    const float3& left,
    const float3& right) {
    return make_float3(
        left.x + right.x,
        left.y + right.y,
        left.z + right.z);
}

static __forceinline__ __device__ float3 subtract(
    const float3& left,
    const float3& right) {
    return make_float3(
        left.x - right.x,
        left.y - right.y,
        left.z - right.z);
}

static __forceinline__ __device__ float3 multiply(
    const float3& value,
    float scale) {
    return make_float3(
        value.x * scale,
        value.y * scale,
        value.z * scale);
}

static __forceinline__ __device__ float dot_product(
    const float3& left,
    const float3& right) {
    return left.x * right.x +
        left.y * right.y +
        left.z * right.z;
}

static __forceinline__ __device__ float3 cross_product(
    const float3& left,
    const float3& right) {
    return make_float3(
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x);
}

static __forceinline__ __device__ float3 normalized(
    const float3& value) {
    const float inverse_length =
        rsqrtf(dot_product(value, value));
    return multiply(value, inverse_length);
}

static __forceinline__ __device__ float3
orthonormal_tangent(
    const float3& tangent,
    const float3& normal) {
    const float3 projected = subtract(
        tangent,
        multiply(
            normal,
            dot_product(tangent, normal)));
    const float length_squared =
        dot_product(projected, projected);
    if (length_squared <= 1.0e-12f) {
        const float3 axis =
            fabsf(normal.z) < 0.999f
                ? make_float3(0.0f, 0.0f, 1.0f)
                : make_float3(0.0f, 1.0f, 0.0f);
        return normalized(
            cross_product(axis, normal));
    }
    return multiply(
        projected,
        rsqrtf(length_squared));
}

static __forceinline__ __device__ float3 load_position(
    const GeometryData& geometry,
    unsigned int index) {
    return *reinterpret_cast<const float3*>(
        geometry.vertices +
        static_cast<unsigned long long>(index) *
            geometry.vertex_stride);
}

static __forceinline__ __device__ AccelerationHit miss_hit() {
    AccelerationHit result{};
    result.position_t.w = -1.0f;
    result.ids = {
        0xffffffffu,
        0xffffffffu,
        0xffffffffu,
        0xffffffffu};
    return result;
}

extern "C" __global__ void __raygen__main() {
    const unsigned int index =
        optixGetLaunchIndex().x;
    if (index >= params.ray_count) return;
    const AccelerationRay input = params.rays[index];
    unsigned int payload = 0;
    const unsigned int flags =
        input.mask_flags.y != 0
            ? OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT
            : OPTIX_RAY_FLAG_NONE;
    optixTrace(
        params.scene,
        to_float3(input.origin_tmin),
        to_float3(input.direction_tmax),
        input.origin_tmin.w,
        input.direction_tmax.w,
        0.0f,
        static_cast<OptixVisibilityMask>(
            input.mask_flags.x),
        flags,
        0,
        1,
        0,
        payload);
}

extern "C" __global__ void __miss__main() {
    const unsigned int index =
        optixGetLaunchIndex().x;
    params.hits[index] = miss_hit();
    params.framebuffer[index] = {};
}

extern "C" __global__ void __closesthit__main() {
    const unsigned int launch_index =
        optixGetLaunchIndex().x;
    const auto* geometry =
        reinterpret_cast<const GeometryData*>(
            optixGetSbtDataPointer());
    const unsigned int primitive =
        optixGetPrimitiveIndex();
    const unsigned int instance_ordinal =
        optixGetInstanceIndex();
    const InstanceData instance =
        params.instances[instance_ordinal];
    const unsigned int i0 =
        geometry->indices[primitive * 3 + 0];
    const unsigned int i1 =
        geometry->indices[primitive * 3 + 1];
    const unsigned int i2 =
        geometry->indices[primitive * 3 + 2];
    const float2 barycentrics =
        optixGetTriangleBarycentrics();
    const float weight =
        1.0f - barycentrics.x - barycentrics.y;
    const float3 p0 = load_position(*geometry, i0);
    const float3 p1 = load_position(*geometry, i1);
    const float3 p2 = load_position(*geometry, i2);
    const float3 object_geometric =
        normalized(cross_product(
            subtract(p1, p0),
            subtract(p2, p0)));
    const float3 object_shading = normalized(add(
        add(
            multiply(
                to_float3(geometry->normals[i0]),
                weight),
            multiply(
                to_float3(geometry->normals[i1]),
                barycentrics.x)),
        multiply(
            to_float3(geometry->normals[i2]),
            barycentrics.y)));
    const Float4 tangent0 = geometry->tangents[i0];
    const Float4 tangent1 = geometry->tangents[i1];
    const Float4 tangent2 = geometry->tangents[i2];
    const float3 object_tangent = add(
        add(
            multiply(to_float3(tangent0), weight),
            multiply(
                to_float3(tangent1),
                barycentrics.x)),
        multiply(
            to_float3(tangent2),
            barycentrics.y));
    const float tangent_w =
        tangent0.w * weight +
        tangent1.w * barycentrics.x +
        tangent2.w * barycentrics.y;
    const float3 geometric = normalized(
        optixTransformNormalFromObjectToWorldSpace(
            object_geometric));
    const float3 shading = normalized(
        optixTransformNormalFromObjectToWorldSpace(
            object_shading));
    float3 tangent =
        optixTransformVectorFromObjectToWorldSpace(
            object_tangent);
    tangent = orthonormal_tangent(
        tangent, shading);
    const float2 uv = {
        geometry->texcoords[i0].x * weight +
            geometry->texcoords[i1].x *
                barycentrics.x +
            geometry->texcoords[i2].x *
                barycentrics.y,
        geometry->texcoords[i0].y * weight +
            geometry->texcoords[i1].y *
                barycentrics.x +
            geometry->texcoords[i2].y *
                barycentrics.y};
    const AccelerationRay input =
        params.rays[launch_index];
    const float ray_t = optixGetRayTmax();
    const float3 position = add(
        to_float3(input.origin_tmin),
        multiply(
            to_float3(input.direction_tmax),
            ray_t));
    AccelerationHit result{};
    result.position_t = to_float4(position, ray_t);
    result.shading_normal =
        to_float4(shading, 0.0f);
    result.geometric_normal =
        to_float4(geometric, 0.0f);
    result.tangent_handedness =
        to_float4(tangent, tangent_w < 0.0f ? -1.0f : 1.0f);
    result.uv_barycentrics = {
        uv.x,
        uv.y,
        barycentrics.x,
        barycentrics.y};
    result.ids = {
        instance.material_index,
        2,
        instance.stable_index,
        primitive};
    params.hits[launch_index] = result;
    params.framebuffer[launch_index] = {
        uv.x,
        uv.y,
        fabsf(shading.z),
        1.0f};
}
