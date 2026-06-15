#pragma once

#include "path_tracer_decl.cuh"

struct ComplexF {
    float re;
    float im;
};

__device__ inline ComplexF c_make(float re, float im) {
    return {re, im};
}

__device__ inline ComplexF c_add(ComplexF a, ComplexF b) {
    return {a.re + b.re, a.im + b.im};
}

__device__ inline ComplexF c_sub(ComplexF a, ComplexF b) {
    return {a.re - b.re, a.im - b.im};
}

__device__ inline ComplexF c_scale(ComplexF a, float s) {
    return {a.re * s, a.im * s};
}

__device__ inline ComplexF c_mul(ComplexF a, ComplexF b) {
    return {a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re};
}

__device__ inline ComplexF c_div(ComplexF a, ComplexF b) {
    float denom = b.re * b.re + b.im * b.im;
    return {(a.re * b.re + a.im * b.im) / denom, (a.im * b.re - a.re * b.im) / denom};
}

__device__ inline float c_abs2(ComplexF a) {
    return a.re * a.re + a.im * a.im;
}

__device__ inline ComplexF c_exp_i(float phase) {
    return {cosf(phase), sinf(phase)};
}

__device__ inline ComplexF c_sqrt(ComplexF z) {
    float r = sqrtf(z.re * z.re + z.im * z.im);
    float re = sqrtf(fmaxf(0.0f, 0.5f * (r + z.re)));
    float im = copysignf(sqrtf(fmaxf(0.0f, 0.5f * (r - z.re))), z.im);
    return {re, im};
}

struct DielectricBoundary {
    float rs;
    float rp;
    float ts;
    float tp;
    float Rs;
    float Rp;
    float Ts;
    float Tp;
    float cos_t;
    float eta_jacobian;
    bool tir;
};

enum class BoundaryTransportMode {
    Radiance,
    Importance
};

__device__ inline float eval_boundary_transport_scale(float eta_i, float eta_t, BoundaryTransportMode mode) {
    float safe_eta_i = fmaxf(1e-6f, eta_i);
    float safe_eta_t = fmaxf(1e-6f, eta_t);
    float eta_ratio = safe_eta_i / safe_eta_t;
    float radiance_scale = eta_ratio * eta_ratio;
    return mode == BoundaryTransportMode::Radiance
        ? radiance_scale
        : 1.0f / fmaxf(1e-12f, radiance_scale);
}

__device__ inline float select_boundary_transport_scale(float radiance_scale, float importance_scale, BoundaryTransportMode mode) {
    return mode == BoundaryTransportMode::Radiance ? radiance_scale : importance_scale;
}

struct ConductorMaterialSemantics {
    bool measured_conductor;
    bool spectral_eta;
};

__device__ inline ConductorMaterialSemantics eval_conductor_material_semantics(
    const SpectralPacket& metal_eta,
    const SpectralPacket& extinction,
    int num_spec
) {
    float eta_len_sq = 0.0f;
    float k_len_sq = 0.0f;
    for (int c = 0; c < num_spec; ++c) {
        eta_len_sq += metal_eta.values[c] * metal_eta.values[c];
        k_len_sq += extinction.values[c] * extinction.values[c];
    }
    return {k_len_sq > 1e-8f, eta_len_sq > 1e-8f};
}

__device__ inline float conductor_eta_for_channel(
    const ConductorMaterialSemantics& semantics,
    const SpectralPacket& metal_eta,
    float fallback_eta,
    int channel
) {
    return semantics.spectral_eta ? metal_eta.values[channel] : fallback_eta;
}

__device__ inline float conductor_f0_eta_from_albedo(float f0) {
    float clamped_f0 = fminf(0.98f, fmaxf(1e-4f, f0));
    float sqrt_f0 = sqrtf(clamped_f0);
    return (1.0f + sqrt_f0) / fmaxf(1e-4f, 1.0f - sqrt_f0);
}

struct DielectricReflectionAmplitude {
    ComplexF rs;
    ComplexF rp;
    float Rs;
    float Rp;
    bool tir;
};

