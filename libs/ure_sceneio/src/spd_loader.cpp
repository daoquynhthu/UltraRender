#include "ure/spd_loader.hpp"
#include "ure/spectral/spectral.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

#include <ure/log.hpp>

namespace ure::spectral {

SPDData load_spd_file(const std::string& path) {
    SPDData result;

    std::ifstream file(path);
    if (!file.is_open()) {
        UR_LOG_WARN(SceneIO, "could not open SPD file: {}", path);
        return result;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;

        auto first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) continue;
        auto last = line.find_last_not_of(" \t\r\n");
        line = line.substr(first, last - first + 1);
        if (line[0] == '#') continue;

        std::istringstream ss(line);
        float lambda, value;
        if (!(ss >> lambda >> value)) {
            UR_LOG_WARN(SceneIO, "skipping malformed SPD line: {}", line);
            continue;
        }

        result.lambdas.push_back(lambda);
        result.values.push_back(value);
    }

    if (result.lambdas.empty()) {
        return result;
    }

    // Ensure sorted by wavelength
    std::vector<size_t> indices(result.lambdas.size());
    for (size_t i = 0; i < indices.size(); ++i) indices[i] = i;
    std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
        return result.lambdas[a] < result.lambdas[b];
    });

    SPDData sorted;
    sorted.lambdas.reserve(indices.size());
    sorted.values.reserve(indices.size());
    for (size_t i : indices) {
        sorted.lambdas.push_back(result.lambdas[i]);
        sorted.values.push_back(result.values[i]);
    }

    return sorted;
}

std::vector<float> resample_uniform(const SPDData& spd, int n_samples,
                                    float lambda_min, float lambda_max) {
    std::vector<float> out;
    if (spd.lambdas.empty() || n_samples <= 0) {
        out.resize(std::max(n_samples, 0), 0.0f);
        return out;
    }

    std::vector<SPD::Sample> samples;
    samples.reserve(spd.lambdas.size());
    for (size_t i = 0; i < spd.lambdas.size(); ++i) {
        samples.push_back({spd.lambdas[i], spd.values[i]});
    }
    SPD spd_obj(samples);
    out.reserve(n_samples);

    float step = (lambda_max - lambda_min) / std::max(n_samples - 1, 1);
    for (int i = 0; i < n_samples; ++i) {
        float lambda = lambda_min + step * i;
        out.push_back(spd_obj.evaluate(lambda));
    }

    return out;
}

}
