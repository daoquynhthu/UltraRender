#include <cuda_runtime.h>
#include <stdio.h>
#include <math.h>

#include "test_framework.cuh"

#include "../../libs/ure_core/src/path_tracer_kernel.cu"

using namespace ure::gpu;

__global__ void test_rotate_stokes_identity_kernel(StokesVector* out) {
    StokesVector s(1.0f, 0.3f, 0.5f, 0.1f);
    rotate_stokes(s, 0.0f);
    out[0] = s;
}

__global__ void test_rotate_stokes_half_kernel(StokesVector* out) {
    StokesVector s(1.0f, 0.3f, 0.5f, 0.1f);
    rotate_stokes(s, 3.14159265f);
    out[0] = s;
}

__global__ void test_mueller_reflection_normal_kernel(StokesVector* out) {
    StokesVector s(1.0f, 0.0f, 0.0f, 0.0f);
    float rs = 0.3f, rp = 0.2f;
    apply_mueller_reflection_dielectric(s, rs, rp);
    out[0] = s;
}

__global__ void test_mueller_transmission_normal_kernel(StokesVector* out) {
    StokesVector s(1.0f, 0.0f, 0.0f, 0.0f);
    float ts = 0.8f, tp = 0.9f;
    apply_mueller_transmission_dielectric(s, ts, tp, 1.5f);
    out[0] = s;
}

__global__ void test_mueller_conductor_boundary_kernel(float* out) {
    StokesVector s(1.0f, 0.0f, 0.0f, 0.0f);
    float n = 0.2f;
    float k = 3.0f;
    float cos_theta = 0.45f;
    ConductorBoundary b = eval_conductor_boundary(n, k, cos_theta);
    apply_mueller_reflection_conductor(s, n, k, cos_theta);
    out[0] = s.I;
    out[1] = s.Q;
    out[2] = 0.5f * (b.Rs + b.Rp);
    out[3] = 0.5f * (b.Rs - b.Rp);
}

__global__ void test_mueller_thin_film_boundary_kernel(float* out) {
    StokesVector s(1.0f, 0.0f, 0.0f, 0.0f);
    ThinFilmBoundary film = eval_thin_film_boundary(510.0f, 120.0f, 1.0f, 1.38f, 1.62f, 0.52f);
    apply_mueller_reflection_boundary(s, film.rs, film.rp, film.Rs, film.Rp);
    out[0] = s.I;
    out[1] = s.Q;
    out[2] = 0.5f * (film.Rs + film.Rp);
    out[3] = 0.5f * (film.Rs - film.Rp);
}

__global__ void test_mueller_thin_film_transmission_kernel(float* out) {
    StokesVector s(1.0f, 0.0f, 0.0f, 0.0f);
    ThinFilmBoundary film = eval_thin_film_boundary(510.0f, 120.0f, 1.0f, 1.38f, 1.62f, 0.52f);
    apply_mueller_transmission_boundary(s, film.ts, film.tp, film.Ts, film.Tp, film.eta_jacobian);
    out[0] = s.I;
    out[1] = s.Q;
    out[2] = 0.5f * (film.Ts + film.Tp);
    out[3] = 0.5f * (film.Ts - film.Tp);
}

__global__ void test_stokes_sp_convention_kernel(float* out) {
    StokesVector s_pol(1.0f, 1.0f, 0.0f, 0.0f);
    StokesVector p_pol(1.0f, -1.0f, 0.0f, 0.0f);
    out[0] = stokes_s_intensity(s_pol);
    out[1] = stokes_p_intensity(s_pol);
    out[2] = stokes_s_intensity(p_pol);
    out[3] = stokes_p_intensity(p_pol);

    float theta_b = atanf(1.5f);
    DielectricSurfaceBoundary surface = eval_dielectric_surface_boundary(550.0f, 0.0f, 1.0f, 1.25f, 1.5f, cosf(theta_b));
    out[4] = (surface.Rs * stokes_s_intensity(s_pol) + surface.Rp * stokes_p_intensity(s_pol)) / s_pol.I;
    out[5] = (surface.Rs * stokes_s_intensity(p_pol) + surface.Rp * stokes_p_intensity(p_pol)) / p_pol.I;
}