__device__ inline DielectricReflectionAmplitude eval_dielectric_reflection_amplitude(float eta_i, float eta_t, float cos_i) {
    DielectricReflectionAmplitude a = {};
    cos_i = fminf(1.0f, fmaxf(0.0f, cos_i));
    float sin_i2 = fmaxf(0.0f, 1.0f - cos_i * cos_i);
    float eta = eta_i / eta_t;
    float sin_t2 = eta * eta * sin_i2;
    if (sin_t2 >= 1.0f) {
        float gamma = sqrtf(fmaxf(0.0f, sin_t2 - 1.0f));
        ComplexF eta_t_cos_t = c_make(0.0f, eta_t * gamma);
        ComplexF eta_i_cos_t = c_make(0.0f, eta_i * gamma);
        ComplexF eta_i_cos_i = c_make(eta_i * cos_i, 0.0f);
        ComplexF eta_t_cos_i = c_make(eta_t * cos_i, 0.0f);
        a.rs = c_div(c_sub(eta_i_cos_i, eta_t_cos_t), c_add(eta_i_cos_i, eta_t_cos_t));
        a.rp = c_div(c_sub(eta_t_cos_i, eta_i_cos_t), c_add(eta_t_cos_i, eta_i_cos_t));
        a.Rs = c_abs2(a.rs);
        a.Rp = c_abs2(a.rp);
        a.tir = true;
        return a;
    }

    float cos_t = sqrtf(fmaxf(0.0f, 1.0f - sin_t2));
    float rs = (eta_i * cos_i - eta_t * cos_t) / (eta_i * cos_i + eta_t * cos_t);
    float rp = (eta_t * cos_i - eta_i * cos_t) / (eta_t * cos_i + eta_i * cos_t);
    a.rs = c_make(rs, 0.0f);
    a.rp = c_make(rp, 0.0f);
    a.Rs = rs * rs;
    a.Rp = rp * rp;
    return a;
}

__device__ inline DielectricBoundary eval_dielectric_boundary(float eta_i, float eta_t, float cos_i) {
    DielectricBoundary b = {};
    cos_i = fminf(1.0f, fmaxf(0.0f, cos_i));
    float sin_i2 = fmaxf(0.0f, 1.0f - cos_i * cos_i);
    float eta = eta_i / eta_t;
    float sin_t2 = eta * eta * sin_i2;
    if (sin_t2 >= 1.0f) {
        b.rs = 1.0f;
        b.rp = 1.0f;
        b.Rs = 1.0f;
        b.Rp = 1.0f;
        b.tir = true;
        return b;
    }

    b.cos_t = sqrtf(fmaxf(0.0f, 1.0f - sin_t2));
    float n1c1 = eta_i * cos_i;
    float n2c2 = eta_t * b.cos_t;
    float n2c1 = eta_t * cos_i;
    float n1c2 = eta_i * b.cos_t;

    b.rs = (n1c1 - n2c2) / (n1c1 + n2c2);
    b.rp = (n2c1 - n1c2) / (n2c1 + n1c2);
    b.ts = (2.0f * n1c1) / (n1c1 + n2c2);
    b.tp = (2.0f * n1c1) / (n2c1 + n1c2);
    b.Rs = b.rs * b.rs;
    b.Rp = b.rp * b.rp;
    b.eta_jacobian = (eta_t * b.cos_t) / fmaxf(1e-6f, eta_i * cos_i);
    b.Ts = b.ts * b.ts * b.eta_jacobian;
    b.Tp = b.tp * b.tp * b.eta_jacobian;
    return b;
}

struct ConductorBoundary {
    ComplexF rs;
    ComplexF rp;
    float Rs;
    float Rp;
};

__device__ inline ConductorBoundary eval_conductor_boundary(float n, float k, float cos_i) {
    ConductorBoundary b = {};
    cos_i = fminf(1.0f, fmaxf(0.0f, cos_i));
    float sin_theta2 = 1.0f - cos_i * cos_i;
    float n2_minus_k2 = n * n - k * k;
    float two_nk = 2.0f * n * k;

    float re_inner = n2_minus_k2 - sin_theta2;
    float im_inner = two_nk;
    float r = sqrtf(re_inner * re_inner + im_inner * im_inner);
    float a = sqrtf(fmaxf(0.0f, 0.5f * (r + re_inner)));
    float b_im = sqrtf(fmaxf(0.0f, 0.5f * (r - re_inner)));

    b.rs = c_div(c_make(cos_i - a, -b_im), c_make(cos_i + a, b_im));

    ComplexF n2_cos = c_make(n2_minus_k2 * cos_i, two_nk * cos_i);
    b.rp = c_div(c_add(n2_cos, c_make(-a, -b_im)), c_add(n2_cos, c_make(a, b_im)));
    b.Rs = c_abs2(b.rs);
    b.Rp = c_abs2(b.rp);
    return b;
}

