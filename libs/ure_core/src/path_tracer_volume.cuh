#pragma once

#include "path_tracer_decl.cuh"

enum class VolumePhaseFunction {
    HenyeyGreenstein = 0,
    Rayleigh = 1,
    Mie = 2
};

static __device__ StokesVector apply_volume_phase_polarization(
    VolumePhaseFunction phase, const StokesVector& input) {
    return phase == VolumePhaseFunction::Mie
        ? StokesVector(input.I, 0.0f, 0.0f, 0.0f)
        : input;
}

static __device__ bool find_mie_interval(const float* values, int count, float query,
                                         int* lower, float* fraction) {
    if (!values || count < 2 || !lower || !fraction ||
        query < values[0] || query > values[count - 1]) {
        return false;
    }
    int left = 0;
    int right = count - 1;
    while (right - left > 1) {
        const int middle = left + (right - left) / 2;
        if (values[middle] <= query) left = middle;
        else right = middle;
    }
    if (query == values[count - 1]) left = count - 2;
    const float width = values[left + 1] - values[left];
    if (!(width > 0.0f)) return false;
    *lower = left;
    *fraction = (query - values[left]) / width;
    return true;
}

static __device__ bool lookup_mie_resource(const GpuScene& scene, int resource_index,
                                           const GpuMiePhaseResource** resource) {
    if (!resource || !scene.mie_phase_resources || resource_index < 0 ||
        resource_index >= scene.mie_phase_resource_count) {
        return false;
    }
    const auto* candidate = &scene.mie_phase_resources[resource_index];
    if (candidate->wavelength_offset < 0 || candidate->wavelength_count < 2 ||
        candidate->angle_offset < 0 || candidate->angle_count < 2 ||
        candidate->phase_offset < 0 || candidate->cross_section_offset < 0 ||
        candidate->wavelength_count > scene.mie_wavelength_count ||
        candidate->wavelength_offset > scene.mie_wavelength_count - candidate->wavelength_count ||
        candidate->angle_count > scene.mie_angle_count ||
        candidate->angle_offset > scene.mie_angle_count - candidate->angle_count ||
        candidate->wavelength_count > 2147483647 / candidate->angle_count) {
        return false;
    }
    const int table_count = candidate->wavelength_count * candidate->angle_count;
    if (table_count > scene.mie_phase_value_count ||
        candidate->phase_offset > scene.mie_phase_value_count - table_count ||
        table_count > scene.mie_cdf_value_count ||
        candidate->phase_offset > scene.mie_cdf_value_count - table_count) {
        return false;
    }
    *resource = candidate;
    return true;
}

static __device__ bool lookup_mie_phase(const GpuScene& scene, int resource_index,
                                        float wavelength_nm, float cos_theta, float* value) {
    const GpuMiePhaseResource* resource = nullptr;
    if (!value || !scene.mie_wavelengths || !scene.mie_cos_theta ||
        !scene.mie_phase_values ||
        !lookup_mie_resource(scene, resource_index, &resource)) {
        return false;
    }
    int wavelength_lower = 0;
    int angle_lower = 0;
    float wavelength_fraction = 0.0f;
    float angle_fraction = 0.0f;
    constexpr float kCosineTolerance = 4.0f * 1.1920928955078125e-7f;
    if (!isfinite(cos_theta) || cos_theta < -1.0f - kCosineTolerance ||
        cos_theta > 1.0f + kCosineTolerance) {
        return false;
    }
    const float clamped_cos_theta = fminf(1.0f, fmaxf(-1.0f, cos_theta));
    if (!find_mie_interval(scene.mie_wavelengths + resource->wavelength_offset,
                           resource->wavelength_count, wavelength_nm,
                           &wavelength_lower, &wavelength_fraction) ||
        !find_mie_interval(scene.mie_cos_theta + resource->angle_offset,
                            resource->angle_count, clamped_cos_theta,
                           &angle_lower, &angle_fraction)) {
        return false;
    }
    const int row0 = resource->phase_offset + wavelength_lower * resource->angle_count;
    const int row1 = row0 + resource->angle_count;
    const float p00 = scene.mie_phase_values[row0 + angle_lower];
    const float p01 = scene.mie_phase_values[row0 + angle_lower + 1];
    const float p10 = scene.mie_phase_values[row1 + angle_lower];
    const float p11 = scene.mie_phase_values[row1 + angle_lower + 1];
    const float lower = p00 + angle_fraction * (p01 - p00);
    const float upper = p10 + angle_fraction * (p11 - p10);
    *value = lower + wavelength_fraction * (upper - lower);
    return isfinite(*value) && *value >= 0.0f;
}

