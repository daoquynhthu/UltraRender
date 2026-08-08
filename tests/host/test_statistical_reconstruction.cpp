#include "ure/reconstruction/statistical_reconstruction.hpp"

#include "ure/runtime/multi_backend.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace rec = ure::reconstruction;

static int failures = 0;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", \
                         __FILE__, __LINE__, #condition); \
            ++failures; \
        } \
    } while (false)

static ure::semantic::IdentityDigest id(std::string_view value) {
    return ure::runtime::identity_digest(value);
}

static ure::transport::ObservableDescriptor observable(
    ure::transport::ValueDomain domain,
    std::uint32_t components) {
    ure::transport::ObservableDescriptor value;
    value.kind = domain == ure::transport::ValueDomain::Stokes
        ? ure::transport::ObservableKind::StokesRadiance
        : ure::transport::ObservableKind::SpectralRadiance;
    value.value_domain = domain;
    value.coherence = ure::transport::CoherenceClass::Incoherent;
    value.component_count = components;
    value.unit.dimension.length = -1;
    value.unit.dimension.mass = 1;
    value.unit.dimension.time = -3;
    return value;
}

static rec::StatisticalReconstructionFrame frame(
    std::uint32_t width,
    std::uint32_t height,
    ure::transport::ValueDomain domain,
    std::uint32_t components,
    double signal = 1.0) {
    rec::StatisticalReconstructionFrame value;
    value.width = width;
    value.height = height;
    value.component_count = components;
    value.observable = observable(domain, components);
    value.identities.world_definition = id("world-definition");
    value.identities.world_state = id("world-state");
    value.identities.time_sample = id("time-sample");
    value.identities.observation_snapshot = id("snapshot");
    value.identities.technique_graph = id("technique-graph");
    value.identities.measurement_schema = id("measurement-schema");
    value.measurement_schema_identity =
        value.identities.measurement_schema;
    const auto pixels = static_cast<std::size_t>(width) * height;
    value.raw_estimate.assign(pixels * components, signal);
    value.sample_variance.assign(pixels * components, 1.0);
    value.effective_sample_count.assign(pixels, 4.0);
    value.tail_frequency.assign(pixels, 0.0);
    value.maximum_absolute_contribution.assign(pixels, signal);
    value.normal.assign(pixels * 3, 0.0);
    value.albedo.assign(pixels * components, 0.5);
    value.depth.assign(pixels, 1.0);
    value.motion.assign(pixels * 2, 0.0);
    value.motion_time_confidence.assign(pixels, 1.0);
    value.validity.assign(pixels, 1);
    for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
        value.normal[pixel * 3 + 2] = 1.0;
    }
    rec::finalize_statistical_reconstruction_frame(value);
    return value;
}

static rec::StatisticalReconstructionConfig config() {
    rec::StatisticalReconstructionConfig value;
    rec::finalize_statistical_reconstruction_config(value);
    return value;
}

static double mse(const std::vector<double>& values, double target) {
    double result = 0.0;
    for (const double value : values) {
        const double error = value - target;
        result += error * error;
    }
    return result / static_cast<double>(values.size());
}

static void test_spatial_spectral_baseline() {
    auto input = frame(
        5, 5, ure::transport::ValueDomain::Spectrum, 3, 2.0);
    for (std::size_t index = 0; index < input.raw_estimate.size(); ++index) {
        input.raw_estimate[index] += (index & 1u) == 0 ? 0.75 : -0.75;
        input.sample_variance[index] = 4.0;
        input.effective_sample_count[index / 3] = 1.0;
    }
    rec::finalize_statistical_reconstruction_frame(input);
    const auto settings = config();
    const auto output = rec::reconstruct_statistics(input, settings);
    CHECK(output.raw_estimate == input.raw_estimate);
    CHECK(mse(output.reconstructed, 2.0) <
          mse(output.raw_estimate, 2.0));
    CHECK(output.spatial_support[12] > 1.0);
    CHECK(output.uncertainty[12 * 3] < 2.0);
    CHECK(std::ranges::all_of(
        output.reconstructed, [](double value) { return value >= 0.0; }));
    const auto repeated = rec::reconstruct_statistics(input, settings);
    CHECK(repeated.output_identity == output.output_identity);
}