__device__ inline float eval_conductor_unpolarized_reflectance(float n, float k, float cos_i) {
    ConductorBoundary b = eval_conductor_boundary(n, k, cos_i);
    return 0.5f * (b.Rs + b.Rp);
}

struct ThinFilmBoundary {
    ComplexF rs;
    ComplexF rp;
    ComplexF ts;
    ComplexF tp;
    float Rs;
    float Rp;
    float Ts;
    float Tp;
    float eta_jacobian;
    bool tir;
};

struct DielectricSurfaceBoundary {
    ComplexF rs;
    ComplexF rp;
    ComplexF ts;
    ComplexF tp;
    float Rs;
    float Rp;
    float Ts;
    float Tp;
    float cos_t;
    float eta_jacobian;
    float radiance_scale;
    float importance_scale;
    bool tir;
    bool has_thin_film;
};

__device__ inline float eval_unpolarized_reflection_probability(const DielectricSurfaceBoundary& surface) {
    return 0.5f * (surface.Rs + surface.Rp);
}

__device__ inline float eval_unpolarized_transmission_probability(const DielectricSurfaceBoundary& surface) {
    return surface.tir ? 0.0f : 0.5f * (surface.Ts + surface.Tp);
}

__device__ inline float eval_unpolarized_transmission_transport_weight(
    const DielectricSurfaceBoundary& surface,
    BoundaryTransportMode mode
) {
    return eval_unpolarized_transmission_probability(surface) *
        select_boundary_transport_scale(surface.radiance_scale, surface.importance_scale, mode);
}

__device__ inline bool is_rough_dielectric_bsdf(const GpuMaterial& mat) {
    return mat.type == MaterialType::Dielectric &&
           mat.roughness > 1e-4f;
}

__device__ inline float dispersed_dielectric_ior(float base_ior, float dispersion, float wavelength, float clamp) {
    if (dispersion <= 0.0f) return base_ior;
    float inv_lambda2 = 1.0f / (wavelength * wavelength);
    float inv_ref2 = 1.0f / (550.0f * 550.0f);
    float offset = (inv_lambda2 - inv_ref2) * 4e5f;
    offset = fminf(clamp, fmaxf(-clamp, offset));
    return fmaxf(1.01f, base_ior + dispersion * offset);
}

struct RoughDielectricLobe {
    GpuVec3 m;
    float eta_i;
    float eta_t;
    float NdotV;
    float NdotL;
    float VdotM;
    float LdotM;
    float NdotM;
    float D;
    float G;
    float G1_V;
    float jacobian;
    bool valid;
};

__device__ inline RoughDielectricLobe eval_rough_dielectric_reflection_lobe(
    const GpuMaterial& mat,
    const GpuVec3& n,
    const GpuVec3& wo,
    const GpuVec3& wi,
    float wavelength = 550.0f,
    float dispersion_clamp = 20.0f
) {
    RoughDielectricLobe l = {};
    GpuVec3 N = n;
    GpuVec3 V = wo.normalize();
    GpuVec3 L = wi.normalize();
    if (V.dot(N) < 0.0f) N = -N;
    l.NdotV = N.dot(V);
    l.NdotL = N.dot(L);
    if (l.NdotV <= 1e-6f || l.NdotL <= 1e-6f) return l;

    l.m = (V + L).normalize();
    l.NdotM = N.dot(l.m);
    l.VdotM = V.dot(l.m);
    l.LdotM = L.dot(l.m);
    if (l.NdotM <= 1e-6f || l.VdotM <= 1e-6f || l.LdotM <= 1e-6f) return l;

    float alpha = ggx_alpha_from_roughness(mat.roughness);
    l.D = ggx_D(l.NdotM, alpha);
    l.G1_V = smith_G1_ggx(l.NdotV, alpha);
    l.G = smith_G_ggx(l.NdotV, l.NdotL, alpha);
    float material_ior = dispersed_dielectric_ior(mat.ior, mat.dispersion, wavelength, dispersion_clamp);
    l.eta_i = V.dot(n) >= 0.0f ? 1.0f : material_ior;
    l.eta_t = V.dot(n) >= 0.0f ? material_ior : 1.0f;
    l.jacobian = 1.0f / (4.0f * l.VdotM);
    l.valid = true;
    return l;
}

