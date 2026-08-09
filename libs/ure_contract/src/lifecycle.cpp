#include "runtime_adapter.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace ure::contract {
namespace {

enum class ObjectType : std::uint32_t {
    Instance = 1,
    Error = 2,
    Operation = 3
};

struct Object {
    ObjectType type{};
    std::uint64_t generation{};
    ure_handle_t owner{};
    ure_handle_t parent{};
    std::uint32_t thread_policy{};
    std::atomic<std::uint32_t> references{1};
    std::atomic<bool> closed{false};
    virtual ~Object() = default;
};

struct ErrorObject final : Object {
    ure_result_t result{};
    std::uint32_t domain{URE_ERROR_DOMAIN_CORE};
    std::uint32_t detail{};
    std::string message;
    std::vector<std::uint8_t> structured_detail;
    ure_handle_t cause{};
    ure_handle_t operation{};
};

struct EventData {
    std::uint32_t type{};
    std::uint32_t affected_classes{};
    std::uint64_t sequence{};
    std::uint64_t timestamp_ns{};
    ure_handle_t operation{};
    std::uint64_t first_lost{};
    std::uint64_t last_lost{};
    std::uint64_t coalesced_count{1};
};

struct OperationObject;

struct InstanceObject final : Object {
    std::mutex mutex;
    std::condition_variable event_ready;
    std::deque<EventData> events;
    std::vector<std::weak_ptr<OperationObject>> operations;
    std::uint64_t next_sequence{1};
    std::size_t event_capacity{64};
};

struct OperationObject final : Object {
    std::mutex mutex;
    std::condition_variable changed;
    std::shared_ptr<InstanceObject> instance;
    std::jthread worker;
    std::uint32_t state{URE_OPERATION_STATE_QUEUED};
    std::uint32_t steps{1};
    std::uint32_t delay_ms{1};
    std::uint64_t completed{};
    std::uint64_t progress_sequence{};
    bool cancel_requested{};
    bool fail_at_end{};
    bool device_lost_at_end{};
    ure_handle_t terminal_error{};
};

class HandleTable {
  public:
    template <class T>
    ure_handle_t insert(const std::shared_ptr<T>& object) {
        const std::uint64_t token = next_.fetch_add(1, std::memory_order_relaxed);
        object->generation = token;
        const auto handle =
            reinterpret_cast<ure_handle_t>(static_cast<std::uintptr_t>(token));
        std::scoped_lock lock(mutex_);
        objects_.emplace(token, object);
        return handle;
    }

    template <class T>
    std::shared_ptr<T> get(ure_handle_t handle, ObjectType type,
                           bool allow_closed = false) {
        const auto token =
            static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(handle));
        std::scoped_lock lock(mutex_);
        const auto found = objects_.find(token);
        if (found == objects_.end() || found->second->type != type ||
            (!allow_closed &&
             found->second->closed.load(std::memory_order_acquire)))
            return {};
        return std::static_pointer_cast<T>(found->second);
    }

    bool retain(ure_handle_t handle, ObjectType type) {
        const auto token =
            static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(handle));
        std::scoped_lock lock(mutex_);
        const auto found = objects_.find(token);
        if (found == objects_.end() || found->second->type != type ||
            found->second->closed.load(std::memory_order_acquire))
            return false;
        found->second->references.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    bool release(ure_handle_t handle, ObjectType type,
                 std::shared_ptr<Object>* final_object = nullptr) {
        const auto token =
            static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(handle));
        std::shared_ptr<Object> object;
        {
            std::scoped_lock lock(mutex_);
            const auto found = objects_.find(token);
            if (found == objects_.end() || found->second->type != type)
                return false;
            object = found->second;
            const std::uint32_t previous =
                object->references.fetch_sub(1, std::memory_order_acq_rel);
            if (previous == 0) {
                object->references.store(0, std::memory_order_release);
                return false;
            }
            if (previous == 1) {
                object->closed.store(true, std::memory_order_release);
                objects_.erase(found);
                if (final_object)
                    *final_object = object;
            }
        }
        return true;
    }

    std::uint64_t size() {
        std::scoped_lock lock(mutex_);
        return objects_.size();
    }

    std::uint32_t reference_count(ure_handle_t handle, ObjectType type) {
        const auto token = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(handle));
        std::scoped_lock lock(mutex_);
        const auto found = objects_.find(token);
        if (found == objects_.end() || found->second->type != type) return 0;
        return found->second->references.load(std::memory_order_acquire);
    }

  private:
    std::mutex mutex_;
    std::unordered_map<std::uint64_t, std::shared_ptr<Object>> objects_;
    std::atomic<std::uint64_t> next_{1};
};

HandleTable& handles() {
    static HandleTable table;
    return table;
}

std::atomic<bool>& fail_next_error_allocation() {
    static std::atomic<bool> value{false};
    return value;
}

