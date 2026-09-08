#include "runtime_adapter.hpp"
#include "runtime_objects.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <ure/product/scene_tool_service.hpp>

namespace ure::contract {
namespace {

constexpr std::uint64_t kMaximumReportBytes = UINT64_C(524288);

std::string text(ure_string_view_t value) {
    if (value.size == 0)
        return {};
    if (!value.data || value.size > UINT64_C(32768))
        throw std::invalid_argument("invalid UTF-8 string span");
    return {value.data, static_cast<std::size_t>(value.size)};
}

std::filesystem::path path(ure_string_view_t value) {
    const auto utf8 = text(value);
    return std::filesystem::path(std::u8string(
        reinterpret_cast<const char8_t *>(utf8.data()),
        reinterpret_cast<const char8_t *>(utf8.data() + utf8.size())));
}

product::SceneToolOperation operation(std::uint32_t value) {
    switch (value) {
    case URE_SCENE_TOOL_VALIDATE:
        return product::SceneToolOperation::Validate;
    case URE_SCENE_TOOL_INSPECT:
        return product::SceneToolOperation::Inspect;
    case URE_SCENE_TOOL_BUILD:
        return product::SceneToolOperation::Build;
    case URE_SCENE_TOOL_MIGRATE:
        return product::SceneToolOperation::Migrate;
    case URE_SCENE_TOOL_PACK:
        return product::SceneToolOperation::Pack;
    case URE_SCENE_TOOL_UNPACK:
        return product::SceneToolOperation::Unpack;
    case URE_SCENE_TOOL_REALIZE:
        return product::SceneToolOperation::Realize;
    case URE_SCENE_TOOL_MATERIAL_IMPORT:
        return product::SceneToolOperation::ImportMaterialX;
    case URE_SCENE_TOOL_MATERIAL_EXPORT:
        return product::SceneToolOperation::ExportMaterialX;
    case URE_SCENE_TOOL_MATERIAL_PRESET:
        return product::SceneToolOperation::ApplyMaterialPreset;
    default:
        throw std::invalid_argument("unknown scene-tool operation");
    }
}

bool valid_budget(const ure_scene_budget_t &budget) noexcept {
    return budget.header.type == URE_STRUCTURE_SCENE_BUDGET &&
           budget.header.size >= core_1_0_size<ure_scene_budget_t>() &&
           !budget.header.next && budget.max_content_bytes >= 128 &&
           budget.max_content_bytes <= UINT64_C(17179869184) &&
           budget.max_uncompressed_bytes >= budget.max_content_bytes &&
           budget.max_uncompressed_bytes <= UINT64_C(34359738368) &&
           budget.max_resident_bytes >= 16 &&
           budget.max_resident_bytes <= UINT64_C(8589934592) &&
           budget.max_resource_count != 0 &&
           budget.max_resource_count <= UINT64_C(1000000) &&
           budget.max_object_count != 0 &&
           budget.max_object_count <= UINT64_C(10000000) &&
           budget.max_nesting_depth >= 8 &&
           budget.max_nesting_depth <= 64 &&
           budget.max_decompression_ratio != 0 &&
           budget.max_decompression_ratio <= 256 &&
           budget.reserved[0] == 0 && budget.reserved[1] == 0;
}

native_scene::ValidationLimits limits(const ure_scene_budget_t &budget) {
    native_scene::ValidationLimits output;
    output.max_total_stored_bytes = budget.max_content_bytes;
    output.max_total_uncompressed_bytes = budget.max_uncompressed_bytes;
    output.max_resident_resource_bytes = budget.max_resident_bytes;
    output.max_directory_entries = budget.max_resource_count;
    output.max_decompression_ratio = budget.max_decompression_ratio;
    output.max_nesting_depth = budget.max_nesting_depth;
    output.max_object_count = budget.max_object_count;
    return output;
}

bool digest_from_hex(std::string_view value,
                     ure_digest256_t &output) noexcept {
    if (value.size() != 64)
        return false;
    for (std::size_t index = 0; index < 32; ++index) {
        const auto nibble = [](char character) -> int {
            if (character >= '0' && character <= '9')
                return character - '0';
            const auto lower = static_cast<char>(
                std::tolower(static_cast<unsigned char>(character)));
            return lower >= 'a' && lower <= 'f' ? lower - 'a' + 10 : -1;
        };
        const int high = nibble(value[index * 2]);
        const int low = nibble(value[index * 2 + 1]);
        if (high < 0 || low < 0)
            return false;
        output.bytes[index] = static_cast<std::uint8_t>(high * 16 + low);
    }
    return true;
}

std::pair<ure_result_t, std::uint32_t> public_failure(
    const product::ProductSceneDiagnostic &diagnostic) noexcept {
    switch (diagnostic.detail) {
    case 601:
    case 604:
    case 605:
    case 627:
    case 701:
    case 704:
    case 705:
        return {URE_RESULT_CAPABILITY_UNAVAILABLE, diagnostic.detail};
    case 606:
    case 614:
    case 615:
    case 616:
    case 617:
    case 624:
        return {URE_RESULT_BUDGET_EXHAUSTED, diagnostic.detail};
    case 622:
    case 623:
    case 706:
        return {URE_RESULT_INVALID_ARGUMENT, diagnostic.detail};
    default:
        return {URE_RESULT_MALFORMED_DATA, diagnostic.detail};
    }
}

ure_result_t execute_impl(ure_handle_t instance_handle,
                          const ure_scene_tool_request_t *request,
                          ure_scene_tool_result_t *result,
                          ure_handle_t *error) {
    clear_error(error);
    const auto instance = handles().get<InstanceObject>(
        instance_handle, ObjectType::Instance);
    if (!instance)
        return make_error(URE_RESULT_INVALID_HANDLE, 626,
                          "invalid scene-tool instance", error);
    if (!instance->scene_tool_enabled)
        return make_error(URE_RESULT_CAPABILITY_UNAVAILABLE, 627,
                          "scene-tool capability is not enabled", error);
    if (!valid_input(request, URE_STRUCTURE_SCENE_TOOL_REQUEST) ||
        !valid_output(result, URE_STRUCTURE_SCENE_TOOL_RESULT) ||
        request->input_count == 0 || request->input_count > 256 ||
        !request->input_paths || !valid_budget(request->budget) ||
        request->temporary_budget_bytes == 0 ||
        request->temporary_budget_bytes > UINT64_C(17179869184) ||
        request->report_buffer.size > kMaximumReportBytes ||
        (request->report_buffer.size != 0 &&
         !request->report_buffer.data) ||
        request->allow_script_execution > 1 || request->reserved32 != 0 ||
        request->reserved[0] != 0 || request->reserved[1] != 0 ||
        result->reserved32 != 0 || result->reserved[0] != 0 ||
        result->reserved[1] != 0)
        return make_error(URE_RESULT_INVALID_ARGUMENT, 622,
                          "invalid scene-tool request or result", error);
    product::SceneToolRequest internal;
    try {
        internal.operation = operation(request->operation);
        internal.inputs.reserve(request->input_count);
        for (std::uint32_t index = 0; index < request->input_count; ++index)
            internal.inputs.push_back(path(request->input_paths[index]));
        internal.output = path(request->output_path);
        internal.package_scene_id = text(request->package_scene_id);
        internal.material_selector = text(request->material_selector);
        internal.preset_name = text(request->preset_name);
        internal.validation = limits(request->budget);
        internal.temporary_bytes = request->temporary_budget_bytes;
        internal.allow_script_execution = request->allow_script_execution != 0;
    } catch (const std::exception &exception) {
        return make_error(URE_RESULT_INVALID_ARGUMENT, 622,
                          exception.what(), error);
    }
    const auto completed = product::execute_scene_tool(internal);
    const std::string report = completed.report_json();
    result->operation = request->operation;
    result->disposition_count = static_cast<std::uint32_t>(
        std::min<std::size_t>(completed.dispositions.size(), UINT32_MAX));
    result->diagnostic_count = static_cast<std::uint32_t>(
        std::min<std::size_t>(completed.diagnostics.size(), UINT32_MAX));
    std::memcpy(result->snapshot_identity.bytes,
                completed.snapshot_identity.data(),
                completed.snapshot_identity.size());
    result->semantic_identity = {};
    static_cast<void>(
        digest_from_hex(completed.semantic_identity,
                        result->semantic_identity));
    result->stored_bytes = completed.resource_budget.stored_bytes != 0
                               ? completed.resource_budget.stored_bytes
                               : completed.stored_bytes;
    result->decompressed_bytes = completed.resource_budget.decompressed_bytes;
    result->resident_bytes = completed.resource_budget.resident_bytes != 0
                                 ? completed.resource_budget.resident_bytes
                                 : completed.resident_bytes;
    result->streamed_bytes = completed.resource_budget.streamed_bytes;
    result->temporary_bytes = completed.resource_budget.temporary_bytes;
    result->output_bytes = completed.resource_budget.output_bytes;
    result->scene_count = completed.scene_count;
    result->resource_count = completed.resource_count;
    result->cache_count = completed.cache_count;
    result->dependency_count = completed.dependency_count;
    std::memcpy(result->material_program_set_identity.bytes,
                completed.material_program_set_identity.data(),
                completed.material_program_set_identity.size());
    result->material_program_count = completed.material_program_count;
    result->adapter_loss_report_size = completed.adapter_loss_report.size();
    result->report_size = report.size();
    if (report.size() > kMaximumReportBytes)
        return make_error(URE_RESULT_BUDGET_EXHAUSTED, 624,
                          "scene-tool report exceeds its bounded budget", error);
    if (report.size() > request->report_buffer.size)
        return make_error(URE_RESULT_BUFFER_TOO_SMALL, 625,
                          "scene-tool report buffer is too small", error);
    if (!report.empty())
        std::memcpy(request->report_buffer.data, report.data(), report.size());
    if (!completed.ok()) {
        const auto [public_result, detail] =
            public_failure(completed.diagnostics.front());
        return make_error(public_result, detail,
                          completed.diagnostics.front().message, error);
    }
    return URE_RESULT_SUCCESS;
}

ure_result_t URE_CALL execute(ure_handle_t instance,
                              const ure_scene_tool_request_t *request,
                              ure_scene_tool_result_t *result,
                              ure_handle_t *error) noexcept {
    return guard_entry(error, [&] {
        return execute_impl(instance, request, result, error);
    });
}

}

const ure_scene_tool_interface_t &scene_tool_interface() noexcept {
    static const ure_scene_tool_interface_t table{
        {sizeof(ure_scene_tool_interface_t), 0, 2}, execute};
    return table;
}

}