static void test_tail_classification() {
    auto heavy = frame(
        3, 3, ure::transport::ValueDomain::Spectrum, 1, 1.0);
    heavy.raw_estimate[4] = 20.0;
    heavy.sample_variance[4] = 1600.0;
    heavy.effective_sample_count[4] = 1.0;
    heavy.tail_frequency[4] = 0.5;
    heavy.maximum_absolute_contribution[4] = 400.0;
    rec::finalize_statistical_reconstruction_frame(heavy);
    const auto settings = config();
    const auto heavy_output = rec::reconstruct_statistics(heavy, settings);
    CHECK(heavy_output.tail_class[4] ==
          rec::ReconstructionTailClass::HeavyTail);
    CHECK(heavy_output.raw_estimate[4] == 20.0);
    CHECK(heavy_output.reconstructed[4] < 10.0);

    auto energy = frame(
        3, 3, ure::transport::ValueDomain::Spectrum, 1, 1.0);
    energy.raw_estimate[4] = 20.0;
    energy.sample_variance[4] = 0.01;
    energy.maximum_absolute_contribution[4] = 20.0;
    rec::finalize_statistical_reconstruction_frame(energy);
    const auto energy_output = rec::reconstruct_statistics(energy, settings);
    CHECK(energy_output.tail_class[4] ==
          rec::ReconstructionTailClass::HighEnergyPreserved);
    CHECK(energy_output.reconstructed[4] > 19.0);
}

static void test_temporal_confidence_and_rejection() {
    const auto settings = config();
    auto previous = frame(
        3, 3, ure::transport::ValueDomain::Spectrum, 1, 2.0);
    previous.sample_variance.assign(9, 1.0);
    previous.effective_sample_count.assign(9, 4.0);
    rec::finalize_statistical_reconstruction_frame(previous);
    const auto previous_output =
        rec::reconstruct_statistics(previous, settings);
    const auto history = rec::make_statistical_reconstruction_history(
        previous, previous_output);
    CHECK(rec::validate_statistical_reconstruction_history(history).ok());

    auto current = frame(
        3, 3, ure::transport::ValueDomain::Spectrum, 1, 4.0);
    current.sample_variance.assign(9, 4.0);
    current.effective_sample_count.assign(9, 1.0);
    rec::finalize_statistical_reconstruction_frame(current);
    const auto accumulated =
        rec::reconstruct_statistics(current, settings, &history);
    CHECK(accumulated.rejection_reason[4] ==
          rec::ReconstructionRejectionReason::None);
    CHECK(accumulated.history_confidence[4] > 0.0);
    CHECK(accumulated.history_length[4] == 2);
    CHECK(accumulated.reconstructed[4] > 2.0);
    CHECK(accumulated.reconstructed[4] < 4.0);
    CHECK(accumulated.uncertainty[4] < 2.0);

    auto low_confidence = current;
    low_confidence.motion_time_confidence[4] = 0.1;
    rec::finalize_statistical_reconstruction_frame(low_confidence);
    const auto attenuated =
        rec::reconstruct_statistics(low_confidence, settings, &history);
    CHECK(attenuated.history_confidence[4] <
          accumulated.history_confidence[4]);

    auto disoccluded = current;
    disoccluded.depth[4] = 2.0;
    rec::finalize_statistical_reconstruction_frame(disoccluded);
    const auto rejected =
        rec::reconstruct_statistics(disoccluded, settings, &history);
    CHECK(rejected.rejection_reason[4] ==
          rec::ReconstructionRejectionReason::DisoccludedDepth);
    CHECK(rejected.history_confidence[4] == 0.0);

    auto incompatible = current;
    incompatible.identities.technique_graph = id("other-graph");
    rec::finalize_statistical_reconstruction_frame(incompatible);
    const auto mismatch =
        rec::reconstruct_statistics(incompatible, settings, &history);
    CHECK(mismatch.rejection_reason[4] ==
          rec::ReconstructionRejectionReason::HistoryIdentityMismatch);

    auto invalid_motion = current;
    invalid_motion.motion[8] = std::numeric_limits<double>::quiet_NaN();
    rec::finalize_statistical_reconstruction_frame(invalid_motion);
    CHECK(rec::validate_statistical_reconstruction_frame(invalid_motion).ok());
    const auto motion_rejected =
        rec::reconstruct_statistics(invalid_motion, settings, &history);
    CHECK(motion_rejected.rejection_reason[4] ==
          rec::ReconstructionRejectionReason::InvalidMotion);

    auto zero_confidence = current;
    zero_confidence.motion_time_confidence[4] = 0.0;
    rec::finalize_statistical_reconstruction_frame(zero_confidence);
    const auto confidence_rejected =
        rec::reconstruct_statistics(zero_confidence, settings, &history);
    CHECK(confidence_rejected.rejection_reason[4] ==
          rec::ReconstructionRejectionReason::InvalidMotion);
}