__global__ void test_get_reference_frame_kernel(GpuVec3* out) {
    out[0] = get_reference_frame(GpuVec3(0.0f, 0.0f, -1.0f));
    out[1] = get_reference_frame(GpuVec3(1.0f, 0.0f, 0.0f));
    out[2] = get_reference_frame(GpuVec3(0.0f, 1.0f, 0.0f));
}

__global__ void test_boundary_dielectric_kernel(float* out) {
    DielectricBoundary normal = eval_dielectric_boundary(1.0f, 1.5f, 1.0f);
    out[0] = normal.Rs;
    out[1] = normal.Rp;
    out[2] = normal.Ts;
    out[3] = normal.Tp;

    float theta_b = atanf(1.5f);
    DielectricBoundary brewster = eval_dielectric_boundary(1.0f, 1.5f, cosf(theta_b));
    out[4] = brewster.Rp;

    float critical_cos = cosf(asinf(1.0f / 1.5f) + 0.05f);
    DielectricBoundary tir = eval_dielectric_boundary(1.5f, 1.0f, critical_cos);
    out[5] = tir.tir ? 1.0f : 0.0f;
}

__global__ void test_boundary_transport_scale_kernel(float* out) {
    float air_to_glass = eval_boundary_transport_scale(1.0f, 1.5f, BoundaryTransportMode::Radiance);
    float glass_to_air = eval_boundary_transport_scale(1.5f, 1.0f, BoundaryTransportMode::Radiance);
    float importance_air_to_glass = eval_boundary_transport_scale(1.0f, 1.5f, BoundaryTransportMode::Importance);
    out[0] = air_to_glass;
    out[1] = glass_to_air;
    out[2] = air_to_glass * glass_to_air;
    out[3] = air_to_glass * importance_air_to_glass;
}

__global__ void test_boundary_transport_weight_kernel(float* out) {
    DielectricSurfaceBoundary forward = eval_dielectric_surface_boundary(550.0f, 0.0f, 1.0f, 1.25f, 1.5f, 0.8f);
    DielectricSurfaceBoundary reverse = eval_dielectric_surface_boundary(550.0f, 0.0f, 1.5f, 1.25f, 1.0f, forward.cos_t);
    float forward_power_t = eval_unpolarized_transmission_probability(forward);
    float reverse_power_t = eval_unpolarized_transmission_probability(reverse);
    float forward_radiance = eval_unpolarized_transmission_transport_weight(forward, BoundaryTransportMode::Radiance);
    float reverse_importance = eval_unpolarized_transmission_transport_weight(reverse, BoundaryTransportMode::Importance);
    float forward_importance = eval_unpolarized_transmission_transport_weight(forward, BoundaryTransportMode::Importance);
    float reverse_radiance = eval_unpolarized_transmission_transport_weight(reverse, BoundaryTransportMode::Radiance);

    out[0] = forward_power_t;
    out[1] = reverse_power_t;
    out[2] = forward.radiance_scale * forward.importance_scale;
    out[3] = forward_radiance / fmaxf(1e-6f, forward_power_t);
    out[4] = forward.radiance_scale;
    out[5] = forward_importance / fmaxf(1e-6f, forward_power_t);
    out[6] = reverse_radiance / fmaxf(1e-6f, reverse_power_t);
    out[7] = reverse_importance / fmaxf(1e-6f, reverse_power_t);
}

