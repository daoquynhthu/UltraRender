#include <ure/client/client.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

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

bool equivalent_frames(const ure::client::Frame &left,
                       const ure::client::Frame &right) {
    if (left.width != right.width || left.height != right.height ||
        left.planes.size() != right.planes.size() || left.planes.empty())
        return false;
    bool nontrivial = false;
    for (std::size_t plane = 0; plane < left.planes.size(); ++plane) {
        const auto &a = left.planes[plane];
        const auto &b = right.planes[plane];
        if (a.semantic != b.semantic || a.scalar_type != b.scalar_type ||
            a.component_layout != b.component_layout ||
            a.width != b.width || a.height != b.height ||
            a.depth != b.depth || a.row_stride != b.row_stride ||
            a.slice_stride != b.slice_stride ||
            a.element_stride != b.element_stride ||
            a.bytes.size() != b.bytes.size() ||
            a.bytes.size() % sizeof(float) != 0)
            return false;
        for (std::size_t offset = 0; offset < a.bytes.size();
             offset += sizeof(float)) {
            float av{};
            float bv{};
            std::memcpy(&av, a.bytes.data() + offset, sizeof(av));
            std::memcpy(&bv, b.bytes.data() + offset, sizeof(bv));
            if (!std::isfinite(av) || !std::isfinite(bv) ||
                std::abs(av - bv) >
                    1.0e-6f * (1.0f + std::max(std::abs(av), std::abs(bv))))
                return false;
            nontrivial = nontrivial || std::abs(av) > 1.0e-6f;
        }
    }
    return nontrivial;
}

class CurrentPathGuard {
  public:
    explicit CurrentPathGuard(const std::filesystem::path &path)
        : original_(std::filesystem::current_path()) {
        std::filesystem::current_path(path);
    }

    ~CurrentPathGuard() {
        std::error_code error;
        std::filesystem::current_path(original_, error);
    }

  private:
    std::filesystem::path original_;
};

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
    if (path.extension() == ".ure")
        result.format = ure::client::SceneFormat::Ure;
    else if (path.extension() == ".urepkg")
        result.format = ure::client::SceneFormat::UrePackage;
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