static void test_stokes_physical_domain() {
    auto input = frame(
        3, 3, ure::transport::ValueDomain::Stokes, 4, 0.0);
    for (std::size_t pixel = 0; pixel < 9; ++pixel) {
        input.raw_estimate[pixel * 4] = 5.0 + 0.1 * pixel;
        input.raw_estimate[pixel * 4 + 1] = 3.0;
        input.raw_estimate[pixel * 4 + 2] = 0.5;
        input.raw_estimate[pixel * 4 + 3] = 0.25;
        input.maximum_absolute_contribution[pixel] =
            input.raw_estimate[pixel * 4];
    }
    rec::finalize_statistical_reconstruction_frame(input);
    const auto output = rec::reconstruct_statistics(input, config());
    CHECK(output.raw_estimate == input.raw_estimate);
    for (std::size_t pixel = 0; pixel < 9; ++pixel) {
        const double intensity = output.reconstructed[pixel * 4];
        const double q = output.reconstructed[pixel * 4 + 1];
        const double u = output.reconstructed[pixel * 4 + 2];
        const double v = output.reconstructed[pixel * 4 + 3];
        CHECK(std::sqrt(q * q + u * u + v * v) <= intensity + 1e-10);
    }
}

static void test_invalid_and_tampered_inputs() {
    auto input = frame(
        3, 3, ure::transport::ValueDomain::Spectrum, 1, 1.0);
    input.validity[4] = 0;
    input.raw_estimate[4] = std::numeric_limits<double>::quiet_NaN();
    rec::finalize_statistical_reconstruction_frame(input);
    CHECK(rec::validate_statistical_reconstruction_frame(input).ok());
    const auto settings = config();
    const auto output = rec::reconstruct_statistics(input, settings);
    CHECK(std::isnan(output.raw_estimate[4]));
    CHECK(std::isfinite(output.reconstructed[4]));
    CHECK(output.validity[4] == 0);
    CHECK(output.tail_class[4] ==
          rec::ReconstructionTailClass::InvalidSample);
    CHECK(output.rejection_reason[4] ==
          rec::ReconstructionRejectionReason::InvalidCurrentSample);

    auto tampered = input;
    tampered.depth[0] = 3.0;
    CHECK(rec::validate_statistical_reconstruction_frame(tampered).has(
        rec::StatisticalReconstructionIssue::Identity));
    auto invalid_config = settings;
    invalid_config.signal_sigma = 0.0;
    CHECK(rec::validate_statistical_reconstruction_config(invalid_config).has(
        rec::StatisticalReconstructionIssue::Configuration));
    auto invalid_output = output;
    invalid_output.reconstructed[0] += 1.0;
    CHECK(!rec::validate_statistical_reconstruction_output(invalid_output));

    auto coherent = frame(
        1, 1, ure::transport::ValueDomain::Spectrum, 1, 1.0);
    coherent.observable.value_domain =
        ure::transport::ValueDomain::ComplexJones;
    coherent.observable.coherence =
        ure::transport::CoherenceClass::Coherent;
    coherent.observable.kind =
        ure::transport::ObservableKind::JonesField;
    coherent.observable.component_count = 1;
    rec::finalize_statistical_reconstruction_frame(coherent);
    CHECK(rec::validate_statistical_reconstruction_frame(coherent).has(
        rec::StatisticalReconstructionIssue::Observable));
    bool rejected = false;
    try {
        (void)rec::reconstruct_statistics(coherent, settings);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK(rejected);
}

int main() {
    test_spatial_spectral_baseline();
    test_tail_classification();
    test_temporal_confidence_and_rejection();
    test_stokes_physical_domain();
    test_invalid_and_tampered_inputs();
    if (failures != 0) {
        std::fprintf(stderr, "%d statistical reconstruction checks failed\n",
                     failures);
        return 1;
    }
    std::puts("Statistical reconstruction tests passed");
    return 0;
}