void clear_error(ure_handle_t* error) noexcept {
    if (error)
        *error = nullptr;
}

bool release_error(ure_handle_t handle) noexcept {
    if (!handle)
        return false;
    try {
        std::shared_ptr<Object> final_object;
        if (!handles().release(handle, ObjectType::Error, &final_object))
            return false;
        if (!final_object)
            return true;
        const auto error = std::static_pointer_cast<ErrorObject>(final_object);
        const ure_handle_t cause = error->cause;
        error->cause = nullptr;
        release_error(cause);
        return true;
    } catch (...) {
        return false;
    }
}

ure_result_t make_error(ure_result_t result, std::uint32_t detail,
                        std::string message, ure_handle_t* output,
                        ure_handle_t cause = nullptr,
                        ure_handle_t operation = nullptr) noexcept {
    if (output) {
        *output = nullptr;
        if (fail_next_error_allocation().exchange(false, std::memory_order_acq_rel)) return result;
        bool cause_retained = false;
        try {
            if (cause) {
                cause_retained = handles().retain(cause, ObjectType::Error);
                if (!cause_retained)
                    cause = nullptr;
            }
            auto error = std::make_shared<ErrorObject>();
            error->type = ObjectType::Error;
            error->thread_policy = URE_THREAD_POLICY_CONCURRENT_READ;
            error->result = result;
            error->detail = detail;
            if (message.size() > 1024)
                message.resize(1024);
            error->message = std::move(message);
            error->structured_detail = {
                static_cast<std::uint8_t>(URE_PAYLOAD_ERROR & 0xffU),
                static_cast<std::uint8_t>((URE_PAYLOAD_ERROR >> 8U) & 0xffU),
                static_cast<std::uint8_t>((URE_PAYLOAD_ERROR >> 16U) & 0xffU),
                static_cast<std::uint8_t>((URE_PAYLOAD_ERROR >> 24U) & 0xffU),
                static_cast<std::uint8_t>(detail & 0xffU),
                static_cast<std::uint8_t>((detail >> 8U) & 0xffU),
                static_cast<std::uint8_t>((detail >> 16U) & 0xffU),
                static_cast<std::uint8_t>((detail >> 24U) & 0xffU)};
            error->cause = cause;
            error->operation = operation;
            *output = handles().insert(error);
        } catch (...) {
            if (cause_retained)
                release_error(cause);
            *output = nullptr;
        }
    }
    return result;
}

template <class T>
bool valid_input(const T* value, std::uint32_t type) noexcept {
    if (!value || value->header.type != type || value->header.size < sizeof(T))
        return false;
    std::array<const void*, 32> pointers{};
    std::array<std::uint32_t, 32> types{};
    const void* next = value->header.next;
    std::size_t count = 0;
    while (next) {
        if (count == pointers.size())
            return false;
        ure_input_header_t header{};
        std::memcpy(&header, next, sizeof(header));
        if (header.type == 0 || header.type == type || header.size < sizeof(header))
            return false;
        for (std::size_t index = 0; index < count; ++index) {
            if (pointers[index] == next || types[index] == header.type)
                return false;
        }
        pointers[count] = next;
        types[count++] = header.type;
        next = header.next;
    }
    return true;
}

template <class T>
bool valid_output(T* value, std::uint32_t type) noexcept {
    return valid_input(reinterpret_cast<const T*>(value), type);
}

std::uint64_t timestamp_ns() noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

void emit_event(const std::shared_ptr<InstanceObject>& instance,
                std::uint32_t type, ure_handle_t operation) noexcept {
    try {
        std::scoped_lock lock(instance->mutex);
        if (type == URE_EVENT_DIAGNOSTIC && !instance->events.empty() &&
            instance->events.back().type == URE_EVENT_DIAGNOSTIC) {
            ++instance->events.back().coalesced_count;
            instance->events.back().timestamp_ns = timestamp_ns();
            instance->event_ready.notify_all();
            return;
        }
        if (instance->events.size() >= instance->event_capacity) {
            const std::uint64_t first = instance->events.front().sequence;
            const std::uint64_t last = instance->events.back().sequence;
            instance->events.clear();
            instance->events.push_back(
                EventData{URE_EVENT_GAP, type, instance->next_sequence++,
                          timestamp_ns(), nullptr, first, last});
        }
        const EventData event{
            type, type, instance->next_sequence++, timestamp_ns(), operation, 0, 0};
        if (instance->events.size() < instance->event_capacity)
            instance->events.push_back(event);
        instance->event_ready.notify_all();
    } catch (...) {
    }
}

bool terminal(std::uint32_t state) noexcept {
    return state == URE_OPERATION_STATE_SUCCEEDED ||
           state == URE_OPERATION_STATE_CANCELED ||
           state == URE_OPERATION_STATE_FAILED ||
           state == URE_OPERATION_STATE_DEVICE_LOST;
}

