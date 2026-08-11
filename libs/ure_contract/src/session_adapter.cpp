#include "scene_adapter.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <ure/native_scene_hash.hpp>
#include <ure/product/product_service.hpp>

namespace ure::contract {
namespace {

using Digest = product::Identity;
using ObjectiveData = product::ProductObjective;

struct SessionObject final : Object {
    ~SessionObject() override {
        if (latest_frame)
            frame_interface().release(latest_frame, nullptr);
        if (active_operation)
            operation_interface().release(active_operation, nullptr);
    }

    std::mutex mutex;
    std::shared_ptr<InstanceObject> instance;
    std::shared_ptr<const SceneRevisionData> revision;
    std::unique_ptr<product::ProductJob> job;
    ObjectiveData objective;
    std::optional<product::ProductArtifactManifest> latest_artifact;
    ure_handle_t active_operation{};
    ure_handle_t latest_frame{};
    std::uint64_t completed_samples{};
    std::uint32_t state{URE_SESSION_STATE_CREATED};
    std::uint32_t reset_reason{URE_SCENE_RESET_FULL_REPLACEMENT};
};

Digest digest_from_hex(std::string_view text) {
    Digest output{};
    if (text.size() != 64)
        throw std::invalid_argument("invalid SHA-256 text");
    const auto nibble = [](char character) -> std::uint8_t {
        if (character >= '0' && character <= '9')
            return static_cast<std::uint8_t>(character - '0');
        if (character >= 'a' && character <= 'f')
            return static_cast<std::uint8_t>(character - 'a' + 10);
        throw std::invalid_argument("invalid SHA-256 text");
    };
    for (std::size_t index = 0; index < output.size(); ++index)
        output[index] = static_cast<std::uint8_t>(
            nibble(text[index * 2]) * 16U + nibble(text[index * 2 + 1]));
    return output;
}

Digest hash(std::span<const std::uint8_t> bytes) {
    return digest_from_hex(native_scene::sha256_hex(bytes));
}

Digest objective_identity(const ure_objective_envelope_t &objective) {
    std::vector<std::uint8_t> bytes;
    const auto append = [&bytes](const auto &value) {
        const auto *begin = reinterpret_cast<const std::uint8_t *>(&value);
        bytes.insert(bytes.end(), begin, begin + sizeof(value));
    };
    append(objective.payload_schema);
    append(objective.payload_version_major);
    append(objective.payload_version_minor);
    append(objective.determinism_policy);
    append(objective.usage_policy);
    append(objective.wall_time_budget_ns);
    append(objective.memory_budget_bytes);
    append(objective.sample_budget);
    append(objective.latency_budget_ns);
    for (std::uint32_t index = 0; index < objective.output_count; ++index)
        append(objective.output_semantics[index]);
    if (objective.payload.size != 0)
        bytes.insert(bytes.end(), objective.payload.data,
                     objective.payload.data + objective.payload.size);
    constexpr std::string_view domain = "UltraRender.RenderObjective.v1";
    bytes.insert(bytes.begin(), 0);
    bytes.insert(bytes.begin(), domain.begin(), domain.end());
    return hash(bytes);
}

bool digest_zero(const ure_digest256_t &digest) noexcept {
    return std::ranges::all_of(digest.bytes,
                               [](std::uint8_t value) { return value == 0; });
}

ure_result_t decode_objective(const ure_objective_envelope_t *objective,
                              ObjectiveData &output,
                              std::string &message) {
    bool conformance_device_loss = false;
#if defined(URE_CONTRACT_CONFORMANCE)
    conformance_device_loss =
        objective && objective->payload_schema == URE_PRIVATE_OBJECTIVE_DEVICE_LOSS &&
        objective->payload_version_major == 1 &&
        objective->payload_version_minor == 0;
#endif
    if (!valid_input(objective, URE_STRUCTURE_OBJECTIVE_ENVELOPE) ||
        objective->reserved[0] != 0 || objective->reserved[1] != 0 ||
        objective->output_count > 64 ||
        (objective->output_count != 0 && !objective->output_semantics) ||
        objective->sample_budget > UINT64_C(1048576) ||
        objective->memory_budget_bytes > UINT64_C(8589934592) ||
        objective->payload.size > UINT64_C(1048576) ||
        (objective->payload.size != 0 && !objective->payload.data) ||
        (!conformance_device_loss &&
         (objective->payload_schema != 0 || objective->payload_version_major != 0 ||
          objective->payload_version_minor != 0)) ||
        (conformance_device_loss && objective->payload.size != 0))
        return URE_RESULT_INVALID_ARGUMENT;
    if (objective->payload.size == 0) {
        if (!digest_zero(objective->payload_digest))
            return URE_RESULT_INVALID_ARGUMENT;
    } else {
        const auto payload = hash(std::span(objective->payload.data,
                                            static_cast<std::size_t>(objective->payload.size)));
        if (std::memcmp(payload.data(), objective->payload_digest.bytes,
                        payload.size()) != 0)
            return URE_RESULT_INVALID_ARGUMENT;
    }
    if (objective->determinism_policy != 0) {
        message = "determinism policy is not executable in Product 0.1";
        return URE_RESULT_CAPABILITY_UNAVAILABLE;
    }
    if (objective->usage_policy != 0) {
        message = "usage policy is not executable in Product 0.1";
        return URE_RESULT_CAPABILITY_UNAVAILABLE;
    }
    if (objective->latency_budget_ns != 0) {
        message = "latency budget is not executable in Product 0.1";
        return URE_RESULT_CAPABILITY_UNAVAILABLE;
    }
    if (objective->wall_time_budget_ns != 0 &&
        objective->wall_time_budget_ns % UINT64_C(1000000) != 0) {
        message = "wall-time budget must be expressed in whole milliseconds";
        return URE_RESULT_CAPABILITY_UNAVAILABLE;
    }
    if (objective->memory_budget_bytes != 0 &&
        objective->memory_budget_bytes % UINT64_C(1048576) != 0) {
        message = "memory budget must be expressed in whole mebibytes";
        return URE_RESULT_CAPABILITY_UNAVAILABLE;
    }
    for (std::uint32_t index = 0; index < objective->output_count; ++index) {
        if (objective->output_semantics[index] != URE_FRAME_PLANE_COLOR) {
            message = "requested output semantic is not executable in Product 0.1";
            return URE_RESULT_CAPABILITY_UNAVAILABLE;
        }
    }
    output.identity = objective_identity(*objective);
    output.wall_time_budget_ns = objective->wall_time_budget_ns;
    output.memory_budget_bytes = objective->memory_budget_bytes;
    output.requested_samples = objective->sample_budget == 0 ? 1 : objective->sample_budget;
    output.latency_budget_ns = objective->latency_budget_ns;
    output.determinism_policy = objective->determinism_policy;
    output.usage_policy = objective->usage_policy;
    if (objective->output_count != 0)
        output.output_semantics.assign(
            objective->output_semantics,
            objective->output_semantics + objective->output_count);
    output.force_device_loss = conformance_device_loss;
    return URE_RESULT_SUCCESS;
}

void store(ure_digest256_t &output, const Digest &value) noexcept {
    std::memcpy(output.bytes, value.data(), value.size());
}

ure_digest256_t public_digest(const Digest &value) noexcept {
    ure_digest256_t output{};
    store(output, value);
    return output;
}

bool operation_nonterminal(ure_handle_t handle) {
    if (!handle)
        return false;
    const auto operation =
        handles().get<OperationObject>(handle, ObjectType::Operation, true);
    if (!operation)
        return false;
    std::scoped_lock lock(operation->mutex);
    return !terminal(operation->state);
}

void finish_operation(const std::shared_ptr<OperationObject> &operation,
                      ure_handle_t operation_handle, std::uint32_t state,
                      ure_result_t result, std::uint32_t detail,
                      std::string message) noexcept {
    {
        std::scoped_lock lock(operation->mutex);
        operation->state = state;
        if (result != URE_RESULT_SUCCESS)
            make_error(result, detail, std::move(message),
                       &operation->terminal_error, nullptr, operation_handle);
        ++operation->progress_sequence;
        operation->changed.notify_all();
    }
    emit_event(operation->instance,
               state == URE_OPERATION_STATE_DEVICE_LOST
                   ? URE_EVENT_DEVICE_LOST
                   : URE_EVENT_OPERATION_STATE,
               operation_handle);
}

void run_render(const std::shared_ptr<SessionObject> &session,
                const std::shared_ptr<OperationObject> &operation,
                ure_handle_t operation_handle) noexcept {
    try {
        session->job->begin();
        {
            std::scoped_lock lock(operation->mutex);
            operation->state = URE_OPERATION_STATE_RUNNING;
            ++operation->progress_sequence;
            operation->changed.notify_all();
        }
        emit_event(operation->instance, URE_EVENT_OPERATION_STATE,
                   operation_handle);
        const auto started = std::chrono::steady_clock::now();
        for (std::uint64_t sample = 0; sample < operation->steps; ++sample) {
            {
                std::unique_lock lock(operation->mutex);
                operation->changed.wait(lock, [&] {
                    return operation->state != URE_OPERATION_STATE_PAUSED ||
                           operation->cancel_requested;
                });
                if (operation->cancel_requested ||
                    operation->instance->closed.load(std::memory_order_acquire)) {
                    lock.unlock();
                    session->job->cancel();
                    finish_operation(operation, operation_handle,
                                     URE_OPERATION_STATE_CANCELED,
                                     URE_RESULT_CANCELED, 500,
                                     "render session was canceled");
                    return;
                }
            }
            session->job->render_sample();
            const auto progress = session->job->operation();
            {
                std::scoped_lock lock(operation->mutex, session->mutex);
                operation->completed = progress.accepted_samples;
                ++operation->progress_sequence;
                session->completed_samples = progress.accepted_samples;
            }
            if (session->objective.wall_time_budget_ns != 0 &&
                static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - started)
                        .count()) >= session->objective.wall_time_budget_ns)
                break;
        }
        const auto product_frame = session->job->publish_frame();
        const auto artifact = session->job->artifact_manifest(product_frame);
        ure_handle_t frame{};
        ure_handle_t frame_error{};
        const ure_digest256_t scene_identity =
            public_digest(product_frame.identities.snapshot);
        const ure_digest256_t objective =
            public_digest(product_frame.identities.objective);
        const ure_result_t frame_result = create_frame_snapshot(
            session->owner, operation_handle, scene_identity, objective,
            artifact.accepted_samples, product_frame.width,
            product_frame.height, product_frame.rgb.data(),
            product_frame.rgb.size(), &frame,
            &frame_error);
        if (frame_result != URE_RESULT_SUCCESS) {
            std::string message = "rendered frame snapshot failed";
            if (frame_error) {
                const auto frame_error_object =
                    handles().get<ErrorObject>(frame_error, ObjectType::Error);
                if (frame_error_object)
                    message = frame_error_object->message;
                release_error(frame_error);
            }
            {
                std::scoped_lock lock(session->mutex);
                session->state = URE_SESSION_STATE_FAILED;
            }
            finish_operation(operation, operation_handle,
                             URE_OPERATION_STATE_FAILED, frame_result, 501,
                             std::move(message));
            return;
        }
        ure_handle_t old_frame{};
        {
            std::scoped_lock lock(session->mutex);
            old_frame = std::exchange(session->latest_frame, frame);
            session->latest_artifact = artifact;
            session->state = URE_SESSION_STATE_READY;
        }
        if (old_frame)
            frame_interface().release(old_frame, nullptr);
        finish_operation(operation, operation_handle,
                         URE_OPERATION_STATE_SUCCEEDED, URE_RESULT_SUCCESS, 0,
                         {});
    } catch (const std::exception &exception) {
        session->job->fail();
        std::string message = exception.what();
        std::string lower = message;
        std::ranges::transform(lower, lower.begin(), [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });
        const bool device_lost = lower.find("device lost") != std::string::npos ||
                                 lower.find("cudaerrorunknown") != std::string::npos;
        {
            std::scoped_lock lock(session->mutex);
            session->state = device_lost ? URE_SESSION_STATE_DEVICE_LOST
                                         : URE_SESSION_STATE_FAILED;
        }
        finish_operation(operation, operation_handle,
                         device_lost ? URE_OPERATION_STATE_DEVICE_LOST
                                     : URE_OPERATION_STATE_FAILED,
                         device_lost ? URE_RESULT_DEVICE_LOST
                                     : URE_RESULT_INTERNAL,
                         502, "render session execution failed: " + message);
    } catch (...) {
        session->job->fail();
        {
            std::scoped_lock lock(session->mutex);
            session->state = URE_SESSION_STATE_FAILED;
        }
        finish_operation(operation, operation_handle,
                         URE_OPERATION_STATE_FAILED, URE_RESULT_INTERNAL, 503,
                         "render session execution failed");
    }
}

