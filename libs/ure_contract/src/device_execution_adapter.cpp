#include "runtime_adapter.hpp"
#include "runtime_objects.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <string_view>
#include <vector>

#include <ure/backend.hpp>
#include <ure/product/product_service.hpp>

namespace ure::contract {
namespace {

std::uint32_t public_backend(BackendKind kind) noexcept {
    switch (kind) {
    case BackendKind::Cuda:
        return URE_BACKEND_CUDA;
    case BackendKind::Vulkan:
        return URE_BACKEND_VULKAN;
    case BackendKind::D3D12:
        return URE_BACKEND_D3D12;
    default:
        return URE_BACKEND_AUTO;
    }
}

template <std::size_t Size>
std::uint32_t copy_text(char (&output)[Size], std::string_view text) noexcept {
    const auto count = std::min(text.size(), Size);
    std::memset(output, 0, Size);
    if (count != 0)
        std::memcpy(output, text.data(), count);
    return static_cast<std::uint32_t>(count);
}

std::vector<BackendAdapterInfo> adapters() {
    return enumerate_backend_adapters(BackendKind::Auto);
}

ure_result_t enumerate_impl(ure_handle_t instance_handle,
                            std::uint32_t *count, ure_handle_t *error) {
    clear_error(error);
    const auto instance = handles().get<InstanceObject>(
        instance_handle, ObjectType::Instance);
    if (!instance)
        return make_error(URE_RESULT_INVALID_HANDLE, 560,
                          "invalid device enumeration instance", error);
    if (!count || !instance->device_execution_enabled)
        return make_error(URE_RESULT_CAPABILITY_UNAVAILABLE, 561,
                          "device execution capability is not enabled", error);
    try {
        *count = static_cast<std::uint32_t>(adapters().size());
        return URE_RESULT_SUCCESS;
    } catch (const std::exception&) {
        return make_error(URE_RESULT_CAPABILITY_UNAVAILABLE, 561,
                          "device inventory is unavailable", error);
    }
}

ure_result_t descriptor_impl(ure_handle_t instance_handle,
                             std::uint32_t index,
                             ure_device_descriptor_t *descriptor,
                             ure_handle_t *error) {
    clear_error(error);
    const auto instance = handles().get<InstanceObject>(
        instance_handle, ObjectType::Instance);
    if (!instance)
        return make_error(URE_RESULT_INVALID_HANDLE, 560,
                          "invalid device enumeration instance", error);
    if (!instance->device_execution_enabled)
        return make_error(URE_RESULT_CAPABILITY_UNAVAILABLE, 561,
                          "device execution capability is not enabled", error);
    if (!valid_output(descriptor, URE_STRUCTURE_DEVICE_DESCRIPTOR) ||
        descriptor->reserved[0] != 0 || descriptor->reserved[1] != 0)
        return make_error(URE_RESULT_INVALID_ARGUMENT, 562,
                          "invalid device descriptor output", error);
    try {
        const auto inventory = adapters();
        if (index >= inventory.size())
            return make_error(URE_RESULT_INVALID_ARGUMENT, 562,
                              "device descriptor index is out of range", error);
        const auto &adapter = inventory[index];
        const auto identity = product::backend_adapter_identity(adapter);
        descriptor->backend = public_backend(adapter.kind);
        descriptor->provider = URE_PROVIDER_SELF_COMPUTE;
        descriptor->runtime_state = adapter.kind == BackendKind::Cuda
                                        ? URE_RUNTIME_STATE_APPLICABLE
                                        : URE_RUNTIME_STATE_AVAILABLE;
        descriptor->ordinal = adapter.ordinal;
        std::memcpy(descriptor->device_identity.bytes, identity.data(),
                    identity.size());
        descriptor->features = adapter.features;
        descriptor->total_memory_bytes = adapter.memory.total_bytes;
        descriptor->available_memory_bytes = adapter.memory.available_bytes;
        descriptor->applicable_budget_bytes = std::min(
            adapter.memory.available_bytes -
                adapter.memory.available_bytes / 5,
            adapter.memory.total_bytes - adapter.memory.total_bytes / 4);
        descriptor->vendor_id = adapter.vendor_id;
        descriptor->device_id = adapter.device_id;
        descriptor->name_size = copy_text(descriptor->name, adapter.name);
        descriptor->adapter_id_size =
            copy_text(descriptor->adapter_id, adapter.adapter_id);
        descriptor->driver_identity_size = copy_text(
            descriptor->driver_identity, adapter.driver_identity);
        descriptor->compiler_identity_size = copy_text(
            descriptor->compiler_identity, adapter.compiler_identity);
        return URE_RESULT_SUCCESS;
    } catch (const std::exception&) {
        return make_error(URE_RESULT_CAPABILITY_UNAVAILABLE, 561,
                          "device inventory is unavailable", error);
    }
}

ure_result_t URE_CALL enumerate_devices(ure_handle_t instance,
                                        std::uint32_t *count,
                                        ure_handle_t *error) noexcept {
    return guard_entry(error, [&] {
        return enumerate_impl(instance, count, error);
    });
}

ure_result_t URE_CALL get_descriptor(
    ure_handle_t instance, std::uint32_t index,
    ure_device_descriptor_t *descriptor, ure_handle_t *error) noexcept {
    return guard_entry(error, [&] {
        return descriptor_impl(instance, index, descriptor, error);
    });
}

ure_result_t URE_CALL get_job_execution(
    ure_handle_t job, ure_execution_info_t *info,
    ure_handle_t *error) noexcept {
    return guard_entry(error, [&] {
        return product_job_execution_info(job, info, error);
    });
}

}

const ure_device_execution_interface_t &device_execution_interface() noexcept {
    static const ure_device_execution_interface_t table{
        {sizeof(ure_device_execution_interface_t), 0, 1},
        enumerate_devices,
        get_descriptor,
        get_job_execution};
    return table;
}

}
