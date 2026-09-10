#include <ure/client/client.hpp>
#include <ultrarender/ure_registry.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <ranges>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

std::string digest_hex(std::span<const std::uint8_t, 32> digest) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const auto value : digest)
        output << std::setw(2) << static_cast<unsigned>(value);
    return output.str();
}

bool identity_present(const std::array<std::uint8_t, 32>& identity) {
    return std::ranges::any_of(identity, [](std::uint8_t value) {
        return value != 0;
    });
}

ure::client::SceneFormat scene_format(const std::filesystem::path& path) {
    const auto extension = path.extension().string();
    if (extension == ".ure") return ure::client::SceneFormat::Ure;
    if (extension == ".urescene") return ure::client::SceneFormat::UreScene;
    if (extension == ".urepkg") return ure::client::SceneFormat::UrePackage;
    throw std::invalid_argument("scene format is unsupported");
}

ure::client::TransportMode transport_mode(std::string_view value) {
    if (value == "direct") return ure::client::TransportMode::Direct;
    if (value == "worker") return ure::client::TransportMode::Worker;
    throw std::invalid_argument("transport must be direct or worker");
}

const ure::client::FramePlane& require_plane(
    const ure::client::Frame& frame, std::uint32_t semantic) {
    const auto found = std::ranges::find(frame.planes, semantic,
                                         &ure::client::FramePlane::semantic);
    if (found == frame.planes.end())
        throw std::runtime_error("required measurement plane is missing");
    return *found;
}

void write_beauty_pfm(const std::filesystem::path& path,
                      const ure::client::Frame& frame) {
    const auto& plane = require_plane(frame, URE_FRAME_PLANE_BEAUTY_RAW);
    const auto row_bytes = static_cast<std::uint64_t>(frame.width) *
                           3 * sizeof(float);
    if (plane.scalar_type != URE_SCALAR_TYPE_FLOAT32 ||
        plane.component_layout != URE_COMPONENT_LAYOUT_RGB ||
        plane.width != frame.width || plane.height != frame.height ||
        plane.element_stride != 3 * sizeof(float) ||
        plane.row_stride < row_bytes ||
        plane.bytes.size() < plane.row_stride * frame.height)
        throw std::runtime_error("raw Beauty plane layout is invalid");
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        throw std::runtime_error("raw Beauty evidence cannot be created");
    output << "PF\n" << frame.width << ' ' << frame.height << "\n-1.0\n";
    std::array<float, 3> rgb{};
    for (std::uint32_t y = frame.height; y-- > 0;) {
        const auto* row = plane.bytes.data() + plane.row_stride * y;
        for (std::uint32_t x = 0; x < frame.width; ++x) {
            const auto* pixel = row + plane.element_stride * x;
            for (std::size_t component = 0; component < rgb.size(); ++component)
                std::memcpy(&rgb[component], pixel + component * sizeof(float),
                            sizeof(float));
            if (!std::ranges::all_of(rgb, [](float value) {
                    return std::isfinite(value);
                }))
                throw std::runtime_error("raw Beauty contains a non-finite value");
            output.write(reinterpret_cast<const char*>(rgb.data()), sizeof(rgb));
        }
    }
    if (!output)
        throw std::runtime_error("raw Beauty evidence write failed");
}

void validate_frame(const ure::client::Frame& frame,
                    std::uint64_t samples) {
    constexpr std::array required{
        URE_FRAME_PLANE_COLOR, URE_FRAME_PLANE_BEAUTY_RAW,
        URE_FRAME_PLANE_NORMAL, URE_FRAME_PLANE_ALBEDO,
        URE_FRAME_PLANE_DEPTH, URE_FRAME_PLANE_UV,
        URE_FRAME_PLANE_MOTION, URE_FRAME_PLANE_VALIDITY,
        URE_FRAME_PLANE_SAMPLE_COUNT, URE_FRAME_PLANE_FIRST_MOMENT,
        URE_FRAME_PLANE_SECOND_MOMENT, URE_FRAME_PLANE_LAG_ONE_PRODUCT,
        URE_FRAME_PLANE_VARIANCE, URE_FRAME_PLANE_EFFECTIVE_SAMPLE_COUNT,
        URE_FRAME_PLANE_MAXIMUM_ABSOLUTE_CONTRIBUTION,
        URE_FRAME_PLANE_FIRST_CONTRIBUTION,
        URE_FRAME_PLANE_LAST_CONTRIBUTION,
        URE_FRAME_PLANE_TAIL_EVENT_COUNT,
        URE_FRAME_PLANE_ESTIMATOR_WEIGHT,
        URE_FRAME_PLANE_TECHNIQUE_IDENTITY};
    if (frame.width == 0 || frame.height == 0 || frame.generation == 0 ||
        frame.sample_count != samples || !identity_present(frame.identity) ||
        !identity_present(frame.measurement_identity))
        throw std::runtime_error("measurement frame identity is invalid");
    for (const auto semantic : required)
        static_cast<void>(require_plane(frame, semantic));
    for (const auto& plane : frame.planes) {
        if (plane.width == 0 || plane.height == 0 || plane.depth == 0 ||
            plane.element_stride == 0 || plane.row_stride == 0 ||
            plane.slice_stride == 0 || plane.bytes.empty() ||
            plane.bytes.size() != plane.slice_stride * plane.depth ||
            !identity_present(plane.observable_identity) ||
            !identity_present(plane.unit_identity) ||
            !identity_present(plane.measure_identity) ||
            !identity_present(plane.time_identity) ||
            !identity_present(plane.uncertainty_identity) ||
            !identity_present(plane.provenance_identity) ||
            !identity_present(plane.content_identity))
            throw std::runtime_error("measurement plane contract is invalid");
    }
}

}