std::chrono::nanoseconds wait_duration(std::uint64_t timeout_ns) noexcept {
    const auto maximum = static_cast<std::uint64_t>(std::chrono::nanoseconds::max().count());
    return std::chrono::nanoseconds(static_cast<std::int64_t>(timeout_ns > maximum ? maximum : timeout_ns));
}

ure_result_t create_instance_impl(const ure_instance_create_info_t* info,
                                  ure_handle_t* output, ure_handle_t* error) {
    clear_error(error);
    if (output)
        *output = nullptr;
    if (!output || !valid_input(info, URE_STRUCTURE_INSTANCE_CREATE_INFO) ||
        info->reserved[0] != 0 || info->reserved[1] != 0 ||
        info->event_capacity == 0 || info->event_capacity < 2 ||
        info->event_capacity > 4096 || info->required_capability_count > 64 ||
        (info->required_capability_count != 0 &&
         info->required_capabilities == nullptr)) {
        return make_error(URE_RESULT_INVALID_ARGUMENT, 100,
                          "invalid instance create info", error);
    }
    for (std::uint32_t index = 0; index < info->required_capability_count;
         ++index) {
        const std::uint32_t capability = info->required_capabilities[index];
        if (capability != URE_CAPABILITY_BOOTSTRAP &&
            capability != URE_CAPABILITY_LIFECYCLE) {
            return make_error(URE_RESULT_CAPABILITY_UNAVAILABLE, 101,
                              "required capability is unavailable", error);
        }
    }
    try {
        auto instance = std::make_shared<InstanceObject>();
        instance->type = ObjectType::Instance;
        instance->thread_policy = URE_THREAD_POLICY_CONCURRENT;
        instance->event_capacity = info->event_capacity;
        *output = handles().insert(instance);
        instance->owner = *output;
        return URE_RESULT_SUCCESS;
    } catch (...) {
        return make_error(URE_RESULT_INTERNAL, 102, "instance allocation failed",
                          error);
    }
}

ure_result_t instance_retain_impl(ure_handle_t instance, ure_handle_t* error) {
    clear_error(error);
    return handles().retain(instance, ObjectType::Instance)
               ? URE_RESULT_SUCCESS
               : make_error(URE_RESULT_INVALID_HANDLE, 103,
                            "invalid instance handle", error);
}

ure_result_t instance_release_impl(ure_handle_t instance, ure_handle_t* error) {
    clear_error(error);
    const auto object =
        handles().get<InstanceObject>(instance, ObjectType::Instance, true);
    if (!object)
        return make_error(URE_RESULT_INVALID_HANDLE, 104, "invalid instance handle",
                          error);
    if (handles().reference_count(instance, ObjectType::Instance) > 1) {
        return handles().release(instance, ObjectType::Instance)
                   ? URE_RESULT_SUCCESS
                   : make_error(URE_RESULT_INVALID_HANDLE, 106,
                                "invalid instance release", error);
    }
    {
        std::scoped_lock lock(object->mutex);
        for (const auto& weak : object->operations) {
            if (const auto operation = weak.lock()) {
                std::scoped_lock operation_lock(operation->mutex);
                if (!terminal(operation->state) && !object->closed) {
                    return make_error(URE_RESULT_BUSY, 105,
                                      "instance has live operations", error);
                }
            }
        }
    }
    return handles().release(instance, ObjectType::Instance)
               ? URE_RESULT_SUCCESS
               : make_error(URE_RESULT_INVALID_HANDLE, 106,
                            "invalid instance release", error);
}

ure_result_t instance_close_impl(ure_handle_t instance, ure_handle_t* error) {
    clear_error(error);
    const auto object =
        handles().get<InstanceObject>(instance, ObjectType::Instance, true);
    if (!object)
        return make_error(URE_RESULT_INVALID_HANDLE, 107, "invalid instance handle",
                          error);
    object->closed.store(true, std::memory_order_release);
    std::scoped_lock lock(object->mutex);
    for (const auto& weak : object->operations) {
        if (const auto operation = weak.lock()) {
            std::scoped_lock operation_lock(operation->mutex);
            if (!terminal(operation->state))
                operation->cancel_requested = true;
            operation->changed.notify_all();
        }
    }
    object->event_ready.notify_all();
    return URE_RESULT_SUCCESS;
}

