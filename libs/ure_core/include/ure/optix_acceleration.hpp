#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "ure/runtime/acceleration.hpp"
#include "ure/runtime/runtime.hpp"

namespace ure::gpu {

struct OptixGeometryTraceDesc {
    runtime::BufferHandle normals;
    runtime::BufferHandle texcoords;
    runtime::BufferHandle tangents;
};

struct OptixAccelerationTraceDesc {
    std::span<const OptixGeometryTraceDesc> geometries;
    runtime::BufferHandle rays;
    runtime::BufferHandle hits;
    runtime::BufferHandle framebuffer;
    std::uint32_t ray_count = 0;
    std::span<const std::byte> module_code;
};

bool optix_acceleration_available() noexcept;
std::unique_ptr<runtime::AccelerationProvider>
make_optix_acceleration_provider(runtime::Device& device);
void trace_optix_acceleration_scene(
    runtime::AccelerationProvider& provider,
    runtime::AccelerationSceneHandle scene,
    const OptixAccelerationTraceDesc& desc);

}