__global__ void test_dielectric_surface_power_conservation_kernel(float* out) {
    DielectricSurfaceBoundary normal = eval_dielectric_surface_boundary(550.0f, 0.0f, 1.0f, 1.25f, 1.5f, 1.0f);
    DielectricSurfaceBoundary oblique = eval_dielectric_surface_boundary(610.0f, 0.0f, 1.0f, 1.25f, 1.52f, 0.37f);
    DielectricSurfaceBoundary reverse = eval_dielectric_surface_boundary(610.0f, 0.0f, 1.52f, 1.25f, 1.0f, oblique.cos_t);
    DielectricSurfaceBoundary film_normal = eval_dielectric_surface_boundary(550.0f, 550.0f / (4.0f * 1.38f), 1.0f, 1.38f, 1.5f, 1.0f);
    DielectricSurfaceBoundary film_oblique = eval_dielectric_surface_boundary(460.0f, 180.0f, 1.0f, 1.42f, 1.62f, 0.43f);
    DielectricSurfaceBoundary tir = eval_dielectric_surface_boundary(550.0f, 0.0f, 1.5f, 1.25f, 1.0f, 0.5f);

    out[0] = eval_unpolarized_reflection_probability(normal) + eval_unpolarized_transmission_probability(normal);
    out[1] = eval_unpolarized_reflection_probability(oblique) + eval_unpolarized_transmission_probability(oblique);
    out[2] = eval_unpolarized_reflection_probability(reverse) + eval_unpolarized_transmission_probability(reverse);
    out[3] = eval_unpolarized_reflection_probability(film_normal) + eval_unpolarized_transmission_probability(film_normal);
    out[4] = eval_unpolarized_reflection_probability(film_oblique) + eval_unpolarized_transmission_probability(film_oblique);
    out[5] = eval_unpolarized_reflection_probability(tir) + eval_unpolarized_transmission_probability(tir);
    out[6] = tir.tir ? 1.0f : 0.0f;
    out[7] = oblique.radiance_scale * oblique.importance_scale;
    out[8] = reverse.radiance_scale * reverse.importance_scale;
}

__global__ void test_thin_film_sp_energy_grid_kernel(float* out) {
    const float wavelengths[3] = {430.0f, 550.0f, 700.0f};
    const float thicknesses[3] = {0.0f, 110.0f, 320.0f};
    const float cosines[3] = {1.0f, 0.73f, 0.41f};
    float max_s_error = 0.0f;
    float max_p_error = 0.0f;
    float min_power = 1.0f;
    float max_power = 0.0f;

    for (int iw = 0; iw < 3; ++iw) {
        for (int it = 0; it < 3; ++it) {
            for (int ia = 0; ia < 3; ++ia) {
                ThinFilmBoundary film = eval_thin_film_boundary(
                    wavelengths[iw], thicknesses[it], 1.0f, 1.38f, 1.62f, cosines[ia]);
                float s_error = fabsf(film.Rs + film.Ts - 1.0f);
                float p_error = fabsf(film.Rp + film.Tp - 1.0f);
                if (s_error > max_s_error) {
                    max_s_error = s_error;
                }
                if (p_error > max_p_error) {
                    max_p_error = p_error;
                }
                float local_min = fminf(fminf(film.Rs, film.Rp), fminf(film.Ts, film.Tp));
                float local_max = fmaxf(fmaxf(film.Rs, film.Rp), fmaxf(film.Ts, film.Tp));
                if (local_min < min_power) {
                    min_power = local_min;
                }
                if (local_max > max_power) {
                    max_power = local_max;
                }
            }
        }
    }

    out[0] = max_s_error;
    out[1] = max_p_error;
    out[2] = min_power;
    out[3] = max_power;
}

__global__ void test_dielectric_surface_boundary_kernel(float* out) {
    DielectricBoundary bare = eval_dielectric_boundary(1.0f, 1.5f, 0.73f);
    DielectricSurfaceBoundary surface_bare = eval_dielectric_surface_boundary(550.0f, 0.0f, 1.0f, 1.25f, 1.5f, 0.73f);
    out[0] = fabsf(surface_bare.Rs - bare.Rs);
    out[1] = fabsf(surface_bare.Tp - bare.Tp);

    ThinFilmBoundary film = eval_thin_film_boundary(510.0f, 120.0f, 1.0f, 1.38f, 1.62f, 0.52f);
    DielectricSurfaceBoundary surface_film = eval_dielectric_surface_boundary(510.0f, 120.0f, 1.0f, 1.38f, 1.62f, 0.52f);
    out[2] = fabsf(surface_film.Rp - film.Rp);
    out[3] = fabsf(surface_film.Ts - film.Ts);

    DielectricSurfaceBoundary surface_tir = eval_dielectric_surface_boundary(550.0f, 0.0f, 1.5f, 1.25f, 1.0f, 0.5f);
    out[4] = surface_tir.tir ? 1.0f : 0.0f;
    out[5] = surface_tir.Rs;
    out[6] = surface_tir.Rp;
    out[7] = fabsf(surface_tir.rs.im) + fabsf(surface_tir.rp.im);
}