ure_result_t query_capability_impl(ure_handle_t instance,
                                   const ure_capability_query_t* query,
                                   ure_capability_descriptor_t* descriptor,
                                   ure_handle_t* error) {
    clear_error(error);
    if (!handles().get<InstanceObject>(instance, ObjectType::Instance)) {
        return make_error(URE_RESULT_INVALID_HANDLE, 108, "invalid instance handle",
                          error);
    }
    if (!valid_input(query, URE_STRUCTURE_CAPABILITY_QUERY) ||
        !valid_output(descriptor, URE_STRUCTURE_CAPABILITY_DESCRIPTOR) ||
        query->reserved != 0 || query->required > 1 ||
        query->request_enable > 1 || descriptor->reserved[0] != 0 ||
        descriptor->reserved[1] != 0) {
        return make_error(URE_RESULT_INVALID_ARGUMENT, 109,
                          "invalid capability query", error);
    }
    static constexpr std::uint32_t lifecycle_dependencies[]{
        URE_CAPABILITY_BOOTSTRAP};
    static constexpr std::string_view frame_reason =
        "Frame leases are unavailable before PB.4";
    static constexpr std::string_view telemetry_reason =
        "No telemetry provider is available";
    descriptor->capability_id = query->capability_id;
    descriptor->version_major = 0;
    descriptor->version_minor = 1;
    descriptor->version_patch = 0;
    descriptor->stability = URE_STABILITY_CORE;
    descriptor->thread_policy = URE_THREAD_POLICY_CONCURRENT_READ;
    descriptor->limits_schema = 0;
    descriptor->reserved[0] = descriptor->reserved[1] = 0;
    descriptor->dependencies = nullptr;
    descriptor->limits = {nullptr, 0};
    descriptor->reason = {nullptr, 0};
    descriptor->dependency_count = 0;
    descriptor->applicable = 0;
    descriptor->enabled = 0;
    if (query->capability_id == URE_CAPABILITY_BOOTSTRAP ||
        query->capability_id == URE_CAPABILITY_LIFECYCLE) {
        descriptor->maturity = URE_MATURITY_NOT_APPLICABLE;
        descriptor->runtime_state = URE_RUNTIME_STATE_APPLICABLE;
        descriptor->enabled = 1;
        descriptor->applicable = 1;
        if (query->capability_id == URE_CAPABILITY_LIFECYCLE) {
            descriptor->dependencies = lifecycle_dependencies;
            descriptor->dependency_count = 1;
        }
        return URE_RESULT_SUCCESS;
    }
    if (query->capability_id != URE_CAPABILITY_FRAME_LEASE &&
        query->capability_id != URE_CAPABILITY_TELEMETRY) {
        return make_error(URE_RESULT_CAPABILITY_UNAVAILABLE, 110,
                          "unknown capability", error);
    }
    descriptor->maturity = URE_MATURITY_EXPERIMENTAL;
    descriptor->runtime_state = URE_RUNTIME_STATE_COMPILED;
    const std::string_view reason =
        query->capability_id == URE_CAPABILITY_FRAME_LEASE ? frame_reason
                                                           : telemetry_reason;
    descriptor->reason = {reason.data(), reason.size()};
    if (query->required || query->request_enable) {
        return make_error(URE_RESULT_CAPABILITY_UNAVAILABLE, 110,
                          "capability cannot be enabled", error);
    }
    return URE_RESULT_SUCCESS;
}

ure_result_t error_retain_impl(ure_handle_t error) {
    return handles().retain(error, ObjectType::Error) ? URE_RESULT_SUCCESS
                                                      : URE_RESULT_INVALID_HANDLE;
}

ure_result_t error_release_impl(ure_handle_t error) {
    return release_error(error) ? URE_RESULT_SUCCESS : URE_RESULT_INVALID_HANDLE;
}

ure_result_t error_get_info_impl(ure_handle_t handle, ure_error_info_t* info) {
    const auto error = handles().get<ErrorObject>(handle, ObjectType::Error);
    if (!error)
        return URE_RESULT_INVALID_HANDLE;
    if (!valid_output(info, URE_STRUCTURE_ERROR_INFO) || info->reserved != 0)
        return URE_RESULT_INVALID_ARGUMENT;
    info->result = error->result;
    info->domain = error->domain;
    info->detail = error->detail;
    info->structured_detail_schema = URE_PAYLOAD_ERROR;
    info->message = {error->message.data(), error->message.size()};
    info->structured_detail = {error->structured_detail.data(),
                               error->structured_detail.size()};
    info->cause = error->cause;
    info->operation = error->operation;
    const auto& digest = runtime_build_digest();
    std::memcpy(info->build_digest.bytes, digest.data(), digest.size());
    return URE_RESULT_SUCCESS;
}