ure_result_t create_impl(ure_handle_t instance_handle, ure_handle_t scene_handle,
                         const ure_objective_envelope_t *objective,
                         ure_handle_t *output, ure_handle_t *error) {
    clear_error(error);
    if (output)
        *output = nullptr;
    const auto instance =
        handles().get<InstanceObject>(instance_handle, ObjectType::Instance);
    const auto scene = handles().get<SceneObject>(scene_handle, ObjectType::Scene);
    ObjectiveData objective_data;
    std::string objective_message;
    if (!instance || !scene || scene->owner != instance_handle)
        return make_error(URE_RESULT_INVALID_HANDLE, 504,
                          "invalid session parent handle", error);
    if (!instance->session_enabled || !output)
        return make_error(URE_RESULT_INVALID_ARGUMENT, 505,
                          "invalid render objective", error);
    const ure_result_t objective_result =
        decode_objective(objective, objective_data, objective_message);
    if (objective_result != URE_RESULT_SUCCESS)
        return make_error(objective_result, 505,
                          objective_message.empty() ? "invalid render objective"
                                                    : objective_message,
                          error);
    const auto revision = scene_revision(scene_handle, error);
    if (!revision)
        return URE_RESULT_INVALID_HANDLE;
    std::unique_ptr<product::ProductJob> job;
    try {
        job = product::ProductJob::create(
            revision->archive, revision->revision_identity, objective_data);
    } catch (const std::exception &exception) {
        return make_error(URE_RESULT_INTERNAL, 505,
                          "renderer scene binding failed: " +
                              std::string(exception.what()),
                          error);
    }
    auto session = std::make_shared<SessionObject>();
    session->type = ObjectType::Session;
    session->owner = instance_handle;
    session->parent = scene_handle;
    session->thread_policy = URE_THREAD_POLICY_EXTERNALLY_SYNCHRONIZED;
    session->instance = instance;
    session->revision = revision;
    session->job = std::move(job);
    session->objective = objective_data;
    session->state = URE_SESSION_STATE_READY;
    *output = handles().insert(session);
    return URE_RESULT_SUCCESS;
}

