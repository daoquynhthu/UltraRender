#ifndef ULTRARENDER_ABI_CORE_1_0_HPP
#define ULTRARENDER_ABI_CORE_1_0_HPP

#include <cstddef>
#include <type_traits>

#include <ultrarender/ure_loader.h>

namespace ure::contract {

template <class T>
constexpr std::size_t core_1_0_size() noexcept {
    return sizeof(T);
}

#define URE_CORE_1_0_SIZE(type, field)                                         \
    template <>                                                               \
    constexpr std::size_t core_1_0_size<type>() noexcept {                    \
        return offsetof(type, field) + sizeof(((type *)nullptr)->field);      \
    }

URE_CORE_1_0_SIZE(ure_bootstrap_diagnostic_t, message_data)
URE_CORE_1_0_SIZE(ure_runtime_manifest_request_t, reserved)
URE_CORE_1_0_SIZE(ure_runtime_manifest_t, abi_manifest_json)
URE_CORE_1_0_SIZE(ure_interface_query_t, reserved)
URE_CORE_1_0_SIZE(ure_interface_response_t, reserved)
URE_CORE_1_0_SIZE(ure_instance_create_info_t, reserved)
URE_CORE_1_0_SIZE(ure_capability_query_t, reserved)
URE_CORE_1_0_SIZE(ure_capability_descriptor_t, reserved)
URE_CORE_1_0_SIZE(ure_error_info_t, build_digest)
URE_CORE_1_0_SIZE(ure_operation_info_t, terminal_error)
URE_CORE_1_0_SIZE(ure_event_record_t, frame)
URE_CORE_1_0_SIZE(ure_instance_frame_budget_t, max_retained_bytes)
URE_CORE_1_0_SIZE(ure_frame_info_t, reserved)
URE_CORE_1_0_SIZE(ure_frame_plane_info_t, reserved)
URE_CORE_1_0_SIZE(ure_frame_map_t, map_token)
URE_CORE_1_0_SIZE(ure_frame_copy_info_t, destination_slice_stride)
URE_CORE_1_0_SIZE(ure_scene_budget_t, reserved)
URE_CORE_1_0_SIZE(ure_native_scene_blob_t, reserved)
URE_CORE_1_0_SIZE(ure_scene_validation_result_t, reserved)
URE_CORE_1_0_SIZE(ure_scene_revision_info_t, reserved)
URE_CORE_1_0_SIZE(ure_objective_envelope_t, reserved)
URE_CORE_1_0_SIZE(ure_session_info_t, reserved)

#undef URE_CORE_1_0_SIZE

template <class T>
bool has_core_abi_bytes(const T *value, std::size_t required) noexcept {
    return value && value->header.size >= required;
}

}

#endif