__device__ inline RoughDielectricLobe eval_rough_dielectric_transmission_lobe(
    const GpuMaterial& mat,
    const GpuVec3& n,
    const GpuVec3& wo,
    const GpuVec3& wi,
    float wavelength = 550.0f,
    float dispersion_clamp = 20.0f
) {
    RoughDielectricLobe l = {};
    GpuVec3 N = n;
    GpuVec3 V = wo.normalize();
    GpuVec3 L = wi.normalize();
    bool outside_to_inside = V.dot(N) > 0.0f;
    if (!outside_to_inside) N = -N;
    l.NdotV = N.dot(V);
    l.NdotL = N.dot(L);
    if (l.NdotV <= 1e-6f || l.NdotL >= -1e-6f) return l;

    float material_ior = dispersed_dielectric_ior(mat.ior, mat.dispersion, wavelength, dispersion_clamp);
    l.eta_i = outside_to_inside ? 1.0f : material_ior;
    l.eta_t = outside_to_inside ? material_ior : 1.0f;
    l.m = (l.eta_i * V + l.eta_t * L).normalize();
    if (l.m.dot(N) < 0.0f) l.m = -l.m;

    l.NdotM = N.dot(l.m);
    l.VdotM = V.dot(l.m);
    l.LdotM = L.dot(l.m);
    if (l.NdotM <= 1e-6f || l.VdotM <= 1e-6f || l.LdotM >= -1e-6f) return l;

    float denom = l.eta_i * l.VdotM + l.eta_t * l.LdotM;
    if (fabsf(denom) <= 1e-6f) return l;

    float alpha = ggx_alpha_from_roughness(mat.roughness);
    l.D = ggx_D(l.NdotM, alpha);
    l.G1_V = smith_G1_ggx(l.NdotV, alpha);
    l.G = smith_G_ggx(l.NdotV, fabsf(l.NdotL), alpha);
    l.jacobian = (l.eta_t * l.eta_t * fabsf(l.LdotM)) / fmaxf(1e-12f, denom * denom);
    l.valid = true;
    return l;
}

__device__ inline float rough_dielectric_visible_microfacet_pdf(const RoughDielectricLobe& l) {
    if (!l.valid) return 0.0f;
    return l.D * l.G1_V * fabsf(l.VdotM) / fmaxf(1e-6f, l.NdotV);
}

__device__ inline float rough_dielectric_reflection_pdf(const RoughDielectricLobe& l, float fresnel) {
    return fresnel * rough_dielectric_visible_microfacet_pdf(l) * l.jacobian;
}

__device__ inline float rough_dielectric_transmission_pdf(const RoughDielectricLobe& l, float fresnel) {
    return (1.0f - fresnel) * rough_dielectric_visible_microfacet_pdf(l) * l.jacobian;
}