__global__ void test_boundary_conductor_kernel(float* out) {
    ConductorBoundary b = eval_conductor_boundary(0.2f, 3.0f, 1.0f);
    out[0] = 0.5f * (b.Rs + b.Rp);
    out[1] = eval_conductor_unpolarized_reflectance(0.2f, 3.0f, 1.0f);
}

__global__ void test_boundary_thin_film_kernel(float* out) {
    ThinFilmBoundary zero = eval_thin_film_boundary(550.0f, 0.0f, 1.0f, sqrtf(1.5f), 1.5f, 1.0f);
    out[0] = 0.5f * (zero.Rs + zero.Rp);

    float eta_f = sqrtf(1.5f);
    float thickness = 550.0f / (4.0f * eta_f);
    ThinFilmBoundary quarter = eval_thin_film_boundary(550.0f, thickness, 1.0f, eta_f, 1.5f, 1.0f);
    out[1] = 0.5f * (quarter.Rs + quarter.Rp);

    ThinFilmBoundary conductor_zero = eval_thin_film_conductor_boundary(550.0f, 0.0f, 1.0f, 1.35f, 0.2f, 3.0f, 1.0f);
    ConductorBoundary conductor_bare = eval_conductor_boundary(0.2f, 3.0f, 1.0f);
    out[2] = 0.5f * (conductor_zero.Rs + conductor_zero.Rp);
    out[3] = 0.5f * (conductor_bare.Rs + conductor_bare.Rp);
    out[4] = 0.5f * (zero.Rs + zero.Rp + zero.Ts + zero.Tp);
    out[5] = 0.5f * (quarter.Ts + quarter.Tp);

    DielectricReflectionAmplitude tir_amp = eval_dielectric_reflection_amplitude(1.5f, 1.0f, 0.5f);
    out[6] = tir_amp.Rs;
    out[7] = tir_amp.Rp;
    out[8] = fabsf(tir_amp.rs.im) + fabsf(tir_amp.rp.im);

    ThinFilmBoundary substrate_tir = eval_thin_film_boundary(550.0f, 130.0f, 1.4f, 1.5f, 1.0f, 0.5f);
    out[9] = 0.5f * (substrate_tir.Rs + substrate_tir.Rp);
}