ure_result_t retain_impl(ure_handle_t session, ure_handle_t *error) {
    clear_error(error);
    return handles().retain(session, ObjectType::Session)
               ? URE_RESULT_SUCCESS
               : make_error(URE_RESULT_INVALID_HANDLE, 506,
                            "invalid session handle", error);
}

ure_result_t release_impl(ure_handle_t session_handle, ure_handle_t *error) {
    clear_error(error);
    const auto session =
        handles().get<SessionObject>(session_handle, ObjectType::Session, true);
    if (!session)
        return make_error(URE_RESULT_INVALID_HANDLE, 507,
                          "invalid session handle", error);
    if (handles().reference_count(session_handle, ObjectType::Session) == 1 &&
        operation_nonterminal(session->active_operation))
        return make_error(URE_RESULT_BUSY, 508,
                          "session has active work", error);
    return handles().release(session_handle, ObjectType::Session)
               ? URE_RESULT_SUCCESS
               : make_error(URE_RESULT_INVALID_HANDLE, 507,
                            "invalid session handle", error);
}

ure_result_t close_impl(ure_handle_t session_handle, ure_handle_t *error) {
    clear_error(error);
    const auto session =
        handles().get<SessionObject>(session_handle, ObjectType::Session, true);
    if (!session)
        return make_error(URE_RESULT_INVALID_HANDLE, 509,
                          "invalid session handle", error);
    ure_handle_t operation{};
    {
        std::scoped_lock lock(session->mutex);
        operation = session->active_operation;
    }
    if (operation_nonterminal(operation)) {
        ure_bool32_t accepted{};
        operation_interface().request_cancel(operation, &accepted, nullptr);
        const ure_result_t wait = operation_interface().wait(
            operation, UINT64_C(30000000000), nullptr);
        if (wait != URE_RESULT_SUCCESS && wait != URE_RESULT_CANCELED)
            return make_error(wait, 510,
                              "session close could not drain active work", error);
    }
    session->job->cancel();
    session->closed.store(true, std::memory_order_release);
    std::scoped_lock lock(session->mutex);
    session->state = URE_SESSION_STATE_CLOSED;
    return URE_RESULT_SUCCESS;
}

