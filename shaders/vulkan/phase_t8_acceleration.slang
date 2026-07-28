struct AccelerationParams
{
    uint rayCount;
    uint triangleCount;
    uint instanceCount;
    uint queryMode;
};

struct AccelerationRay
{
    float4 originTMin;
    float4 directionTMax;
    uint4 maskFlags;
};

struct AccelerationHit
{
    float4 positionT;
    float4 shadingNormal;
    float4 geometricNormal;
    float4 tangentHandedness;
    float4 uvBarycentrics;
    uint4 ids;
};

struct InstanceData
{
    float4 objectToWorld0;
    float4 objectToWorld1;
    float4 objectToWorld2;
    float4 worldToObject0;
    float4 worldToObject1;
    float4 worldToObject2;
    float4 normalTransform0;
    float4 normalTransform1;
    float4 normalTransform2;
    float4 boundsMin;
    float4 boundsMax;
    uint4 ids;
};

[[vk::binding(0, 0)]]
RaytracingAccelerationStructure accelerationScene;

[[vk::binding(1, 0)]]
ConstantBuffer<AccelerationParams> params;

[[vk::binding(2, 0)]]
StructuredBuffer<float4> vertices;

[[vk::binding(3, 0)]]
StructuredBuffer<float4> normals;

[[vk::binding(4, 0)]]
StructuredBuffer<float4> texcoords;

[[vk::binding(5, 0)]]
StructuredBuffer<float4> tangents;

[[vk::binding(6, 0)]]
StructuredBuffer<uint> indices;

[[vk::binding(7, 0)]]
StructuredBuffer<InstanceData> instances;

[[vk::binding(8, 0)]]
StructuredBuffer<AccelerationRay> rays;

[[vk::binding(9, 0)]]
RWStructuredBuffer<AccelerationHit> hits;

[[vk::binding(10, 0)]]
RWStructuredBuffer<float4> framebuffer;

float3 transformPoint(
    float4 row0,
    float4 row1,
    float4 row2,
    float3 value)
{
    float4 homogeneous = float4(value, 1.0);
    return float3(
        dot(row0, homogeneous),
        dot(row1, homogeneous),
        dot(row2, homogeneous));
}

float3 transformVector(
    float4 row0,
    float4 row1,
    float4 row2,
    float3 value)
{
    float4 homogeneous = float4(value, 0.0);
    return float3(
        dot(row0, homogeneous),
        dot(row1, homogeneous),
        dot(row2, homogeneous));
}

float3 orthonormalTangent(
    float3 tangent,
    float3 normal)
{
    float3 projected =
        tangent - normal * dot(tangent, normal);
    float lengthSquared = dot(projected, projected);
    if (lengthSquared <= 1.0e-12)
    {
        float3 axis =
            abs(normal.z) < 0.999
                ? float3(0.0, 0.0, 1.0)
                : float3(0.0, 1.0, 0.0);
        return normalize(cross(axis, normal));
    }
    return projected * rsqrt(lengthSquared);
}

AccelerationHit missHit()
{
    AccelerationHit result;
    result.positionT = float4(0.0, 0.0, 0.0, -1.0);
    result.shadingNormal = float4(0.0);
    result.geometricNormal = float4(0.0);
    result.tangentHandedness = float4(0.0);
    result.uvBarycentrics = float4(0.0);
    result.ids = uint4(0xffffffff);
    return result;
}

