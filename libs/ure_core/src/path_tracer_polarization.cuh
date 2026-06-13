#pragma once

#include "path_tracer_decl.cuh"
#include "path_tracer_boundary.cuh"

__device__ inline GpuVec3 get_reference_frame(const GpuVec3& dir) {
    if (fabsf(dir.y) > 0.999f) {
        return GpuVec3(1.0f, 0.0f, 0.0f);
    }
    return GpuVec3(0.0f, 1.0f, 0.0f).cross(dir).normalize();
}

__device__ inline void rotate_stokes(StokesVector& s, float two_phi) {
    float c = cosf(two_phi);
    float si = sinf(two_phi);
    float new_Q = s.Q * c + s.U * si;
    float new_U = -s.Q * si + s.U * c;
    s.Q = new_Q;
    s.U = new_U;
}

__device__ inline float stokes_s_intensity(const StokesVector& s) {
    return 0.5f * (s.I + s.Q);
}

__device__ inline float stokes_p_intensity(const StokesVector& s) {
    return 0.5f * (s.I - s.Q);
}

__device__ inline void apply_mueller_reflection_dielectric(StokesVector& s, float rs, float rp, float delta = 0.0f) {
    float Rs = rs * rs;
    float Rp = rp * rp;

    float A = 0.5f * (Rs + Rp);
    float B = 0.5f * (Rs - Rp);
    float C = rs * rp * cosf(delta);
    float D = rs * rp * sinf(delta);

    float new_I = A * s.I + B * s.Q;
    float new_Q = B * s.I + A * s.Q;
    float new_U = C * s.U - D * s.V;
    float new_V = D * s.U + C * s.V;

    s.I = new_I;
    s.Q = new_Q;
    s.U = new_U;
    s.V = new_V;
}

__device__ inline void apply_mueller_reflection_boundary(StokesVector& s, ComplexF rs, ComplexF rp, float Rs, float Rp) {
    float A = 0.5f * (Rs + Rp);
    float B = 0.5f * (Rs - Rp);
    float C = rs.re * rp.re + rs.im * rp.im;
    float D = rs.im * rp.re - rs.re * rp.im;

    float new_I = A * s.I + B * s.Q;
    float new_Q = B * s.I + A * s.Q;
    float new_U = C * s.U - D * s.V;
    float new_V = D * s.U + C * s.V;

    s.I = new_I;
    s.Q = new_Q;
    s.U = new_U;
    s.V = new_V;
}

__device__ inline void apply_mueller_transmission_boundary(
    StokesVector& s,
    ComplexF ts,
    ComplexF tp,
    float Ts,
    float Tp,
    float eta_jacobian
) {
    float A = 0.5f * (Ts + Tp);
    float B = 0.5f * (Ts - Tp);
    float C = (ts.re * tp.re + ts.im * tp.im) * eta_jacobian;
    float D = (ts.im * tp.re - ts.re * tp.im) * eta_jacobian;

    float new_I = A * s.I + B * s.Q;
    float new_Q = B * s.I + A * s.Q;
    float new_U = C * s.U - D * s.V;
    float new_V = D * s.U + C * s.V;

    s.I = new_I;
    s.Q = new_Q;
    s.U = new_U;
    s.V = new_V;
}

__device__ inline void apply_mueller_reflection_conductor(StokesVector& s, float n, float k, float cos_theta) {
    ConductorBoundary b = eval_conductor_boundary(n, k, cos_theta);
    apply_mueller_reflection_boundary(s, b.rs, b.rp, b.Rs, b.Rp);
}

__device__ inline float dispersed_dielectric_ior(float base_ior, float dispersion, float wavelength, float clamp) {
    if (dispersion <= 0.0f) return base_ior;
    float inv_lambda2 = 1.0f / (wavelength * wavelength);
    float inv_ref2 = 1.0f / (550.0f * 550.0f);
    float offset = (inv_lambda2 - inv_ref2) * 4e5f;
    offset = fminf(clamp, fmaxf(-clamp, offset));
    return fmaxf(1.01f, base_ior + dispersion * offset);
}

__device__ inline void apply_mueller_transmission_dielectric(StokesVector& s, float ts, float tp, float eta_rel) {
    float Ts = ts * ts * eta_rel;
    float Tp = tp * tp * eta_rel;

    float A = 0.5f * (Ts + Tp);
    float B = 0.5f * (Ts - Tp);
    float C = ts * tp * eta_rel;

    float new_I = A * s.I + B * s.Q;
    float new_Q = B * s.I + A * s.Q;
    float new_U = C * s.U;
    float new_V = C * s.V;

    s.I = new_I;
    s.Q = new_Q;
    s.U = new_U;
    s.V = new_V;
}

__device__ inline float get_cloth_intensity(const GpuVec3& p) {
    float freq = 20.0f;
    float noise = sinf(p.x * freq) * sinf(p.z * freq);
    return 0.75f + noise * 0.25f;
}