static __device__ bool lookup_mie_cross_section(const GpuScene& scene, int resource_index,
                                                float wavelength_nm, const float* values,
                                                float* value) {
    const GpuMiePhaseResource* resource = nullptr;
    if (!value || !values || !scene.mie_wavelengths ||
        !lookup_mie_resource(scene, resource_index, &resource)) {
        return false;
    }
    if (resource->wavelength_count > scene.mie_cross_section_count ||
        resource->cross_section_offset >
            scene.mie_cross_section_count - resource->wavelength_count) {
        return false;
    }
    int lower = 0;
    float fraction = 0.0f;
    if (!find_mie_interval(scene.mie_wavelengths + resource->wavelength_offset,
                           resource->wavelength_count, wavelength_nm, &lower, &fraction)) {
        return false;
    }
    const int offset = resource->cross_section_offset + lower;
    *value = values[offset] + fraction * (values[offset + 1] - values[offset]);
    return isfinite(*value) && *value >= 0.0f;
}

static __device__ bool invert_mie_phase_row(const GpuScene& scene,
                                            const GpuMiePhaseResource& resource,
                                            int wavelength_row, float random_value,
                                            float* cos_theta) {
    if (!cos_theta || !scene.mie_cdf_values) return false;
    const int row = resource.phase_offset + wavelength_row * resource.angle_count;
    const float* cdf = scene.mie_cdf_values + row;
    const float u = fminf(nextafterf(1.0f, 0.0f), fmaxf(0.0f, random_value));
    int left = 0;
    int right = resource.angle_count - 1;
    while (right - left > 1) {
        const int middle = left + (right - left) / 2;
        if (cdf[middle] <= u) left = middle;
        else right = middle;
    }
    while (right < resource.angle_count && !(cdf[right] > cdf[left])) ++right;
    if (right >= resource.angle_count) return false;
    left = right - 1;
    while (left > 0 && cdf[left] == cdf[right]) --left;
    const float mu0 = scene.mie_cos_theta[resource.angle_offset + left];
    const float mu1 = scene.mie_cos_theta[resource.angle_offset + right];
    const float width = mu1 - mu0;
    const float p0 = scene.mie_phase_values[row + left];
    const float p1 = scene.mie_phase_values[row + right];
    const float target_mass = u - cdf[left];
    if (!(width > 0.0f) || target_mass < 0.0f) return false;
    if (target_mass == 0.0f) {
        *cos_theta = mu0;
        return true;
    }
    const float a = 6.28318530718f * p0;
    const float b = 6.28318530718f * (p1 - p0) / width;
    float distance = 0.0f;
    if (fabsf(b) * width <= 1.0e-6f * fmaxf(a, 1.0e-20f)) {
        if (!(a > 0.0f)) return false;
        distance = target_mass / a;
    } else {
        const float discriminant = fmaxf(0.0f, a * a + 2.0f * b * target_mass);
        const float denominator = a + sqrtf(discriminant);
        if (!(denominator > 0.0f)) return false;
        distance = 2.0f * target_mass / denominator;
    }
    *cos_theta = fminf(mu1, fmaxf(mu0, mu0 + distance));
    return true;
}

