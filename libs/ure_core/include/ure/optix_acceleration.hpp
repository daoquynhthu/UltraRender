#pragma once

#include <memory>

#include "ure/runtime/acceleration.hpp"
#include "ure/runtime/runtime.hpp"

namespace ure::gpu {

bool optix_acceleration_available() noexcept;
std::unique_ptr<runtime::AccelerationProvider>
make_optix_acceleration_provider(runtime::Device& device);

}
