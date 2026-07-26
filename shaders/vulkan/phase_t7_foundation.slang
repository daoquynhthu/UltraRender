import portable_semantics;

struct FoundationParams
{
    uint elementCount;
    uint width;
    uint queueCapacity;
    uint reserved;
};

[[vk::binding(0, 0)]]
ConstantBuffer<FoundationParams> params : register(b0);

[[vk::binding(1, 0)]]
RWStructuredBuffer<float4> values : register(u0);

[[vk::binding(2, 0)]]
RWStructuredBuffer<uint> indices : register(u1);

[[vk::binding(3, 0)]]
RWStructuredBuffer<uint> counters : register(u2);

[[vk::binding(4, 0)]]
RWStructuredBuffer<float4> film : register(u3);

[[vk::binding(5, 0)]]
RWStructuredBuffer<float4> aov : register(u4);

[SpecializationConstant]
uniform const uint kRaygenDirectionZ = 1;

[shader("compute")]
[numthreads(64, 1, 1)]
void raygen(uint3 thread_id : SV_DispatchThreadID)
{
    uint index = thread_id.x;
    if (index >= params.elementCount)
        return;
    uint width = params.width;
    uint x = index % width;
    uint y = index / width;
    float fx = (float(x) + 0.5) / float(width);
    float fy = (float(y) + 0.5) / float(width);
    values[index] = float4(
        fx * 2.0 - 1.0,
        fy * 2.0 - 1.0,
        float(kRaygenDirectionZ),
        1.0);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void spectral_polarization(uint3 thread_id : SV_DispatchThreadID)
{
    uint index = thread_id.x;
    if (index >= params.elementCount)
        return;
    float normalized_wavelength =
        (float(index) + 0.5) / float(params.elementCount);
    float4 stokes = values[index];
    values[index] = apply_mueller(
        stokes,
        0.1 + normalized_wavelength * 0.2,
        0.5);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void queue_compaction(uint3 thread_id : SV_DispatchThreadID)
{
    uint index = thread_id.x;
    if (index >= params.elementCount || values[index].x <= 0.0)
        return;
    uint destination;
    InterlockedAdd(counters[0], 1, destination);
    if (destination < params.queueCapacity)
        indices[destination] = index;
    else
        InterlockedAdd(counters[1], 1);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void film_aov(uint3 thread_id : SV_DispatchThreadID)
{
    uint index = thread_id.x;
    if (index >= params.elementCount)
        return;
    float4 value = values[index];
    film[index] = float4(value.xyz, 1.0);
    aov[index] = float4(abs(value.xyz), value.w);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void wave_reference(uint3 thread_id : SV_DispatchThreadID)
{
    uint index = thread_id.x;
    if (index >= params.elementCount)
        return;
    values[index] = propagate_wave(values[index], 0.4);
}