static __device__ bool sample_mie_phase_lds_pdf(
    const GpuScene& scene, int resource_index, const GpuVec3& w_in,
    float wavelength_nm, float r1, float r2, GpuVec3* w_out, float* pdf) {
    const GpuMiePhaseResource* resource = nullptr;
    if (!w_out || !pdf || !scene.mie_cdf_values ||
        !lookup_mie_resource(scene, resource_index, &resource)) {
        return false;
    }
    int wavelength_lower = 0;
    float wavelength_fraction = 0.0f;
    if (!find_mie_interval(scene.mie_wavelengths + resource->wavelength_offset,
                           resource->wavelength_count, wavelength_nm,
                           &wavelength_lower, &wavelength_fraction)) {
        return false;
    }
    int row = wavelength_lower;
    float angular_random = r1;
    const float lower_probability = 1.0f - wavelength_fraction;
    if (lower_probability <= 0.0f) {
        row = wavelength_lower + 1;
    } else if (wavelength_fraction > 0.0f) {
        if (r1 < lower_probability) angular_random = r1 / lower_probability;
        else {
            row = wavelength_lower + 1;
            angular_random = (r1 - lower_probability) / wavelength_fraction;
        }
    }
    float mu = 0.0f;
    if (!invert_mie_phase_row(scene, *resource, row, angular_random, &mu) ||
        !lookup_mie_phase(scene, resource_index, wavelength_nm, mu, pdf)) {
        return false;
    }
    const float sin_theta = sqrtf(fmaxf(0.0f, 1.0f - mu * mu));
    const float phi = 6.28318530718f * r2;
    const GpuVec3 forward = w_in.normalize();
    GpuVec3 tangent = fabsf(forward.x) > 0.9f
        ? GpuVec3(0.0f, 1.0f, 0.0f)
        : GpuVec3(1.0f, 0.0f, 0.0f);
    const GpuVec3 bitangent = forward.cross(tangent).normalize();
    tangent = forward.cross(bitangent).normalize();
    *w_out = (tangent * (cosf(phi) * sin_theta) +
              bitangent * (sinf(phi) * sin_theta) + forward * mu).normalize();
    return true;
}

static __device__ bool sample_mie_packet_phase_lds_pdf(
    const GpuScene& scene, int resource_index, const GpuVec3& w_in,
    const float* wavelengths, int wavelength_count, int spectral_mode,
    int active_channel, float r1, float r2, GpuVec3* w_out, float* pdf) {
    if (!wavelengths || wavelength_count <= 0 || wavelength_count > kMaxPacketLanes ||
        !w_out || !pdf) {
        return false;
    }
    int selected_channel = active_channel;
    float angular_random = r1;
    if (spectral_mode_is_sampled(spectral_mode)) {
        if (selected_channel < 0 || selected_channel >= wavelength_count) return false;
    } else {
        const float nested = fminf(nextafterf(1.0f, 0.0f), fmaxf(0.0f, r1)) *
                             static_cast<float>(wavelength_count);
        selected_channel = min(wavelength_count - 1, static_cast<int>(nested));
        angular_random = nested - static_cast<float>(selected_channel);
    }
    float selected_pdf = 0.0f;
    if (!sample_mie_phase_lds_pdf(scene, resource_index, w_in,
                                  wavelengths[selected_channel], angular_random, r2,
                                  w_out, &selected_pdf)) {
        return false;
    }
    if (spectral_mode_is_sampled(spectral_mode)) {
        *pdf = selected_pdf;
        return true;
    }
    float mixture = 0.0f;
    for (int channel = 0; channel < wavelength_count; ++channel) {
        float lane_pdf = 0.0f;
        if (!lookup_mie_phase(scene, resource_index, wavelengths[channel],
                              w_in.normalize().dot(w_out->normalize()), &lane_pdf)) {
            return false;
        }
        mixture += lane_pdf;
    }
    *pdf = mixture / static_cast<float>(wavelength_count);
    return isfinite(*pdf) && *pdf > 0.0f;
}