ure_result_t get_info_impl(ure_handle_t session_handle, ure_session_info_t *info,
                           ure_handle_t *error) {
    clear_error(error);
    const auto session =
        handles().get<SessionObject>(session_handle, ObjectType::Session, true);
    if (!session)
        return make_error(URE_RESULT_INVALID_HANDLE, 511,
                          "invalid session handle", error);
    if (!valid_output(info, URE_STRUCTURE_SESSION_INFO) ||
        info->reserved[0] != 0 || info->reserved[1] != 0)
        return make_error(URE_RESULT_INVALID_ARGUMENT, 512,
                          "invalid session info output", error);
    std::scoped_lock lock(session->mutex);
    info->state = session->state;
    info->reset_reason = session->reset_reason;
    info->bound_scene_revision = session->revision->revision;
    store(info->scene_revision_identity, session->revision->revision_identity);
    store(info->objective_identity, session->objective.identity);
    const auto progress = session->job->operation();
    info->completed_samples = progress.accepted_samples;
    info->requested_samples = session->objective.requested_samples;
    info->active_operation = session->active_operation;
    info->latest_frame = session->latest_frame;
    return URE_RESULT_SUCCESS;
}

ure_result_t bind_scene_impl(ure_handle_t session_handle,
                             ure_handle_t scene_handle,
                             ure_scene_revision_info_t *revision,
                             ure_handle_t *error) {
    clear_error(error);
    const auto session =
        handles().get<SessionObject>(session_handle, ObjectType::Session);
    const auto scene = handles().get<SceneObject>(scene_handle, ObjectType::Scene);
    if (!session || !scene || scene->owner != session->owner)
        return make_error(URE_RESULT_INVALID_HANDLE, 513,
                          "invalid session scene binding", error);
    if (!valid_output(revision, URE_STRUCTURE_SCENE_REVISION_INFO) ||
        revision->reserved[0] != 0 || revision->reserved[1] != 0)
        return make_error(URE_RESULT_INVALID_ARGUMENT, 514,
                          "invalid scene revision output", error);
    if (operation_nonterminal(session->active_operation))
        return make_error(URE_RESULT_BUSY, 515,
                          "cannot bind a scene during active work", error);
    const auto next_revision = scene_revision(scene_handle, error);
    if (!next_revision)
        return URE_RESULT_INVALID_HANDLE;
    try {
        session->job->replace_scene(
            next_revision->archive, next_revision->revision_identity);
    } catch (const std::exception &exception) {
        return make_error(URE_RESULT_INTERNAL, 515,
                          "renderer scene rebind failed: " +
                              std::string(exception.what()),
                          error);
    }
    ure_handle_t old_frame{};
    {
        std::scoped_lock lock(session->mutex);
        session->revision = next_revision;
        session->completed_samples = 0;
        session->latest_artifact.reset();
        session->reset_reason = URE_SCENE_RESET_FULL_REPLACEMENT;
        session->state = URE_SESSION_STATE_READY;
        old_frame = std::exchange(session->latest_frame, nullptr);
    }
    if (old_frame)
        frame_interface().release(old_frame, nullptr);
    write_scene_revision(*next_revision, *revision);
    return URE_RESULT_SUCCESS;
}

