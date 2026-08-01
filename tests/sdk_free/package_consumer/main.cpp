#include "ure/backend_types.hpp"
#include "ure/config.hpp"
#include "ure/runtime/runtime.hpp"
#include "ure/scene_io.hpp"
#include "ure/transport/semantics.hpp"
#include "ure/transport/legacy_technique_preset.hpp"
#include "ure/research/capability.hpp"

int main() {
    const ure::RenderConfig config;
    const ure::runtime::BufferHandle buffer;
    ure::transport::ObservableDescriptor observable;
    observable.component_count = 8;
    const auto technique_preset =
        ure::transport::compile_legacy_technique_preset(config);
    const ure::research::CapabilityReport report;
    return config.backend.kind == ure::BackendKind::Auto &&
                   !buffer &&
                   technique_preset.executable() &&
                   !report.executable &&
                   ure::transport::validate_observable(observable).ok()
        ? 0
        : 1;
}
