float spectral_sample(
    float4 spectrum,
    float wavelength,
    float wavelengthMin,
    float wavelengthMax,
    uint packetLanes)
{
    float normalized = saturate(
        (wavelength - wavelengthMin) /
        (wavelengthMax - wavelengthMin));
    float scaled = normalized * float(packetLanes - 1);
    uint lower = min(uint(scaled), packetLanes - 1);
    uint upper = min(lower + 1, packetLanes - 1);
    return lerp(spectrum[lower], spectrum[upper], scaled - float(lower));
}

float3 spectral_to_xyz(float4 spectrum, uint packetLanes)
{
    float3 weights[4] = {
        float3(0.143, 0.060, 0.638),
        float3(0.434, 0.995, 0.004),
        float3(0.594, 0.503, 0.000),
        float3(0.011, 0.004, 0.000)
    };
    float3 xyz = 0.0;
    for (uint lane = 0; lane < packetLanes; ++lane)
        xyz += spectrum[lane] * weights[lane];
    return xyz * (300.0 / float(packetLanes)) / 106.856895;
}

float4 apply_mueller(float4 stokes, float diattenuation, float retardance)
{
    float cosine = cos(retardance);
    float sine = sin(retardance);
    return float4(
        stokes.x + diattenuation * stokes.y,
        diattenuation * stokes.x + stokes.y,
        cosine * stokes.z - sine * stokes.w,
        sine * stokes.z + cosine * stokes.w);
}

float3 cosine_sample_hemisphere(float2 sample)
{
    float radius = sqrt(sample.x);
    float angle = 6.283185307179586 * sample.y;
    return float3(
        radius * cos(angle),
        radius * sin(angle),
        sqrt(max(0.0, 1.0 - sample.x)));
}

float4 propagate_wave(float4 field, float phaseScale)
{
    float phase = phaseScale * field.z;
    float2 phasor = float2(cos(phase), sin(phase));
    return float4(
        field.x * phasor.x - field.y * phasor.y,
        field.x * phasor.y + field.y * phasor.x,
        field.z,
        dot(field.xy, field.xy));
}

bool intersect_aabb(
    float3 origin,
    float3 inverseDirection,
    float3 boundsMin,
    float3 boundsMax)
{
    float3 first = (boundsMin - origin) * inverseDirection;
    float3 second = (boundsMax - origin) * inverseDirection;
    float3 nearPlane = min(first, second);
    float3 farPlane = max(first, second);
    float nearDistance = max(nearPlane.x, max(nearPlane.y, nearPlane.z));
    float farDistance = min(farPlane.x, min(farPlane.y, farPlane.z));
    return farDistance >= max(nearDistance, 0.0);
}

float safe_reciprocal(float value)
{
    if (abs(value) >= 1e-6)
        return 1.0 / value;
    return value < 0.0 ? -1e6 : 1e6;
}
