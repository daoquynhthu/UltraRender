#pragma once

#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

#include <ure/native_scene.hpp>
#include <ure/ure_api.hpp>

namespace ure::native_scene {

inline constexpr const char* kSimulationSchemaIdentity = "ure.simulation-contract/1.0";
inline constexpr const char* kSimulationFeature = "ure.scene.simulation";

enum class SimulationDomain : std::uint8_t { RigidBody, SoftBody, Fluid, AcousticModal, AcousticRay, AcousticWave, Extension };
enum class CouplingSemantic : std::uint8_t { Transform, CollisionImpulse, SurfaceVelocity, Pressure, Force, Temperature, AudioSignal, Extension };

struct TimeSamplingContract { std::int64_t start_tick = 0; std::int64_t end_tick = 0; std::uint64_t ticks_per_second = 60; std::uint64_t step_ticks = 1; std::int64_t synchronization_epoch = 0; };
struct SolverDomainRequest { std::string id; SimulationDomain domain = SimulationDomain::Extension; RequirementLevel requirement = RequirementLevel::Required; std::string solver_id; Version solver_version; std::vector<std::string> resources; std::string extension_owner; };
struct CouplingChannel { std::string id; std::string source_domain; std::string target_domain; CouplingSemantic semantic = CouplingSemantic::Transform; RequirementLevel requirement = RequirementLevel::Required; std::uint64_t rate_numerator = 1; std::uint64_t rate_denominator = 1; std::int64_t latency_ticks = 0; bool feedback = false; std::string resource_id; std::string extension_owner; };
struct SolverMigrationPolicy { std::string domain_id; Version minimum_source_version; Version target_version; std::string migration_tool; bool allow_lossy = false; };
struct NativeSimulationContract { std::string id; Version schema_version; TimeSamplingContract time; std::vector<SolverDomainRequest> domains; std::vector<CouplingChannel> coupling; std::vector<SolverMigrationPolicy> migrations; };
struct SimulationCapabilityRegistry { std::map<std::string, Version> solvers; std::set<SimulationDomain> domains; std::set<CouplingSemantic> coupling_semantics; };
struct CompiledSimulationContract { std::optional<PhysicsConfig> physics; std::vector<SolverDomainRequest> retained_domains; std::vector<ValidationDiagnostic> diagnostics; bool ok() const; };

ValidationReport validate_simulation_contract(const NativeSimulationContract& contract, const SimulationCapabilityRegistry& capabilities);
CompiledSimulationContract compile_simulation_contract(const NativeSimulationContract& contract, const SimulationCapabilityRegistry& capabilities);
std::string simulation_contract_semantic_hash(const NativeSimulationContract& contract);
std::vector<std::uint8_t> write_simulation_contract_binary(const NativeSimulationContract& contract);
LoadResult<NativeSimulationContract> read_simulation_contract_binary(std::span<const std::uint8_t> bytes);
std::string write_simulation_contract_text(const NativeSimulationContract& contract);
LoadResult<NativeSimulationContract> read_simulation_contract_text(std::string_view text);

}