static __device__ bool load_mie_medium_cross_sections(
    const GpuScene& scene, int resource_index, const float* wavelengths,
    int wavelength_count, SpectralPacket* scattering, SpectralPacket* extinction) {
    if (!wavelengths || wavelength_count <= 0 || wavelength_count > kMaxPacketLanes ||
        !scattering || !extinction || !scene.mie_scattering_cross_sections ||
        !scene.mie_extinction_cross_sections) {
        return false;
    }
    for (int channel = 0; channel < wavelength_count; ++channel) {
        if (!lookup_mie_cross_section(scene, resource_index, wavelengths[channel],
                                      scene.mie_scattering_cross_sections,
                                      &scattering->values[channel]) ||
            !lookup_mie_cross_section(scene, resource_index, wavelengths[channel],
                                       scene.mie_extinction_cross_sections,
                                       &extinction->values[channel])) {
            return false;
        }
        scattering->wavelengths[channel] = wavelengths[channel];
        extinction->wavelengths[channel] = wavelengths[channel];
    }
    return true;
}

static __device__ bool eval_mie_packet_phase_pdf(
    const GpuScene& scene, int resource_index, const float* wavelengths,
    int wavelength_count, int spectral_mode, int active_channel,
    float cos_theta, float* pdf) {
    if (!wavelengths || wavelength_count <= 0 || !pdf) return false;
    if (spectral_mode_is_sampled(spectral_mode)) {
        if (active_channel < 0 || active_channel >= wavelength_count) return false;
        return lookup_mie_phase(scene, resource_index, wavelengths[active_channel],
                                cos_theta, pdf);
    }
    float sum = 0.0f;
    for (int channel = 0; channel < wavelength_count; ++channel) {
        float value = 0.0f;
        if (!lookup_mie_phase(scene, resource_index, wavelengths[channel],
                              cos_theta, &value)) {
            return false;
        }
        sum += value;
    }
    *pdf = sum / static_cast<float>(wavelength_count);
    return isfinite(*pdf) && *pdf >= 0.0f;
}

__host__ __device__ inline bool is_supported_analytic_volume_phase_function(VolumePhaseFunction phase) {
    return phase == VolumePhaseFunction::HenyeyGreenstein ||
           phase == VolumePhaseFunction::Rayleigh;
}

