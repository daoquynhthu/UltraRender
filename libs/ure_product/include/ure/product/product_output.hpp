#pragma once

#include "ure/product/product_service.hpp"

#include <cstdint>
#include <filesystem>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ure::product {

enum class ProductOutputFailure : std::uint32_t {
    DiskOrPermission,
    Codec,
    AtomicPublication
};

class ProductOutputError final : public std::runtime_error {
public:
    ProductOutputError(ProductOutputFailure failure, std::string message);
    ProductOutputFailure failure() const noexcept;

private:
    ProductOutputFailure failure_;
};

struct ProductExrChannel {
    std::uint32_t part = 0;
    std::string name;
};

struct ProductExrInspection {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t part_count = 0;
    std::vector<ProductExrChannel> channels;
    std::map<std::string, std::string> metadata;
    Identity content_identity{};
};

struct ProductOutputPaths {
    std::filesystem::path exr;
    std::filesystem::path measurement_checkpoint;
    std::filesystem::path manifest;
    Identity exr_content_identity{};
    Identity measurement_content_identity{};
    Identity manifest_content_identity{};
    std::filesystem::path derived_display;
    Identity derived_display_content_identity{};
    std::uint64_t artifact_count{3};
};

enum class ProductOutputFormat : std::uint32_t {
    OpenExr,
    Measurement,
    Hdr,
    Ppm,
    Bmp
};

enum class ProductToneMap : std::uint32_t {
    Linear,
    Reinhard,
    Aces
};

ProductExrInspection inspect_product_exr(
    const std::filesystem::path& path);

ProductOutputPaths publish_product_measurement_set(
    const ProductMeasurementSet& measurements,
    const ProductIdentitySet& identities,
    const std::filesystem::path& directory,
    std::string_view stem,
    ProductOutputFormat format = ProductOutputFormat::OpenExr,
    ProductToneMap tone_map = ProductToneMap::Linear,
    std::uint64_t byte_budget = (std::numeric_limits<std::uint64_t>::max)());

}
