import portable_semantics;

cbuffer FoundationParams : register(b0)
{
    uint elementCount;
    uint width;
    uint queueCapacity;
    uint reserved;
};

RWStructuredBuffer<float4> values : register(u0);
RWTexture2D<float4> imageTarget : register(u1);
Texture2D<float4> sampledImage : register(t0);
SamplerState imageSampler : register(s0);
RWStructuredBuffer<float4> sampledValues : register(u2);

[shader("compute")]
[numthreads(64, 1, 1)]
void foundation(uint3 threadId : SV_DispatchThreadID)
{
    uint index = threadId.x;
    if (index >= elementCount)
        return;
    uint x = index % width;
    uint y = index / width;
    float4 stokes = float4(
        (float(x) + 0.5) / float(width) * 2.0 - 1.0,
        (float(y) + 0.5) / float(width) * 2.0 - 1.0,
        1.0,
        1.0);
    float normalizedWavelength =
        (float(index) + 0.5) / float(elementCount);
    values[index] = apply_mueller(
        stokes,
        0.1 + normalizedWavelength * 0.2,
        0.5);
}

[shader("compute")]
[numthreads(4, 4, 1)]
void image_write(uint3 threadId : SV_DispatchThreadID)
{
    imageTarget[threadId.xy] = float4(
        (float(threadId.x) + 0.5) / 4.0,
        (float(threadId.y) + 0.5) / 4.0,
        0.25,
        1.0);
}

[shader("compute")]
[numthreads(4, 4, 1)]
void image_sample(uint3 threadId : SV_DispatchThreadID)
{
    uint index = threadId.y * 4 + threadId.x;
    float2 uv = (float2(threadId.xy) + 0.5) / 4.0;
    sampledValues[index] =
        sampledImage.SampleLevel(imageSampler, uv, 0.0);
}