void run_operation(OperationObject* operation, ure_handle_t handle) noexcept {
    const auto instance = operation->instance;
    try {
        bool canceled_before_start = false;
        {
            std::scoped_lock lock(operation->mutex);
            if (operation->cancel_requested || instance->closed.load(std::memory_order_acquire)) {
                operation->state = URE_OPERATION_STATE_CANCELED;
                canceled_before_start = true;
            } else {
                operation->state = URE_OPERATION_STATE_RUNNING;
            }
            ++operation->progress_sequence;
            operation->changed.notify_all();
        }
        emit_event(instance, URE_EVENT_OPERATION_STATE, handle);
        if (canceled_before_start) return;
        for (std::uint32_t step = 0; step < operation->steps; ++step) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(operation->delay_ms));
            bool canceled = false;
            {
                std::scoped_lock lock(operation->mutex);
                if (operation->cancel_requested ||
                    instance->closed.load(std::memory_order_acquire)) {
                    operation->state = URE_OPERATION_STATE_CANCELED;
                    ++operation->progress_sequence;
                    operation->changed.notify_all();
                    canceled = true;
                } else {
                    operation->completed = step + 1;
                    ++operation->progress_sequence;
                }
            }
            if (canceled) {
                emit_event(instance, URE_EVENT_OPERATION_STATE, handle);
                return;
            }
        }
        {
            std::scoped_lock lock(operation->mutex);
            operation->state =
                operation->device_lost_at_end ? URE_OPERATION_STATE_DEVICE_LOST
                : operation->fail_at_end      ? URE_OPERATION_STATE_FAILED
                                              : URE_OPERATION_STATE_SUCCEEDED;
            if (operation->state == URE_OPERATION_STATE_FAILED) {
                make_error(URE_RESULT_INTERNAL, 111, "conformance operation failed",
                           &operation->terminal_error, nullptr, handle);
            } else if (operation->state == URE_OPERATION_STATE_DEVICE_LOST) {
                make_error(URE_RESULT_DEVICE_LOST, 112,
                           "conformance operation reported device loss",
                           &operation->terminal_error, nullptr, handle);
            }
            if (operation->closed.load(std::memory_order_acquire) &&
                operation->terminal_error) {
                release_error(operation->terminal_error);
                operation->terminal_error = nullptr;
            }
            ++operation->progress_sequence;
            operation->changed.notify_all();
        }
        emit_event(instance,
                   operation->device_lost_at_end ? URE_EVENT_DEVICE_LOST
                                                 : URE_EVENT_OPERATION_STATE,
                   handle);
    } catch (...) {
        try {
            std::scoped_lock lock(operation->mutex);
            operation->state = URE_OPERATION_STATE_FAILED;
            make_error(URE_RESULT_INTERNAL, 198, "operation execution failed",
                       &operation->terminal_error, nullptr, handle);
            if (operation->closed.load(std::memory_order_acquire) &&
                operation->terminal_error) {
                release_error(operation->terminal_error);
                operation->terminal_error = nullptr;
            }
            ++operation->progress_sequence;
            operation->changed.notify_all();
        } catch (...) {
        }
        emit_event(instance, URE_EVENT_OPERATION_STATE, handle);
    }
}

ure_result_t submit_operation_impl(
    ure_handle_t instance_handle,
    const ure_private_conformance_operation_request_t* request,
    ure_handle_t* output, ure_handle_t* error) {
    clear_error(error);
    if (output)
        *output = nullptr;
    const auto instance =
        handles().get<InstanceObject>(instance_handle, ObjectType::Instance);
    if (!instance)
        return make_error(URE_RESULT_INVALID_HANDLE, 110, "invalid instance handle",
                          error);
    if (!output ||
        !valid_input(request,
                     URE_PRIVATE_STRUCTURE_CONFORMANCE_OPERATION_REQUEST) ||
        request->work_steps == 0 || request->work_steps > 100000 ||
        request->step_delay_milliseconds > 1000 || request->reserved[0] != 0 ||
        request->reserved[1] != 0) {
        return make_error(URE_RESULT_INVALID_ARGUMENT, 110,
                          "invalid conformance operation", error);
    }
    try {
        auto operation = std::make_shared<OperationObject>();
        operation->type = ObjectType::Operation;
        operation->thread_policy = URE_THREAD_POLICY_CONCURRENT;
        operation->owner = instance_handle;
        operation->parent = instance_handle;
        operation->instance = instance;
        operation->steps = request->work_steps;
        operation->delay_ms = request->step_delay_milliseconds;
        operation->fail_at_end = request->fail_at_end != 0;
        operation->device_lost_at_end = request->device_lost_at_end != 0;
        *output = handles().insert(operation);
        {
            std::scoped_lock lock(instance->mutex);
            std::erase_if(instance->operations, [](const auto& child) {
                return child.expired();
            });
            instance->operations.push_back(operation);
        }
        operation->worker =
            std::jthread([operation = operation.get(), handle = *output] {
                run_operation(operation, handle);
            });
        return URE_RESULT_SUCCESS;
    } catch (...) {
        if (output && *output) {
            try {
                std::shared_ptr<Object> final_object;
                handles().release(*output, ObjectType::Operation, &final_object);
            } catch (...) {
            }
            *output = nullptr;
        }
        return make_error(URE_RESULT_INTERNAL, 111, "operation allocation failed",
                          error);
    }
}

