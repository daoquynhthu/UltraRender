#pragma once

#include <string>
#include <vector>

namespace ure::spectral {

struct SPDData {
    std::vector<float> lambdas;
    std::vector<float> values;
};

SPDData load_spd_file(const std::string& path);

// For Phase E: num_wavelengths will become runtime-configurable via RenderConfig
// For now, default to 4 (matching GpuSpectrum::kNumWavelengths)
std::vector<float> resample_uniform(const SPDData& spd,
                                    int n_samples = 4,
                                    float lambda_min = 400.0f,
                                    float lambda_max = 700.0f);

}
