#include <algorithm>
#include <map>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include <ure/native_scene_hash.hpp>
#include <ure/native_script_build.hpp>

namespace ure::native_scene {
namespace {

using Json = nlohmann::ordered_json;

bool hash_valid(const std::string& value) {
    return value.size() == 64 && std::ranges::all_of(value, [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

void error(ValidationReport& report, std::string code, std::string path, std::string message) {
    report.diagnostics.push_back({std::move(code), DiagnosticSeverity::Error, std::move(path), std::move(message), {}});
}

std::string hash_text(const std::string& value) {
    return sha256_hex(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(value.data()), value.size()));
}

Json canonical_manifest(const ScriptBuildManifest& manifest) {
    Json root;
    root["dependency_lock_hash"] = manifest.dependency_lock_hash;
    root["id"] = manifest.id;
    Json inputs = Json::array();
    std::vector<ScriptBuildInput> sorted_inputs = manifest.inputs;
    std::ranges::sort(sorted_inputs, {}, &ScriptBuildInput::name);
    for (const auto& input : sorted_inputs) inputs.push_back({{"content_hash", input.content_hash}, {"name", input.name}});
    root["inputs"] = std::move(inputs);
    root["limits"] = {{"duration_ms", manifest.limits.duration_ms}, {"log_bytes", manifest.limits.log_bytes},
                       {"memory_bytes", manifest.limits.memory_bytes}, {"output_bytes", manifest.limits.output_bytes}};
    Json outputs = Json::array();
    std::vector<ScriptBuildOutputDeclaration> sorted_outputs = manifest.outputs;
    std::ranges::sort(sorted_outputs, {}, &ScriptBuildOutputDeclaration::name);
    for (const auto& output : sorted_outputs) outputs.push_back({{"kind", static_cast<unsigned>(output.kind)},
        {"maximum_bytes", output.maximum_bytes}, {"name", output.name}, {"schema_identity", output.schema_identity}});
    root["outputs"] = std::move(outputs);
    root["runner_hash"] = manifest.runner_hash;
    root["runner_identity"] = manifest.runner_identity;
    root["runtime_hash"] = manifest.runtime_hash;
    root["runtime_identity"] = manifest.runtime_identity;
    root["sandbox"] = {{"ambient_filesystem", manifest.sandbox.ambient_filesystem},
        {"inherited_environment", manifest.sandbox.inherited_environment}, {"network", manifest.sandbox.network},
        {"nondeterministic_entropy", manifest.sandbox.nondeterministic_entropy}, {"policy_hash", manifest.sandbox.policy_hash},
        {"subprocesses", manifest.sandbox.subprocesses}, {"wall_clock", manifest.sandbox.wall_clock}};
    root["schema_version"] = {{"major", manifest.schema_version.major}, {"minor", manifest.schema_version.minor}};
    root["script_hash"] = manifest.script_hash;
    return root;
}

void add_failure(ScriptBuildResult& result, std::string code, std::string path, std::string message) {
    result.diagnostics.push_back({std::move(code), DiagnosticSeverity::Error, std::move(path), std::move(message), {}});
}

}

bool ScriptBuildResult::ok() const {
    return std::ranges::none_of(diagnostics, [](const auto& item) { return item.severity == DiagnosticSeverity::Error; });
}

ValidationReport validate_script_build_manifest(const ScriptBuildManifest& manifest) {
    ValidationReport report;
    if (manifest.id.empty()) error(report, "URE-Q5-ID-001", "/script/id", "Script build ID is empty");
    if (manifest.schema_version.major != 1) error(report, "URE-Q5-VERSION-001", "/script/schema_version", "Unsupported script build schema major version");
    const std::pair<const char*, const std::string*> hashes[]{
        {"script_hash", &manifest.script_hash}, {"runtime_hash", &manifest.runtime_hash},
        {"dependency_lock_hash", &manifest.dependency_lock_hash}, {"runner_hash", &manifest.runner_hash},
        {"policy_hash", &manifest.sandbox.policy_hash}};
    for (const auto& [name, value] : hashes) if (!hash_valid(*value)) error(report, "URE-Q5-HASH-001", std::string("/script/") + name, "Expected lowercase SHA-256");
    if (manifest.runtime_identity.empty() || manifest.runner_identity.empty()) error(report, "URE-Q5-IDENTITY-001", "/script", "Runtime and runner identities are required");
    if (manifest.sandbox.network || manifest.sandbox.subprocesses || manifest.sandbox.inherited_environment ||
        manifest.sandbox.ambient_filesystem || manifest.sandbox.wall_clock || manifest.sandbox.nondeterministic_entropy) {
        error(report, "URE-Q5-POLICY-001", "/script/sandbox", "Q.5 deterministic policy forbids ambient capabilities");
    }
    if (!manifest.limits.duration_ms || !manifest.limits.memory_bytes || !manifest.limits.output_bytes || !manifest.limits.log_bytes) error(report, "URE-Q5-BUDGET-001", "/script/limits", "All limits must be nonzero");
    std::map<std::string, bool> names;
    std::uint64_t input_total = 0;
    for (const auto& input : manifest.inputs) {
        if (input.name.empty() || !names.emplace(input.name, true).second) error(report, "URE-Q5-IO-001", "/script/inputs", "Input names must be nonempty and unique");
        if (!hash_valid(input.content_hash) || sha256_hex(input.bytes) != input.content_hash) error(report, "URE-Q5-IO-002", "/script/inputs", "Input content hash mismatch");
        if (input.bytes.size() > manifest.limits.memory_bytes - std::min<std::uint64_t>(input_total, manifest.limits.memory_bytes)) error(report, "URE-Q5-BUDGET-002", "/script/inputs", "Input bytes exceed memory limit");
        input_total += input.bytes.size();
    }
    names.clear();
    if (manifest.outputs.empty()) error(report, "URE-Q5-IO-004", "/script/outputs", "At least one output must be declared");
    for (const auto& output : manifest.outputs) {
        if (output.name.empty() || !names.emplace(output.name, true).second) error(report, "URE-Q5-IO-003", "/script/outputs", "Output names must be nonempty and unique");
        if (output.schema_identity.empty() || !output.maximum_bytes || output.maximum_bytes > manifest.limits.output_bytes) error(report, "URE-Q5-BUDGET-003", "/script/outputs", "Invalid output schema or byte limit");
    }
    return report;
}

std::string script_build_source_hash(const ScriptBuildManifest& manifest) { return hash_text(canonical_manifest(manifest).dump()); }

std::string script_build_cache_key(const ScriptBuildManifest& manifest) {
    return hash_text(std::string("ure.script-cache/1\n") + script_build_source_hash(manifest));
}

ScriptBuildResult build_script_step(const ScriptBuildManifest& manifest, const ScriptBuildOptions& options, IScriptSandboxRunner* runner) {
    ScriptBuildResult result;
    const auto validation = validate_script_build_manifest(manifest);
    result.diagnostics = validation.diagnostics;
    if (!validation.ok()) return result;
    if (!options.enabled) { add_failure(result, "URE-Q5-DISABLED-001", "/script", "Script build execution is disabled"); return result; }
    if (!runner) { add_failure(result, "URE-Q5-RUNNER-001", "/script/runner", "No sandbox runner was supplied"); return result; }
    ScriptRunnerResult untrusted;
    try { untrusted = runner->run(manifest); }
    catch (const std::exception& exception) { add_failure(result, "URE-Q5-RUNNER-002", "/script/runner", exception.what()); return result; }
    const auto& a = untrusted.attestation;
    if (a.runner_identity != manifest.runner_identity || a.runner_hash != manifest.runner_hash ||
        a.runtime_identity != manifest.runtime_identity || a.runtime_hash != manifest.runtime_hash ||
        a.dependency_lock_hash != manifest.dependency_lock_hash || a.policy_hash != manifest.sandbox.policy_hash) {
        add_failure(result, "URE-Q5-ATTEST-001", "/script/attestation", "Runner attestation does not match the manifest"); return result;
    }
    if (untrusted.exit_code != 0) { add_failure(result, "URE-Q5-EXIT-001", "/script/runner", "Script runner reported failure"); return result; }
    if (untrusted.standard_output.size() + untrusted.standard_error.size() > manifest.limits.log_bytes) { add_failure(result, "URE-Q5-BUDGET-004", "/script/log", "Runner logs exceed limit"); return result; }
    std::map<std::string, const ScriptBuildOutputDeclaration*> declarations;
    for (const auto& declaration : manifest.outputs) declarations.emplace(declaration.name, &declaration);
    std::uint64_t total = 0;
    for (auto artifact : untrusted.outputs) {
        const auto found = declarations.find(artifact.name);
        if (found == declarations.end()) { add_failure(result, "URE-Q5-OUTPUT-001", "/script/outputs", "Runner returned an undeclared output"); return result; }
        const auto& declaration = *found->second;
        if (artifact.kind != declaration.kind || artifact.schema_identity != declaration.schema_identity || artifact.bytes.size() > declaration.maximum_bytes ||
            artifact.bytes.size() > manifest.limits.output_bytes - std::min<std::uint64_t>(total, manifest.limits.output_bytes)) {
            add_failure(result, "URE-Q5-OUTPUT-002", "/script/outputs/" + artifact.name, "Output contract or byte limit mismatch"); return result;
        }
        artifact.content_hash = sha256_hex(artifact.bytes);
        total += artifact.bytes.size();
        declarations.erase(found);
        result.outputs.push_back(std::move(artifact));
    }
    if (!declarations.empty()) { add_failure(result, "URE-Q5-OUTPUT-003", "/script/outputs", "Runner omitted a declared output"); return result; }
    std::ranges::sort(result.outputs, {}, &ScriptBuildArtifact::name);
    result.provenance.source_hash = script_build_source_hash(manifest);
    result.provenance.cache_key = script_build_cache_key(manifest);
    result.provenance.attestation = a;
    Json output_identity = {{"cache_key", result.provenance.cache_key}, {"exit_code", untrusted.exit_code}, {"outputs", Json::array()},
        {"policy_hash", a.policy_hash}, {"runner_hash", a.runner_hash}, {"runtime_hash", a.runtime_hash}};
    for (const auto& output : result.outputs) output_identity["outputs"].push_back({{"content_hash", output.content_hash}, {"kind", static_cast<unsigned>(output.kind)}, {"name", output.name}, {"schema_identity", output.schema_identity}});
    result.provenance.output_hash = hash_text(output_identity.dump());
    return result;
}

}