ure_result_t start_impl(ure_handle_t session_handle, ure_handle_t *output,
                        ure_handle_t *error) {
    clear_error(error);
    if (output)
        *output = nullptr;
    const auto session =
        handles().get<SessionObject>(session_handle, ObjectType::Session);
    if (!session)
        return make_error(URE_RESULT_INVALID_HANDLE, 516,
                          "invalid session handle", error);
    if (!output || operation_nonterminal(session->active_operation))
        return make_error(URE_RESULT_BUSY, 517,
                          "session already has active work", error);
    ure_handle_t old_operation{};
    {
        std::scoped_lock lock(session->mutex);
        old_operation = std::exchange(session->active_operation, nullptr);
    }
    if (old_operation)
        operation_interface().release(old_operation, nullptr);
    auto operation = std::make_shared<OperationObject>();
    operation->type = ObjectType::Operation;
    operation->owner = session->owner;
    operation->parent = session_handle;
    operation->thread_policy = URE_THREAD_POLICY_CONCURRENT;
    operation->instance = session->instance;
    operation->steps = static_cast<std::uint32_t>(
        session->objective.requested_samples);
    operation->stage = URE_OPERATION_RENDER_SESSION;
    *output = handles().insert(operation);
    if (!handles().retain(*output, ObjectType::Operation))
        return make_error(URE_RESULT_INTERNAL, 518,
                          "session operation retention failed", error);
    {
        std::scoped_lock lock(session->instance->mutex);
        std::erase_if(session->instance->operations,
                      [](const auto &child) { return child.expired(); });
        session->instance->operations.push_back(operation);
    }
    {
        std::scoped_lock lock(session->mutex);
        session->active_operation = *output;
        session->completed_samples = 0;
        session->latest_artifact.reset();
        session->state = URE_SESSION_STATE_RUNNING;
    }
    operation->worker = std::jthread(
        [session, operation, handle = *output] {
            run_render(session, operation, handle);
        });
    return URE_RESULT_SUCCESS;
}