int main(int argc, char** argv) {
    try {
        if (argc != 7)
            throw std::invalid_argument(
                "usage: product_measurement_scenario_runner <direct|worker> <runtime> <worker> <scene> <samples> <output-prefix>");
        const auto mode = transport_mode(argv[1]);
        const auto samples = std::stoull(argv[5]);
        if (samples < 4 || samples > 4096)
            throw std::invalid_argument("samples must be in [4, 4096]");
        ure::client::ConnectionOptions connection;
        connection.transport = mode;
        connection.runtime_path = std::filesystem::absolute(argv[2]);
        connection.worker_path = std::filesystem::absolute(argv[3]);
        auto client = ure::client::Client::connect(connection);
        ure::client::SceneInput scene;
        scene.path = std::filesystem::absolute(argv[4]);
        scene.format = scene_format(scene.path);
        ure::client::Objective objective;
        objective.sample_budget = samples;
        objective.output_semantics = {
            URE_FRAME_PLANE_BEAUTY_RAW, URE_FRAME_PLANE_NORMAL,
            URE_FRAME_PLANE_ALBEDO, URE_FRAME_PLANE_DEPTH,
            URE_FRAME_PLANE_UV, URE_FRAME_PLANE_MOTION,
            URE_FRAME_PLANE_SAMPLE_COUNT, URE_FRAME_PLANE_VARIANCE,
            URE_FRAME_PLANE_EFFECTIVE_SAMPLE_COUNT,
            URE_FRAME_PLANE_TAIL_EVENT_COUNT};
        auto job = client.create_job(scene, objective);
        const auto start = std::chrono::steady_clock::now();
        job.start();
        if (!job.wait(std::chrono::hours(2)))
            throw std::runtime_error("measurement ProductJob timed out");
        const auto result = job.result();
        validate_frame(result.frame, samples);
        const auto largest = std::ranges::max_element(
            result.frame.planes, {},
            [](const ure::client::FramePlane& plane) {
                return plane.bytes.size();
            });
        if (largest == result.frame.planes.end())
            throw std::runtime_error("measurement frame has no readable plane");
        const auto byte_count = std::min<std::uint64_t>(
            largest->bytes.size(), UINT64_C(4096));
        const auto plane_index = static_cast<std::uint32_t>(
            std::distance(result.frame.planes.begin(), largest));
        ure::client::PlaneRangeRequest range;
        range.plane_index = plane_index;
        range.source_offset = (largest->bytes.size() - byte_count) / 2;
        range.byte_count = byte_count;
        range.expected_generation = result.frame.generation;
        range.expected_content_identity = largest->content_identity;
        const auto partial = job.copy_plane_range(range);
        if (partial.size() != byte_count ||
            !std::ranges::equal(
                partial,
                std::span(largest->bytes).subspan(
                    static_cast<std::size_t>(range.source_offset),
                    static_cast<std::size_t>(range.byte_count))))
            throw std::runtime_error("partial measurement read is corrupted");
        const auto prefix = std::filesystem::absolute(argv[6]);
        std::filesystem::create_directories(prefix.parent_path());
        const auto beauty_path =
            std::filesystem::path(prefix.string() + ".beauty.pfm");
        write_beauty_pfm(beauty_path, result.frame);
        ure::client::OutputRequest display;
        display.path = std::filesystem::path(prefix.string() + ".product");
        display.format = ure::client::OutputFormat::Ppm;
        display.tone_map = ure::client::ToneMap::Reinhard;
        const auto display_manifest = job.publish_artifacts(display);
        if (display_manifest.publication_status != URE_PUBLICATION_COMPLETE ||
            display_manifest.artifact_count != 4)
            throw std::runtime_error("artifact graph publication is incomplete");
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        std::cout << "transport=" << argv[1] << '\n'
                  << "frame=" << result.frame.width << 'x'
                  << result.frame.height << '\n'
                  << "samples=" << samples << '\n'
                  << "planes=" << result.frame.planes.size() << '\n'
                  << "partial_plane=" << plane_index << '\n'
                  << "partial_bytes=" << partial.size() << '\n'
                  << "elapsed_ms=" << elapsed << '\n'
                  << "build_identity="
                  << digest_hex(result.info.identities.build) << '\n'
                  << "snapshot_identity="
                  << digest_hex(result.info.identities.snapshot) << '\n'
                  << "objective_identity="
                  << digest_hex(result.info.identities.objective) << '\n'
                  << "plan_identity="
                  << digest_hex(result.info.identities.plan) << '\n'
                  << "measurement_identity="
                  << digest_hex(result.frame.measurement_identity) << '\n'
                  << "manifest_identity="
                  << digest_hex(display_manifest.manifest_identity) << '\n'
                  << "beauty_raw=" << beauty_path.string() << '\n';
        return 0;
    } catch (const ure::client::Error& error) {
        std::cerr << error.what() << " (result=" << error.info().result
                  << ", domain=" << error.info().domain
                  << ", detail=" << error.info().detail << ")\n";
        return 2;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
