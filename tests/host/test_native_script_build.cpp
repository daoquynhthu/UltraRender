#include <cstdlib>
#include <iostream>

#include <ure/native_scene_hash.hpp>
#include <ure/native_script_build.hpp>

namespace {

int failures = 0;
void check(bool condition, const char* message) { if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; } }
std::string hash(std::string_view value) { return ure::native_scene::sha256_hex(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(value.data()), value.size())); }

ure::native_scene::ScriptBuildManifest manifest() {
    using namespace ure::native_scene;
    ScriptBuildManifest value;
    value.id = "build/generator"; value.schema_version = {1, 0}; value.script_hash = hash("script");
    value.runtime_identity = "python/cpython-3.13.5"; value.runtime_hash = hash("runtime");
    value.dependency_lock_hash = hash("lock"); value.runner_identity = "ure.test.recording-runner/1"; value.runner_hash = hash("runner");
    value.sandbox.policy_hash = hash("deny-ambient-v1");
    value.inputs.push_back({"source", hash("input"), {'i','n','p','u','t'}});
    value.outputs.push_back({"scene", ScriptArtifactKind::Scene, "ure.scene/1.0", 1024});
    return value;
}

class RecordingRunner final : public ure::native_scene::IScriptSandboxRunner {
public:
    ure::native_scene::ScriptRunnerResult run(const ure::native_scene::ScriptBuildManifest& value) override {
        ++calls;
        return {0, {value.runner_identity, value.runner_hash, value.runtime_identity, value.runtime_hash,
                    value.dependency_lock_hash, value.sandbox.policy_hash},
                {{"scene", ure::native_scene::ScriptArtifactKind::Scene, "ure.scene/1.0", {}, {'u','r','e'}}}, {}, {}};
    }
    int calls = 0;
};

}

int main() {
    using namespace ure::native_scene;
    auto value = manifest();
    check(validate_script_build_manifest(value).ok(), "valid manifest rejected");
    RecordingRunner runner;
    const auto disabled = build_script_step(value, {}, &runner);
    check(!disabled.ok() && runner.calls == 0, "disabled build executed runner");
    const auto built = build_script_step(value, {true}, &runner);
    check(built.ok() && runner.calls == 1 && built.outputs.size() == 1, "enabled build failed");
    check(built.outputs[0].content_hash == hash("ure"), "output was not independently hashed");
    const auto repeated = build_script_step(value, {true}, &runner);
    check(repeated.provenance.cache_key == built.provenance.cache_key && repeated.provenance.output_hash == built.provenance.output_hash, "identical build identities are not deterministic");
    value.dependency_lock_hash = hash("changed-lock");
    check(script_build_cache_key(value) != built.provenance.cache_key, "dependency change did not invalidate cache");
    auto unsafe = manifest(); unsafe.sandbox.network = true;
    check(!validate_script_build_manifest(unsafe).ok(), "network capability accepted");
    auto mismatch = manifest();
    class BadRunner final : public IScriptSandboxRunner { public: ScriptRunnerResult run(const ScriptBuildManifest& v) override { return {0, {v.runner_identity, hash("bad"), v.runtime_identity, v.runtime_hash, v.dependency_lock_hash, v.sandbox.policy_hash}, {}, {}, {}}; } } bad;
    check(!build_script_step(mismatch, {true}, &bad).ok(), "bad attestation accepted");
    std::cout << "Phase Q.5 script build checks: " << (failures ? "FAILED" : "PASSED") << '\n';
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
