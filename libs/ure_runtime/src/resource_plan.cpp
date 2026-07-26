#include "ure/runtime/resource_plan.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <type_traits>
#include <unordered_map>

namespace ure::runtime {
namespace {

std::uint64_t checked_add(std::uint64_t left, std::uint64_t right) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        throw Error(ErrorCode::Overflow, "resource size addition overflow");
    }
    return left + right;
}

std::uint64_t checked_multiply(std::uint64_t left, std::uint64_t right) {
    if (right != 0 &&
        left > std::numeric_limits<std::uint64_t>::max() / right) {
        throw Error(ErrorCode::Overflow, "resource size multiplication overflow");
    }
    return left * right;
}

bool power_of_two(std::uint64_t value) {
    return value != 0 && (value & (value - 1)) == 0;
}

std::uint64_t format_bytes(Format format) {
    switch (format) {
    case Format::R32Float:
    case Format::R32Uint:
        return 4;
    case Format::Rg32Float:
        return 8;
    case Format::Rgba16Float:
        return 8;
    case Format::Rgba32Float:
        return 16;
    }
    throw Error(ErrorCode::InvalidArgument, "resource format is invalid");
}

void validate_residency(
    const ResourceDesc& desc,
    std::uint64_t logical_bytes) {
    if (desc.residency.priority > 7 ||
        desc.residency.minimum_bytes > desc.residency.maximum_bytes ||
        desc.residency.maximum_bytes > logical_bytes) {
        throw Error(ErrorCode::InvalidArgument, "resource residency is invalid");
    }
    if (desc.residency.mode == resource::ResidencyMode::Resident) {
        if (desc.residency.minimum_bytes != logical_bytes ||
            desc.residency.maximum_bytes != logical_bytes ||
            desc.sparse) {
            throw Error(
                ErrorCode::InvalidArgument,
                "resident resource must cover its complete layout");
        }
        return;
    }
    if (desc.residency.mode == resource::ResidencyMode::SparseTiled) {
        if (!desc.sparse || desc.sparse->tile_bytes == 0 ||
            desc.sparse->tile_count == 0) {
            throw Error(
                ErrorCode::InvalidArgument,
                "sparse resource requires a tile layout");
        }
        const auto tiled_bytes = checked_multiply(
            desc.sparse->tile_bytes,
            desc.sparse->tile_count);
        if (checked_add(tiled_bytes, desc.sparse->mip_tail_bytes) !=
            logical_bytes) {
            throw Error(
                ErrorCode::InvalidArgument,
                "sparse tile coverage does not match resource layout");
        }
    } else if (desc.sparse) {
        throw Error(
            ErrorCode::InvalidArgument,
            "non-sparse resource contains a tile layout");
    }
}

}

std::uint64_t resource_size_bytes(const ResourceLayout& layout) {
    return std::visit(
        [](const auto& value) -> std::uint64_t {
            using Type = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Type, BufferLayout>) {
                if (value.size_bytes == 0 ||
                    !power_of_two(value.alignment) ||
                    value.alignment > (1ull << 30)) {
                    throw Error(
                        ErrorCode::InvalidArgument,
                        "buffer resource layout is invalid");
                }
                return value.size_bytes;
            } else if constexpr (std::is_same_v<Type, ImageLayout>) {
                validate(value.image);
                const auto expected_subresources = checked_multiply(
                    value.image.mip_levels,
                    value.image.array_layers);
                if (value.subresources.size() != expected_subresources) {
                    throw Error(
                        ErrorCode::InvalidArgument,
                        "image subresource count is invalid");
                }
                std::uint64_t previous_end = 0;
                for (std::uint32_t layer = 0;
                     layer < value.image.array_layers;
                     ++layer) {
                    for (std::uint32_t mip = 0;
                         mip < value.image.mip_levels;
                         ++mip) {
                        const auto index =
                            static_cast<std::size_t>(layer) *
                                value.image.mip_levels +
                            mip;
                        const auto& subresource =
                            value.subresources[index];
                        const auto width =
                            std::max(1u, value.image.width >> mip);
                        const auto height =
                            std::max(1u, value.image.height >> mip);
                        const auto depth =
                            std::max(1u, value.image.depth >> mip);
                        const auto minimum_row = checked_multiply(
                            width,
                            format_bytes(value.image.format));
                        if (subresource.mip_level != mip ||
                            subresource.array_layer != layer ||
                            subresource.offset_bytes < previous_end ||
                            subresource.row_pitch_bytes < minimum_row ||
                            subresource.slice_pitch_bytes <
                                checked_multiply(
                                    subresource.row_pitch_bytes,
                                    height)) {
                            throw Error(
                                ErrorCode::InvalidArgument,
                                "image subresource layout is invalid");
                        }
                        previous_end = checked_add(
                            subresource.offset_bytes,
                            checked_multiply(
                                subresource.slice_pitch_bytes,
                                depth));
                    }
                }
                return previous_end;
            } else {
                if (value.texel_count == 0 ||
                    value.source_sample_count == 0 ||
                    value.domain_bins == 0 ||
                    !std::isfinite(value.wavelength_min_nm) ||
                    !std::isfinite(value.wavelength_max_nm) ||
                    value.wavelength_max_nm <= value.wavelength_min_nm) {
                    throw Error(
                        ErrorCode::InvalidArgument,
                        "spectral resource layout is invalid");
                }
                const auto texel_bytes = checked_multiply(
                    value.source_sample_count,
                    sizeof(float));
                if (value.row_pitch_bytes < texel_bytes) {
                    throw Error(
                        ErrorCode::InvalidArgument,
                        "spectral resource pitch is invalid");
                }
                return checked_multiply(
                    value.texel_count,
                    value.row_pitch_bytes);
            }
        },
        layout);
}

