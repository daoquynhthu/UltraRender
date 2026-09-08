#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <utility>

#include <ure/native_scene_tooling.hpp>
#include <ure/product/product_service.hpp>

namespace {

int failures{};

void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

bool nonzero(const ure::product::Identity& identity) {
    return std::ranges::any_of(identity,
                               [](std::uint8_t value) { return value != 0; });
}

class CurrentPathGuard {
public:
    explicit CurrentPathGuard(const std::filesystem::path& path)
        : original_(std::filesystem::current_path()) {
        std::filesystem::current_path(path);
    }

    ~CurrentPathGuard() {
        std::error_code error;
        std::filesystem::current_path(original_, error);
    }

private:
    std::filesystem::path original_;
};

}

int main() {
    const auto loaded = ure::native_scene::load_native_asset(
        URE_PRODUCT_TEST_SCENE);
    check(loaded.ok() && loaded.value.has_value(),
          "native product fixture did not load");
    if (!loaded.ok() || !loaded.value)
        return 1;

    ure::product::Identity snapshot{};
    snapshot[0] = 1;
    ure::product::ProductObjective objective;
    objective.identity[0] = 2;
    objective.requested_samples = 2;
    objective.memory_budget_bytes = UINT64_C(268435456);
    auto job = ure::product::ProductJob::create(
        *loaded.value, snapshot, objective);

    const auto& memory_plan = job->memory_plan();
    check(memory_plan.persistent_executor_count == 4 &&
              memory_plan.framebuffer_bytes > 0 &&
              memory_plan.spectral_plane_bytes > 0 &&
              memory_plan.queue_bytes > 0 &&
              memory_plan.executor_state_bytes > 0 &&
              memory_plan.output_bytes > 0 &&
              memory_plan.estimated_peak_bytes <=
                  memory_plan.applicable_budget_bytes,
          "product memory preflight did not retain a bounded plan");
    check(nonzero(job->identities().build), "build identity is empty");
    check(nonzero(job->identities().plan), "plan identity is empty");
    const auto product_snapshot = job->identities().snapshot;
    check(nonzero(product_snapshot) && product_snapshot != snapshot,
          "realized product snapshot identity was not derived");
    check(job->identities().objective == objective.identity,
          "objective identity was not retained");

    auto constrained_objective = objective;
    constrained_objective.memory_budget_bytes = UINT64_C(1048576);
    bool constrained_rejected = false;
    try {
        static_cast<void>(ure::product::ProductJob::create(
            *loaded.value, snapshot, constrained_objective));
    } catch (const ure::product::ProductError& error) {
        constrained_rejected = error.code() ==
            ure::product::ProductFailureCode::MemoryNotApplicable;
    }
    check(constrained_rejected,
          "memory-inapplicable product plan was not rejected before allocation");

    job->begin();
    job->render_sample();
    const auto first_progress = job->operation();
    check(first_progress.requested_samples == 2 &&
              first_progress.accepted_samples == 2 &&
              first_progress.completed_samples == 1,
          "first work quantum accounting is incorrect");
    check(first_progress.actual_renderer_samples == 1 &&
              first_progress.scene_realizations == 1 &&
              first_progress.executor_creations > 0 &&
              first_progress.pilot_samples > 0,
          "first renderer work evidence is incomplete");
    check(first_progress.eligible_integrator_modes != 0 &&
              first_progress.qualified_integrator_modes != 0 &&
              first_progress.executed_integrator_modes != 0 &&
              (first_progress.qualified_integrator_modes &
               ~first_progress.eligible_integrator_modes) == 0 &&
              (first_progress.executed_integrator_modes &
               ~first_progress.qualified_integrator_modes) == 0,
          "integrator observation masks are incomplete or inconsistent");
    job->render_sample();
    bool budget_rejected = false;
    try {
        job->render_sample();
    } catch (const std::out_of_range&) {
        budget_rejected = true;
    }
    check(budget_rejected, "exhausted product sample budget was accepted");
    const auto progress = job->operation();
    check(progress.accepted_samples == 2,
          "accepted sample accounting is incorrect");
    check(progress.completed_samples == 2 &&
              progress.actual_renderer_samples == 2,
          "completed renderer work accounting is incorrect");
    check(progress.scene_realizations ==
              first_progress.scene_realizations &&
              progress.executor_creations >=
                  first_progress.executor_creations &&
              progress.executor_creations <=
                  first_progress.executor_creations + 1,
          "scene realization or executor construction is unbounded");
    check(progress.requested_samples == 2,
          "requested sample accounting is incorrect");

    const auto frame = job->publish_frame();
    check(frame.accepted_samples == 2, "frame sample count is incorrect");
    check(frame.width > 0 && frame.height > 0, "frame extent is empty");
    check(frame.rgb.size() ==
              static_cast<std::size_t>(frame.width) * frame.height * 3,
          "frame RGB layout is incorrect");
    check(std::ranges::all_of(frame.rgb,
                              [](float value) { return std::isfinite(value); }),
          "frame contains non-finite values");
    const auto artifact = job->artifact_manifest(frame);
    check(nonzero(artifact.frame_content), "artifact identity is empty");
    check(artifact.accepted_samples == 2,
          "artifact sample accounting is incorrect");
    check(artifact.rgb_value_count == frame.rgb.size(),
          "artifact layout accounting is incorrect");

    job->reset();
    check(job->operation().state ==
              ure::product::ProductOperationState::Ready &&
              job->operation().eligible_integrator_modes != 0 &&
              job->operation().qualified_integrator_modes == 0 &&
              job->operation().executed_integrator_modes == 0,
          "reset did not return the product job to ready");
    ure::product::Identity replacement_snapshot{};
    replacement_snapshot[0] = 3;
    const auto old_plan = job->identities().plan;
    job->replace_scene(*loaded.value, replacement_snapshot);
    check(job->identities().snapshot != product_snapshot &&
              job->identities().snapshot != replacement_snapshot,
          "replacement source identity did not derive a new product snapshot");
    check(job->identities().plan != old_plan,
          "replacement did not change product plan identity");

    job->begin();
    job->cancel();
    check(job->operation().state ==
              ure::product::ProductOperationState::Canceled,
          "cancel did not terminate the product operation");

    const auto isolated = std::filesystem::temp_directory_path() /
        "ultrarender_product_resource_root";
    std::filesystem::create_directories(isolated);
    {
        CurrentPathGuard guard(isolated);
        const auto resource_scene = ure::native_scene::load_native_asset(
            std::filesystem::path(URE_PRODUCT_RESOURCE_TEST_SCENE));
        check(resource_scene.ok() && resource_scene.value.has_value(),
              "resource-root fixture did not load from an isolated directory");
        if (resource_scene.ok() && resource_scene.value) {
            ure::product::ProductObjective resource_objective;
            resource_objective.requested_samples = 1;
            resource_objective.memory_budget_bytes = UINT64_C(268435456);
            auto rootless = *resource_scene.value;
            rootless.execution_root.clear();
            bool rootless_rejected = false;
            try {
                static_cast<void>(ure::product::ProductJob::create(
                    std::move(rootless), snapshot, resource_objective));
            } catch (const ure::product::ProductError& error) {
                rootless_rejected = error.code() ==
                        ure::product::ProductFailureCode::ResourceMissing &&
                    error.detail() == 612;
            }
            check(rootless_rejected,
                  "relative product resources were accepted without an execution root");
            auto resource_job = ure::product::ProductJob::create(
                *resource_scene.value, snapshot, resource_objective);
            resource_job->begin();
            resource_job->render_sample();
            const auto resource_frame = resource_job->publish_frame();
            check(resource_frame.accepted_samples == 1 &&
                      !resource_frame.rgb.empty(),
                  "isolated resource-root render did not publish a frame");
        }
    }
    std::filesystem::remove_all(isolated);

    if (failures == 0)
        std::printf("product service tests passed\n");
    return failures == 0 ? 0 : 1;
}
