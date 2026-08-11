#include <ure/client/client.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <ultrarender/ure_registry.h>

namespace {

int failures{};

void check(bool condition, const std::string &message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool write_pfm(const std::filesystem::path &path,
               const ure::client::Frame &frame) {
    if (frame.planes.size() != 1 || frame.width == 0 || frame.height == 0 ||
        frame.planes.front().bytes.size() !=
            static_cast<std::size_t>(frame.width) * frame.height * 4 *
                sizeof(float))
        return false;
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        return false;
    output << "PF\n" << frame.width << ' ' << frame.height << "\n-1.0\n";
    const auto *rgba = reinterpret_cast<const float *>(
        frame.planes.front().bytes.data());
    for (std::uint32_t y = frame.height; y-- > 0;) {
        for (std::uint32_t x = 0; x < frame.width; ++x) {
            const std::size_t offset =
                (static_cast<std::size_t>(y) * frame.width + x) * 4;
            output.write(reinterpret_cast<const char *>(rgba + offset),
                         3 * sizeof(float));
        }
    }
    return static_cast<bool>(output);
}

ure::client::ConnectionOptions options(
    ure::client::TransportMode mode, const std::filesystem::path &runtime,
    const std::filesystem::path &worker) {
    ure::client::ConnectionOptions result;
    result.transport = mode;
    result.runtime_path = std::filesystem::absolute(runtime);
    result.worker_path = std::filesystem::absolute(worker);
    return result;
}

ure::client::SceneInput scene(const std::filesystem::path &path) {
    ure::client::SceneInput result;
    result.path = std::filesystem::absolute(path);
    return result;
}

ure::client::JobResult render(ure::client::TransportMode mode,
                              const std::filesystem::path &runtime,
                              const std::filesystem::path &worker,
                              const std::filesystem::path &scene_path) {
    auto client = ure::client::Client::connect(
        options(mode, runtime, worker));
    ure::client::Objective objective;
    objective.output_semantics = {URE_FRAME_PLANE_COLOR};
    objective.sample_budget = 2;
    auto job = client.create_job(scene(scene_path), objective);
    check(job.info().state == ure::client::JobState::Created,
          "new client job is not in Created state");
    job.start();
    check(job.wait(std::chrono::seconds(30)),
          "client product render timed out");
    return job.result();
}

void rejected_objective(ure::client::TransportMode mode,
                        const std::filesystem::path &runtime,
                        const std::filesystem::path &worker,
                        const std::filesystem::path &scene_path) {
    try {
        auto client = ure::client::Client::connect(
            options(mode, runtime, worker));
        ure::client::Objective objective;
        objective.sample_budget = 1;
        objective.latency_budget_ns = 1;
        static_cast<void>(client.create_job(scene(scene_path), objective));
        check(false, "unsupported client Objective was accepted");
    } catch (const ure::client::Error &error) {
        check(error.info().result == URE_RESULT_CAPABILITY_UNAVAILABLE,
              "unsupported client Objective returned the wrong error");
    }
}

void cancel(ure::client::TransportMode mode,
            const std::filesystem::path &runtime,
            const std::filesystem::path &worker,
            const std::filesystem::path &scene_path) {
    auto client = ure::client::Client::connect(
        options(mode, runtime, worker));
    ure::client::Objective objective;
    objective.sample_budget = 100000;
    auto job = client.create_job(scene(scene_path), objective);
    job.start();
    job.request_cancel();
    try {
        static_cast<void>(job.wait(std::chrono::seconds(30)));
        check(false, "canceled client job completed successfully");
    } catch (const ure::client::Error &error) {
        check(error.info().result == URE_RESULT_CANCELED,
              "client cancellation returned the wrong error");
        check(job.info().state == ure::client::JobState::Canceled,
              "client cancellation did not reach a terminal state");
    }
}

void negative_wait(const std::filesystem::path &runtime,
                   const std::filesystem::path &worker,
                   const std::filesystem::path &scene_path) {
    auto client = ure::client::Client::connect(
        options(ure::client::TransportMode::Direct, runtime, worker));
    ure::client::Objective objective;
    auto job = client.create_job(scene(scene_path), objective);
    try {
        static_cast<void>(job.wait(std::chrono::nanoseconds(-1)));
        check(false, "negative client wait timeout was accepted");
    } catch (const ure::client::Error &error) {
        check(error.info().result == URE_RESULT_INVALID_ARGUMENT,
              "negative client wait returned the wrong error");
    }
}

void multiple_jobs(ure::client::TransportMode mode,
                   const std::filesystem::path &runtime,
                   const std::filesystem::path &worker,
                   const std::filesystem::path &scene_path) {
    auto client = ure::client::Client::connect(
        options(mode, runtime, worker));
    for (int index = 0; index < 2; ++index) {
        ure::client::Objective objective;
        objective.sample_budget = 1;
        auto job = client.create_job(scene(scene_path), objective);
        job.start();
        check(job.wait(std::chrono::seconds(30)),
              "multiple-job client render timed out");
        const auto result = job.result();
        check(result.info.state == ure::client::JobState::Succeeded &&
                  result.info.accepted_samples == 1 &&
                  !result.frame.planes.empty(),
              "multiple-job client lifecycle is transport-dependent");
    }
}

}

int main(int argc, char **argv) {
    if (argc != 6) {
        std::cerr << "usage: test_client_transport <runtime> <worker> <scene> "
                     "<direct.pfm> <worker.pfm>\n";
        return 2;
    }
    const std::filesystem::path runtime = argv[1];
    const std::filesystem::path worker = argv[2];
    const std::filesystem::path scene_path = argv[3];
    try {
        const auto direct = render(ure::client::TransportMode::Direct, runtime,
                                   worker, scene_path);
        const auto isolated = render(ure::client::TransportMode::Worker, runtime,
                                     worker, scene_path);
        check(direct.info.state == ure::client::JobState::Succeeded &&
                  isolated.info.state == ure::client::JobState::Succeeded,
              "client transports did not report successful jobs");
        check(direct.info.accepted_samples == 2 &&
                  isolated.info.accepted_samples == 2,
              "client accepted-sample accounting is inconsistent");
        check(direct.info.identities.build == isolated.info.identities.build &&
                  direct.info.identities.snapshot ==
                      isolated.info.identities.snapshot &&
                  direct.info.identities.objective ==
                      isolated.info.identities.objective &&
                  direct.info.identities.plan == isolated.info.identities.plan,
              "client product identities differ by transport");
        check(direct.artifact.frame_content_identity ==
                  isolated.artifact.frame_content_identity &&
                  direct.artifact.rgb_value_count ==
                      isolated.artifact.rgb_value_count,
              "client artifact manifests differ by transport");
        check(direct.frame.width != 0 && direct.frame.height != 0 &&
                  direct.frame.planes.size() == 1 &&
                  isolated.frame.planes.size() == 1 &&
                  !direct.frame.planes.front().bytes.empty() &&
                  direct.frame.planes.front().bytes ==
                      isolated.frame.planes.front().bytes,
              "client frame payloads are empty or transport-dependent");
        check(write_pfm(argv[4], direct.frame) &&
                  write_pfm(argv[5], isolated.frame),
              "client transports did not publish real image artifacts");
        rejected_objective(ure::client::TransportMode::Direct, runtime, worker,
                           scene_path);
        rejected_objective(ure::client::TransportMode::Worker, runtime, worker,
                           scene_path);
        cancel(ure::client::TransportMode::Direct, runtime, worker, scene_path);
        cancel(ure::client::TransportMode::Worker, runtime, worker, scene_path);
        negative_wait(runtime, worker, scene_path);
        multiple_jobs(ure::client::TransportMode::Direct, runtime, worker,
                      scene_path);
        multiple_jobs(ure::client::TransportMode::Worker, runtime, worker,
                      scene_path);
        try {
            auto missing = options(ure::client::TransportMode::Worker, runtime,
                                   worker.parent_path() / "missing_worker.exe");
            static_cast<void>(ure::client::Client::connect(missing));
            check(false, "worker launch failure silently changed transport");
        } catch (const ure::client::Error &error) {
            check(error.info().result == URE_RESULT_WORKER_LOST,
                  "worker launch failure returned the wrong error");
        }
    } catch (const ure::client::Error &error) {
        std::cerr << "unexpected client error " << error.info().result << ": "
                  << error.what() << '\n';
        ++failures;
    }
    if (failures == 0)
        std::cout << "ure_client direct/Worker parity passed\n";
    return failures == 0 ? 0 : 1;
}
