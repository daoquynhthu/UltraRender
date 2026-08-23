#include "render_example.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include <ultrarender/ure_registry.h>

namespace {

const ure::client::FramePlane &color_plane(const ure::client::Frame &frame) {
    for (const auto &plane : frame.planes) {
        if (plane.semantic == URE_FRAME_PLANE_COLOR)
            return plane;
    }
    throw std::runtime_error("the product frame has no color plane");
}

void write_pfm(const std::filesystem::path &path,
               const ure::client::Frame &frame) {
    const auto &plane = color_plane(frame);
    const auto pixel_count = static_cast<std::uint64_t>(frame.width) *
                             static_cast<std::uint64_t>(frame.height);
    if (frame.width == 0 || frame.height == 0 ||
        plane.scalar_type != URE_SCALAR_TYPE_FLOAT32 ||
        plane.component_layout != URE_COMPONENT_LAYOUT_RGBA ||
        plane.bytes.size() != pixel_count * 4 * sizeof(float))
        throw std::runtime_error("the product color plane layout is invalid");
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        throw std::runtime_error("the output artifact cannot be created");
    output << "PF\n" << frame.width << ' ' << frame.height << "\n-1.0\n";
    const auto *rgba = reinterpret_cast<const float *>(plane.bytes.data());
    for (std::uint32_t y = frame.height; y-- > 0;) {
        for (std::uint32_t x = 0; x < frame.width; ++x) {
            const auto offset =
                (static_cast<std::uint64_t>(y) * frame.width + x) * 4;
            output.write(reinterpret_cast<const char *>(rgba + offset),
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
        objective.output_semantics = {URE_FRAME_PLANE_COLOR};
        objective.sample_budget = 2;
        auto client = ure::client::Client::connect(options);
        auto job = client.create_job(scene, objective);
        job.start();
        ure::client::ProgressEvent progress;
        while (!job.wait(std::chrono::milliseconds(50)))
            static_cast<void>(job.poll_event(progress));
        const auto result = job.result();
        if (result.info.completed_samples != objective.sample_budget ||
            result.frame.sample_count != objective.sample_budget)
            throw std::runtime_error("the product job did not complete its accepted work");
        write_pfm(argv[output_index], result.frame);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
