#ifndef ULTRARENDER_RUNTIME_OBJECTS_HPP
#define ULTRARENDER_RUNTIME_OBJECTS_HPP

#include "runtime_adapter.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace ure::contract {

enum class ObjectType : std::uint32_t {
    Instance = 1,
    Error = 2,
    Operation = 3,
    Frame = 4,
    Scene = 5,
    Session = 6
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
    ure_handle_t frame{};
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
    bool frame_enabled{};
    bool scene_enabled{};
    bool session_enabled{};
    std::uint32_t max_retained_frames{4};
    std::uint64_t max_retained_bytes{UINT64_C(16) * 1024 * 1024};
    std::uint32_t retained_frames{};
    std::uint64_t retained_bytes{};
};

struct OperationObject final : Object {
    ~OperationObject() override {
        if (worker.joinable() && worker.get_id() == std::this_thread::get_id())
            worker.detach();
    }

    std::mutex mutex;
    std::condition_variable changed;
    std::shared_ptr<InstanceObject> instance;
    std::jthread worker;
    std::uint32_t state{URE_OPERATION_STATE_QUEUED};
    std::uint32_t stage{URE_OPERATION_START};
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
    ure_handle_t insert(const std::shared_ptr<T> &object) {
        const std::uint64_t token = next_.fetch_add(1, std::memory_order_relaxed);
        if (token == 0)
            throw std::overflow_error("handle identity exhausted");
        object->generation = token;
        const auto handle =
            reinterpret_cast<ure_handle_t>(static_cast<std::uintptr_t>(token));
        std::scoped_lock lock(mutex_);
        if (!objects_.emplace(token, object).second)
            throw std::overflow_error("handle identity collision");
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

    bool retain(ure_handle_t handle, ObjectType type);
    bool release(ure_handle_t handle, ObjectType type,
                 std::shared_ptr<Object> *final_object = nullptr);
    std::uint64_t size();
    std::uint32_t reference_count(ure_handle_t handle, ObjectType type);
    std::uint64_t child_count(ure_handle_t owner);

  private:
    std::mutex mutex_;
    std::unordered_map<std::uint64_t, std::shared_ptr<Object>> objects_;
    std::atomic<std::uint64_t> next_{1};
};

HandleTable &handles();
std::atomic<bool> &fail_next_error_allocation();
void clear_error(ure_handle_t *error) noexcept;
bool release_error(ure_handle_t handle) noexcept;
ure_result_t make_error(ure_result_t result, std::uint32_t detail,
                        std::string message, ure_handle_t *output,
                        ure_handle_t cause = nullptr,
                        ure_handle_t operation = nullptr) noexcept;

template <class T>
bool valid_input(const T *value, std::uint32_t type) noexcept {
    if (!value || value->header.type != type || value->header.size < sizeof(T))
        return false;
    std::array<const void *, 32> pointers{};
    std::array<std::uint32_t, 32> types{};
    const void *next = value->header.next;
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
bool valid_output(T *value, std::uint32_t type) noexcept {
    return valid_input(reinterpret_cast<const T *>(value), type);
}

std::uint64_t timestamp_ns() noexcept;
void emit_event(const std::shared_ptr<InstanceObject> &instance,
                std::uint32_t type, ure_handle_t operation,
                ure_handle_t frame = nullptr) noexcept;
bool terminal(std::uint32_t state) noexcept;
std::chrono::nanoseconds wait_duration(std::uint64_t timeout_ns) noexcept;

template <class Function>
ure_result_t guard_entry(ure_handle_t *error, Function &&function) noexcept {
    clear_error(error);
    try {
        return function();
    } catch (...) {
        return make_error(URE_RESULT_INTERNAL, 199, "runtime adapter failure",
                          error);
    }
}

template <class Function>
ure_result_t guard_entry(Function &&function) noexcept {
    try {
        return function();
    } catch (...) {
        return URE_RESULT_INTERNAL;
    }
}

}

#endif
