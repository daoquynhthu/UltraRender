#include "automatic_render_engine.hpp"

#include "ure/backend.hpp"
#include "ure/scene_ir.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ure {
namespace {

using Clock = std::chrono::steady_clock;

const char* mode_name(IntegratorMode mode) {
    switch (mode) {
    case IntegratorMode::Wavefront: return "wavefront";
    case IntegratorMode::PathGuided: return "path_guided";
    case IntegratorMode::RestirDI: return "restir_di_unbiased";
    case IntegratorMode::SpecularManifold: return "specular_manifold";
    case IntegratorMode::MLT: return "mlt";
    case IntegratorMode::RestirPT: return "restir_pt";
    case IntegratorMode::BDPT: return "bdpt";
    case IntegratorMode::VCM: return "vcm";
    case IntegratorMode::Automatic: return "automatic";
    }
    return "unknown";
}

double framebuffer_mean(const std::vector<float>& values) {
    if (values.empty()) {
        throw std::runtime_error("Automatic pilot produced an empty framebuffer");
    }
    const auto sum = std::accumulate(
        values.begin(), values.end(), 0.0);
    return sum / static_cast<double>(values.size());
}

double sample_variance(const std::vector<double>& values) {
    if (values.size() < 2) {
        throw std::runtime_error("Automatic pilot requires repeated samples");
    }
    const auto mean = std::accumulate(
        values.begin(), values.end(), 0.0) /
        static_cast<double>(values.size());
    double squared = 0.0;
    for (const auto value : values) {
        const auto delta = value - mean;
        squared += delta * delta;
    }
    return squared / static_cast<double>(values.size() - 1);
}

RenderConfig candidate_config(
    const RenderConfig& input,
    IntegratorMode mode) {
    RenderConfig result = input;
    result.integrator.mode = mode;
    result.automatic_integrator.enabled = false;
    result.path_guiding.enabled = false;
    result.restir_di.enabled = false;
    result.restir_pt.enabled = false;
    result.specular_manifold.enabled = false;
    result.bidirectional.enabled = false;
    result.vcm.enabled = false;
    result.mlt.enabled = false;
    result.integrator.allow_biased_reuse = false;
    result.integrator.sampler = IntegratorSampler::Default;
    result.samples_per_pass = 1;
    switch (mode) {
    case IntegratorMode::Wavefront:
        break;
    case IntegratorMode::PathGuided:
        result.path_guiding.enabled = true;
        break;
    case IntegratorMode::RestirDI:
        result.restir_di.enabled = true;
        result.restir_di.unbiased = true;
        result.restir_di.temporal_reuse = true;
        result.restir_di.spatial_reuse = false;
        break;
    case IntegratorMode::RestirPT:
        result.restir_pt.enabled = true;
        result.restir_pt.temporal_reuse = true;
        break;
    case IntegratorMode::SpecularManifold:
        result.specular_manifold.enabled = true;
        break;
    case IntegratorMode::BDPT:
        result.bidirectional.enabled = true;
        break;
    case IntegratorMode::VCM:
        result.bidirectional.enabled = true;
        result.vcm.enabled = true;
        break;
    case IntegratorMode::MLT:
        result.mlt.enabled = true;
        result.integrator.sampler = IntegratorSampler::PrimarySampleSpace;
        break;
    case IntegratorMode::Automatic:
        throw std::invalid_argument(
            "Automatic mode cannot be nested as a portfolio candidate");
    }
    const auto memory_budget = input.automatic_integrator.memory_budget_mb;
    if (memory_budget > 0) {
        if (result.backend.memory_budget_bytes == 0) {
            result.backend.memory_budget_bytes =
                static_cast<std::uint64_t>(memory_budget) *
                1024ull * 1024ull;
        }
        if (result.path_guiding.memory_budget_mb == 0) {
            result.path_guiding.memory_budget_mb = memory_budget;
        }
        if (result.bidirectional.memory_budget_mb == 0) {
            result.bidirectional.memory_budget_mb = memory_budget;
        }
        if (result.mlt.memory_budget_mb == 0) {
            result.mlt.memory_budget_mb = memory_budget;
        }
    }
    return result;
}

std::vector<IntegratorMode> candidate_modes(
    const RenderConfig& config) {
    std::vector<IntegratorMode> result{
        IntegratorMode::Wavefront};
    if (!wave_optics_is_radiometric_only(config.wave_optics)) {
        return result;
    }
    const std::array alternatives{
        IntegratorMode::PathGuided,
        IntegratorMode::RestirDI,
        IntegratorMode::BDPT,
        IntegratorMode::RestirPT,
        IntegratorMode::SpecularManifold,
        IntegratorMode::VCM,
        IntegratorMode::MLT};
    for (const auto mode : alternatives) {
        result.push_back(mode);
    }
    return result;
}

bool complete_unbiased_endpoint_candidate(IntegratorMode mode) {
    switch (mode) {
    case IntegratorMode::Wavefront:
    case IntegratorMode::PathGuided:
    case IntegratorMode::RestirDI:
    case IntegratorMode::RestirPT:
    case IntegratorMode::BDPT:
        return true;
    case IntegratorMode::SpecularManifold:
    case IntegratorMode::VCM:
    case IntegratorMode::MLT:
    case IntegratorMode::Automatic:
        return false;
    }
    return false;
}

std::uint64_t current_available_device_bytes(
    const BackendSelection& selection) {
    const auto adapters = enumerate_backend_adapters(
        selection.adapter.kind);
    const auto found = std::ranges::find_if(
        adapters,
        [&selection](const BackendAdapterInfo& adapter) {
            return adapter.ordinal == selection.adapter.ordinal &&
                adapter.device_id == selection.adapter.device_id;
        });
    return found == adapters.end()
        ? 0
        : found->memory.available_bytes;
}

struct CandidateState {
    RenderConfig config;
    AutomaticTechniqueReport report;
    double maximum_absolute_pilot = 0.0;
    int rendered_spp = 0;
    std::uint64_t measured_resident_device_bytes = 0;
    std::uint64_t estimated_peak_device_bytes = 0;
    std::vector<float> framebuffer;
};

class AutomaticRenderEngine final : public IRenderEngine {
public:
    explicit AutomaticRenderEngine(RenderConfig config)
        : config_(std::move(config)),
          backend_selection_(select_backend(config_)) {
        validate_config();
    }

