#include "ure/backend_types.hpp"
#include "ure/config.hpp"
#include "ure/runtime/runtime.hpp"
#include "ure/scene_io.hpp"
#include "ure/transport/semantics.hpp"
#include "ure/transport/legacy_technique_preset.hpp"
#include "ure/transport/support_measure_graph.hpp"
#include "ure/research/capability.hpp"
#include "ure/reconstruction/measurement.hpp"

int main() {
    const ure::RenderConfig config;
    const ure::runtime::BufferHandle buffer;
    ure::transport::ObservableDescriptor observable;
    observable.component_count = 8;
    const auto technique_preset =
        ure::transport::compile_legacy_technique_preset(config);
    const ure::transport::PackedMisProgram mis_program;
    const ure::research::CapabilityReport report;
    ure::reconstruction::MeasurementSchema measurement_schema;
    return config.backend.kind == ure::BackendKind::Auto &&
                   !buffer &&
                   technique_preset.executable() &&
                   mis_program.version ==
                       ure::transport::kSupportMeasureGraphVersion &&
                   measurement_schema.version ==
                       ure::reconstruction::kMeasurementSchemaVersion &&
                   !report.executable &&
                   ure::transport::validate_observable(observable).ok()
        ? 0
        : 1;
}