ure_result_t pause_impl(ure_handle_t session_handle, ure_handle_t *error) {
    clear_error(error);
    const auto session =
        handles().get<SessionObject>(session_handle, ObjectType::Session);
    if (!session)
        return make_error(URE_RESULT_INVALID_HANDLE, 519,
                          "invalid session handle", error);
    const auto operation = handles().get<OperationObject>(
        session->active_operation, ObjectType::Operation);
    if (!operation)
        return make_error(URE_RESULT_BUSY, 520,
                          "session has no active work", error);
    {
        std::scoped_lock lock(operation->mutex, session->mutex);
        if (operation->state != URE_OPERATION_STATE_RUNNING)
            return make_error(URE_RESULT_BUSY, 521,
                              "session cannot be paused", error);
        operation->state = URE_OPERATION_STATE_PAUSED;
        session->state = URE_SESSION_STATE_PAUSED;
        ++operation->progress_sequence;
    }
    session->job->pause();
    emit_event(session->instance, URE_EVENT_OPERATION_STATE,
               session->active_operation);
    return URE_RESULT_SUCCESS;
}

ure_result_t resume_impl(ure_handle_t session_handle, ure_handle_t *error) {
    clear_error(error);
    const auto session =
        handles().get<SessionObject>(session_handle, ObjectType::Session);
    if (!session)
        return make_error(URE_RESULT_INVALID_HANDLE, 522,
                          "invalid session handle", error);
    const auto operation = handles().get<OperationObject>(
        session->active_operation, ObjectType::Operation);
    if (!operation)
        return make_error(URE_RESULT_BUSY, 523,
                          "session has no active work", error);
    {
        std::scoped_lock lock(operation->mutex, session->mutex);
        if (operation->state != URE_OPERATION_STATE_PAUSED)
            return make_error(URE_RESULT_BUSY, 524,
                              "session cannot be resumed", error);
        operation->state = URE_OPERATION_STATE_RUNNING;
        session->state = URE_SESSION_STATE_RUNNING;
        ++operation->progress_sequence;
        operation->changed.notify_all();
    }
    session->job->resume();
    emit_event(session->instance, URE_EVENT_OPERATION_STATE,
               session->active_operation);
    return URE_RESULT_SUCCESS;
}

ure_result_t reset_impl(ure_handle_t session_handle, std::uint32_t reason,
                        ure_handle_t *error) {
    clear_error(error);
    const auto session =
        handles().get<SessionObject>(session_handle, ObjectType::Session);
    if (!session)
        return make_error(URE_RESULT_INVALID_HANDLE, 525,
                          "invalid session handle", error);
    if (reason != URE_SCENE_RESET_EXPLICIT ||
        operation_nonterminal(session->active_operation))
        return make_error(URE_RESULT_BUSY, 526,
                          "session reset is unavailable", error);
    session->job->reset();
    ure_handle_t old_frame{};
    {
        std::scoped_lock lock(session->mutex);
        session->completed_samples = 0;
        session->latest_artifact.reset();
        session->reset_reason = reason;
        session->state = URE_SESSION_STATE_READY;
        old_frame = std::exchange(session->latest_frame, nullptr);
    }
    if (old_frame)
        frame_interface().release(old_frame, nullptr);
    return URE_RESULT_SUCCESS;
}

ure_result_t acquire_frame_impl(ure_handle_t session_handle,
                                ure_handle_t *output, ure_handle_t *error) {
    clear_error(error);
    if (output)
        *output = nullptr;
    const auto session =
        handles().get<SessionObject>(session_handle, ObjectType::Session, true);
    if (!session)
        return make_error(URE_RESULT_INVALID_HANDLE, 527,
                          "invalid session handle", error);
    if (!output)
        return make_error(URE_RESULT_INVALID_ARGUMENT, 528,
                          "frame output is required", error);
    std::scoped_lock lock(session->mutex);
    if (!session->latest_frame)
        return make_error(URE_RESULT_INCOMPLETE, 529,
                          "session has no completed frame", error);
    if (!handles().retain(session->latest_frame, ObjectType::Frame))
        return make_error(URE_RESULT_INTERNAL, 530,
                          "session frame is unavailable", error);
    *output = session->latest_frame;
    return URE_RESULT_SUCCESS;
}

