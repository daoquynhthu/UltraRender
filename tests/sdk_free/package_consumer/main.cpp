#include "ure/backend_types.hpp"
#include "ure/config.hpp"
#include "ure/runtime/runtime.hpp"
#include "ure/scene_io.hpp"
#include "ure/transport/semantics.hpp"

int main() {
    const ure::RenderConfig config;
    const ure::runtime::BufferHandle buffer;
    ure::transport::ObservableDescriptor observable;
    observable.component_count = 8;
    return config.backend.kind == ure::BackendKind::Auto &&
                   !buffer &&
                   ure::transport::validate_observable(observable).ok()
        ? 0
        : 1;
}
