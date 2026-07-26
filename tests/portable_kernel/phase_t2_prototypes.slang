import portable_semantics;

struct KernelParams
{
    uint elementCount;
    uint queueCapacity;
    uint64_t baseAddress;
    float wavelengthMin;
    float wavelengthMax;
    float phaseScale;
};

[[vk::binding(0, 0)]]
ConstantBuffer<KernelParams> params : register(b0);

[[vk::binding(1, 0)]]
RWStructuredBuffer<float4> values : register(u0);

[[vk::binding(2, 0)]]
RWStructuredBuffer<uint> indices : register(u1);

[[vk::binding(3, 0)]]
RWStructuredBuffer<uint> counters : register(u2);

[SpecializationConstant]
uniform const uint kPacketLanes = 4;

[shader("compute")]
[numthreads(64, 1, 1)]
void spectral_conversion(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint index = dispatchThreadId.x;
    if (index >= params.elementCount)
        return;
    float wavelength = lerp(
        params.wavelengthMin,
        params.wavelengthMax,
        (float(index) + 0.5) / float(params.elementCount));
    float4 spectrum = values[index];
    float sampled = spectral_sample(
        spectrum, wavelength, params.wavelengthMin,
        params.wavelengthMax, kPacketLanes);
    float3 xyz = spectral_to_xyz(spectrum, kPacketLanes);
    values[index] = float4(xyz, sampled);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void mueller_transport(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint index = dispatchThreadId.x;
    if (index >= params.elementCount)
        return;
    values[index] = apply_mueller(values[index], 0.35, 0.7);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void queue_compaction(
    uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint index = dispatchThreadId.x;
    bool active = index < params.elementCount && values[index].x > 0.0;
    uint activeCount = WaveActiveCountBits(active);
    if (WaveIsFirstLane())
        InterlockedAdd(counters[0], activeCount);
    if (!active)
        return;
    uint destination;
    InterlockedAdd(counters[1], 1, destination);
    if (destination < params.queueCapacity)
        indices[destination] = index + uint(params.baseAddress & 0xffffffff);
    if (destination >= params.queueCapacity)
        InterlockedAdd(counters[2], 1);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void bsdf_sampling(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint index = dispatchThreadId.x;
    if (index >= params.elementCount)
        return;
    float2 sample = frac(values[index].xy);
    float3 direction = cosine_sample_hemisphere(sample);
    values[index] = float4(direction, direction.z * 0.3183098861837907);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void wave_propagation(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint index = dispatchThreadId.x;
    if (index >= params.elementCount)
        return;
    values[index] = propagate_wave(values[index], params.phaseScale);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void traversal_query(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint index = dispatchThreadId.x;
    if (index >= params.elementCount)
        return;
    float4 ray = values[index];
    float3 inverseDirection = float3(
        safe_reciprocal(ray.x),
        safe_reciprocal(ray.y),
        safe_reciprocal(ray.z));
    bool hit = intersect_aabb(
        float3(0.0, 0.0, -2.0),
        inverseDirection,
        float3(-1.0, -1.0, -1.0),
        float3(1.0, 1.0, 1.0));
    values[index].w = hit ? 1.0 : 0.0;
}