    void load_scene_ir(const scene_ir::SceneIR& scene_ir) override {
        scene_ = scene_ir;
        loaded_ = true;
        clear_all();
        auto baseline = RenderEngineFactory::create_gpu_renderer(
            candidate_config(config_, IntegratorMode::Wavefront));
        baseline->load_scene_ir(scene_);
        acceleration_stats_ = baseline->get_acceleration_stats();
        dynamic_geometry_stats_ = baseline->get_dynamic_geometry_stats();
    }

    void reload_scene_ir(const scene_ir::SceneIR& scene_ir) override {
        load_scene_ir(scene_ir);
    }

    void update_transforms(const scene_ir::SceneIR& scene_ir) override {
        load_scene_ir(scene_ir);
    }

    void update_materials(const scene_ir::SceneIR& scene_ir) override {
        load_scene_ir(scene_ir);
    }

    void update_geometry(const scene_ir::SceneIR& scene_ir) override {
        load_scene_ir(scene_ir);
    }

    void render(const RenderSettings& settings) override {
        require_scene();
        if (settings.spp <= 0) {
            throw std::invalid_argument(
                "Automatic render requires a positive sample budget");
        }
        ensure_pilot();
        reset_production();
        const auto allocations = allocate_samples(settings.spp);
        const auto start = Clock::now();
        for (std::size_t index = 0; index < candidates_.size(); ++index) {
            if (allocations[index] == 0) continue;
            render_candidate(index, allocations[index]);
        }
        production_elapsed_nanoseconds_ = elapsed_nanoseconds(start);
        combine_outputs(settings.spp);
    }

    int render_pass() override {
        require_scene();
        ensure_pilot();
        if (selected_indices_.empty()) {
            throw std::runtime_error(
                "Automatic pilot qualified no production estimator");
        }
        const auto index = selected_indices_[next_selected_];
        next_selected_ = (next_selected_ + 1) % selected_indices_.size();
        const auto current = candidates_[index].rendered_spp;
        const auto target = current == 0
            ? std::max(1, config_.samples_per_pass)
            : std::max(current + config_.samples_per_pass, current * 2);
        const auto start = Clock::now();
        render_candidate(index, target);
        production_elapsed_nanoseconds_ += elapsed_nanoseconds(start);
        combine_outputs(total_rendered_spp());
        return current_spp_;
    }

    void reset_accumulation() override {
        reset_production();
    }

    void update_camera(const Camera& camera) override {
        require_scene();
        scene_.camera = camera;
        clear_all();
    }

