#include "ure/backend_types.hpp"
#include "ure/config.hpp"
#include "ure/runtime/runtime.hpp"
#include "ure/scene_io.hpp"
#include "ure/transport/semantics.hpp"
#include "ure/research/capability.hpp"

int main() {
    const ure::RenderConfig config;
    const ure::runtime::BufferHandle buffer;
    ure::transport::ObservableDescriptor observable;
    observable.component_count = 8;
    const ure::research::CapabilityReport report;
    return config.backend.kind == ure::BackendKind::Auto &&
                   !buffer &&
                   !report.executable &&
                   ure::transport::validate_observable(observable).ok()
        ? 0
        : 1;
}
