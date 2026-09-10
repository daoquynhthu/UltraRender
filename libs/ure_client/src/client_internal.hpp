#pragma once

#include <chrono>
#include <memory>

#include <ure/client/client.hpp>
#include <ultrarender/ure_loader.h>

namespace ure::client::detail {

class JobTransport {
  public:
    virtual ~JobTransport() = default;
    virtual void start() = 0;
    virtual bool wait(std::chrono::nanoseconds timeout) = 0;
    virtual void request_cancel() = 0;
    virtual JobInfo info() const = 0;
    virtual bool poll_event(ProgressEvent &event) = 0;
    virtual bool wait_event(std::chrono::nanoseconds timeout,
                            ProgressEvent &event) = 0;
    virtual Frame latest_frame() const = 0;
    virtual JobResult result() const = 0;
    virtual OutputManifest publish_artifacts(const OutputRequest &request) = 0;
    virtual std::vector<std::uint8_t>
    copy_plane_range(const PlaneRangeRequest &request) = 0;
};

class ClientTransport {
  public:
    virtual ~ClientTransport() = default;
    virtual std::vector<DeviceInfo> devices() = 0;
    virtual SceneToolResult scene_tool(const SceneToolRequest &request) = 0;
    virtual std::shared_ptr<JobTransport>
    create_job(const SceneInput &scene, const Objective &objective) = 0;
};

std::shared_ptr<ClientTransport>
connect_direct(const ConnectionOptions &options);
std::shared_ptr<ClientTransport>
connect_worker(const ConnectionOptions &options);

[[noreturn]] void throw_error(ure_result_t result, std::uint32_t domain,
                              std::uint32_t detail, std::string message);

JobState job_state(std::uint32_t state);

bool decode_error_detail(ErrorInfo &info) noexcept;

}