    int get_current_spp() const override {
        return current_spp_;
    }

    void get_framebuffer_size(
        int& out_width,
        int& out_height) const override {
        out_width = loaded_ ? scene_.width : 0;
        out_height = loaded_ ? scene_.height : 0;
    }

    const std::vector<float>& get_framebuffer() const override {
        return framebuffer_;
    }

    const std::vector<float>& get_aov(AovType type) const override {
        if (type == AovType::Beauty) return framebuffer_;
        return aov_buffers_[static_cast<std::size_t>(type)];
    }

    IntegratorEstimatorMetadata get_estimator_metadata() const override {
        IntegratorEstimatorMetadata result;
        result.mode = IntegratorMode::Automatic;
        result.policy = IntegratorEstimatorPolicy::AutomaticPortfolio;
        result.sample_space_version =
            kAutomaticPortfolioSampleSpaceVersion;
        return result;
    }

    const BackendSelection& get_backend_selection() const override {
        return backend_selection_;
    }

    AccelerationStats get_acceleration_stats() const override {
        return acceleration_stats_;
    }

    runtime::DynamicGeometryStats
    get_dynamic_geometry_stats() const override {
        return dynamic_geometry_stats_;
    }

    AutomaticIntegratorReport
    get_automatic_integrator_report() const override {
        return report_;
    }

private:
    void validate_config() const {
        const auto& automatic = config_.automatic_integrator;
        if (config_.integrator.mode != IntegratorMode::Automatic ||
            !automatic.enabled ||
            !std::isfinite(
                automatic.target_relative_standard_error) ||
            automatic.target_relative_standard_error <= 0.0 ||
            automatic.memory_budget_mb < 0 ||
            automatic.pilot_spp < 2 ||
            automatic.maximum_techniques < 1 ||
            config_.sample_index_offset >
                static_cast<std::uint64_t>(
                    std::numeric_limits<int>::max() -
                    automatic.pilot_spp) ||
            !std::isfinite(automatic.minimum_wavefront_fraction) ||
            automatic.minimum_wavefront_fraction <= 0.0f ||
            automatic.minimum_wavefront_fraction > 1.0f) {
            throw std::invalid_argument(
                "Invalid or non-production automatic integrator configuration");
        }
    }

    void require_scene() const {
        if (!loaded_) {
            throw std::runtime_error(
                "Automatic renderer has no scene loaded");
        }
    }