ure_result_t get_product_info_impl(ure_handle_t session_handle,
                                   ure_product_job_info_t *info,
                                   ure_handle_t *error) {
    clear_error(error);
    const auto session =
        handles().get<SessionObject>(session_handle, ObjectType::Session, true);
    if (!session)
        return make_error(URE_RESULT_INVALID_HANDLE, 531,
                          "invalid product job handle", error);
    if (!valid_output(info, URE_STRUCTURE_PRODUCT_JOB_INFO) ||
        info->reserved32 != 0 || info->reserved[0] != 0 ||
        info->reserved[1] != 0)
        return make_error(URE_RESULT_INVALID_ARGUMENT, 532,
                          "invalid product job info output", error);
    std::scoped_lock lock(session->mutex);
    const auto progress = session->job->operation();
    const auto &identities = session->job->identities();
    info->state = session->state;
    info->requested_samples = progress.requested_samples;
    info->accepted_samples = progress.accepted_samples;
    info->active_operation = session->active_operation;
    info->latest_frame = session->latest_frame;
    store(info->build_identity, identities.build);
    store(info->snapshot_identity, identities.snapshot);
    store(info->objective_identity, identities.objective);
    store(info->plan_identity, identities.plan);
    return URE_RESULT_SUCCESS;
}

ure_result_t request_product_cancel_impl(ure_handle_t session_handle,
                                         ure_bool32_t *accepted,
                                         ure_handle_t *error) {
    clear_error(error);
    if (accepted)
        *accepted = 0;
    const auto session =
        handles().get<SessionObject>(session_handle, ObjectType::Session);
    if (!session)
        return make_error(URE_RESULT_INVALID_HANDLE, 533,
                          "invalid product job handle", error);
    if (!accepted)
        return make_error(URE_RESULT_INVALID_ARGUMENT, 534,
                          "cancel acceptance output is required", error);
    ure_handle_t operation{};
    {
        std::scoped_lock lock(session->mutex);
        operation = session->active_operation;
    }
    if (!operation)
        return make_error(URE_RESULT_BUSY, 535,
                          "product job has no active operation", error);
    return operation_interface().request_cancel(operation, accepted, error);
}

ure_result_t get_product_artifact_impl(
    ure_handle_t session_handle, ure_product_artifact_manifest_t *manifest,
    ure_handle_t *error) {
    clear_error(error);
    const auto session =
        handles().get<SessionObject>(session_handle, ObjectType::Session, true);
    if (!session)
        return make_error(URE_RESULT_INVALID_HANDLE, 536,
                          "invalid product job handle", error);
    if (!valid_output(manifest, URE_STRUCTURE_PRODUCT_ARTIFACT_MANIFEST) ||
        manifest->reserved[0] != 0 || manifest->reserved[1] != 0)
        return make_error(URE_RESULT_INVALID_ARGUMENT, 537,
                          "invalid product artifact manifest output", error);
    std::scoped_lock lock(session->mutex);
    if (!session->latest_artifact)
        return make_error(URE_RESULT_INCOMPLETE, 538,
                          "product job has no published artifact", error);
    const auto &artifact = *session->latest_artifact;
    manifest->accepted_samples = artifact.accepted_samples;
    manifest->rgb_value_count = artifact.rgb_value_count;
    store(manifest->build_identity, artifact.identities.build);
    store(manifest->snapshot_identity, artifact.identities.snapshot);
    store(manifest->objective_identity, artifact.identities.objective);
    store(manifest->plan_identity, artifact.identities.plan);
    store(manifest->frame_content_identity, artifact.frame_content);
    return URE_RESULT_SUCCESS;
}

ure_result_t URE_CALL create_session(
    ure_handle_t instance, ure_handle_t scene,
    const ure_objective_envelope_t *objective, ure_handle_t *session,
    ure_handle_t *error) noexcept {
    return guard_entry(error, [&] {
        return create_impl(instance, scene, objective, session, error);
    });
}

