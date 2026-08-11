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
    virtual JobResult result() const = 0;
};

class ClientTransport {
  public:
    virtual ~ClientTransport() = default;
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

}