UploadPlanSummary validate(const UploadPlan& plan) {
    if (plan.resources.empty()) {
        throw Error(ErrorCode::InvalidArgument, "upload plan has no resources");
    }

    std::map<resource::ResourceId, const ResourceDesc*> resources;
    std::map<resource::ResourceId, std::uint64_t> sizes;
    UploadPlanSummary summary;
    for (const auto& desc : plan.resources) {
        if (!desc.id || !resources.emplace(desc.id, &desc).second) {
            throw Error(
                ErrorCode::InvalidArgument,
                "resource id is empty or duplicated");
        }
        const auto size = resource_size_bytes(desc.layout);
        validate_residency(desc, size);
        sizes.emplace(desc.id, size);
        summary.logical_bytes = checked_add(summary.logical_bytes, size);
        summary.minimum_resident_bytes = checked_add(
            summary.minimum_resident_bytes,
            desc.residency.minimum_bytes);
        summary.maximum_resident_bytes = checked_add(
            summary.maximum_resident_bytes,
            desc.residency.maximum_bytes);
    }
    if (summary.minimum_resident_bytes > plan.budget_bytes) {
        throw Error(ErrorCode::OutOfMemory, "upload plan exceeds memory budget");
    }

    std::map<resource::ResourceId, std::uint8_t> dependency_state;
    std::function<void(resource::ResourceId)> visit =
        [&](resource::ResourceId id) {
            if (dependency_state[id] == 1) {
                throw Error(
                    ErrorCode::InvalidArgument,
                    "resource dependency graph has a cycle");
            }
            if (dependency_state[id] == 2) return;
            dependency_state[id] = 1;
            for (const auto dependency : resources.at(id)->dependencies) {
                if (!resources.contains(dependency)) {
                    throw Error(
                        ErrorCode::InvalidArgument,
                        "resource dependency is missing");
                }
                visit(dependency);
            }
            dependency_state[id] = 2;
        };
    for (const auto& [id, desc] : resources) {
        static_cast<void>(desc);
        visit(id);
    }

    std::map<resource::ResourceId, std::uint64_t> previous_end;
    std::map<resource::ResourceId, std::uint64_t> uploaded;
    std::optional<resource::ResourceId> previous_resource;
    for (const auto& chunk : plan.chunks) {
        const auto resource_it = resources.find(chunk.resource);
        if (resource_it == resources.end() || chunk.size_bytes == 0) {
            throw Error(
                ErrorCode::InvalidArgument,
                "upload chunk references an invalid resource");
        }
        if (previous_resource && chunk.resource < *previous_resource) {
            throw Error(
                ErrorCode::InvalidArgument,
                "upload chunks are not deterministically ordered");
        }
        previous_resource = chunk.resource;
        const auto source_end = checked_add(
            chunk.source_offset,
            chunk.size_bytes);
        const auto destination_end = checked_add(
            chunk.destination_offset,
            chunk.size_bytes);
        if (source_end > plan.source_size_bytes ||
            destination_end > sizes.at(chunk.resource) ||
            chunk.destination_offset < previous_end[chunk.resource]) {
            throw Error(
                ErrorCode::Overflow,
                "upload chunk exceeds or overlaps its bounds");
        }
        const auto& desc = *resource_it->second;
        if (desc.residency.mode == resource::ResidencyMode::SparseTiled) {
            if (!chunk.tile_index ||
                *chunk.tile_index >= desc.sparse->tile_count ||
                chunk.destination_offset !=
                    checked_multiply(
                        *chunk.tile_index,
                        desc.sparse->tile_bytes) ||
                chunk.size_bytes != desc.sparse->tile_bytes) {
                throw Error(
                    ErrorCode::InvalidArgument,
                    "sparse upload chunk does not match a tile");
            }
        } else if (chunk.tile_index) {
            throw Error(
                ErrorCode::InvalidArgument,
                "linear resource upload contains a tile index");
        }
        previous_end[chunk.resource] = destination_end;
        uploaded[chunk.resource] = checked_add(
            uploaded[chunk.resource],
            chunk.size_bytes);
        summary.initial_upload_bytes = checked_add(
            summary.initial_upload_bytes,
            chunk.size_bytes);
    }
    for (const auto& [id, desc] : resources) {
        if (uploaded[id] < desc->residency.minimum_bytes ||
            uploaded[id] > desc->residency.maximum_bytes) {
            throw Error(
                ErrorCode::InvalidArgument,
                "initial upload violates resource residency");
        }
    }
    return summary;
}

}
