#include "ure/runtime/runtime.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace ure::runtime {
namespace {

bool power_of_two(std::uint64_t value) {
    return value != 0 && (value & (value - 1)) == 0;
}

void checked_multiply(std::uint64_t& value, std::uint64_t factor) {
    if (factor != 0 &&
        value > std::numeric_limits<std::uint64_t>::max() / factor) {
        throw Error(ErrorCode::Overflow, "descriptor size overflow");
    }
    value *= factor;
}

template <typename HandleType>
void require_handle(HandleType handle, std::string_view label) {
    if (!handle) {
        throw Error(
            ErrorCode::InvalidHandle,
            std::string(label) + " handle is invalid");
    }
}

}

Error::Error(ErrorCode code, std::string message)
    : std::runtime_error(std::move(message)), code_(code) {}

ErrorCode Error::code() const noexcept {
    return code_;
}

void validate(const QueueDesc& desc) {
    if (desc.priority > 3) {
        throw Error(ErrorCode::InvalidArgument, "queue priority exceeds 3");
    }
}

void validate(const BufferDesc& desc) {
    if (desc.size_bytes == 0) {
        throw Error(ErrorCode::InvalidArgument, "buffer size is zero");
    }
    if (!power_of_two(desc.alignment) || desc.alignment > (1ull << 30)) {
        throw Error(ErrorCode::InvalidArgument, "buffer alignment is invalid");
    }
    if (desc.usage == BufferUsage::None) {
        throw Error(ErrorCode::InvalidArgument, "buffer usage is empty");
    }
}

void validate(const ImageDesc& desc) {
    if (desc.width == 0 || desc.height == 0 || desc.depth == 0 ||
        desc.mip_levels == 0 || desc.array_layers == 0 ||
        desc.usage == ImageUsage::None) {
        throw Error(ErrorCode::InvalidArgument, "image descriptor is empty");
    }
    if (desc.dimension == ImageDimension::One &&
        (desc.height != 1 || desc.depth != 1)) {
        throw Error(ErrorCode::InvalidArgument, "1D image extent is invalid");
    }
    if (desc.dimension == ImageDimension::Two && desc.depth != 1) {
        throw Error(ErrorCode::InvalidArgument, "2D image depth is invalid");
    }
    const auto largest_extent =
        std::max({desc.width, desc.height, desc.depth});
    std::uint32_t maximum_mips = 1;
    for (auto extent = largest_extent; extent > 1; extent >>= 1) {
        ++maximum_mips;
    }
    if (desc.mip_levels > maximum_mips) {
        throw Error(
            ErrorCode::InvalidArgument,
            "image mip count exceeds extent");
    }
    std::uint64_t elements = desc.width;
    checked_multiply(elements, desc.height);
    checked_multiply(elements, desc.depth);
    checked_multiply(elements, desc.array_layers);
    checked_multiply(elements, 16);
}

void validate(const SamplerDesc& desc) {
    if (!std::isfinite(desc.min_lod) || !std::isfinite(desc.max_lod) ||
        desc.min_lod < 0.0f || desc.max_lod < desc.min_lod) {
        throw Error(ErrorCode::InvalidArgument, "sampler LOD range is invalid");
    }
}

void validate(
    const ModuleDesc& desc,
    std::span<const std::byte> code) {
    if (code.empty()) {
        throw Error(ErrorCode::InvalidArgument, "module code is empty");
    }
    if (desc.compiler_identity.empty()) {
        throw Error(
            ErrorCode::InvalidArgument,
            "module compiler identity is empty");
    }
    if (std::ranges::all_of(
            desc.content_hash,
            [](std::byte value) { return value == std::byte{}; })) {
        throw Error(ErrorCode::InvalidArgument, "module hash is empty");
    }
}

