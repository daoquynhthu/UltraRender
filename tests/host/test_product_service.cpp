#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

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

    check(nonzero(job->identities().build), "build identity is empty");
    check(nonzero(job->identities().plan), "plan identity is empty");
    check(job->identities().snapshot == snapshot,
          "snapshot identity was not retained");
    check(job->identities().objective == objective.identity,
          "objective identity was not retained");

    job->begin();
    job->render_sample();
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
              ure::product::ProductOperationState::Ready,
          "reset did not return the product job to ready");
    ure::product::Identity replacement_snapshot{};
    replacement_snapshot[0] = 3;
    const auto old_plan = job->identities().plan;
    job->replace_scene(*loaded.value, replacement_snapshot);
    check(job->identities().snapshot == replacement_snapshot,
          "replacement snapshot identity was not retained");
    check(job->identities().plan != old_plan,
          "replacement did not change product plan identity");

    job->begin();
    job->cancel();
    check(job->operation().state ==
              ure::product::ProductOperationState::Canceled,
          "cancel did not terminate the product operation");

    if (failures == 0)
        std::printf("product service tests passed\n");
    return failures == 0 ? 0 : 1;
}
