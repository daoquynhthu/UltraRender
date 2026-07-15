#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <ure/native_scene.hpp>

namespace ure::native_scene {

inline constexpr std::string_view kScriptBuildSchemaIdentity = "ure.script-build/1.0";
inline constexpr std::string_view kScriptBuildFeature = "ure.build.script";

enum class ScriptArtifactKind : std::uint8_t { Scene, Resource, Cache };

struct ScriptBuildInput {
    std::string name;
    std::string content_hash;
    std::vector<std::uint8_t> bytes;
};

struct ScriptBuildOutputDeclaration {
    std::string name;
    ScriptArtifactKind kind = ScriptArtifactKind::Resource;
    std::string schema_identity;
    std::uint64_t maximum_bytes = 0;
};

struct ScriptSandboxPolicy {
    std::string policy_hash;
    bool network = false;
    bool subprocesses = false;
    bool inherited_environment = false;
    bool ambient_filesystem = false;
    bool wall_clock = false;
    bool nondeterministic_entropy = false;
};

struct ScriptBuildLimits {
    std::uint64_t duration_ms = 10'000;
    std::uint64_t memory_bytes = 256ull * 1024ull * 1024ull;
    std::uint64_t output_bytes = 64ull * 1024ull * 1024ull;
    std::uint64_t log_bytes = 1ull * 1024ull * 1024ull;
};

struct ScriptBuildManifest {
    std::string id;
    Version schema_version;
    std::string script_hash;
    std::string runtime_identity;
    std::string runtime_hash;
    std::string dependency_lock_hash;
    std::string runner_identity;
    std::string runner_hash;
    ScriptSandboxPolicy sandbox;
    ScriptBuildLimits limits;
    std::vector<ScriptBuildInput> inputs;
    std::vector<ScriptBuildOutputDeclaration> outputs;
};

struct ScriptBuildArtifact {
    std::string name;
    ScriptArtifactKind kind = ScriptArtifactKind::Resource;
    std::string schema_identity;
    std::string content_hash;
    std::vector<std::uint8_t> bytes;
};

struct ScriptRunnerAttestation {
    std::string runner_identity;
    std::string runner_hash;
    std::string runtime_identity;
    std::string runtime_hash;
    std::string dependency_lock_hash;
    std::string policy_hash;
};

struct ScriptRunnerResult {
    int exit_code = -1;
    ScriptRunnerAttestation attestation;
    std::vector<ScriptBuildArtifact> outputs;
    std::vector<std::uint8_t> standard_output;
    std::vector<std::uint8_t> standard_error;
};

class IScriptSandboxRunner {
public:
    virtual ~IScriptSandboxRunner() = default;
    virtual ScriptRunnerResult run(const ScriptBuildManifest& manifest) = 0;
};

struct ScriptBuildOptions { bool enabled = false; };

struct ScriptBuildProvenance {
    std::string source_hash;
    std::string cache_key;
    std::string output_hash;
    ScriptRunnerAttestation attestation;
};

struct ScriptBuildResult {
    std::vector<ScriptBuildArtifact> outputs;
    ScriptBuildProvenance provenance;
    std::vector<ValidationDiagnostic> diagnostics;
    bool ok() const;
};

ValidationReport validate_script_build_manifest(const ScriptBuildManifest& manifest);
std::string script_build_source_hash(const ScriptBuildManifest& manifest);
std::string script_build_cache_key(const ScriptBuildManifest& manifest);
ScriptBuildResult build_script_step(const ScriptBuildManifest& manifest,
                                    const ScriptBuildOptions& options,
                                    IScriptSandboxRunner* runner);

}
