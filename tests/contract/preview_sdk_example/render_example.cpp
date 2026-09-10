#include "render_example.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <ranges>
#include <span>
#include <stdexcept>

#include <ultrarender/ure_registry.h>

namespace {

const ure::client::FramePlane &beauty_plane(const ure::client::Frame &frame) {
    for (const auto &plane : frame.planes) {
        if (plane.semantic == URE_FRAME_PLANE_BEAUTY_RAW)
            return plane;
    }
    throw std::runtime_error("the product frame has no raw Beauty plane");
}

void write_pfm(const std::filesystem::path &path,
               const ure::client::Frame &frame) {
    const auto &plane = beauty_plane(frame);
    const auto pixel_count = static_cast<std::uint64_t>(frame.width) *
                             static_cast<std::uint64_t>(frame.height);
    if (frame.width == 0 || frame.height == 0 ||
        plane.scalar_type != URE_SCALAR_TYPE_FLOAT32 ||
        plane.component_layout != URE_COMPONENT_LAYOUT_RGB ||
        plane.bytes.size() != pixel_count * 3 * sizeof(float))
        throw std::runtime_error("the product Beauty plane layout is invalid");
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        throw std::runtime_error("the output artifact cannot be created");
    output << "PF\n" << frame.width << ' ' << frame.height << "\n-1.0\n";
    const auto *rgb = reinterpret_cast<const float *>(plane.bytes.data());
    for (std::uint32_t y = frame.height; y-- > 0;) {
        for (std::uint32_t x = 0; x < frame.width; ++x) {
            const auto offset =
                (static_cast<std::uint64_t>(y) * frame.width + x) * 3;
            output.write(reinterpret_cast<const char *>(rgb + offset),
                         3 * sizeof(float));
        }
    }
    if (!output)
        throw std::runtime_error("the output artifact could not be written");
}

}

int render_example(ure::client::TransportMode mode, int argc, char **argv) {
    try {
        const int expected = mode == ure::client::TransportMode::Direct ? 4 : 5;
        if (argc != expected)
            throw std::runtime_error(
                mode == ure::client::TransportMode::Direct
                    ? "usage: ure_preview_direct <runtime> <scene> <output>"
                    : "usage: ure_preview_worker <worker> <runtime> <scene> <output>");
        const int runtime_index = mode == ure::client::TransportMode::Direct ? 1 : 2;
        const int scene_index = runtime_index + 1;
        const int output_index = scene_index + 1;
        ure::client::ConnectionOptions options;
        options.transport = mode;
        options.runtime_path = std::filesystem::absolute(argv[runtime_index]);
        if (mode == ure::client::TransportMode::Worker)
            options.worker_path = std::filesystem::absolute(argv[1]);
        ure::client::SceneInput scene;
        scene.path = std::filesystem::absolute(argv[scene_index]);
        ure::client::Objective objective;
        objective.output_semantics = {
            URE_FRAME_PLANE_BEAUTY_RAW, URE_FRAME_PLANE_NORMAL,
            URE_FRAME_PLANE_ALBEDO, URE_FRAME_PLANE_DEPTH,
            URE_FRAME_PLANE_MOTION, URE_FRAME_PLANE_SAMPLE_COUNT,
            URE_FRAME_PLANE_VARIANCE,
            URE_FRAME_PLANE_EFFECTIVE_SAMPLE_COUNT,
            URE_FRAME_PLANE_TAIL_EVENT_COUNT};
        objective.sample_budget = 4;
        auto client = ure::client::Client::connect(options);
        auto job = client.create_job(scene, objective);
        job.start();
        ure::client::ProgressEvent progress;
        while (!job.wait(std::chrono::milliseconds(50)))
            static_cast<void>(job.poll_event(progress));
        const auto result = job.result();
        if (result.info.completed_samples != objective.sample_budget ||
            result.frame.sample_count != objective.sample_budget ||
            result.frame.generation == 0 || result.frame.planes.size() < 20 ||
            std::ranges::none_of(result.frame.measurement_identity,
                                 [](std::uint8_t value) { return value != 0; }))
            throw std::runtime_error("the product job did not complete its accepted work");
        const auto &beauty = beauty_plane(result.frame);
        ure::client::PlaneRangeRequest range;
        range.plane_index = static_cast<std::uint32_t>(
            &beauty - result.frame.planes.data());
        range.byte_count = std::min<std::uint64_t>(64, beauty.bytes.size());
        range.expected_generation = result.frame.generation;
        range.expected_content_identity = beauty.content_identity;
        const auto partial = job.copy_plane_range(range);
        if (partial.size() != range.byte_count ||
            !std::ranges::equal(partial,
                                std::span(beauty.bytes).first(partial.size())))
            throw std::runtime_error("the product partial plane read is invalid");
        ure::client::OutputRequest publication;
        publication.path = std::filesystem::path(argv[output_index]).concat(
            ".product");
        publication.format = ure::client::OutputFormat::OpenExr;
        const auto manifest = job.publish_artifacts(publication);
        if (manifest.publication_status != URE_PUBLICATION_COMPLETE ||
            manifest.artifact_count != 3 || manifest.byte_count == 0)
            throw std::runtime_error("the product artifact graph is incomplete");
        write_pfm(argv[output_index], result.frame);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
