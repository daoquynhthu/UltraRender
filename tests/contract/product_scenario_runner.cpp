#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include <ure/client/client.hpp>
#include <ultrarender/ure_registry.h>

namespace {

std::string digest_hex(std::span<const std::uint8_t, 32> digest) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const auto value : digest)
        output << std::setw(2) << static_cast<unsigned>(value);
    return output.str();
}

const ure::client::FramePlane &color_plane(const ure::client::Frame &frame) {
    const auto found = std::ranges::find(
        frame.planes, URE_FRAME_PLANE_COLOR,
        &ure::client::FramePlane::semantic);
    if (found == frame.planes.end())
        throw std::runtime_error("product frame has no color plane");
    return *found;
}

void write_pfm(const std::filesystem::path &path,
               const ure::client::Frame &frame) {
    const auto &plane = color_plane(frame);
    const std::uint64_t row_bytes =
        static_cast<std::uint64_t>(frame.width) * 4 * sizeof(float);
    if (frame.width == 0 || frame.height == 0 ||
        plane.scalar_type != URE_SCALAR_TYPE_FLOAT32 ||
        plane.component_layout != URE_COMPONENT_LAYOUT_RGBA ||
        plane.width != frame.width || plane.height != frame.height ||
        plane.element_stride != 4 * sizeof(float) ||
        plane.row_stride < row_bytes ||
        plane.bytes.size() < plane.row_stride * frame.height)
        throw std::runtime_error("product color plane layout is invalid");
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        throw std::runtime_error("raw artifact cannot be created");
    output << "PF\n" << frame.width << ' ' << frame.height << "\n-1.0\n";
    std::array<float, 3> rgb{};
    for (std::uint32_t y = frame.height; y-- > 0;) {
        const auto *row = plane.bytes.data() + plane.row_stride * y;
        for (std::uint32_t x = 0; x < frame.width; ++x) {
            const auto *pixel = row + plane.element_stride * x;
            for (std::size_t component = 0; component < rgb.size(); ++component)
                std::memcpy(&rgb[component], pixel + component * sizeof(float),
                            sizeof(float));
            if (!std::ranges::all_of(rgb, [](float value) {
                    return std::isfinite(value);
                }))
                throw std::runtime_error("product frame contains non-finite color");
            output.write(reinterpret_cast<const char *>(rgb.data()),
                         sizeof(rgb));
        }
    }
    if (!output)
        throw std::runtime_error("raw artifact write failed");
}

ure::client::TransportMode transport_mode(std::string_view value) {
    if (value == "direct")
        return ure::client::TransportMode::Direct;
    if (value == "worker")
        return ure::client::TransportMode::Worker;
    throw std::invalid_argument("transport must be direct or worker");
}

ure::client::SceneFormat scene_format(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    if (extension == ".ure")
        return ure::client::SceneFormat::Ure;
    if (extension == ".urescene")
        return ure::client::SceneFormat::UreScene;
    if (extension == ".urepkg")
        return ure::client::SceneFormat::UrePackage;
    throw std::invalid_argument("scene must use .ure, .urescene, or .urepkg");
}

std::uint64_t parse_samples(std::string_view value) {
    std::size_t offset{};
    const auto parsed = std::stoull(std::string(value), &offset);
    if (offset != value.size() || parsed < 4 || parsed > 4096)
        throw std::invalid_argument("sample count must be in [4, 4096]");
    return parsed;
}

void retain_progressive(const ure::client::Frame &frame,
                        std::uint64_t low_target,
                        std::uint64_t mid_target,
                        std::optional<ure::client::Frame> &low,
                        std::optional<ure::client::Frame> &mid) {
    if (frame.sample_count <= low_target &&
        (!low || frame.sample_count > low->sample_count))
        low = frame;
    if (frame.sample_count <= mid_target &&
        (!mid || frame.sample_count > mid->sample_count))
        mid = frame;
}

}

