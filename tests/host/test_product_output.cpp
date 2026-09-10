#include <ure/product/product_measurement.hpp>
#include <ure/product/product_output.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace product = ure::product;

static int failures = 0;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", \
                         __FILE__, __LINE__, #condition); \
            ++failures; \
        } \
    } while (false)

static product::ProductIdentitySet identities() {
    product::ProductIdentitySet value;
    value.build[0] = 1;
    value.snapshot[0] = 2;
    value.objective[0] = 3;
    value.plan[0] = 4;
    return value;
}

static product::ProductMeasurementSet measurements() {
    ure::RenderMeasurementStatistics statistics;
    statistics.width = 2;
    statistics.height = 1;
    statistics.estimate = {1, 2, 3, 4, 5, 6};
    statistics.normal = {0, 1, 0, 0, 0, 1};
    statistics.albedo = {0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f};
    statistics.depth = {1, 2};
    statistics.uv = {0, 0, 1, 1};
    statistics.motion = {0, 0, 0.25f, -0.25f};
    statistics.valid = true;
    ure::RenderMeasurementEndpointStatistics endpoint;
    endpoint.sample_count = 4;
    endpoint.aggregation_weight = 1;
    endpoint.estimator.mode = ure::IntegratorMode::Wavefront;
    endpoint.first_moment_sums = {4, 8, 12, 16, 20, 24};
    endpoint.second_moment_sums = {6, 22, 46, 84, 130, 186};
    endpoint.lag_one_product_sums = {2, 11, 26, 50, 80, 116};
    endpoint.first_contributions = {0, 1, 2, 3, 4, 5};
    endpoint.last_contributions = {2, 3, 4, 5, 6, 7};
    endpoint.maximum_absolute_contributions = {2, 4, 6, 8, 10, 12};
    endpoint.tail_event_counts = {0, 0, 0, 0, 0, 0};
    statistics.auxiliary_estimator = endpoint.estimator;
    statistics.endpoints.push_back(std::move(endpoint));
    const auto identity = identities();
    return product::make_product_measurement_set(
        statistics, identity, 4, identity.build);
}

static std::vector<std::uint8_t> bytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::vector<std::uint8_t>(
        std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

int main() {
    const auto directory = std::filesystem::temp_directory_path() /
        "ultrarender_product_output_test";
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    const auto result = product::publish_product_measurement_set(
        measurements(), identities(), directory, "frame");
    CHECK(std::filesystem::exists(result.exr));
    CHECK(std::filesystem::exists(result.measurement_checkpoint));
    CHECK(std::filesystem::exists(result.manifest));
    const auto inspection = product::inspect_product_exr(result.exr);
    CHECK(inspection.part_count == 2);
    CHECK(inspection.width == 2 && inspection.height == 1);
    CHECK(inspection.channels.size() > 20);
    CHECK(inspection.metadata.at("ure_scene_identity") ==
          "0200000000000000000000000000000000000000000000000000000000000000");
    CHECK(inspection.metadata.at("part.1.ure_channel_storage").find(
              "uint64->float32") != std::string::npos);
    const auto checkpoint = bytes(result.measurement_checkpoint);
    CHECK(checkpoint.size() > 16);
    CHECK(std::string(checkpoint.begin(), checkpoint.begin() + 8) == "UREMEAS2");
    const auto manifest = bytes(result.manifest);
    CHECK(std::string(manifest.begin(), manifest.end()).find(
        "\"publication\":\"manifest-last\"") != std::string::npos);
    const auto ppm = product::publish_product_measurement_set(
        measurements(), identities(), directory, "frame-ppm",
        product::ProductOutputFormat::Ppm,
        product::ProductToneMap::Reinhard);
    const auto bmp = product::publish_product_measurement_set(
        measurements(), identities(), directory, "frame-bmp",
        product::ProductOutputFormat::Bmp, product::ProductToneMap::Aces);
    const auto hdr = product::publish_product_measurement_set(
        measurements(), identities(), directory, "frame-hdr",
        product::ProductOutputFormat::Hdr, product::ProductToneMap::Linear);
    CHECK(ppm.artifact_count == 4 &&
          ppm.derived_display.extension() == ".ppm" &&
          std::filesystem::exists(ppm.derived_display));
    const auto ppm_manifest = bytes(ppm.manifest);
    const std::string ppm_manifest_text(ppm_manifest.begin(),
                                        ppm_manifest.end());
    CHECK(ppm_manifest_text.find(
              "ure.preview.view.linear-srgb-reinhard-srgb8/1.0") !=
          std::string::npos);
    CHECK(bmp.artifact_count == 4 &&
          bmp.derived_display.extension() == ".bmp" &&
          std::filesystem::exists(bmp.derived_display));
    CHECK(hdr.artifact_count == 4 &&
          hdr.derived_display.extension() == ".hdr" &&
          std::filesystem::exists(hdr.derived_display));
    bool rejected_noop_tone_map = false;
    try {
        static_cast<void>(product::publish_product_measurement_set(
            measurements(), identities(), directory, "invalid-exr",
            product::ProductOutputFormat::OpenExr,
            product::ProductToneMap::Aces));
    } catch (const std::invalid_argument&) {
        rejected_noop_tone_map = true;
    }
    CHECK(rejected_noop_tone_map);
    std::size_t temporary_count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory))
        if (entry.path().filename().string().find(".tmp.") != std::string::npos)
            ++temporary_count;
    CHECK(temporary_count == 0);

    auto original = bytes(result.exr);
    {
        std::ofstream stream(result.exr, std::ios::binary | std::ios::trunc);
        stream.put(static_cast<char>(0));
    }
    bool rejected = false;
    try { static_cast<void>(product::inspect_product_exr(result.exr)); }
    catch (const std::exception&) { rejected = true; }
    CHECK(rejected);
    std::ofstream restore(result.exr, std::ios::binary | std::ios::trunc);
    restore.write(reinterpret_cast<const char*>(original.data()),
                  static_cast<std::streamsize>(original.size()));
    restore.close();
    std::filesystem::remove_all(directory, error);
    if (failures != 0) {
        std::fprintf(stderr, "%d product output checks failed\n", failures);
        return 1;
    }
    std::printf("Product output checks passed\n");
    return 0;
}
