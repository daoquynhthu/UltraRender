#pragma once

struct DiffractiveJonesMatrix {
    ComplexF ss = {};
    ComplexF sp = {};
    ComplexF ps = {};
    ComplexF pp = {};
};

static __device__ inline ComplexF diffraction_complex_add(
    ComplexF a,
    ComplexF b) {
    return c_make(a.re + b.re, a.im + b.im);
}

static __device__ inline ComplexF diffraction_complex_scale(
    ComplexF value,
    float scale) {
    return c_make(value.re * scale, value.im * scale);
}

static __device__ inline ComplexF diffraction_complex_conjugate(
    ComplexF value) {
    return c_make(value.re, -value.im);
}

static __device__ inline float diffraction_complex_real(
    ComplexF value) {
    return value.re;
}

static __device__ inline StokesVector apply_diffractive_jones(
    const StokesVector& input,
    const DiffractiveJonesMatrix& jones) {
    const float c00 =
        0.5f * (input.I + input.Q);
    const float c11 =
        0.5f * (input.I - input.Q);
    const ComplexF c01 =
        c_make(0.5f * input.U, 0.5f * input.V);
    const ComplexF c10 =
        diffraction_complex_conjugate(c01);
    const ComplexF row0_c0 =
        diffraction_complex_add(
            diffraction_complex_scale(
                jones.ss,
                c00),
            c_mul(jones.sp, c10));
    const ComplexF row0_c1 =
        diffraction_complex_add(
            c_mul(jones.ss, c01),
            diffraction_complex_scale(
                jones.sp,
                c11));
    const ComplexF row1_c0 =
        diffraction_complex_add(
            diffraction_complex_scale(
                jones.ps,
                c00),
            c_mul(jones.pp, c10));
    const ComplexF row1_c1 =
        diffraction_complex_add(
            c_mul(jones.ps, c01),
            diffraction_complex_scale(
                jones.pp,
                c11));
    const float out00 =
        diffraction_complex_real(
            diffraction_complex_add(
                c_mul(
                    row0_c0,
                    diffraction_complex_conjugate(
                        jones.ss)),
                c_mul(
                    row0_c1,
                    diffraction_complex_conjugate(
                        jones.sp))));
    const float out11 =
        diffraction_complex_real(
            diffraction_complex_add(
                c_mul(
                    row1_c0,
                    diffraction_complex_conjugate(
                        jones.ps)),
                c_mul(
                    row1_c1,
                    diffraction_complex_conjugate(
                        jones.pp))));
    const ComplexF out01 =
        diffraction_complex_add(
            c_mul(
                row0_c0,
                diffraction_complex_conjugate(
                    jones.ps)),
            c_mul(
                row0_c1,
                diffraction_complex_conjugate(
                    jones.pp)));
    StokesVector output;
    output.I = fmaxf(0.0f, out00 + out11);
    output.Q = out00 - out11;
    output.U = 2.0f * out01.re;
    output.V = 2.0f * out01.im;
    return output;
}