void validate(const PipelineDesc& desc) {
    require_handle(desc.module, "module");
    if (desc.entry_point.empty()) {
        throw Error(ErrorCode::InvalidArgument, "entry point is empty");
    }
    std::uint64_t threads = 1;
    for (const auto dimension : desc.workgroup_size) {
        if (dimension == 0) {
            throw Error(
                ErrorCode::InvalidArgument,
                "workgroup dimension is zero");
        }
        checked_multiply(threads, dimension);
    }
    if (threads > 1024) {
        throw Error(
            ErrorCode::InvalidArgument,
            "workgroup exceeds portable limit");
    }
    std::unordered_set<std::uint32_t> binding_slots;
    for (const auto& binding : desc.bindings) {
        if (!binding_slots.insert(binding.slot).second) {
            throw Error(
                ErrorCode::InvalidArgument,
                "pipeline binding slot is duplicated");
        }
    }
    std::unordered_set<std::uint32_t> specialization_ids;
    for (const auto& constant : desc.specialization) {
        if (!specialization_ids.insert(constant.id).second) {
            throw Error(
                ErrorCode::InvalidArgument,
                "specialization constant id is duplicated");
        }
        if (constant.size_bytes != 1 &&
            constant.size_bytes != 2 &&
            constant.size_bytes != 4 &&
            constant.size_bytes != 8) {
            throw Error(
                ErrorCode::InvalidArgument,
                "specialization constant size is invalid");
        }
    }
}

void validate(const DispatchGraph& graph) {
    if (graph.nodes.empty()) {
        throw Error(ErrorCode::InvalidArgument, "dispatch graph is empty");
    }
    std::unordered_map<std::uint32_t, const GraphNode*> nodes;
    for (const auto& node : graph.nodes) {
        if (!nodes.emplace(node.id, &node).second) {
            throw Error(ErrorCode::InvalidArgument, "duplicate graph node id");
        }
        std::visit(
            [](const auto& command) {
                using Type = std::decay_t<decltype(command)>;
                if constexpr (std::is_same_v<Type, DispatchCommand>) {
                    require_handle(command.pipeline, "pipeline");
                    for (const auto groups : command.groups) {
                        if (groups == 0) {
                            throw Error(
                                ErrorCode::InvalidArgument,
                                "dispatch group is zero");
                        }
                    }
                    std::unordered_set<std::uint32_t> slots;
                    for (const auto& binding : command.bindings) {
                        std::visit(
                            [&](const auto& value) {
                                using BindingType =
                                    std::decay_t<decltype(value)>;
                                if constexpr (std::is_same_v<
                                                  BindingType,
                                                  BufferBinding>) {
                                    require_handle(
                                        value.buffer, "dispatch buffer");
                                    if (value.size == 0) {
                                        throw Error(
                                            ErrorCode::InvalidArgument,
                                            "buffer binding size is zero");
                                    }
                                } else {
                                    require_handle(
                                        value.image, "dispatch image");
                                    if (value.sampler) {
                                        require_handle(
                                            *value.sampler,
                                            "dispatch sampler");
                                    }
                                }
                                if (!slots.insert(value.slot).second) {
                                    throw Error(
                                        ErrorCode::InvalidArgument,
                                        "duplicate binding slot");
                                }
                            },
                            binding);
                    }
                } else if constexpr (
                    std::is_same_v<Type, CopyBufferCommand>) {
                    require_handle(command.source, "copy source");
                    require_handle(command.destination, "copy destination");
                    if (command.size == 0) {
                        throw Error(
                            ErrorCode::InvalidArgument,
                            "copy size is zero");
                    }
                } else if constexpr (
                    std::is_same_v<Type, BufferBarrierCommand>) {
                    require_handle(command.buffer, "barrier buffer");
                } else {
                    require_handle(command.event, "event");
                }
            },
            node.command);
    }
    for (const auto& node : graph.nodes) {
        for (const auto dependency : node.dependencies) {
            if (!nodes.contains(dependency)) {
                throw Error(
                    ErrorCode::InvalidArgument,
                    "graph dependency is missing");
            }
        }
    }
    std::unordered_map<std::uint32_t, std::uint8_t> state;
    std::function<void(std::uint32_t)> visit = [&](std::uint32_t id) {
        if (state[id] == 1) {
            throw Error(ErrorCode::InvalidArgument, "dispatch graph has cycle");
        }
        if (state[id] == 2) return;
        state[id] = 1;
        for (const auto dependency : nodes.at(id)->dependencies) {
            visit(dependency);
        }
        state[id] = 2;
    };
    for (const auto& node : graph.nodes) visit(node.id);
}

}