ure_result_t emit_events_impl(ure_handle_t instance_handle, std::uint32_t count,
                              std::uint32_t event_type, ure_handle_t* error) {
    clear_error(error);
    const auto instance =
        handles().get<InstanceObject>(instance_handle, ObjectType::Instance);
    if (!instance)
        return make_error(URE_RESULT_INVALID_HANDLE, 112, "invalid instance handle",
                          error);
    if (count > 100000 || (event_type != URE_EVENT_OPERATION_STATE &&
                           event_type != URE_EVENT_DIAGNOSTIC)) {
        return make_error(URE_RESULT_INVALID_ARGUMENT, 113,
                          "invalid conformance event emission", error);
    }
    for (std::uint32_t index = 0; index < count; ++index)
        emit_event(instance, event_type, nullptr);
    return URE_RESULT_SUCCESS;
}

ure_result_t validate_operation_owner_impl(ure_handle_t instance_handle,
                                           ure_handle_t operation_handle,
                                           ure_handle_t* error) {
    clear_error(error);
    const auto instance =
        handles().get<InstanceObject>(instance_handle, ObjectType::Instance);
    const auto operation =
        handles().get<OperationObject>(operation_handle, ObjectType::Operation);
    if (!instance || !operation || operation->owner != instance_handle) {
        return make_error(URE_RESULT_INVALID_HANDLE, 113,
                          "operation belongs to a different instance", error);
    }
    return URE_RESULT_SUCCESS;
}

ure_result_t live_handle_count_impl(std::uint64_t* count) {
    if (!count)
        return URE_RESULT_INVALID_ARGUMENT;
    *count = handles().size();
    return URE_RESULT_SUCCESS;
}

ure_result_t fail_error_allocation_impl() {
    fail_next_error_allocation().store(true, std::memory_order_release);
    return URE_RESULT_SUCCESS;
}

ure_result_t operation_retain_impl(ure_handle_t operation,
                                   ure_handle_t* error) {
    clear_error(error);
    return handles().retain(operation, ObjectType::Operation)
               ? URE_RESULT_SUCCESS
               : make_error(URE_RESULT_INVALID_HANDLE, 113,
                            "invalid operation handle", error);
}

ure_result_t operation_release_impl(ure_handle_t operation,
                                    ure_handle_t* error) {
    clear_error(error);
    std::shared_ptr<Object> final_object;
    if (!handles().release(operation, ObjectType::Operation, &final_object)) {
        return make_error(URE_RESULT_INVALID_HANDLE, 114,
                          "invalid operation handle", error);
    }
    if (final_object) {
        const auto object = std::static_pointer_cast<OperationObject>(final_object);
        ure_handle_t terminal_error = nullptr;
        {
            std::scoped_lock lock(object->mutex);
            if (!terminal(object->state))
                object->cancel_requested = true;
            terminal_error = object->terminal_error;
            object->terminal_error = nullptr;
            object->changed.notify_all();
        }
        release_error(terminal_error);
    }
    return URE_RESULT_SUCCESS;
}

ure_result_t operation_get_info_impl(ure_handle_t handle,
                                     ure_operation_info_t* info,
                                     ure_handle_t* error) {
    clear_error(error);
    const auto operation =
        handles().get<OperationObject>(handle, ObjectType::Operation);
    if (!operation)
        return make_error(URE_RESULT_INVALID_HANDLE, 115,
                          "invalid operation handle", error);
    if (!valid_output(info, URE_STRUCTURE_OPERATION_INFO) ||
        info->reserved != 0) {
        return make_error(URE_RESULT_INVALID_ARGUMENT, 116,
                          "invalid operation query", error);
    }
    std::scoped_lock lock(operation->mutex);
    info->state = operation->state;
    info->progress_available = 1;
    info->progress = static_cast<double>(operation->completed) / operation->steps;
    info->stage = URE_OPERATION_START;
    info->completed_work = operation->completed;
    info->total_work = operation->steps;
    info->progress_sequence = operation->progress_sequence;
    info->terminal_error = operation->terminal_error;
    return URE_RESULT_SUCCESS;
}

ure_result_t operation_wait_impl(ure_handle_t handle, std::uint64_t timeout_ns,
                                 ure_handle_t* error) {
    clear_error(error);
    const auto operation =
        handles().get<OperationObject>(handle, ObjectType::Operation);
    if (!operation)
        return make_error(URE_RESULT_INVALID_HANDLE, 116,
                          "invalid operation handle", error);
    std::unique_lock lock(operation->mutex);
    if (!operation->changed.wait_for(
            lock, wait_duration(timeout_ns),
            [&] { return terminal(operation->state); })) {
        return URE_RESULT_TIMEOUT;
    }
    if (operation->state == URE_OPERATION_STATE_CANCELED)
        return URE_RESULT_CANCELED;
    if (operation->state == URE_OPERATION_STATE_DEVICE_LOST) {
        return make_error(URE_RESULT_DEVICE_LOST, 117,
                          "operation reported device loss", error,
                          operation->terminal_error, handle);
    }
    if (operation->state == URE_OPERATION_STATE_FAILED) {
        return make_error(URE_RESULT_INTERNAL, 118, "operation failed", error,
                          operation->terminal_error, handle);
    }
    return URE_RESULT_SUCCESS;
}

