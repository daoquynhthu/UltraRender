#include <ure/client/client.hpp>

#include <memory>
#include <utility>

#include "client_internal.hpp"

namespace ure::client {

class Job::Impl final {
  public:
    explicit Impl(std::shared_ptr<detail::JobTransport> transport)
        : transport_(std::move(transport)) {}

    std::shared_ptr<detail::JobTransport> transport_;
};

class Client::Impl final {
  public:
    Impl(TransportMode mode,
         std::shared_ptr<detail::ClientTransport> transport)
        : mode_(mode), transport_(std::move(transport)) {}

    TransportMode mode_;
    std::shared_ptr<detail::ClientTransport> transport_;
};

Error::Error(ErrorInfo info)
    : std::runtime_error(info.message), info_(std::move(info)) {}

const ErrorInfo &Error::info() const noexcept { return info_; }

Job::Job() = default;
Job::~Job() = default;
Job::Job(Job &&) noexcept = default;
Job &Job::operator=(Job &&) noexcept = default;
Job::Job(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}

void Job::start() {
    if (!impl_)
        detail::throw_error(URE_RESULT_INVALID_HANDLE, URE_ERROR_DOMAIN_CORE, 1,
                            "client job is empty");
    impl_->transport_->start();
}

bool Job::wait(std::chrono::nanoseconds timeout) {
    if (!impl_)
        detail::throw_error(URE_RESULT_INVALID_HANDLE, URE_ERROR_DOMAIN_CORE, 2,
                            "client job is empty");
    if (timeout.count() < 0)
        detail::throw_error(URE_RESULT_INVALID_ARGUMENT, URE_ERROR_DOMAIN_CORE,
                            7, "client wait timeout cannot be negative");
    return impl_->transport_->wait(timeout);
}

void Job::request_cancel() {
    if (!impl_)
        detail::throw_error(URE_RESULT_INVALID_HANDLE, URE_ERROR_DOMAIN_CORE, 3,
                            "client job is empty");
    impl_->transport_->request_cancel();
}

JobInfo Job::info() const {
    if (!impl_)
        detail::throw_error(URE_RESULT_INVALID_HANDLE, URE_ERROR_DOMAIN_CORE, 4,
                            "client job is empty");
    return impl_->transport_->info();
}

JobResult Job::result() const {
    if (!impl_)
        detail::throw_error(URE_RESULT_INVALID_HANDLE, URE_ERROR_DOMAIN_CORE, 5,
                            "client job is empty");
    return impl_->transport_->result();
}

Job::operator bool() const noexcept { return static_cast<bool>(impl_); }

Client::Client() = default;
Client::~Client() = default;
Client::Client(Client &&) noexcept = default;
Client &Client::operator=(Client &&) noexcept = default;
Client::Client(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}

Client Client::connect(const ConnectionOptions &options) {
    auto transport = options.transport == TransportMode::Direct
                         ? detail::connect_direct(options)
                         : detail::connect_worker(options);
    return Client(std::make_shared<Impl>(options.transport,
                                         std::move(transport)));
}

Job Client::create_job(const SceneInput &scene, const Objective &objective) {
    if (!impl_)
        detail::throw_error(URE_RESULT_INVALID_HANDLE, URE_ERROR_DOMAIN_CORE, 6,
                            "client is not connected");
    return Job(std::make_shared<Job::Impl>(
        impl_->transport_->create_job(scene, objective)));
}

TransportMode Client::transport() const noexcept {
    return impl_ ? impl_->mode_ : TransportMode::Worker;
}

Client::operator bool() const noexcept { return static_cast<bool>(impl_); }

std::span<const std::uint8_t, 32> registry_digest() noexcept {
    static constexpr std::array<std::uint8_t, 32> digest =
        URE_REGISTRY_DIGEST_BYTES;
    return digest;
}

}

namespace ure::client::detail {

[[noreturn]] void throw_error(ure_result_t result, std::uint32_t domain,
                              std::uint32_t detail, std::string message) {
    throw Error({result, domain, detail, std::move(message)});
}

JobState job_state(std::uint32_t state) {
    switch (state) {
    case URE_OPERATION_STATE_QUEUED:
        return JobState::Queued;
    case URE_OPERATION_STATE_RUNNING:
        return JobState::Running;
    case URE_OPERATION_STATE_CANCEL_PENDING:
        return JobState::CancelPending;
    case URE_OPERATION_STATE_SUCCEEDED:
        return JobState::Succeeded;
    case URE_OPERATION_STATE_CANCELED:
        return JobState::Canceled;
    case URE_OPERATION_STATE_FAILED:
        return JobState::Failed;
    case URE_OPERATION_STATE_DEVICE_LOST:
        return JobState::DeviceLost;
    default:
        return JobState::Created;
    }
}

}
