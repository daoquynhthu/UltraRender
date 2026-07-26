#include "ure/vulkan_runtime.hpp"

int main() {
    const auto adapters =
        ure::vulkan::enumerate_vulkan_adapters();
    return adapters.empty() ? 1 : 0;
}