static int test_rotate_stokes_identity() {
    REQUIRE_GPU();
    StokesVector* d_out;
    CHECK_CUDA(cudaMalloc(&d_out, sizeof(StokesVector)));
    test_rotate_stokes_identity_kernel<<<1, 1>>>(d_out);
    CHECK_CUDA(cudaGetLastError());
    StokesVector h_out;
    CHECK_CUDA(cudaMemcpy(&h_out, d_out, sizeof(StokesVector), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(h_out.I, 1.0f, 1e-6f);
    CHECK_FLOAT_EQ(h_out.Q, 0.3f, 1e-6f);
    CHECK_FLOAT_EQ(h_out.U, 0.5f, 1e-6f);
    CHECK_FLOAT_EQ(h_out.V, 0.1f, 1e-6f);
    cudaFree(d_out);
    return 0;
}

static int test_rotate_stokes_half() {
    REQUIRE_GPU();
    StokesVector* d_out;
    CHECK_CUDA(cudaMalloc(&d_out, sizeof(StokesVector)));
    test_rotate_stokes_half_kernel<<<1, 1>>>(d_out);
    CHECK_CUDA(cudaGetLastError());
    StokesVector h_out;
    CHECK_CUDA(cudaMemcpy(&h_out, d_out, sizeof(StokesVector), cudaMemcpyDeviceToHost));
    // Rotating by 180°: cos(π) = -1, sin(π) = 0 → Q→ -Q, U→ -U
    CHECK_FLOAT_EQ(h_out.I, 1.0f, 1e-5f);
    CHECK_FLOAT_EQ(h_out.Q, -0.3f, 1e-5f);
    CHECK_FLOAT_EQ(h_out.U, -0.5f, 1e-5f);
    CHECK_FLOAT_EQ(h_out.V, 0.1f, 1e-5f);
    cudaFree(d_out);
    return 0;
}

static int test_mueller_reflection_normal() {
    REQUIRE_GPU();
    StokesVector* d_out;
    CHECK_CUDA(cudaMalloc(&d_out, sizeof(StokesVector)));
    test_mueller_reflection_normal_kernel<<<1, 1>>>(d_out);
    CHECK_CUDA(cudaGetLastError());
    StokesVector h_out;
    CHECK_CUDA(cudaMemcpy(&h_out, d_out, sizeof(StokesVector), cudaMemcpyDeviceToHost));
    float A = 0.5f * (0.09f + 0.04f); // Rs=0.09, Rp=0.04
    float B = 0.5f * (0.09f - 0.04f);
    CHECK_FLOAT_EQ(h_out.I, A, 1e-5f);
    CHECK_FLOAT_EQ(h_out.Q, B, 1e-5f);
    CHECK_FLOAT_EQ(h_out.U, 0.0f, 1e-6f);
    CHECK_FLOAT_EQ(h_out.V, 0.0f, 1e-6f);
    cudaFree(d_out);
    return 0;
}

static int test_mueller_transmission_normal() {
    REQUIRE_GPU();
    StokesVector* d_out;
    CHECK_CUDA(cudaMalloc(&d_out, sizeof(StokesVector)));
    test_mueller_transmission_normal_kernel<<<1, 1>>>(d_out);
    CHECK_CUDA(cudaGetLastError());
    StokesVector h_out;
    CHECK_CUDA(cudaMemcpy(&h_out, d_out, sizeof(StokesVector), cudaMemcpyDeviceToHost));
    float eta_rel = 1.5f;
    float Ts = 0.64f * eta_rel;
    float Tp = 0.81f * eta_rel;
    float A = 0.5f * (Ts + Tp);
    float B = 0.5f * (Ts - Tp);
    CHECK_FLOAT_EQ(h_out.I, A, 1e-5f);
    CHECK_FLOAT_EQ(h_out.Q, B, 1e-5f);
    CHECK_FLOAT_EQ(h_out.U, 0.0f, 1e-6f);
    CHECK_FLOAT_EQ(h_out.V, 0.0f, 1e-6f);
    cudaFree(d_out);
    return 0;
}

static int test_mueller_conductor_uses_boundary() {
    REQUIRE_GPU();
    float* d_out;
    CHECK_CUDA(cudaMalloc(&d_out, 4 * sizeof(float)));
    test_mueller_conductor_boundary_kernel<<<1, 1>>>(d_out);
    CHECK_CUDA(cudaGetLastError());
    float h_out[4];
    CHECK_CUDA(cudaMemcpy(h_out, d_out, 4 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(h_out[0], h_out[2], 1e-5f);
    CHECK_FLOAT_EQ(h_out[1], h_out[3], 1e-5f);
    cudaFree(d_out);
    return 0;
}

static int test_mueller_thin_film_uses_boundary() {
    REQUIRE_GPU();
    float* d_out;
    CHECK_CUDA(cudaMalloc(&d_out, 4 * sizeof(float)));
    test_mueller_thin_film_boundary_kernel<<<1, 1>>>(d_out);
    CHECK_CUDA(cudaGetLastError());
    float h_out[4];
    CHECK_CUDA(cudaMemcpy(h_out, d_out, 4 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(h_out[0], h_out[2], 1e-5f);
    CHECK_FLOAT_EQ(h_out[1], h_out[3], 1e-5f);
    cudaFree(d_out);
    return 0;
}

static int test_mueller_thin_film_transmission_uses_boundary() {
    REQUIRE_GPU();
    float* d_out;
    CHECK_CUDA(cudaMalloc(&d_out, 4 * sizeof(float)));
    test_mueller_thin_film_transmission_kernel<<<1, 1>>>(d_out);
    CHECK_CUDA(cudaGetLastError());
    float h_out[4];
    CHECK_CUDA(cudaMemcpy(h_out, d_out, 4 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(h_out[0], h_out[2], 1e-5f);
    CHECK_FLOAT_EQ(h_out[1], h_out[3], 1e-5f);
    cudaFree(d_out);
    return 0;
}

static int test_stokes_sp_convention() {
    REQUIRE_GPU();
    float* d_out;
    CHECK_CUDA(cudaMalloc(&d_out, 6 * sizeof(float)));
    test_stokes_sp_convention_kernel<<<1, 1>>>(d_out);
    CHECK_CUDA(cudaGetLastError());
    float h_out[6];
    CHECK_CUDA(cudaMemcpy(h_out, d_out, 6 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(h_out[0], 1.0f, 1e-6f);
    CHECK_FLOAT_EQ(h_out[1], 0.0f, 1e-6f);
    CHECK_FLOAT_EQ(h_out[2], 0.0f, 1e-6f);
    CHECK_FLOAT_EQ(h_out[3], 1.0f, 1e-6f);
    CHECK(h_out[4] > 0.1f);
    CHECK(h_out[5] < 1e-5f);
    cudaFree(d_out);
    return 0;
}

static int test_get_reference_frame() {
    REQUIRE_GPU();
    GpuVec3* d_out;
    CHECK_CUDA(cudaMalloc(&d_out, 3 * sizeof(GpuVec3)));
    test_get_reference_frame_kernel<<<1, 1>>>(d_out);
    CHECK_CUDA(cudaGetLastError());
    GpuVec3 h_out[3];
    CHECK_CUDA(cudaMemcpy(h_out, d_out, 3 * sizeof(GpuVec3), cudaMemcpyDeviceToHost));
    CHECK(h_out[0].dot(GpuVec3(0,0,-1)) < 0.01f);
    CHECK(fabsf(h_out[0].length_sq() - 1.0f) < 0.01f);
    CHECK(fabsf(h_out[1].length_sq() - 1.0f) < 0.01f);
    CHECK(fabsf(h_out[2].length_sq() - 1.0f) < 0.01f);
    cudaFree(d_out);
    return 0;
}

static int test_boundary_dielectric() {
    REQUIRE_GPU();
    float* d_out;
    CHECK_CUDA(cudaMalloc(&d_out, 6 * sizeof(float)));
    test_boundary_dielectric_kernel<<<1, 1>>>(d_out);
    CHECK_CUDA(cudaGetLastError());
    float h_out[6];
    CHECK_CUDA(cudaMemcpy(h_out, d_out, 6 * sizeof(float), cudaMemcpyDeviceToHost));

    float r = (1.0f - 1.5f) / (1.0f + 1.5f);
    float R = r * r;
    CHECK_FLOAT_EQ(h_out[0], R, 1e-5f);
    CHECK_FLOAT_EQ(h_out[1], R, 1e-5f);
    CHECK_FLOAT_EQ(h_out[2], 1.0f - R, 1e-5f);
    CHECK_FLOAT_EQ(h_out[3], 1.0f - R, 1e-5f);
    CHECK(h_out[4] < 1e-5f);
    CHECK_FLOAT_EQ(h_out[5], 1.0f, 1e-6f);
    cudaFree(d_out);
    return 0;
}

static int test_boundary_transport_scale() {
    REQUIRE_GPU();
    float* d_out;
    CHECK_CUDA(cudaMalloc(&d_out, 4 * sizeof(float)));
    test_boundary_transport_scale_kernel<<<1, 1>>>(d_out);
    CHECK_CUDA(cudaGetLastError());
    float h_out[4];
    CHECK_CUDA(cudaMemcpy(h_out, d_out, 4 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(h_out[0], 1.0f / 2.25f, 1e-6f);
    CHECK_FLOAT_EQ(h_out[1], 2.25f, 1e-6f);
    CHECK_FLOAT_EQ(h_out[2], 1.0f, 1e-6f);
    CHECK_FLOAT_EQ(h_out[3], 1.0f, 1e-6f);
    cudaFree(d_out);
    return 0;
}

static int test_boundary_transport_weight() {
    REQUIRE_GPU();
    float* d_out;
    CHECK_CUDA(cudaMalloc(&d_out, 8 * sizeof(float)));
    test_boundary_transport_weight_kernel<<<1, 1>>>(d_out);
    CHECK_CUDA(cudaGetLastError());
    float h_out[8];
    CHECK_CUDA(cudaMemcpy(h_out, d_out, 8 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK(h_out[0] > 0.0f);
    CHECK_FLOAT_EQ(h_out[0], h_out[1], 1e-5f);
    CHECK_FLOAT_EQ(h_out[2], 1.0f, 1e-5f);
    CHECK_FLOAT_EQ(h_out[3], h_out[4], 1e-5f);
    CHECK(h_out[3] < 1.0f);
    CHECK(h_out[5] > 1.0f);
    CHECK(h_out[6] > 1.0f);
    CHECK(h_out[7] < 1.0f);
    cudaFree(d_out);
    return 0;
}

static int test_dielectric_surface_power_conservation() {
    REQUIRE_GPU();
    float* d_out;
    CHECK_CUDA(cudaMalloc(&d_out, 9 * sizeof(float)));
    test_dielectric_surface_power_conservation_kernel<<<1, 1>>>(d_out);
    CHECK_CUDA(cudaGetLastError());
    float h_out[9];
    CHECK_CUDA(cudaMemcpy(h_out, d_out, 9 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(h_out[0], 1.0f, 2e-5f);
    CHECK_FLOAT_EQ(h_out[1], 1.0f, 2e-5f);
    CHECK_FLOAT_EQ(h_out[2], 1.0f, 2e-5f);
    CHECK_FLOAT_EQ(h_out[3], 1.0f, 2e-5f);
    CHECK_FLOAT_EQ(h_out[4], 1.0f, 2e-5f);
    CHECK_FLOAT_EQ(h_out[5], 1.0f, 2e-5f);
    CHECK_FLOAT_EQ(h_out[6], 1.0f, 1e-6f);
    CHECK_FLOAT_EQ(h_out[7], 1.0f, 1e-5f);
    CHECK_FLOAT_EQ(h_out[8], 1.0f, 1e-5f);
    cudaFree(d_out);
    return 0;
}

static int test_thin_film_sp_energy_grid() {
    REQUIRE_GPU();
    float* d_out;
    CHECK_CUDA(cudaMalloc(&d_out, 4 * sizeof(float)));
    test_thin_film_sp_energy_grid_kernel<<<1, 1>>>(d_out);
    CHECK_CUDA(cudaGetLastError());
    float h_out[4];
    CHECK_CUDA(cudaMemcpy(h_out, d_out, 4 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK(h_out[0] < 2e-4f);
    CHECK(h_out[1] < 2e-4f);
    CHECK(h_out[2] > -1e-5f);
    CHECK(h_out[3] < 1.0001f);
    cudaFree(d_out);
    return 0;
}

static int test_dielectric_surface_boundary() {
    REQUIRE_GPU();
    float* d_out;
    CHECK_CUDA(cudaMalloc(&d_out, 8 * sizeof(float)));
    test_dielectric_surface_boundary_kernel<<<1, 1>>>(d_out);
    CHECK_CUDA(cudaGetLastError());
    float h_out[8];
    CHECK_CUDA(cudaMemcpy(h_out, d_out, 8 * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK(h_out[0] < 1e-6f);
    CHECK(h_out[1] < 1e-6f);
    CHECK(h_out[2] < 1e-6f);
    CHECK(h_out[3] < 1e-6f);
    CHECK_FLOAT_EQ(h_out[4], 1.0f, 1e-6f);
    CHECK_FLOAT_EQ(h_out[5], 1.0f, 1e-5f);
    CHECK_FLOAT_EQ(h_out[6], 1.0f, 1e-5f);
    CHECK(h_out[7] > 0.1f);
    cudaFree(d_out);
    return 0;
}

static int test_boundary_conductor() {
    REQUIRE_GPU();
    float* d_out;
    CHECK_CUDA(cudaMalloc(&d_out, 2 * sizeof(float)));
    test_boundary_conductor_kernel<<<1, 1>>>(d_out);
    CHECK_CUDA(cudaGetLastError());
    float h_out[2];
    CHECK_CUDA(cudaMemcpy(h_out, d_out, 2 * sizeof(float), cudaMemcpyDeviceToHost));

    float n = 0.2f;
    float k = 3.0f;
    float expected = ((n - 1.0f) * (n - 1.0f) + k * k) / ((n + 1.0f) * (n + 1.0f) + k * k);
    CHECK_FLOAT_EQ(h_out[0], expected, 1e-5f);
    CHECK_FLOAT_EQ(h_out[1], expected, 1e-5f);
    cudaFree(d_out);
    return 0;
}

static int test_boundary_thin_film() {
    REQUIRE_GPU();
    float* d_out;
    CHECK_CUDA(cudaMalloc(&d_out, 10 * sizeof(float)));
    test_boundary_thin_film_kernel<<<1, 1>>>(d_out);
    CHECK_CUDA(cudaGetLastError());
    float h_out[10];
    CHECK_CUDA(cudaMemcpy(h_out, d_out, 10 * sizeof(float), cudaMemcpyDeviceToHost));

    float r = (1.0f - 1.5f) / (1.0f + 1.5f);
    CHECK_FLOAT_EQ(h_out[0], r * r, 1e-5f);
    CHECK(h_out[1] < 1e-5f);
    CHECK_FLOAT_EQ(h_out[2], h_out[3], 1e-5f);
    CHECK_FLOAT_EQ(h_out[4], 1.0f, 1e-5f);
    CHECK(h_out[5] > 0.999f);
    CHECK_FLOAT_EQ(h_out[6], 1.0f, 1e-5f);
    CHECK_FLOAT_EQ(h_out[7], 1.0f, 1e-5f);
    CHECK(h_out[8] > 0.1f);
    CHECK(h_out[9] > 0.999f);
    cudaFree(d_out);
    return 0;
}

int main() {
    printf("[GPU Polarization Test]\n");
    RUN_TEST(test_rotate_stokes_identity);
    RUN_TEST(test_rotate_stokes_half);
    RUN_TEST(test_mueller_reflection_normal);
    RUN_TEST(test_mueller_transmission_normal);
    RUN_TEST(test_mueller_conductor_uses_boundary);
    RUN_TEST(test_mueller_thin_film_uses_boundary);
    RUN_TEST(test_mueller_thin_film_transmission_uses_boundary);
    RUN_TEST(test_stokes_sp_convention);
    RUN_TEST(test_get_reference_frame);
    RUN_TEST(test_boundary_dielectric);
    RUN_TEST(test_boundary_transport_scale);
    RUN_TEST(test_boundary_transport_weight);
    RUN_TEST(test_dielectric_surface_power_conservation);
    RUN_TEST(test_thin_film_sp_energy_grid);
    RUN_TEST(test_dielectric_surface_boundary);
    RUN_TEST(test_boundary_conductor);
    RUN_TEST(test_boundary_thin_film);
    printf("  passed: %d, failed: %d\n", g_tests_passed, g_tests_failed);
    return g_test_result;
}
