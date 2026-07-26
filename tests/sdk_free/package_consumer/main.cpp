#include "ure/backend_types.hpp"
#include "ure/config.hpp"
#include "ure/runtime/runtime.hpp"
#include "ure/scene_io.hpp"

int main() {
    const ure::RenderConfig config;
    const ure::runtime::BufferHandle buffer;
    return config.backend.kind == ure::BackendKind::Auto &&
                   !buffer
        ? 0
        : 1;
}
