#include <fstream>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <unordered_set>

#include <nlohmann/json.hpp>

#include "ure/mie_phase_io.hpp"
#include "ure/mie_phase_validation.hpp"

namespace ure::sceneio {

std::string mie_phase_content_hash(const scene_ir::MiePhaseResource& resource) {
    return scene_ir::mie_phase_content_hash(resource);
}

void validate_mie_phase_resource(scene_ir::MiePhaseResource& resource,
                                 float normalization_tolerance) {
    scene_ir::validate_mie_phase_resource(resource, normalization_tolerance);
}

scene_ir::MiePhaseResource load_mie_phase_table(const std::string& path,
                                                std::size_t maximum_resource_bytes) {
    std::error_code file_error;
    const auto file_bytes = std::filesystem::file_size(path, file_error);
    if (maximum_resource_bytes == 0 || (!file_error && file_bytes > maximum_resource_bytes)) {
        throw std::invalid_argument("Mie phase table exceeds import byte budget");
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("Failed to open Mie phase table: " + path);
    }
    nlohmann::json document;
    try {
        stream >> document;
        const std::unordered_set<std::string> allowed_keys = {
            "format", "version", "units", "wavelengths_nm", "cos_theta", "phase",
            "scattering_cross_section_m2", "extinction_cross_section_m2",
            "polarization_model", "provenance", "source_hash"};
        for (const auto& [key, value] : document.items()) {
            static_cast<void>(value);
            if (!allowed_keys.contains(key)) {
                throw std::invalid_argument("Unknown Mie phase table field: " + key);
            }
        }
        if (document.at("format").get<std::string>() != "ure-mie-phase-table" ||
            document.at("version").get<int>() != 1) {
            throw std::invalid_argument("Unsupported Mie phase table format or version");
        }
        const auto& units = document.at("units");
        if (units.at("wavelength") != "nm" || units.at("cos_theta") != "dimensionless" ||
            units.at("phase") != "sr^-1" || units.at("cross_section") != "m^2") {
            throw std::invalid_argument("Unsupported Mie phase table units");
        }
    } catch (const nlohmann::json::exception& error) {
        throw std::invalid_argument(std::string("Malformed Mie phase table: ") + error.what());
    }
    scene_ir::MiePhaseResource resource;
    try {
        resource.wavelengths_nm = document.at("wavelengths_nm").get<std::vector<float>>();
        resource.cos_theta = document.at("cos_theta").get<std::vector<float>>();
        if (resource.cos_theta.size() != 0 &&
            resource.wavelengths_nm.size() >
                std::numeric_limits<std::size_t>::max() / resource.cos_theta.size()) {
            throw std::invalid_argument("Mie phase table size overflow");
        }
        const std::size_t cells = resource.wavelengths_nm.size() * resource.cos_theta.size();
        if (resource.wavelengths_nm.size() > std::numeric_limits<std::size_t>::max() / 5 ||
            5 * resource.wavelengths_nm.size() >
                std::numeric_limits<std::size_t>::max() - resource.cos_theta.size() ||
            cells > (std::numeric_limits<std::size_t>::max() -
                         5 * resource.wavelengths_nm.size() - resource.cos_theta.size()) / 2 ||
            2 * cells + 5 * resource.wavelengths_nm.size() + resource.cos_theta.size() >
                maximum_resource_bytes / sizeof(float)) {
            throw std::invalid_argument("Mie phase table exceeds canonical resource byte budget");
        }
        resource.scattering_cross_section_m2 =
            document.at("scattering_cross_section_m2").get<std::vector<float>>();
        resource.extinction_cross_section_m2 =
            document.at("extinction_cross_section_m2").get<std::vector<float>>();
        const auto& rows = document.at("phase");
        if (!rows.is_array() || rows.size() != resource.wavelengths_nm.size()) {
            throw std::invalid_argument("Mie phase table row count mismatch");
        }
        for (const auto& row : rows) {
            const auto values = row.get<std::vector<float>>();
            if (values.size() != resource.cos_theta.size()) {
                throw std::invalid_argument("Mie phase table row width mismatch");
            }
            resource.phase.insert(resource.phase.end(), values.begin(), values.end());
        }
        resource.provenance = document.value("provenance", std::string{});
        resource.source_hash = document.value("source_hash", std::string{});
        if (document.value("polarization_model", std::string{}) != "scalar_depolarizing") {
            throw std::invalid_argument("Unsupported Mie polarization model");
        }
    } catch (const nlohmann::json::exception& error) {
        throw std::invalid_argument(std::string("Malformed Mie phase table payload: ") + error.what());
    }
    scene_ir::validate_mie_phase_resource(resource);
    return resource;
}

void save_mie_phase_table(const scene_ir::MiePhaseResource& input,
                          const std::string& path) {
    auto resource = input;
    scene_ir::validate_mie_phase_resource(resource);
    nlohmann::json document;
    document["format"] = "ure-mie-phase-table";
    document["version"] = 1;
    document["units"] = {
        {"wavelength", "nm"},
        {"cos_theta", "dimensionless"},
        {"phase", "sr^-1"},
        {"cross_section", "m^2"}
    };
    document["wavelengths_nm"] = resource.wavelengths_nm;
    document["cos_theta"] = resource.cos_theta;
    document["phase"] = nlohmann::json::array();
    for (std::size_t wavelength = 0; wavelength < resource.wavelengths_nm.size(); ++wavelength) {
        const auto first = resource.phase.begin() +
                           static_cast<std::ptrdiff_t>(wavelength * resource.cos_theta.size());
        document["phase"].push_back(std::vector<float>(first, first + resource.cos_theta.size()));
    }
    document["scattering_cross_section_m2"] = resource.scattering_cross_section_m2;
    document["extinction_cross_section_m2"] = resource.extinction_cross_section_m2;
    document["polarization_model"] = "scalar_depolarizing";
    document["provenance"] = resource.provenance;
    document["source_hash"] = resource.source_hash;
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        throw std::runtime_error("Failed to create Mie phase table: " + path);
    }
    stream << document.dump(2) << '\n';
    if (!stream) {
        throw std::runtime_error("Failed to write Mie phase table: " + path);
    }
}

}