__device__ inline ThinFilmBoundary eval_thin_film_boundary(
    float wavelength,
    float thickness,
    float eta_i,
    float eta_f,
    float eta_s,
    float cos_i
) {
    ThinFilmBoundary out = {};
    DielectricBoundary i_f = eval_dielectric_boundary(eta_i, eta_f, cos_i);
    if (i_f.tir) {
        DielectricReflectionAmplitude i_f_amp = eval_dielectric_reflection_amplitude(eta_i, eta_f, cos_i);
        out.rs = i_f_amp.rs;
        out.rp = i_f_amp.rp;
        out.Rs = i_f_amp.Rs;
        out.Rp = i_f_amp.Rp;
        out.tir = true;
        return out;
    }
    DielectricBoundary f_s = eval_dielectric_boundary(eta_f, eta_s, i_f.cos_t);
    DielectricReflectionAmplitude f_s_amp = eval_dielectric_reflection_amplitude(eta_f, eta_s, i_f.cos_t);
    if (f_s.tir) {
        f_s.rs = 0.0f;
        f_s.rp = 0.0f;
    }

    float phase = (4.0f * 3.14159265f * eta_f * thickness * i_f.cos_t) / wavelength;
    ComplexF e = c_exp_i(phase);
    ComplexF one = c_make(1.0f, 0.0f);
    ComplexF r01s = c_make(i_f.rs, 0.0f);
    ComplexF r01p = c_make(i_f.rp, 0.0f);
    ComplexF r12s = f_s_amp.rs;
    ComplexF r12p = f_s_amp.rp;
    ComplexF t01s = c_make(i_f.ts, 0.0f);
    ComplexF t01p = c_make(i_f.tp, 0.0f);
    ComplexF t12s = c_make(f_s.ts, 0.0f);
    ComplexF t12p = c_make(f_s.tp, 0.0f);

    ComplexF r12s_e = c_mul(r12s, e);
    ComplexF r12p_e = c_mul(r12p, e);
    ComplexF denom_s = c_add(one, c_mul(r01s, r12s_e));
    ComplexF denom_p = c_add(one, c_mul(r01p, r12p_e));
    out.rs = c_div(c_add(r01s, r12s_e), denom_s);
    out.rp = c_div(c_add(r01p, r12p_e), denom_p);
    out.ts = c_div(c_mul(t01s, t12s), denom_s);
    out.tp = c_div(c_mul(t01p, t12p), denom_p);
    out.Rs = c_abs2(out.rs);
    out.Rp = c_abs2(out.rp);
    out.eta_jacobian = f_s.tir ? 0.0f : (eta_s * f_s.cos_t) / fmaxf(1e-6f, eta_i * cos_i);
    out.Ts = f_s.tir ? 0.0f : c_abs2(out.ts) * out.eta_jacobian;
    out.Tp = f_s.tir ? 0.0f : c_abs2(out.tp) * out.eta_jacobian;
    return out;
}

__device__ inline DielectricSurfaceBoundary eval_dielectric_surface_boundary(
    float wavelength,
    float thickness,
    float eta_i,
    float eta_f,
    float eta_t,
    float cos_i
) {
    DielectricSurfaceBoundary out = {};
    out.radiance_scale = eval_boundary_transport_scale(eta_i, eta_t, BoundaryTransportMode::Radiance);
    out.importance_scale = eval_boundary_transport_scale(eta_i, eta_t, BoundaryTransportMode::Importance);

    DielectricBoundary bare = eval_dielectric_boundary(eta_i, eta_t, cos_i);
    out.cos_t = bare.cos_t;

    if (bare.tir) {
        DielectricReflectionAmplitude amp = eval_dielectric_reflection_amplitude(eta_i, eta_t, cos_i);
        out.rs = amp.rs;
        out.rp = amp.rp;
        out.Rs = amp.Rs;
        out.Rp = amp.Rp;
        out.tir = true;
        return out;
    }

    if (thickness > 0.0f) {
        ThinFilmBoundary film = eval_thin_film_boundary(wavelength, thickness, eta_i, eta_f, eta_t, cos_i);
        out.rs = film.rs;
        out.rp = film.rp;
        out.ts = film.ts;
        out.tp = film.tp;
        out.Rs = film.Rs;
        out.Rp = film.Rp;
        out.Ts = film.Ts;
        out.Tp = film.Tp;
        out.eta_jacobian = film.eta_jacobian;
        out.tir = film.tir;
        out.has_thin_film = true;
        return out;
    }

    out.rs = c_make(bare.rs, 0.0f);
    out.rp = c_make(bare.rp, 0.0f);
    out.ts = c_make(bare.ts, 0.0f);
    out.tp = c_make(bare.tp, 0.0f);
    out.Rs = bare.Rs;
    out.Rp = bare.Rp;
    out.Ts = bare.Ts;
    out.Tp = bare.Tp;
    out.eta_jacobian = bare.eta_jacobian;
    return out;
}