std::vector<ure::client::DeviceInfo>
devices(ure::client::TransportMode mode,
        const std::filesystem::path &runtime,
        const std::filesystem::path &worker) {
    auto client = ure::client::Client::connect(options(mode, runtime, worker));
    return client.devices();
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

ure::client::ErrorInfo
rejected_memory(ure::client::TransportMode mode,
                const std::filesystem::path &runtime,
                const std::filesystem::path &worker,
                const std::filesystem::path &scene_path) {
    try {
        auto client = ure::client::Client::connect(
            options(mode, runtime, worker));
        ure::client::Objective objective;
        objective.sample_budget = 1;
        objective.memory_budget_bytes = UINT64_C(1048576);
        static_cast<void>(client.create_job(scene(scene_path), objective));
        check(false, "memory-inapplicable client Objective was accepted");
    } catch (const ure::client::Error &error) {
        check(error.info().result == URE_RESULT_BUDGET_EXHAUSTED &&
                  error.info().domain == URE_ERROR_DOMAIN_CORE &&
                  error.info().detail == 543 &&
                  error.info().structured_detail_schema == URE_PAYLOAD_ERROR &&
                  !error.info().structured_detail.empty() &&
                  std::ranges::any_of(
                      error.info().correlation_identity,
                      [](std::uint8_t value) { return value != 0; }) &&
                  error.info().retryability == 1 &&
                  !error.info().recovery_hint.empty(),
              "memory preflight classification differs by client transport");
        if (mode == ure::client::TransportMode::Worker)
            check(error.info().transport_correlation_id != 0,
                  "Worker error lost its transport correlation identity");
        return error.info();
    }
    return {};
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
    try {
        static_cast<void>(job.latest_frame());
        check(false, "client job exposed a frame before publication");
    } catch (const ure::client::Error &error) {
        check(error.info().result == URE_RESULT_INCOMPLETE &&
                  error.info().domain == URE_ERROR_DOMAIN_CORE &&
                  error.info().detail == 529,
              "unpublished frame classification differs by transport: " +
                  std::to_string(static_cast<int>(mode)) + "/" +
                  std::to_string(error.info().result) + "/" +
                  std::to_string(error.info().domain) + "/" +
                  std::to_string(error.info().detail));
    }
    job.start();
    ure::client::ProgressEvent progressive;
    const auto frame_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    do {
        static_cast<void>(job.wait_event(std::chrono::milliseconds(100),
                                         progressive));
    } while (progressive.latest_frame_generation == 0 &&
             std::chrono::steady_clock::now() < frame_deadline);
    check(progressive.sequence != 0 &&
              progressive.stage == URE_PRODUCT_STAGE_PRODUCTION &&
              progressive.completed_samples <= progressive.accepted_samples &&
              progressive.latest_frame_generation != 0,
          "client job exposed no monotonic progressive frame event");
    const auto progressive_frame = job.latest_frame();
    const auto after_frame = job.info();
    check(progressive_frame.sample_count != 0 &&
              progressive_frame.sample_count <= after_frame.completed_samples &&
              !progressive_frame.planes.empty(),
          "client job could not acquire an immutable progressive frame");
    const auto progress_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (job.info().completed_samples == 0 &&
           std::chrono::steady_clock::now() < progress_deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    check(job.info().completed_samples > 0,
          "client job exposed no in-flight progress before cancellation");
    const auto cancel_started = std::chrono::steady_clock::now();
    job.request_cancel();
    try {
        static_cast<void>(job.wait(std::chrono::seconds(30)));
        check(false, "canceled client job completed successfully");
    } catch (const ure::client::Error &error) {
        check(error.info().result == URE_RESULT_CANCELED,
              "client cancellation returned the wrong error");
        check(job.info().state == ure::client::JobState::Canceled,
              "client cancellation did not reach a terminal state");
        job.request_cancel();
        check(job.info().state == ure::client::JobState::Canceled,
              "repeated client cancellation changed terminal state");
        check(std::chrono::steady_clock::now() - cancel_started <
                  std::chrono::seconds(5),
              "client cancellation exceeded the bounded work quantum");
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
                  result.info.completed_samples == 1 &&
                  !result.frame.planes.empty(),
              "multiple-job client lifecycle is transport-dependent");
    }
}

void concurrent_worker_control(const std::filesystem::path &runtime,
                               const std::filesystem::path &worker,
                               const std::filesystem::path &scene_path) {
    auto client = ure::client::Client::connect(
        options(ure::client::TransportMode::Worker, runtime, worker));
    ure::client::Objective objective;
    objective.sample_budget = 100000;
    auto job = client.create_job(scene(scene_path), objective);
    const auto created = job.info();
    check(created.requested_samples == objective.sample_budget &&
              created.accepted_samples == objective.sample_budget &&
              created.completed_samples == 0,
          "worker did not separate requested, accepted, and completed work");
    job.start();
    std::atomic<int> wait_outcome{};
    std::jthread waiter([&] {
        try {
            wait_outcome.store(job.wait(std::chrono::seconds(30)) ? 1 : 4,
                               std::memory_order_release);
        } catch (const ure::client::Error &error) {
            wait_outcome.store(error.info().result == URE_RESULT_CANCELED ? 2
                                                                          : 3,
                               std::memory_order_release);
        }
    });
    std::uint64_t previous_completed{};
    for (int probe = 0; probe < 8; ++probe) {
        const auto started = std::chrono::steady_clock::now();
        const auto progress = job.info();
        check(std::chrono::steady_clock::now() - started <
                  std::chrono::seconds(1),
              "worker control probe was blocked by long-running work");
        check(progress.completed_samples >= previous_completed &&
                  progress.completed_samples <= progress.accepted_samples,
              "worker progress was non-monotonic or exceeded accepted work");
        previous_completed = progress.completed_samples;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const auto cancel_started = std::chrono::steady_clock::now();
    job.request_cancel();
    waiter.join();
    const auto terminal = job.info();
    check(wait_outcome.load(std::memory_order_acquire) == 2 &&
              terminal.state == ure::client::JobState::Canceled &&
              terminal.completed_samples <= terminal.accepted_samples &&
              std::chrono::steady_clock::now() - cancel_started <
                  std::chrono::seconds(5),
          "worker wait, poll, and cancel control did not remain preemptible");
}

void bounded_worker_sessions(const std::filesystem::path &runtime,
                             const std::filesystem::path &worker,
                             const std::filesystem::path &scene_path) {
    auto connection = options(ure::client::TransportMode::Worker, runtime,
                              worker);
    connection.max_worker_sessions = 1;
    auto client = ure::client::Client::connect(connection);
    ure::client::Objective objective;
    objective.sample_budget = 1;
    auto first = client.create_job(scene(scene_path), objective);
    try {
        static_cast<void>(client.create_job(scene(scene_path), objective));
        check(false, "worker accepted an unbounded concurrent session");
    } catch (const ure::client::Error &error) {
        check(error.info().result == URE_RESULT_BACKPRESSURE,
              "worker session limit returned the wrong result");
    }
    first = {};
    auto next = client.create_job(scene(scene_path), objective);
    next.start();
    check(next.wait(std::chrono::seconds(30)) &&
              next.result().info.completed_samples == 1,
          "worker session capacity was not restored after release");

    connection.max_worker_sessions = 0;
    try {
        static_cast<void>(ure::client::Client::connect(connection));
        check(false, "worker accepted an invalid session limit");
    } catch (const ure::client::Error &error) {
        check(error.info().result == URE_RESULT_INVALID_ARGUMENT,
              "invalid worker session limit returned the wrong result");
    }
}

void sample_precedence(ure::client::TransportMode mode,
                       const std::filesystem::path &runtime,
                       const std::filesystem::path &worker,
                       const std::filesystem::path &scene_path) {
    auto client = ure::client::Client::connect(
        options(mode, runtime, worker));
    ure::client::Objective inherited;
    {
        auto job = client.create_job(scene(scene_path), inherited);
        const auto info = job.info();
        check(info.requested_samples == 8 && info.accepted_samples == 8 &&
                  info.completed_samples == 0,
              "scene spp was not used as the unspecified product default");
    }
    ure::client::Objective explicit_samples;
    explicit_samples.sample_budget = 3;
    {
        auto job = client.create_job(scene(scene_path), explicit_samples);
        const auto info = job.info();
        check(info.requested_samples == 3 && info.accepted_samples == 3 &&
                  info.completed_samples == 0,
              "explicit product samples did not override scene and simulation spp");
    }
}

}

int main(int argc, char **argv) {
    if (argc != 6) {
        std::cerr << "usage: test_client_transport <runtime> <worker> <scene> "
                     "<direct.pfm> <worker.pfm>\n";
        return 2;
    }
    const std::filesystem::path runtime = std::filesystem::absolute(argv[1]);
    const std::filesystem::path worker = std::filesystem::absolute(argv[2]);
    const std::filesystem::path scene_path = std::filesystem::absolute(argv[3]);
    const std::filesystem::path direct_output =
        std::filesystem::absolute(argv[4]);
    const std::filesystem::path worker_output =
        std::filesystem::absolute(argv[5]);
    const auto isolated_cwd = std::filesystem::temp_directory_path() /
        "ultrarender_client_transport_cwd";
    std::filesystem::create_directories(isolated_cwd);
    try {
        CurrentPathGuard cwd_guard(isolated_cwd);
        const auto direct_devices = devices(
            ure::client::TransportMode::Direct, runtime, worker);
        const auto worker_devices = devices(
            ure::client::TransportMode::Worker, runtime, worker);
        const auto direct_cuda = std::ranges::find_if(
            direct_devices, [](const ure::client::DeviceInfo &device) {
                return device.backend == URE_BACKEND_CUDA &&
                       device.runtime_state == URE_RUNTIME_STATE_APPLICABLE;
            });
        const auto worker_cuda = std::ranges::find_if(
            worker_devices, [](const ure::client::DeviceInfo &device) {
                return device.backend == URE_BACKEND_CUDA &&
                       device.runtime_state == URE_RUNTIME_STATE_APPLICABLE;
            });
        check(direct_cuda != direct_devices.end() &&
                  worker_cuda != worker_devices.end() &&
                  direct_cuda->identity == worker_cuda->identity &&
                  direct_cuda->provider == worker_cuda->provider &&
                  direct_cuda->name == worker_cuda->name &&
                  direct_cuda->adapter_id == worker_cuda->adapter_id &&
                  direct_cuda->total_memory_bytes ==
                      worker_cuda->total_memory_bytes &&
                  direct_cuda->available_memory_bytes <=
                      direct_cuda->total_memory_bytes &&
                  worker_cuda->available_memory_bytes <=
                      worker_cuda->total_memory_bytes,
              "device inventory identity differs by client transport");
        const auto direct = render(ure::client::TransportMode::Direct, runtime,
                                   worker, scene_path);
        const auto isolated = render(ure::client::TransportMode::Worker, runtime,
                                     worker, scene_path);
        check(direct.info.state == ure::client::JobState::Succeeded &&
                  isolated.info.state == ure::client::JobState::Succeeded,
              "client transports did not report successful jobs");
        check(direct.info.accepted_samples == 2 &&
                  isolated.info.accepted_samples == 2 &&
                  direct.info.completed_samples == 2 &&
                  isolated.info.completed_samples == 2,
              "client accepted-sample accounting is inconsistent");
        check(direct.info.identities.build == isolated.info.identities.build &&
                  direct.info.identities.snapshot ==
                      isolated.info.identities.snapshot &&
                  direct.info.identities.objective ==
                      isolated.info.identities.objective &&
                  direct.info.identities.plan == isolated.info.identities.plan,
              "client product identities differ by transport");
        check(direct.info.execution.backend == URE_BACKEND_CUDA &&
                  direct.info.execution.provider ==
                      URE_PROVIDER_SELF_COMPUTE &&
                  direct.info.execution.runtime_state ==
                      URE_RUNTIME_STATE_APPLICABLE &&
                  direct.info.execution.device_identity ==
                      isolated.info.execution.device_identity &&
                  direct.info.execution.device_identity ==
                      direct_cuda->identity &&
                  direct.info.execution.plan_identity ==
                      direct.info.identities.plan &&
                  isolated.info.execution.plan_identity ==
                      isolated.info.identities.plan &&
                  direct.info.execution.name ==
                      isolated.info.execution.name,
              "selected execution identity differs by client transport");
        check(direct.info.eligible_integrator_modes ==
                  isolated.info.eligible_integrator_modes &&
                  direct.info.qualified_integrator_modes ==
                      isolated.info.qualified_integrator_modes &&
                  direct.info.executed_integrator_modes ==
                      isolated.info.executed_integrator_modes,
              "integrator applicability and execution reports differ by client transport");
        check((direct.info.eligible_integrator_modes & UINT64_C(1)) != 0 &&
                  (direct.info.qualified_integrator_modes & UINT64_C(1)) != 0 &&
                  (direct.info.executed_integrator_modes & UINT64_C(1)) != 0 &&
                  (direct.info.qualified_integrator_modes &
                   ~direct.info.eligible_integrator_modes) == 0 &&
                  (direct.info.executed_integrator_modes &
                   ~direct.info.qualified_integrator_modes) == 0,
              "integrator report masks violate eligibility/qualification/execution containment");
        check(direct.artifact.rgb_value_count ==
                      isolated.artifact.rgb_value_count,
              "client artifact layouts differ by transport");
        check(direct.frame.width != 0 && direct.frame.height != 0 &&
                  direct.frame.planes.size() == 1 &&
                  isolated.frame.planes.size() == 1 &&
                  !direct.frame.planes.front().bytes.empty() &&
                  equivalent_frames(direct.frame, isolated.frame),
              "client frame payloads are empty or transport-dependent");
        check(write_pfm(direct_output, direct.frame) &&
                  write_pfm(worker_output, isolated.frame),
              "client transports did not publish real image artifacts");
        rejected_objective(ure::client::TransportMode::Direct, runtime, worker,
                           scene_path);
        rejected_objective(ure::client::TransportMode::Worker, runtime, worker,
                           scene_path);
        const auto direct_memory = rejected_memory(
            ure::client::TransportMode::Direct, runtime, worker, scene_path);
        const auto worker_memory = rejected_memory(
            ure::client::TransportMode::Worker, runtime, worker, scene_path);
        check(direct_memory.result == worker_memory.result &&
                  direct_memory.domain == worker_memory.domain &&
                  direct_memory.detail == worker_memory.detail &&
                  direct_memory.retryability == worker_memory.retryability &&
                  direct_memory.recovery_hint == worker_memory.recovery_hint &&
                  direct_memory.cause_depth == worker_memory.cause_depth,
              "structured diagnostic semantics differ by transport");
        cancel(ure::client::TransportMode::Direct, runtime, worker, scene_path);
        cancel(ure::client::TransportMode::Worker, runtime, worker, scene_path);
        negative_wait(runtime, worker, scene_path);
        multiple_jobs(ure::client::TransportMode::Direct, runtime, worker,
                      scene_path);
        multiple_jobs(ure::client::TransportMode::Worker, runtime, worker,
                      scene_path);
        concurrent_worker_control(runtime, worker, scene_path);
        bounded_worker_sessions(runtime, worker, scene_path);
        sample_precedence(ure::client::TransportMode::Direct, runtime, worker,
                          scene_path);
        sample_precedence(ure::client::TransportMode::Worker, runtime, worker,
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
    std::filesystem::remove_all(isolated_cwd);
    if (failures == 0)
        std::cout << "ure_client direct/Worker parity passed\n";
    return failures == 0 ? 0 : 1;
}
