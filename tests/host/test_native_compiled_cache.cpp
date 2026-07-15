#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include <ure/native_compiled_cache.hpp>
#include <ure/native_scene_hash.hpp>

namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
std::string hash_of(std::string_view text) {
    return ure::native_scene::sha256_hex(std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
}
}

int main() {
    using namespace ure::native_scene;
    CompiledCacheManifest cache;
    cache.format_version = {1, 0};
    cache.schema_version = {1, 0};
    cache.source_hash = hash_of("source");
    cache.compiler_hash = hash_of("compiler");
    cache.scene_ir_hash = hash_of("scene-ir");
    cache.gpu_upload_plan.push_back({"mesh", 0, 4096, 256});
    cache.spectral_cache.push_back({"spectrum", ResourceKind::SpectralTable, hash_of("spectrum"), 1024});
    cache.resource_cache.push_back({"texture", ResourceKind::Texture, hash_of("texture"), 2048});
    cache.acceleration = {"ure.cuda.bvh", hash_of("bvh-layout"), 4, 1};
    cache.validation_metrics.push_back({"reference_mse", 0.001, 0.01, true});
    const auto binary = write_compiled_cache(cache);
    const auto loaded = read_compiled_cache(binary);
    check(loaded.ok() && loaded.value->gpu_upload_plan.size() == 1, "compiled cache roundtrip failed");
    check(validate_compiled_cache(*loaded.value, cache.source_hash, cache.compiler_hash, CacheMismatchPolicy::Reject).usable, "matching cache was rejected");
    const auto rebuild = validate_compiled_cache(*loaded.value, hash_of("new-source"), cache.compiler_hash, CacheMismatchPolicy::Rebuild);
    check(!rebuild.usable && rebuild.rebuild_required, "source mismatch did not request rebuild");
    const auto reject = validate_compiled_cache(*loaded.value, cache.source_hash, hash_of("new-compiler"), CacheMismatchPolicy::Reject);
    check(!reject.usable && !reject.rebuild_required && !reject.diagnostics.empty(), "compiler mismatch was not rejected");
    auto damaged = binary;
    damaged.back() ^= 1;
    check(!read_compiled_cache(damaged).ok(), "damaged cache payload was accepted");
    auto overlap = cache;
    overlap.gpu_upload_plan.push_back({"overlap", 2048, 4096, 256});
    try { (void)write_compiled_cache(overlap); check(false, "overlapping upload ranges were accepted"); }
    catch (const std::invalid_argument&) {}

    PackageManifest package;
    package.id = "farm";
    package.format_version = {1, 0};
    package.scenes.push_back({"scene", hash_of("scene"), "ure+sha256://" + hash_of("payload")});
    package.resources.push_back({"mesh", hash_of("mesh"), ResourceKind::Geometry, {1, 0}, "mesh.bin", {}, 100, 100});
    package.resources.push_back({"spectrum", hash_of("spectrum"), ResourceKind::SpectralTable, {1, 0}, "spectrum.bin", {}, 50, 50});
    const std::vector<FarmWorkerInventory> workers{{"cold", 1000, {}}, {"warm", 1000, {hash_of("mesh"), hash_of("spectrum")}}};
    const auto assignments = schedule_farm_shards(package, workers);
    check(assignments.size() == 1 && assignments[0].worker_id == "warm", "farm scheduler ignored resource locality");
    check(assignments[0].local_bytes == 150 && assignments[0].transfer_bytes == 0, "farm transfer accounting is incorrect");
    std::cout << "Phase Q.11 compiled cache checks: " << (failures ? "FAILED" : "PASSED") << '\n';
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