__device__ inline float eval_dielectric_surface_unpolarized_reflectance(
    float wavelength,
    float eta_i,
    float eta_t,
    float cos_i
) {
    DielectricSurfaceBoundary surface = eval_dielectric_surface_boundary(
        wavelength,
        0.0f,
        eta_i,
        1.0f,
        eta_t,
        cos_i);
    return surface.tir ? 1.0f : eval_unpolarized_reflection_probability(surface);
}

__device__ inline ThinFilmBoundary eval_thin_film_conductor_boundary(
    float wavelength,
    float thickness,
    float eta_i,
    float eta_f,
    float n_s,
    float k_s,
    float cos_i
) {
    ThinFilmBoundary out = {};
    DielectricBoundary i_f = eval_dielectric_boundary(eta_i, eta_f, cos_i);
    if (i_f.tir) {
        out.rs = c_make(1.0f, 0.0f);
        out.rp = c_make(1.0f, 0.0f);
        out.Rs = 1.0f;
        out.Rp = 1.0f;
        out.tir = true;
        return out;
    }

    float sin_f2 = fmaxf(0.0f, 1.0f - i_f.cos_t * i_f.cos_t);
    ComplexF eta_s = c_make(n_s, k_s);
    ComplexF eta_f_over_s = c_div(c_make(eta_f, 0.0f), eta_s);
    ComplexF sin_s2 = c_scale(c_mul(eta_f_over_s, eta_f_over_s), sin_f2);
    ComplexF cos_s = c_sqrt(c_sub(c_make(1.0f, 0.0f), sin_s2));

    ComplexF eta_s_cos_s = c_mul(eta_s, cos_s);
    ComplexF eta_s_cos_f = c_scale(eta_s, i_f.cos_t);
    ComplexF eta_f_cos_s = c_scale(cos_s, eta_f);
    ComplexF eta_f_cos_f = c_make(eta_f * i_f.cos_t, 0.0f);

    ComplexF r12s = c_div(c_sub(eta_f_cos_f, eta_s_cos_s), c_add(eta_f_cos_f, eta_s_cos_s));
    ComplexF r12p = c_div(c_sub(eta_s_cos_f, eta_f_cos_s), c_add(eta_s_cos_f, eta_f_cos_s));

    float phase = (4.0f * 3.14159265f * eta_f * thickness * i_f.cos_t) / wavelength;
    ComplexF e = c_exp_i(phase);
    ComplexF one = c_make(1.0f, 0.0f);
    ComplexF r01s = c_make(i_f.rs, 0.0f);
    ComplexF r01p = c_make(i_f.rp, 0.0f);
    ComplexF r12s_e = c_mul(r12s, e);
    ComplexF r12p_e = c_mul(r12p, e);

    out.rs = c_div(c_add(r01s, r12s_e), c_add(one, c_mul(r01s, r12s_e)));
    out.rp = c_div(c_add(r01p, r12p_e), c_add(one, c_mul(r01p, r12p_e)));
    out.Rs = c_abs2(out.rs);
    out.Rp = c_abs2(out.rp);
    return out;
}

__device__ inline float eval_metal_reflectance_for_channel(
    const ConductorMaterialSemantics& conductor,
    float albedo_value,
    float eta_value,
    float extinction_value,
    float fallback_ior,
    float wavelength,
    float effective_thickness,
    float thin_film_ior,
    float cos_theta
) {
    if (!conductor.measured_conductor) {
        if (effective_thickness > 0.0f) {
            float eta_equiv = conductor_f0_eta_from_albedo(albedo_value);
            ThinFilmBoundary film = eval_thin_film_boundary(
                wavelength, effective_thickness, 1.0f, thin_film_ior, eta_equiv, cos_theta);
            return 0.5f * (film.Rs + film.Rp);
        }
        float one_minus = powf(1.0f - cos_theta, 5.0f);
        return albedo_value + (1.0f - albedo_value) * one_minus;
    }

    float eta_c = conductor.spectral_eta ? eta_value : fallback_ior;
    if (effective_thickness > 0.0f) {
        ThinFilmBoundary film = eval_thin_film_conductor_boundary(
            wavelength, effective_thickness, 1.0f, thin_film_ior, eta_c, extinction_value, cos_theta);
        return 0.5f * (film.Rs + film.Rp);
    }
    return eval_conductor_unpolarized_reflectance(eta_c, extinction_value, cos_theta);
}
