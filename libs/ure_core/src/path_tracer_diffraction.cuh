static __device__ inline bool diffraction_film_enabled(
    const GpuScene& scene) {
    return scene.diffraction_spectral_accum &&
           scene.diffraction_psf_weights &&
           scene.diffraction_pixel_count > 0 &&
           scene.diffraction_wavelength_count >= 2;
}

static __device__ inline void accumulate_diffraction_lane(
    const GpuScene& scene,
    int pixel_index,
    float wavelength_nm,
    const GpuVec3& xyz) {
    const float span =
        scene.diffraction_wavelength_max_nm -
        scene.diffraction_wavelength_min_nm;
    const float coordinate =
        fminf(1.0f, fmaxf(
            0.0f,
            (wavelength_nm -
             scene.diffraction_wavelength_min_nm) / span)) *
        float(scene.diffraction_wavelength_count - 1);
    const int lower = min(
        scene.diffraction_wavelength_count - 1,
        max(0, int(floorf(coordinate))));
    const int upper = min(
        scene.diffraction_wavelength_count - 1,
        lower + 1);
    const float upper_weight = coordinate - float(lower);
    const float lower_weight = 1.0f - upper_weight;
    GpuVec3* lower_value =
        &scene.diffraction_spectral_accum[
            lower * scene.diffraction_pixel_count +
            pixel_index];
    atomicAdd(&lower_value->x, xyz.x * lower_weight);
    atomicAdd(&lower_value->y, xyz.y * lower_weight);
    atomicAdd(&lower_value->z, xyz.z * lower_weight);
    if (upper != lower && upper_weight > 0.0f) {
        GpuVec3* upper_value =
            &scene.diffraction_spectral_accum[
                upper * scene.diffraction_pixel_count +
                pixel_index];
        atomicAdd(&upper_value->x, xyz.x * upper_weight);
        atomicAdd(&upper_value->y, xyz.y * upper_weight);
        atomicAdd(&upper_value->z, xyz.z * upper_weight);
    }
}

static __device__ inline void accumulate_diffraction_spectrum(
    const GpuScene& scene,
    int pixel_index,
    const SpectralPacket& spectrum,
    int spectral_mode,
    int active_channel,
    float wavelength_pdf,
    float scale = 1.0f) {
    if (!diffraction_film_enabled(scene)) return;
    constexpr float domain_width =
        kSpectralLambdaMax - kSpectralLambdaMin;
    if (spectral_mode_is_sampled(spectral_mode)) {
        if (active_channel < 0 ||
            active_channel >= scene.num_spectral_channels) {
            return;
        }
        const float safe_pdf =
            fmaxf(1.0e-12f, wavelength_pdf);
        const float normalization =
            spectral_mode == SpectralRayModeLane
                ? (domain_width /
                   float(scene.num_spectral_channels) /
                   safe_pdf) /
                      cie_y_integral()
                : (1.0f / safe_pdf) /
                      cie_y_integral();
        accumulate_diffraction_lane(
            scene,
            pixel_index,
            spectrum.wavelengths[active_channel],
            GpuVec3(
                cie_x(spectrum.wavelengths[active_channel]),
                cie_y(spectrum.wavelengths[active_channel]),
                cie_z(spectrum.wavelengths[active_channel])) *
                (spectrum.values[active_channel] *
                 normalization * scale));
        return;
    }
    const float normalization =
        (domain_width /
         float(scene.num_spectral_channels)) /
        cie_y_integral();
    for (int channel = 0;
         channel < scene.num_spectral_channels;
         ++channel) {
        accumulate_diffraction_lane(
            scene,
            pixel_index,
            spectrum.wavelengths[channel],
            GpuVec3(
                cie_x(spectrum.wavelengths[channel]),
                cie_y(spectrum.wavelengths[channel]),
                cie_z(spectrum.wavelengths[channel])) *
                (spectrum.values[channel] *
                 normalization * scale));
    }
}