int main(int argc, char **argv) {
    try {
        if (argc != 7)
            throw std::invalid_argument(
                "usage: product_scenario_runner <direct|worker> <runtime> <worker> <scene> <samples> <output-prefix>");
        const auto mode = transport_mode(argv[1]);
        const auto samples = parse_samples(argv[5]);
        const std::filesystem::path output_prefix =
            std::filesystem::absolute(argv[6]);
        ure::client::ConnectionOptions connection;
        connection.transport = mode;
        connection.runtime_path = std::filesystem::absolute(argv[2]);
        connection.worker_path = std::filesystem::absolute(argv[3]);
        ure::client::SceneInput scene;
        scene.path = std::filesystem::absolute(argv[4]);
        scene.format = scene_format(scene.path);
        ure::client::Objective objective;
        objective.output_semantics = {URE_FRAME_PLANE_COLOR};
        objective.sample_budget = samples;
        auto client = ure::client::Client::connect(connection);
        const auto devices = client.devices();
        auto job = client.create_job(scene, objective);
        const auto low_target = std::max<std::uint64_t>(samples / 4, 1);
        const auto mid_target = std::max<std::uint64_t>(samples / 2, 1);
        std::optional<ure::client::Frame> low;
        std::optional<ure::client::Frame> mid;
        std::uint64_t acquired_generation{};
        const auto started = std::chrono::steady_clock::now();
        job.start();
        while (!job.wait(std::chrono::milliseconds(20))) {
            ure::client::ProgressEvent event;
            if (!job.poll_event(event) ||
                event.latest_frame_generation <= acquired_generation)
                continue;
            const auto frame = job.latest_frame();
            acquired_generation = event.latest_frame_generation;
            retain_progressive(frame, low_target, mid_target, low, mid);
        }
        const auto result = job.result();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - started)
                                 .count();
        if (result.info.state != ure::client::JobState::Succeeded ||
            result.info.requested_samples != samples ||
            result.info.accepted_samples != samples ||
            result.info.completed_samples != samples ||
            result.frame.sample_count != samples)
            throw std::runtime_error("product work accounting is incomplete");
        if (!low || !mid)
            throw std::runtime_error(
                "bounded progressive publications did not retain convergence frames");
        const auto prefix = output_prefix.string();
        const auto low_path = std::filesystem::path(
            prefix + ".spp" + std::to_string(low->sample_count) + ".pfm");
        const auto mid_path = std::filesystem::path(
            prefix + ".spp" + std::to_string(mid->sample_count) + ".pfm");
        const auto final_path = std::filesystem::path(
            prefix + ".spp" + std::to_string(samples) + ".pfm");
        write_pfm(low_path, *low);
        write_pfm(mid_path, *mid);
        write_pfm(final_path, result.frame);
        const auto selected_device = std::ranges::find(
            devices, result.info.execution.device_identity,
            &ure::client::DeviceInfo::identity);
        if (selected_device == devices.end())
            throw std::runtime_error(
                "executed device is absent from the product inventory");
        std::cout << "transport=" << argv[1] << '\n'
                  << "requested_samples=" << result.info.requested_samples << '\n'
                  << "accepted_samples=" << result.info.accepted_samples << '\n'
                  << "completed_samples=" << result.info.completed_samples << '\n'
                  << "eligible_integrator_modes="
                  << result.info.eligible_integrator_modes << '\n'
                  << "qualified_integrator_modes="
                  << result.info.qualified_integrator_modes << '\n'
                  << "executed_integrator_modes="
                  << result.info.executed_integrator_modes << '\n'
                  << "low_samples=" << low->sample_count << '\n'
                  << "mid_samples=" << mid->sample_count << '\n'
                  << "frame=" << result.frame.width << 'x'
                  << result.frame.height << '\n'
                  << "elapsed_ms=" << elapsed << '\n'
                  << "build_identity="
                  << digest_hex(result.info.identities.build) << '\n'
                  << "snapshot_identity="
                  << digest_hex(result.info.identities.snapshot) << '\n'
                  << "objective_identity="
                  << digest_hex(result.info.identities.objective) << '\n'
                  << "plan_identity="
                  << digest_hex(result.info.identities.plan) << '\n'
                  << "frame_content_identity="
                  << digest_hex(result.artifact.frame_content_identity) << '\n'
                  << "device_identity="
                  << digest_hex(result.info.execution.device_identity) << '\n'
                  << "backend=" << result.info.execution.backend << '\n'
                  << "provider=" << result.info.execution.provider << '\n'
                  << "device_name=" << result.info.execution.name << '\n'
                  << "adapter_id=" << result.info.execution.adapter_id << '\n'
                  << "driver_identity=" << selected_device->driver_identity
                  << '\n'
                  << "compiler_identity=" << selected_device->compiler_identity
                  << '\n'
                  << "total_memory_bytes="
                  << selected_device->total_memory_bytes << '\n'
                  << "available_memory_bytes="
                  << result.info.execution.available_memory_bytes << '\n'
                  << "selected_memory_budget_bytes="
                  << result.info.execution.selected_memory_budget_bytes << '\n'
                  << "enumerated_devices=" << devices.size() << '\n'
                  << "low_raw=" << low_path.string() << '\n'
                  << "mid_raw=" << mid_path.string() << '\n'
                  << "final_raw=" << final_path.string() << '\n';
        return 0;
    } catch (const ure::client::Error &error) {
        std::cerr << error.what() << " (result=" << error.info().result
                  << ", domain=" << error.info().domain
                  << ", detail=" << error.info().detail << ")\n";
        return 2;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