    static std::uint64_t elapsed_nanoseconds(Clock::time_point start) {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                Clock::now() - start).count());
    }

    void clear_all() {
        candidates_.clear();
        selected_indices_.clear();
        pilot_complete_ = false;
        pilot_elapsed_nanoseconds_ = 0;
        acceleration_stats_ = {};
        dynamic_geometry_stats_ = {};
        reset_production();
    }

    void reset_production() {
        for (auto& candidate : candidates_) {
            candidate.rendered_spp = 0;
            candidate.framebuffer.clear();
            candidate.report.allocated_spp = 0;
            candidate.report.aggregation_weight = 0.0;
        }
        framebuffer_.clear();
        for (auto& buffer : aov_buffers_) buffer.clear();
        current_spp_ = 0;
        next_selected_ = 0;
        production_elapsed_nanoseconds_ = 0;
        report_ = {};
    }

    void ensure_pilot() {
        if (pilot_complete_) return;
        const auto start = Clock::now();
        candidates_.clear();
        for (const auto mode : candidate_modes(config_)) {
            CandidateState state;
            state.config = candidate_config(config_, mode);
            state.report.mode = mode;
            state.report.pilot_spp = config_.automatic_integrator.pilot_spp;
            if (!complete_unbiased_endpoint_candidate(mode)) {
                state.report.reason =
                    "excluded: current CUDA endpoint bridge lacks complete "
                    "finite-sample unbiased support/normalization evidence";
                candidates_.push_back(std::move(state));
                continue;
            }
            try {
                run_pilot(state);
                state.report.qualified = true;
                state.report.reason = "qualified by independent pilot";
            } catch (const std::exception& error) {
                state.report.reason =
                    std::string("pilot rejected: ") + error.what();
                if (mode == IntegratorMode::Wavefront) throw;
            }
            candidates_.push_back(std::move(state));
        }
        select_candidates();
        pilot_elapsed_nanoseconds_ = elapsed_nanoseconds(start);
        pilot_complete_ = true;
    }

    void run_pilot(CandidateState& state) {
        auto pilot_config = state.config;
        pilot_config.sample_index_offset = config_.sample_index_offset;
        auto engine = RenderEngineFactory::create_gpu_renderer(pilot_config);
        engine->load_scene_ir(scene_);
        engine->reset_accumulation();
        std::vector<double> samples;
        samples.reserve(static_cast<std::size_t>(
            config_.automatic_integrator.pilot_spp));
        int previous_spp = 0;
        double previous_mean = 0.0;
        const auto start = Clock::now();
        for (int index = 0;
             index < config_.automatic_integrator.pilot_spp;
             ++index) {
            const auto current_spp = engine->render_pass();
            if (current_spp <= previous_spp) {
                throw std::runtime_error(
                    "candidate did not advance its sample range");
            }
            const auto current_mean = framebuffer_mean(
                engine->get_framebuffer());
            const auto contribution =
                (current_mean * static_cast<double>(current_spp) -
                 previous_mean * static_cast<double>(previous_spp)) /
                static_cast<double>(current_spp - previous_spp);
            if (!std::isfinite(contribution)) {
                throw std::runtime_error(
                    "candidate produced a non-finite pilot contribution");
            }
            samples.push_back(contribution);
            state.maximum_absolute_pilot = std::max(
                state.maximum_absolute_pilot,
                std::abs(contribution));
            previous_spp = current_spp;
            previous_mean = current_mean;
        }
        const auto elapsed = elapsed_nanoseconds(start);
        const auto metadata = engine->get_estimator_metadata();
        if (!validate_integrator_estimator_metadata(metadata) ||
            metadata.biased) {
            throw std::runtime_error(
                "candidate estimator metadata is not production-unbiased");
        }
        state.report.pilot_mean = std::accumulate(
            samples.begin(), samples.end(), 0.0) /
            static_cast<double>(samples.size());
        state.report.pilot_variance = sample_variance(samples);
        state.report.maximum_absolute_pilot_contribution =
            state.maximum_absolute_pilot;
        if (!std::isfinite(state.report.pilot_variance) ||
            state.report.pilot_variance < 0.0) {
            throw std::runtime_error(
                "candidate pilot variance is invalid");
        }
        state.report.nanoseconds_per_sample =
            static_cast<double>(elapsed) /
            static_cast<double>(samples.size());
        state.config.sample_index_offset = config_.sample_index_offset +
            static_cast<std::uint64_t>(
                config_.automatic_integrator.pilot_spp);
    }

    void select_candidates() {
        selected_indices_.clear();
        const auto baseline = std::ranges::find_if(
            candidates_,
            [](const CandidateState& value) {
                return value.report.mode == IntegratorMode::Wavefront &&
                    value.report.qualified;
            });
        if (baseline == candidates_.end()) {
            throw std::runtime_error(
                "Automatic renderer lost defensive wavefront coverage");
        }
        selected_indices_.push_back(static_cast<std::size_t>(
            std::distance(candidates_.begin(), baseline)));
        std::vector<std::size_t> alternatives;
        for (std::size_t index = 0; index < candidates_.size(); ++index) {
            if (candidates_[index].report.qualified &&
                candidates_[index].report.mode !=
                    IntegratorMode::Wavefront) {
                alternatives.push_back(index);
            }
        }
        std::ranges::sort(
            alternatives,
            [this](std::size_t left, std::size_t right) {
                const auto score = [this](std::size_t index) {
                    const auto& report = candidates_[index].report;
                    return std::max(report.pilot_variance, 1e-20) *
                        report.nanoseconds_per_sample;
                };
                return score(left) < score(right);
            });
        const auto limit = static_cast<std::size_t>(
            config_.automatic_integrator.maximum_techniques);
        for (const auto index : alternatives) {
            if (selected_indices_.size() >= limit) break;
            selected_indices_.push_back(index);
        }
        for (std::size_t index = 0; index < candidates_.size(); ++index) {
            auto& report = candidates_[index].report;
            report.selected = std::ranges::find(
                selected_indices_, index) != selected_indices_.end();
            if (report.qualified && !report.selected) {
                report.reason =
                    "qualified but excluded by cost-variance rank or budget";
            }
        }
    }

    std::vector<int> allocate_samples(int total_samples) const {
        std::vector<int> result(candidates_.size(), 0);
        if (selected_indices_.empty()) return result;
        auto selected = selected_indices_;
        if (static_cast<int>(selected.size()) > total_samples) {
            selected.resize(static_cast<std::size_t>(total_samples));
        }
        const auto baseline = selected.front();
        const auto baseline_floor = std::max(
            1,
            static_cast<int>(std::ceil(
                static_cast<double>(total_samples) *
                config_.automatic_integrator.minimum_wavefront_fraction)));
        result[baseline] = std::min(total_samples, baseline_floor);
        int remaining = total_samples - result[baseline];
        for (std::size_t offset = 1;
             offset < selected.size() && remaining > 0;
             ++offset) {
            result[selected[offset]] = 1;
            --remaining;
        }
        std::vector<double> shares(selected.size(), 0.0);
        double share_sum = 0.0;
        for (std::size_t offset = 0; offset < selected.size(); ++offset) {
            const auto& report = candidates_[selected[offset]].report;
            shares[offset] = 1.0 / std::sqrt(
                std::max(report.pilot_variance, 1e-20) *
                std::max(report.nanoseconds_per_sample, 1.0));
            share_sum += shares[offset];
        }
        while (remaining > 0) {
            std::size_t best = 0;
            double best_deficit = -std::numeric_limits<double>::infinity();
            const auto assigned = total_samples - remaining;
            for (std::size_t offset = 0; offset < selected.size(); ++offset) {
                const auto target =
                    shares[offset] / share_sum *
                    static_cast<double>(assigned + 1);
                const auto deficit = target -
                    static_cast<double>(result[selected[offset]]);
                if (deficit > best_deficit) {
                    best_deficit = deficit;
                    best = offset;
                }
            }
            ++result[selected[best]];
            --remaining;
        }
        if (config_.automatic_integrator.time_budget_milliseconds > 0) {
            const auto budget = static_cast<double>(
                config_.automatic_integrator.time_budget_milliseconds) *
                1.0e6;
            double estimated = 0.0;
            for (std::size_t index = 0; index < result.size(); ++index) {
                estimated += static_cast<double>(result[index]) *
                    candidates_[index].report.nanoseconds_per_sample;
            }
            if (estimated > budget) {
                const auto scale = budget / estimated;
                for (const auto index : selected) {
                    result[index] = std::max(
                        index == baseline ? 1 : 0,
                        static_cast<int>(std::floor(
                            static_cast<double>(result[index]) * scale)));
                }
            }
        }
        return result;
    }

    void render_candidate(std::size_t index, int spp) {
        auto& state = candidates_.at(index);
        if (!state.report.selected || spp <= 0) return;
        const auto available_before = current_available_device_bytes(
            backend_selection_);
        auto engine = RenderEngineFactory::create_gpu_renderer(state.config);
        engine->load_scene_ir(scene_);
        RenderSettings settings;
        settings.width = scene_.width;
        settings.height = scene_.height;
        settings.spp = spp;
        engine->render(settings);
        const auto available_during = current_available_device_bytes(
            backend_selection_);
        state.framebuffer = engine->get_framebuffer();
        state.rendered_spp = spp;
        state.report.allocated_spp = spp;
        state.measured_resident_device_bytes =
            available_before > available_during
            ? available_before - available_during
            : 0;
        const auto candidate_acceleration =
            engine->get_acceleration_stats();
        state.estimated_peak_device_bytes =
            state.measured_resident_device_bytes +
            candidate_acceleration.build_temporary_bytes_peak;
        if (state.report.mode == IntegratorMode::Wavefront) {
            for (std::size_t value = 1; value < aov_buffers_.size(); ++value) {
                aov_buffers_[value] = engine->get_aov(
                    static_cast<AovType>(value));
            }
            acceleration_stats_ = engine->get_acceleration_stats();
            dynamic_geometry_stats_ = engine->get_dynamic_geometry_stats();
        }
    }

    int total_rendered_spp() const {
        int result = 0;
        for (const auto index : selected_indices_) {
            result += candidates_[index].rendered_spp;
        }
        return result;
    }

    void combine_outputs(int requested_spp) {
        std::size_t value_count = 0;
        double precision_sum = 0.0;
        for (const auto index : selected_indices_) {
            const auto& state = candidates_[index];
            if (state.rendered_spp <= 0) continue;
            if (value_count == 0) value_count = state.framebuffer.size();
            if (state.framebuffer.size() != value_count) {
                throw std::runtime_error(
                    "Automatic candidate framebuffer shapes differ");
            }
            precision_sum += static_cast<double>(state.rendered_spp) /
                std::max(state.report.pilot_variance, 1e-20);
        }
        if (value_count == 0 || !(precision_sum > 0.0)) {
            throw std::runtime_error(
                "Automatic renderer has no executable portfolio output");
        }
        framebuffer_.assign(value_count, 0.0f);
        double conservative_standard_error = 0.0;
        for (const auto index : selected_indices_) {
            auto& state = candidates_[index];
            if (state.rendered_spp <= 0) continue;
            const auto precision =
                static_cast<double>(state.rendered_spp) /
                std::max(state.report.pilot_variance, 1e-20);
            const auto weight = precision / precision_sum;
            state.report.aggregation_weight = weight;
            for (std::size_t value = 0; value < value_count; ++value) {
                framebuffer_[value] += static_cast<float>(
                    weight * static_cast<double>(state.framebuffer[value]));
            }
            conservative_standard_error += weight * std::sqrt(
                state.report.pilot_variance /
                static_cast<double>(state.rendered_spp));
        }
        current_spp_ = total_rendered_spp();
        const auto mean = framebuffer_mean(framebuffer_);
        const auto relative_error = conservative_standard_error /
            std::max(std::abs(mean), 1e-12);
        report_.automatic = true;
        report_.complete = std::ranges::all_of(
            selected_indices_,
            [this](std::size_t index) {
                return candidates_[index].rendered_spp > 0;
            });
        report_.quality_target_met = relative_error <=
            config_.automatic_integrator.target_relative_standard_error;
        const auto elapsed = pilot_elapsed_nanoseconds_ +
            production_elapsed_nanoseconds_;
        const auto deadline =
            config_.automatic_integrator.time_budget_milliseconds;
        report_.time_budget_met = deadline == 0 ||
            elapsed <= deadline * 1000000ull;
        report_.requested_spp = requested_spp;
        report_.total_allocated_spp = current_spp_;
        report_.estimated_relative_standard_error = relative_error;
        report_.elapsed_nanoseconds = elapsed;
        report_.peak_memory_budget_bytes =
            config_.automatic_integrator.memory_budget_mb > 0
            ? static_cast<std::uint64_t>(
                  config_.automatic_integrator.memory_budget_mb) *
                  1024ull * 1024ull
            : backend_selection_.adapter.memory.available_bytes;
        report_.measured_peak_resident_device_bytes = 0;
        report_.estimated_peak_device_bytes = 0;
        report_.technique_coverage_mask = 0;
        report_.independent_endpoint_ensemble = true;
        report_.pilot_precision_weighted = true;
        report_.conservative_uncertainty_bound = true;
        report_.auxiliary_outputs_wavefront_only = true;
        for (const auto index : selected_indices_) {
            const auto& state = candidates_[index];
            if (state.rendered_spp <= 0) continue;
            report_.measured_peak_resident_device_bytes = std::max(
                report_.measured_peak_resident_device_bytes,
                state.measured_resident_device_bytes);
            report_.estimated_peak_device_bytes = std::max(
                report_.estimated_peak_device_bytes,
                state.estimated_peak_device_bytes);
            report_.technique_coverage_mask |=
                1ull << static_cast<std::uint32_t>(state.report.mode);
        }
        report_.memory_budget_met =
            report_.estimated_peak_device_bytes <=
            report_.peak_memory_budget_bytes;
        report_.techniques.clear();
        for (const auto& state : candidates_) {
            report_.techniques.push_back(state.report);
        }
    }

    RenderConfig config_;
    BackendSelection backend_selection_;
    scene_ir::SceneIR scene_;
    bool loaded_ = false;
    bool pilot_complete_ = false;
    std::vector<CandidateState> candidates_;
    std::vector<std::size_t> selected_indices_;
    std::size_t next_selected_ = 0;
    std::vector<float> framebuffer_;
    std::array<std::vector<float>, 6> aov_buffers_;
    int current_spp_ = 0;
    std::uint64_t pilot_elapsed_nanoseconds_ = 0;
    std::uint64_t production_elapsed_nanoseconds_ = 0;
    AccelerationStats acceleration_stats_;
    runtime::DynamicGeometryStats dynamic_geometry_stats_;
    AutomaticIntegratorReport report_;
};

}

std::unique_ptr<IRenderEngine> create_automatic_render_engine(
    const RenderConfig& config) {
    return std::make_unique<AutomaticRenderEngine>(config);
}

}
