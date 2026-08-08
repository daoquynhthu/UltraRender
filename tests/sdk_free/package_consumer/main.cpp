#include "ure/backend_types.hpp"
#include "ure/config.hpp"
#include "ure/runtime/runtime.hpp"
#include "ure/scene_io.hpp"
#include "ure/transport/semantics.hpp"
#include "ure/transport/legacy_technique_preset.hpp"
#include "ure/transport/support_measure_graph.hpp"
#include "ure/transport/pilot.hpp"
#include "ure/transport/portfolio.hpp"
#include "ure/transport/automatic_integrator.hpp"
#include "ure/research/capability.hpp"
#include "ure/research/transport.hpp"
#include "ure/reconstruction/measurement.hpp"
#include "ure/reconstruction/portfolio_measurement.hpp"
#include "ure/reconstruction/statistical_reconstruction.hpp"
#include "ure/reconstruction/sample_reconstruction.hpp"

int main() {
    const ure::RenderConfig config;
    const ure::runtime::BufferHandle buffer;
    ure::transport::ObservableDescriptor observable;
    observable.component_count = 8;
    const auto technique_preset =
        ure::transport::compile_legacy_technique_preset(config);
    const ure::transport::PackedMisProgram mis_program;
    const ure::transport::PilotSamplingProvenance pilot_provenance;
    const ure::transport::PortfolioSchedule portfolio_schedule;
    const ure::transport::AutomaticIntegratorPlan automatic_plan;
    const ure::research::CapabilityReport report;
    const ure::research::TransportResearchDescriptor research_descriptor;
    ure::reconstruction::MeasurementSchema measurement_schema;
    const ure::reconstruction::StatisticalReconstructionConfig
        reconstruction_config;
    const ure::reconstruction::SampleReconstructionConfig
        sample_reconstruction_config;
    return config.backend.kind == ure::BackendKind::Auto &&
                   !buffer &&
                   technique_preset.executable() &&
                   mis_program.version ==
                       ure::transport::kSupportMeasureGraphVersion &&
                   pilot_provenance.version ==
                       ure::transport::kPilotContractVersion &&
                   portfolio_schedule.version ==
                       ure::transport::kPortfolioContractVersion &&
                   automatic_plan.version ==
                       ure::transport::kAutomaticIntegratorContractVersion &&
                   measurement_schema.version ==
                       ure::reconstruction::kMeasurementSchemaVersion &&
                   reconstruction_config.version ==
                       ure::reconstruction::kStatisticalReconstructionVersion &&
                   sample_reconstruction_config.version ==
                       ure::reconstruction::kSampleReconstructionVersion &&
                   research_descriptor.version ==
                       ure::research::kTransportResearchContractVersion &&
                   !report.executable &&
                   ure::transport::validate_observable(observable).ok()
        ? 0
        : 1;
}