ure_result_t operation_cancel_impl(ure_handle_t handle, ure_bool32_t* accepted,
                                   ure_handle_t* error) {
    clear_error(error);
    const auto operation =
        handles().get<OperationObject>(handle, ObjectType::Operation);
    if (!operation)
        return make_error(URE_RESULT_INVALID_HANDLE, 119,
                          "invalid operation handle", error);
    if (!accepted)
        return make_error(URE_RESULT_INVALID_ARGUMENT, 120,
                          "invalid cancellation output", error);
    std::scoped_lock lock(operation->mutex);
    if (terminal(operation->state)) {
        *accepted = 0;
        return URE_RESULT_SUCCESS;
    }
    operation->cancel_requested = true;
    operation->state = URE_OPERATION_STATE_CANCEL_PENDING;
    ++operation->progress_sequence;
    *accepted = 1;
    operation->changed.notify_all();
    return URE_RESULT_SUCCESS;
}

ure_result_t take_event(ure_handle_t instance_handle, std::uint64_t timeout_ns,
                        bool wait, ure_event_record_t* output,
                        ure_handle_t* error) noexcept {
    clear_error(error);
    const auto instance = handles().get<InstanceObject>(
        instance_handle, ObjectType::Instance, true);
    if (!instance)
        return make_error(URE_RESULT_INVALID_HANDLE, 119, "invalid instance handle",
                          error);
    if (!valid_output(output, URE_STRUCTURE_EVENT_RECORD) ||
        output->reserved != 0)
        return make_error(URE_RESULT_INVALID_ARGUMENT, 120, "invalid event query",
                          error);
    std::unique_lock lock(instance->mutex);
    if (wait && !instance->event_ready.wait_for(
                    lock, wait_duration(timeout_ns), [&] {
                        return !instance->events.empty() ||
                               instance->closed.load(std::memory_order_acquire);
                    }))
        return URE_RESULT_TIMEOUT;
    if (instance->events.empty())
        return instance->closed ? URE_RESULT_WORKER_LOST : URE_RESULT_INCOMPLETE;
    const EventData event = instance->events.front();
    instance->events.pop_front();
    output->event_type = event.type;
    output->affected_classes = event.affected_classes;
    output->sequence = event.sequence;
    output->timestamp_ns = event.timestamp_ns;
    output->instance = instance_handle;
    output->operation = event.operation;
    output->first_lost_sequence = event.first_lost;
    output->last_lost_sequence = event.last_lost;
    output->payload_schema = 0;
    output->reserved = 0;
    output->coalesced_count = event.coalesced_count;
    output->payload = {nullptr, 0};
    return URE_RESULT_SUCCESS;
}

ure_result_t event_poll_impl(ure_handle_t instance, ure_event_record_t* output,
                             ure_handle_t* error) {
    return take_event(instance, 0, false, output, error);
}

ure_result_t event_wait_impl(ure_handle_t instance, std::uint64_t timeout_ns,
                             ure_event_record_t* output, ure_handle_t* error) {
    return take_event(instance, timeout_ns, true, output, error);
}

template <class Function>
ure_result_t guard_entry(ure_handle_t* error, Function&& function) noexcept {
    clear_error(error);
    try {
        return function();
    } catch (...) {
        return make_error(URE_RESULT_INTERNAL, 199, "runtime adapter failure",
                          error);
    }
}

template <class Function>
ure_result_t guard_entry(Function&& function) noexcept {
    try {
        return function();
    } catch (...) {
        return URE_RESULT_INTERNAL;
    }
}

ure_result_t URE_CALL create_instance(const ure_instance_create_info_t* info,
                                      ure_handle_t* output,
                                      ure_handle_t* error) noexcept {
    return guard_entry(error,
                       [&] { return create_instance_impl(info, output, error); });
}

ure_result_t URE_CALL instance_retain(ure_handle_t instance,
                                      ure_handle_t* error) noexcept {
    return guard_entry(error,
                       [&] { return instance_retain_impl(instance, error); });
}

ure_result_t URE_CALL instance_release(ure_handle_t instance,
                                       ure_handle_t* error) noexcept {
    return guard_entry(error,
                       [&] { return instance_release_impl(instance, error); });
}

ure_result_t URE_CALL instance_close(ure_handle_t instance,
                                     ure_handle_t* error) noexcept {
    return guard_entry(error,
                       [&] { return instance_close_impl(instance, error); });
}

ure_result_t URE_CALL query_capability(ure_handle_t instance,
                                       const ure_capability_query_t* query,
                                       ure_capability_descriptor_t* descriptor,
                                       ure_handle_t* error) noexcept {
    return guard_entry(error, [&] {
        return query_capability_impl(instance, query, descriptor, error);
    });
}