ure_result_t URE_CALL create_product_job(
    ure_handle_t instance_handle, ure_handle_t scene,
    const ure_objective_envelope_t *objective, ure_handle_t *job,
    ure_handle_t *error) noexcept {
    return guard_entry(error, [&] {
        const auto instance =
            handles().get<InstanceObject>(instance_handle, ObjectType::Instance);
        if (!instance)
            return make_error(URE_RESULT_INVALID_HANDLE, 539,
                              "invalid product instance handle", error);
        {
            std::scoped_lock lock(instance->mutex);
            if (!instance->product_enabled)
                return make_error(URE_RESULT_CAPABILITY_UNAVAILABLE, 540,
                                  "product job capability is not enabled", error);
        }
        return create_impl(instance_handle, scene, objective, job, error);
    });
}

ure_result_t URE_CALL retain_session(ure_handle_t session,
                                     ure_handle_t *error) noexcept {
    return guard_entry(error, [&] { return retain_impl(session, error); });
}

ure_result_t URE_CALL release_session(ure_handle_t session,
                                      ure_handle_t *error) noexcept {
    return guard_entry(error, [&] { return release_impl(session, error); });
}

ure_result_t URE_CALL close_session(ure_handle_t session,
                                    ure_handle_t *error) noexcept {
    return guard_entry(error, [&] { return close_impl(session, error); });
}

ure_result_t URE_CALL get_session_info(ure_handle_t session,
                                       ure_session_info_t *info,
                                       ure_handle_t *error) noexcept {
    return guard_entry(error, [&] { return get_info_impl(session, info, error); });
}

ure_result_t URE_CALL bind_session_scene(
    ure_handle_t session, ure_handle_t scene,
    ure_scene_revision_info_t *revision, ure_handle_t *error) noexcept {
    return guard_entry(error, [&] {
        return bind_scene_impl(session, scene, revision, error);
    });
}

ure_result_t URE_CALL start_session(ure_handle_t session,
                                    ure_handle_t *operation,
                                    ure_handle_t *error) noexcept {
    return guard_entry(error,
                       [&] { return start_impl(session, operation, error); });
}

ure_result_t URE_CALL pause_session(ure_handle_t session,
                                    ure_handle_t *error) noexcept {
    return guard_entry(error, [&] { return pause_impl(session, error); });
}

ure_result_t URE_CALL resume_session(ure_handle_t session,
                                     ure_handle_t *error) noexcept {
    return guard_entry(error, [&] { return resume_impl(session, error); });
}

ure_result_t URE_CALL reset_session(ure_handle_t session, std::uint32_t reason,
                                    ure_handle_t *error) noexcept {
    return guard_entry(error,
                       [&] { return reset_impl(session, reason, error); });
}

ure_result_t URE_CALL acquire_session_frame(ure_handle_t session,
                                            ure_handle_t *frame,
                                            ure_handle_t *error) noexcept {
    return guard_entry(error,
                       [&] { return acquire_frame_impl(session, frame, error); });
}

ure_result_t URE_CALL get_product_job_info(
    ure_handle_t job, ure_product_job_info_t *info,
    ure_handle_t *error) noexcept {
    return guard_entry(error,
                       [&] { return get_product_info_impl(job, info, error); });
}

ure_result_t URE_CALL request_product_cancel(
    ure_handle_t job, ure_bool32_t *accepted, ure_handle_t *error) noexcept {
    return guard_entry(error, [&] {
        return request_product_cancel_impl(job, accepted, error);
    });
}

ure_result_t URE_CALL get_product_artifact_manifest(
    ure_handle_t job, ure_product_artifact_manifest_t *manifest,
    ure_handle_t *error) noexcept {
    return guard_entry(error, [&] {
        return get_product_artifact_impl(job, manifest, error);
    });
}

}

const ure_session_interface_t &session_interface() noexcept {
    static const ure_session_interface_t table{
        {sizeof(table), 1, 0}, create_session, retain_session, release_session,
        close_session, get_session_info, bind_session_scene, start_session,
        pause_session, resume_session, reset_session, acquire_session_frame};
    return table;
}

const ure_product_job_interface_t &product_job_interface() noexcept {
    static const ure_product_job_interface_t table{
        {sizeof(table), 0, 1},
        create_product_job,
        retain_session,
        release_session,
        close_session,
        get_product_job_info,
        start_session,
        request_product_cancel,
        acquire_session_frame,
        get_product_artifact_manifest};
    return table;
}

}