__device__ GpuVec3 sample_henyey_greenstein_lds(const GpuVec3& w_in, float g, float r1, float r2) {
    if (fabsf(g) < 1e-3f) {
        return sample_unit_vector_lds(r1, r2);
    }

    float sqr_term = (1.0f - g * g) / (1.0f - g + 2.0f * g * r1);
    float cos_theta = (1.0f + g * g - sqr_term * sqr_term) / (2.0f * g);

    if (cos_theta > 1.0f) cos_theta = 1.0f;
    if (cos_theta < -1.0f) cos_theta = -1.0f;

    float sin_theta = sqrtf(fmaxf(0.0f, 1.0f - cos_theta * cos_theta));
    float phi = 2.0f * 3.14159265359f * r2;

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

__device__ GpuVec3 sample_henyey_greenstein(const GpuVec3& w_in, float g, unsigned int& seed) {
    float r1 = rand_float(seed);
    float r2 = rand_float(seed);
    return sample_henyey_greenstein_lds(w_in, g, r1, r2);
}

__device__ float eval_henyey_greenstein(float cos_theta, float g) {
    if (fabsf(g) < 1e-3f) return 1.0f / (4.0f * 3.14159265359f);

    float denom = 1.0f + g * g - 2.0f * g * cos_theta;
    return (1.0f - g * g) / (4.0f * 3.14159265359f * denom * sqrtf(fmaxf(0.0f, denom)));
}

__device__ float pdf_henyey_greenstein(float cos_theta, float g) {
    return eval_henyey_greenstein(cos_theta, g);
}

__device__ float pdf_henyey_greenstein(const GpuVec3& w_in, const GpuVec3& w_out, float g) {
    return pdf_henyey_greenstein(w_in.normalize().dot(w_out.normalize()), g);
}

__device__ GpuVec3 sample_henyey_greenstein_lds_pdf(
    const GpuVec3& w_in,
    float g,
    float r1,
    float r2,
    float* pdf
) {
    GpuVec3 out = sample_henyey_greenstein_lds(w_in, g, r1, r2);
    if (pdf) {
        *pdf = pdf_henyey_greenstein(w_in, out, g);
    }
    return out;
}

__device__ float eval_rayleigh_phase(float cos_theta) {
    float mu = fminf(1.0f, fmaxf(-1.0f, cos_theta));
    return (3.0f / (16.0f * 3.14159265359f)) * (1.0f + mu * mu);
}

__device__ float pdf_rayleigh_phase(float cos_theta) {
    return eval_rayleigh_phase(cos_theta);
}

__device__ float pdf_rayleigh_phase(const GpuVec3& w_in, const GpuVec3& w_out) {
    return pdf_rayleigh_phase(w_in.normalize().dot(w_out.normalize()));
}

__device__ GpuVec3 sample_rayleigh_phase_lds(const GpuVec3& w_in, float r1, float r2) {
    float mu = 2.0f * r1 - 1.0f;
    for (int i = 0; i < 8; ++i) {
        float f = 0.5f + 0.375f * mu + 0.125f * mu * mu * mu - r1;
        float df = 0.375f * (1.0f + mu * mu);
        mu -= f / fmaxf(1e-6f, df);
        mu = fminf(1.0f, fmaxf(-1.0f, mu));
    }

    float sin_theta = sqrtf(fmaxf(0.0f, 1.0f - mu * mu));
    float phi = 2.0f * 3.14159265359f * r2;
    GpuVec3 forward = w_in.normalize();
    GpuVec3 v1 = fabsf(forward.x) > 0.9f
        ? GpuVec3(0.0f, 1.0f, 0.0f)
        : GpuVec3(1.0f, 0.0f, 0.0f);
    GpuVec3 v2 = forward.cross(v1).normalize();
    v1 = forward.cross(v2).normalize();

    return (v1 * (cosf(phi) * sin_theta) +
            v2 * (sinf(phi) * sin_theta) +
            forward * mu).normalize();
}

__device__ GpuVec3 sample_rayleigh_phase_lds_pdf(
    const GpuVec3& w_in,
    float r1,
    float r2,
    float* pdf
) {
    GpuVec3 out = sample_rayleigh_phase_lds(w_in, r1, r2);
    if (pdf) {
        *pdf = pdf_rayleigh_phase(w_in, out);
    }
    return out;
}

__device__ float eval_volume_phase(
    VolumePhaseFunction phase,
    float cos_theta,
    float anisotropy,
    bool* supported
) {
    bool ok = is_supported_analytic_volume_phase_function(phase);
    if (supported) {
        *supported = ok;
    }
    if (!ok) {
        return 0.0f;
    }
    if (phase == VolumePhaseFunction::Rayleigh) {
        return eval_rayleigh_phase(cos_theta);
    }
    return eval_henyey_greenstein(cos_theta, anisotropy);
}

__device__ bool sample_volume_phase_lds_pdf(
    VolumePhaseFunction phase,
    const GpuVec3& w_in,
    float anisotropy,
    float r1,
    float r2,
    GpuVec3* w_out,
    float* pdf
) {
    if (!is_supported_analytic_volume_phase_function(phase)) {
        if (w_out) {
            *w_out = GpuVec3(0.0f, 0.0f, 0.0f);
        }
        if (pdf) {
            *pdf = 0.0f;
        }
        return false;
    }
    if (phase == VolumePhaseFunction::Rayleigh) {
        if (w_out) {
            *w_out = sample_rayleigh_phase_lds_pdf(w_in, r1, r2, pdf);
        }
        return true;
    }
    if (w_out) {
        *w_out = sample_henyey_greenstein_lds_pdf(w_in, anisotropy, r1, r2, pdf);
    }
    return true;
}