ure_result_t URE_CALL error_retain(ure_handle_t error) noexcept {
    return guard_entry([&] { return error_retain_impl(error); });
}

ure_result_t URE_CALL error_release(ure_handle_t error) noexcept {
    return guard_entry([&] { return error_release_impl(error); });
}

ure_result_t URE_CALL error_get_info(ure_handle_t error,
                                     ure_error_info_t* info) noexcept {
    return guard_entry([&] { return error_get_info_impl(error, info); });
}

ure_result_t URE_CALL
submit_operation(ure_handle_t instance,
                 const ure_private_conformance_operation_request_t* request,
                 ure_handle_t* output, ure_handle_t* error) noexcept {
    return guard_entry(error, [&] {
        return submit_operation_impl(instance, request, output, error);
    });
}

ure_result_t URE_CALL emit_events(ure_handle_t instance, std::uint32_t count,
                                  std::uint32_t event_type,
                                  ure_handle_t* error) noexcept {
    return guard_entry(error, [&] {
        return emit_events_impl(instance, count, event_type, error);
    });
}

ure_result_t URE_CALL validate_operation_owner(ure_handle_t instance,
                                               ure_handle_t operation,
                                               ure_handle_t* error) noexcept {
    return guard_entry(error, [&] {
        return validate_operation_owner_impl(instance, operation, error);
    });
}

ure_result_t URE_CALL live_handle_count(std::uint64_t* count) noexcept {
    return guard_entry([&] { return live_handle_count_impl(count); });
}

ure_result_t URE_CALL fail_error_allocation() noexcept {
    return guard_entry([&] { return fail_error_allocation_impl(); });
}

ure_result_t URE_CALL operation_retain(ure_handle_t operation,
                                       ure_handle_t* error) noexcept {
    return guard_entry(error,
                       [&] { return operation_retain_impl(operation, error); });
}

ure_result_t URE_CALL operation_release(ure_handle_t operation,
                                        ure_handle_t* error) noexcept {
    return guard_entry(error,
                       [&] { return operation_release_impl(operation, error); });
}

ure_result_t URE_CALL operation_get_info(ure_handle_t operation,
                                         ure_operation_info_t* info,
                                         ure_handle_t* error) noexcept {
    return guard_entry(
        error, [&] { return operation_get_info_impl(operation, info, error); });
}

ure_result_t URE_CALL operation_wait(ure_handle_t operation,
                                     std::uint64_t timeout_ns,
                                     ure_handle_t* error) noexcept {
    return guard_entry(
        error, [&] { return operation_wait_impl(operation, timeout_ns, error); });
}

ure_result_t URE_CALL operation_cancel(ure_handle_t operation,
                                       ure_bool32_t* accepted,
                                       ure_handle_t* error) noexcept {
    return guard_entry(
        error, [&] { return operation_cancel_impl(operation, accepted, error); });
}

ure_result_t URE_CALL event_poll(ure_handle_t instance,
                                 ure_event_record_t* output,
                                 ure_handle_t* error) noexcept {
    return guard_entry(error,
                       [&] { return event_poll_impl(instance, output, error); });
}

ure_result_t URE_CALL event_wait(ure_handle_t instance,
                                 std::uint64_t timeout_ns,
                                 ure_event_record_t* output,
                                 ure_handle_t* error) noexcept {
    return guard_entry(error, [&] {
        return event_wait_impl(instance, timeout_ns, output, error);
    });
}

}

const ure_runtime_interface_t& runtime_interface() noexcept {
    static const ure_runtime_interface_t table{{sizeof(table), 0, 1},
                                               create_instance};
    return table;
}

const ure_instance_interface_t& instance_interface() noexcept {
    static const ure_instance_interface_t table{{sizeof(table), 0, 1},
                                                instance_retain,
                                                instance_release,
                                                instance_close,
                                                query_capability};
    return table;
}

const ure_error_interface_t& error_interface() noexcept {
    static const ure_error_interface_t table{
        {sizeof(table), 0, 1}, error_retain, error_release, error_get_info};
    return table;
}

const ure_operation_interface_t& operation_interface() noexcept {
    static const ure_operation_interface_t table{
        {sizeof(table), 0, 1}, operation_retain, operation_release, operation_get_info, operation_wait, operation_cancel};
    return table;
}

const ure_event_interface_t& event_interface() noexcept {
    static const ure_event_interface_t table{
        {sizeof(table), 0, 1}, event_poll, event_wait};
    return table;
}

const ure_private_conformance_interface_t& conformance_interface() noexcept {
    static const ure_private_conformance_interface_t table{
        {sizeof(table), 0, 1},
        submit_operation,
        emit_events,
        validate_operation_owner,
        live_handle_count,
        fail_error_allocation};
    return table;
}

}