AccelerationHit makeHit(
    AccelerationRay ray,
    uint instanceOrdinal,
    uint primitiveIndex,
    float rayT,
    float2 barycentrics)
{
    InstanceData instance = instances[instanceOrdinal];
    uint i0 = indices[primitiveIndex * 3 + 0];
    uint i1 = indices[primitiveIndex * 3 + 1];
    uint i2 = indices[primitiveIndex * 3 + 2];
    float weight = 1.0 - barycentrics.x - barycentrics.y;
    float3 objectGeometric = normalize(cross(
        vertices[i1].xyz - vertices[i0].xyz,
        vertices[i2].xyz - vertices[i0].xyz));
    float3 objectShading = normalize(
        normals[i0].xyz * weight +
        normals[i1].xyz * barycentrics.x +
        normals[i2].xyz * barycentrics.y);
    float3 geometric = normalize(transformVector(
        instance.normalTransform0,
        instance.normalTransform1,
        instance.normalTransform2,
        objectGeometric));
    float3 shading = normalize(transformVector(
        instance.normalTransform0,
        instance.normalTransform1,
        instance.normalTransform2,
        objectShading));
    float4 objectTangent =
        tangents[i0] * weight +
        tangents[i1] * barycentrics.x +
        tangents[i2] * barycentrics.y;
    float3 tangent = transformVector(
        instance.objectToWorld0,
        instance.objectToWorld1,
        instance.objectToWorld2,
        objectTangent.xyz);
    tangent = orthonormalTangent(
        tangent, shading);
    float2 uv =
        texcoords[i0].xy * weight +
        texcoords[i1].xy * barycentrics.x +
        texcoords[i2].xy * barycentrics.y;
    AccelerationHit result;
    result.positionT = float4(
        ray.originTMin.xyz +
            ray.directionTMax.xyz * rayT,
        rayT);
    result.shadingNormal = float4(shading, 0.0);
    result.geometricNormal = float4(geometric, 0.0);
    result.tangentHandedness =
        float4(tangent, objectTangent.w < 0.0 ? -1.0 : 1.0);
    result.uvBarycentrics =
        float4(uv, barycentrics);
    result.ids = uint4(
        instance.ids.y,
        2,
        instance.ids.x,
        primitiveIndex);
    return result;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void ray_query_native(uint3 threadId : SV_DispatchThreadID)
{
    uint index = threadId.x;
    if (index >= params.rayCount)
        return;
    AccelerationRay input = rays[index];
    RayDesc ray;
    ray.Origin = input.originTMin.xyz;
    ray.TMin = input.originTMin.w;
    ray.Direction = input.directionTMax.xyz;
    ray.TMax = input.directionTMax.w;
    RayQuery<RAY_FLAG_NONE> query;
    uint rayFlags =
        input.maskFlags.y != 0
            ? RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH
            : RAY_FLAG_NONE;
    query.TraceRayInline(
        accelerationScene,
        rayFlags,
        input.maskFlags.x,
        ray);
    while (query.Proceed())
    {
    }
    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
    {
        hits[index] = missHit();
        framebuffer[index] = float4(0.0);
        return;
    }
    AccelerationHit result = makeHit(
        input,
        query.CommittedInstanceIndex(),
        query.CommittedPrimitiveIndex(),
        query.CommittedRayT(),
        query.CommittedTriangleBarycentrics());
    hits[index] = result;
    framebuffer[index] = float4(
        result.uvBarycentrics.xy,
        abs(result.shadingNormal.z),
        1.0);
}

bool hitAabb(
    AccelerationRay ray,
    float3 minimum,
    float3 maximum,
    float currentMaximum)
{
    float3 inverseDirection =
        1.0 / ray.directionTMax.xyz;
    float3 first =
        (minimum - ray.originTMin.xyz) *
        inverseDirection;
    float3 second =
        (maximum - ray.originTMin.xyz) *
        inverseDirection;
    float3 nearValues = min(first, second);
    float3 farValues = max(first, second);
    float nearT = max(
        ray.originTMin.w,
        max(nearValues.x, max(nearValues.y, nearValues.z)));
    float farT = min(
        currentMaximum,
        min(farValues.x, min(farValues.y, farValues.z)));
    return nearT <= farT;
}

bool hitTriangle(
    float3 origin,
    float3 direction,
    uint primitiveIndex,
    float minimumT,
    inout float maximumT,
    out float2 barycentrics)
{
    barycentrics = float2(0.0);
    uint i0 = indices[primitiveIndex * 3 + 0];
    uint i1 = indices[primitiveIndex * 3 + 1];
    uint i2 = indices[primitiveIndex * 3 + 2];
    float3 edge1 = vertices[i1].xyz - vertices[i0].xyz;
    float3 edge2 = vertices[i2].xyz - vertices[i0].xyz;
    float3 p = cross(direction, edge2);
    float determinant = dot(edge1, p);
    if (abs(determinant) < 1.0e-8)
        return false;
    float inverse = 1.0 / determinant;
    float3 t = origin - vertices[i0].xyz;
    float u = dot(t, p) * inverse;
    if (u < 0.0 || u > 1.0)
        return false;
    float3 q = cross(t, edge1);
    float v = dot(direction, q) * inverse;
    if (v < 0.0 || u + v > 1.0)
        return false;
    float rayT = dot(edge2, q) * inverse;
    if (rayT <= minimumT || rayT >= maximumT)
        return false;
    maximumT = rayT;
    barycentrics = float2(u, v);
    return true;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void compute_bvh(uint3 threadId : SV_DispatchThreadID)
{
    uint index = threadId.x;
    if (index >= params.rayCount)
        return;
    AccelerationRay input = rays[index];
    float closest = input.directionTMax.w;
    uint closestInstance = 0xffffffff;
    uint closestPrimitive = 0xffffffff;
    float2 closestBarycentrics = float2(0.0);
    bool shadow = input.maskFlags.y != 0;
    for (uint instanceIndex = 0;
         instanceIndex < params.instanceCount;
        ++instanceIndex)
    {
        InstanceData instance = instances[instanceIndex];
        if ((input.maskFlags.x & instance.ids.z) == 0)
            continue;
        if (!hitAabb(
                input,
                instance.boundsMin.xyz,
                instance.boundsMax.xyz,
                closest))
            continue;
        float3 objectOrigin = transformPoint(
            instance.worldToObject0,
            instance.worldToObject1,
            instance.worldToObject2,
            input.originTMin.xyz);
        float3 objectDirection = transformVector(
            instance.worldToObject0,
            instance.worldToObject1,
            instance.worldToObject2,
            input.directionTMax.xyz);
        for (uint primitive = 0;
             primitive < params.triangleCount;
             ++primitive)
        {
            float2 barycentrics;
            if (hitTriangle(
                    objectOrigin,
                    objectDirection,
                    primitive,
                    input.originTMin.w,
                    closest,
                    barycentrics))
            {
                closestInstance = instanceIndex;
                closestPrimitive = primitive;
                closestBarycentrics = barycentrics;
                if (shadow)
                    break;
            }
        }
        if (shadow && closestInstance != 0xffffffff)
            break;
    }
    if (closestInstance == 0xffffffff)
    {
        hits[index] = missHit();
        framebuffer[index] = float4(0.0);
        return;
    }
    AccelerationHit result = makeHit(
        input,
        closestInstance,
        closestPrimitive,
        closest,
        closestBarycentrics);
    hits[index] = result;
    framebuffer[index] = float4(
        result.uvBarycentrics.xy,
        abs(result.shadingNormal.z),
        1.0);
}
