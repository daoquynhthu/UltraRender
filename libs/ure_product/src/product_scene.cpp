#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <ure/native_procedural_graph.hpp>
#include <ure/native_scene_hash.hpp>
#include <ure/native_scene_validation.hpp>
#include <ure/native_scene_uuid.hpp>
#include <ure/native_script_build.hpp>
#include <ure/product/product_scene.hpp>
#include <ure/spectral_limits.hpp>

namespace ure::product {
namespace {

constexpr std::uint32_t kInvalidArchive = 600;
constexpr std::uint32_t kRequiredFeatureUnavailable = 601;
constexpr std::uint32_t kResourceResolutionFailed = 602;
constexpr std::uint32_t kProceduralBuildFailed = 603;
constexpr std::uint32_t kSolverCompileFailed = 604;
constexpr std::uint32_t kSimulationUnavailable = 605;
constexpr std::uint32_t kTemporaryBudgetExceeded = 606;
constexpr std::uint32_t kPackageInvalid = 607;
constexpr std::string_view kProceduralFeature = "ure.scene.procedural";

std::uint32_t resource_detail(std::string_view code) {
    if (code.find("HASH") != std::string_view::npos) return 609;
    if (code.find("DEP") != std::string_view::npos) return 610;
    if (code.find("DOMAIN") != std::string_view::npos) return 611;
    if (code.find("PATH") != std::string_view::npos) return 612;
    if (code.find("DECOMP") != std::string_view::npos ||
        code == "URE-Q-BUDGET-003")
        return 613;
    if (code == "URE-PRV2-RESOURCE-BUDGET-001") return 614;
    if (code == "URE-PRV2-RESOURCE-BUDGET-002") return 615;
    if (code == "URE-PRV2-RESOURCE-BUDGET-003") return 616;
    if (code == "URE-PRV2-RESOURCE-BUDGET-005") return 617;
    if (code.find("AMBIGUOUS") != std::string_view::npos) return 618;
    if (code.find("SIZE") != std::string_view::npos) return 619;
    return 608;
}

struct MaterializationRoot {
    std::filesystem::path path;

    ~MaterializationRoot() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

native_scene::RequirementLevel feature_requirement(
    const native_scene::NativeSceneArchive& archive,
    std::string_view id,
    native_scene::RequirementLevel fallback =
        native_scene::RequirementLevel::Required) {
    const auto found = std::ranges::find(archive.document.features, id,
                                         &native_scene::FeatureDeclaration::name);
    return found == archive.document.features.end() ? fallback
                                                     : found->requirement;
}

void add_diagnostic(ProductRealizationResult& result,
                    ProductSceneDiagnosticDomain domain,
                    std::uint32_t detail,
                    std::string code,
                    std::string field_path,
                    std::string message,
                    std::string recovery,
                    native_scene::RequirementLevel requirement =
                        native_scene::RequirementLevel::Required,
                    ProductFeatureDisposition disposition =
                        ProductFeatureDisposition::Rejected) {
    result.diagnostics.push_back(
        {domain, detail, std::move(code), std::move(field_path),
         std::move(message), std::move(recovery), requirement, disposition});
}

void add_disposition(std::vector<ProductFeatureRecord>& records,
                     std::string id,
                     native_scene::RequirementLevel requirement,
                     ProductFeatureDisposition disposition,
                     std::uint32_t detail = 0) {
    const auto found = std::ranges::find(records, id,
                                         &ProductFeatureRecord::id);
    if (found == records.end())
        records.push_back(
            {std::move(id), requirement, disposition, detail});
    else {
        found->requirement = requirement;
        found->disposition = disposition;
        found->detail = detail;
    }
}

std::filesystem::path make_materialization_root() {
    static std::atomic<std::uint64_t> sequence{};
#if defined(_WIN32)
    const auto process = static_cast<std::uint64_t>(GetCurrentProcessId());
#else
    const auto process = static_cast<std::uint64_t>(getpid());
#endif
    const auto path = std::filesystem::temp_directory_path() /
                      "ultrarender" / "product-snapshots" /
                      (std::to_string(process) + "-" +
                       std::to_string(sequence.fetch_add(1)));
    std::filesystem::create_directories(path);
    return path;
}

std::filesystem::path materialized_relative_path(
    const native_scene::NativeResolvedResource& resource) {
    if (resource.source_uri.starts_with("ure+sha256://"))
        return std::filesystem::path("content") /
               resource.descriptor.content_hash;
    return std::filesystem::path(resource.source_uri);
}

std::shared_ptr<MaterializationRoot> materialize_resources(
    std::span<const native_scene::NativeResolvedResource> resources,
    std::uint64_t temporary_limit,
    native_scene::NativeResourceBudgetBreakdown& budget,
    std::map<std::string, std::filesystem::path>& resolved_paths) {
    auto owner = std::make_shared<MaterializationRoot>();
    owner->path = make_materialization_root();
    std::map<std::filesystem::path, std::string> written;
    for (const auto& resource : resources) {
        const auto relative = materialized_relative_path(resource);
        const auto validation = native_scene::validate_exploded_resource_path(
            owner->path, relative.generic_string());
        if (!validation.ok())
            throw std::invalid_argument(
                validation.diagnostics.front().message);
        if (const auto found = written.find(relative); found != written.end()) {
            if (found->second != resource.descriptor.content_hash)
                throw std::invalid_argument(
                    "Resource materialization path is ambiguous");
        } else {
            if (budget.temporary_bytes > temporary_limit ||
                resource.payload.size() >
                    temporary_limit - budget.temporary_bytes)
                throw std::length_error(
                    "Resource materialization exceeds the temporary-byte budget");
            const auto destination = owner->path / relative;
            std::filesystem::create_directories(destination.parent_path());
            std::ofstream output(destination,
                                 std::ios::binary | std::ios::trunc);
            if (!output)
                throw std::runtime_error(
                    "Resource materialization could not create its output");
            if (!resource.payload.empty())
                output.write(
                    reinterpret_cast<const char*>(resource.payload.data()),
                    static_cast<std::streamsize>(resource.payload.size()));
            output.close();
            if (!output)
                throw std::runtime_error(
                    "Resource materialization write failed");
            budget.temporary_bytes += resource.payload.size();
            written.emplace(relative, resource.descriptor.content_hash);
        }
        resolved_paths.emplace(resource.source_uri,
                               owner->path / relative);
    }
    return owner;
}

scene_ir::SceneIR clone_scene(
    const scene_ir::SceneIR& input,
    const std::function<std::string(std::string_view)>& resolve) {
    auto scene = input;
    std::unordered_map<const scene_ir::ImageResource*,
                       std::shared_ptr<scene_ir::ImageResource>> images;
    std::unordered_map<const scene_ir::TextureResource*,
                       std::shared_ptr<scene_ir::TextureResource>> textures;
    std::unordered_map<const scene_ir::MaterialNode*,
                       std::shared_ptr<scene_ir::MaterialNode>> materials;
    std::unordered_map<const scene_ir::MeshResource*,
                       std::shared_ptr<scene_ir::MeshResource>> meshes;
    const auto clone_image = [&](const std::shared_ptr<scene_ir::ImageResource>& value) {
        if (!value)
            return std::shared_ptr<scene_ir::ImageResource>{};
        if (const auto found = images.find(value.get()); found != images.end())
            return found->second;
        auto output = std::make_shared<scene_ir::ImageResource>(*value);
        output->uri = resolve(output->uri);
        images.emplace(value.get(), output);
        return output;
    };
    std::function<std::shared_ptr<scene_ir::TextureResource>(
        const std::shared_ptr<scene_ir::TextureResource>&)> clone_texture;
    clone_texture = [&](const std::shared_ptr<scene_ir::TextureResource>& value) {
        if (!value)
            return std::shared_ptr<scene_ir::TextureResource>{};
        if (const auto found = textures.find(value.get());
            found != textures.end())
            return found->second;
        auto output = std::make_shared<scene_ir::TextureResource>(*value);
        textures.emplace(value.get(), output);
        output->image = clone_image(value->image);
        return output;
    };
    std::function<std::shared_ptr<scene_ir::MaterialNode>(
        const std::shared_ptr<scene_ir::MaterialNode>&)> clone_material;
    clone_material = [&](const std::shared_ptr<scene_ir::MaterialNode>& value) {
        if (!value)
            return std::shared_ptr<scene_ir::MaterialNode>{};
        if (const auto found = materials.find(value.get());
            found != materials.end())
            return found->second;
        auto output = std::make_shared<scene_ir::MaterialNode>(*value);
        materials.emplace(value.get(), output);
        output->base_color_texture = clone_texture(value->base_color_texture);
        output->roughness_texture = clone_texture(value->roughness_texture);
        output->emission_texture = clone_texture(value->emission_texture);
        output->normal_texture = clone_texture(value->normal_texture);
        if (value->spectral_extension) {
            output->spectral_extension =
                std::make_shared<scene_ir::SpectralMaterialExtension>(
                    *value->spectral_extension);
            output->spectral_extension->albedo_spd =
                resolve(value->spectral_extension->albedo_spd);
            output->spectral_extension->emission_spd =
                resolve(value->spectral_extension->emission_spd);
        }
        if (value->graph) {
            output->graph =
                std::make_shared<scene_ir::MaterialGraph>(*value->graph);
            for (auto& node : output->graph->nodes)
                node.texture = clone_texture(node.texture);
        }
        return output;
    };
    for (auto& image : scene.images)
        image = clone_image(image);
    for (auto& texture : scene.textures)
        texture = clone_texture(texture);
    for (auto& material : scene.materials)
        material = clone_material(material);
    const auto clone_mesh = [&meshes](
        const std::shared_ptr<scene_ir::MeshResource>& value) {
        if (!value)
            return std::shared_ptr<scene_ir::MeshResource>{};
        if (const auto found = meshes.find(value.get()); found != meshes.end())
            return found->second;
        auto output = std::make_shared<scene_ir::MeshResource>(*value);
        if (value->mesh)
            output->mesh = std::make_shared<Mesh>(*value->mesh);
        meshes.emplace(value.get(), output);
        return output;
    };
    for (auto& mesh : scene.meshes)
        mesh = clone_mesh(mesh);
    for (auto& instance : scene.instances) {
        instance.mesh = clone_mesh(instance.mesh);
        instance.material = clone_material(instance.material);
    }
    for (auto& sphere : scene.spheres)
        sphere.material = clone_material(sphere.material);
    for (auto& light : scene.quad_lights)
        light.material = clone_material(light.material);
    return scene;
}

native_scene::SolverCapabilityRegistry product_solver_capabilities() {
    native_scene::SolverCapabilityRegistry result;
    result.integrators.insert(native_scene::NativeIntegratorMode::Wavefront);
    result.samplers = {IntegratorSampler::Default,
                       IntegratorSampler::LowDiscrepancy};
    result.wave_modes.insert(WaveOpticsMode::Radiometric);
    result.backends.insert(native_scene::ExecutionBackend::Cuda);
    result.acceleration_providers.insert(
        native_scene::AccelerationProvider::SoftwareBvh);
    result.coherent_merge_modes.insert(
        native_scene::CoherentMergeMode::None);
    result.validation_metrics.emplace("relative_error",
                                      native_scene::Version{1, 0});
    result.validation_metrics.emplace("energy_conservation",
                                      native_scene::Version{1, 0});
    return result;
}

Identity identity_from_hash(std::string_view hash) {
    return identity_from_hex(std::span<const char, 64>(hash.data(), 64));
}

Identity snapshot_identity(
    const native_scene::NativeSceneArchive& archive,
    const Identity& source_identity,
    std::span<const native_scene::NativeResolvedResource> resources,
    std::span<const ProductFeatureRecord> dispositions,
    const std::optional<RenderConfig>& solver,
    const std::optional<PhysicsConfig>& simulation) {
    std::string canonical = "UltraRender.ProductSnapshot.v1";
    canonical.push_back('\0');
    canonical.append(reinterpret_cast<const char*>(source_identity.data()),
                     source_identity.size());
    canonical.push_back('\0');
    canonical += native_scene::scene_ir_semantic_hash(archive);
    canonical.push_back('\0');
    canonical += archive.package_semantic_hash;
    auto sorted_resources =
        std::vector<native_scene::NativeResolvedResource>(resources.begin(),
                                                           resources.end());
    std::ranges::sort(sorted_resources, [](const auto& left, const auto& right) {
        return std::tie(left.logical_id, left.source_uri,
                        left.descriptor.content_hash) <
               std::tie(right.logical_id, right.source_uri,
                        right.descriptor.content_hash);
    });
    for (const auto& resource : sorted_resources) {
        canonical.push_back('\0');
        canonical += resource.logical_id;
        canonical.push_back('\0');
        canonical += resource.descriptor.content_hash;
    }
    auto sorted_dispositions =
        std::vector<ProductFeatureRecord>(dispositions.begin(),
                                          dispositions.end());
    std::ranges::sort(sorted_dispositions, {}, &ProductFeatureRecord::id);
    for (const auto& disposition : sorted_dispositions) {
        canonical.push_back('\0');
        canonical += disposition.id;
        canonical.push_back(static_cast<char>(disposition.requirement));
        canonical.push_back(static_cast<char>(disposition.disposition));
    }
    canonical.push_back(solver ? 1 : 0);
    if (solver) {
        canonical.append(reinterpret_cast<const char*>(&solver->max_trace_depth),
                         sizeof(solver->max_trace_depth));
        canonical.append(
            reinterpret_cast<const char*>(&solver->spectral_domain_bins),
            sizeof(solver->spectral_domain_bins));
        canonical.append(
            reinterpret_cast<const char*>(&solver->spectral_packet_lanes),
            sizeof(solver->spectral_packet_lanes));
    }
    canonical.push_back(simulation ? 1 : 0);
    if (simulation) {
        canonical.append(reinterpret_cast<const char*>(&simulation->dt),
                         sizeof(simulation->dt));
        canonical.append(
            reinterpret_cast<const char*>(&simulation->total_frames),
            sizeof(simulation->total_frames));
    }
    return content_identity(std::span(
        reinterpret_cast<const std::uint8_t*>(canonical.data()),
        canonical.size()));
}

}

struct ProductSnapshotBuilder {
    static std::shared_ptr<const ProductSnapshot> build(
        native_scene::NativeSceneArchive archive,
        std::vector<native_scene::NativeResolvedResource> resources,
        native_scene::NativeResourceBudgetBreakdown budget,
        std::vector<ProductFeatureRecord> dispositions,
        std::optional<RenderConfig> solver,
        std::optional<PhysicsConfig> simulation,
        std::shared_ptr<MaterializationRoot> owner,
        scene_ir::SceneIR render_scene,
        Identity source_identity) {
        auto snapshot = std::make_shared<ProductSnapshot>();
        snapshot->source_identity_ = source_identity;
        snapshot->semantic_identity_ =
            native_scene::scene_ir_semantic_hash(archive);
        snapshot->archive_ = std::move(archive);
        snapshot->resources_ = std::move(resources);
        snapshot->resource_budget_ = budget;
        snapshot->feature_dispositions_ = std::move(dispositions);
        snapshot->solver_config_ = std::move(solver);
        snapshot->simulation_plan_ = std::move(simulation);
        snapshot->materialization_root_ = owner->path;
        snapshot->materialization_owner_ = std::move(owner);
        snapshot->render_scene_ = std::move(render_scene);
        snapshot->identity_ = snapshot_identity(
            snapshot->archive_, snapshot->source_identity_, snapshot->resources_,
            snapshot->feature_dispositions_, snapshot->solver_config_,
            snapshot->simulation_plan_);
        return snapshot;
    }
};

const Identity& ProductSnapshot::identity() const noexcept { return identity_; }
const Identity& ProductSnapshot::source_identity() const noexcept {
    return source_identity_;
}
std::string_view ProductSnapshot::semantic_identity() const noexcept {
    return semantic_identity_;
}
int ProductSnapshot::declared_samples() const noexcept {
    return render_scene_.spp;
}
scene_ir::SceneIR ProductSnapshot::render_scene() const {
    return clone_scene(render_scene_, [](std::string_view value) {
        return std::string(value);
    });
}
const std::vector<native_scene::NativeResolvedResource>&
ProductSnapshot::resources() const noexcept {
    return resources_;
}
const native_scene::NativeResourceBudgetBreakdown&
ProductSnapshot::resource_budget() const noexcept {
    return resource_budget_;
}
const std::vector<ProductFeatureRecord>&
ProductSnapshot::feature_dispositions() const noexcept {
    return feature_dispositions_;
}
const std::optional<RenderConfig>& ProductSnapshot::solver_config() const noexcept {
    return solver_config_;
}
const std::optional<PhysicsConfig>& ProductSnapshot::simulation_plan() const noexcept {
    return simulation_plan_;
}
const std::filesystem::path& ProductSnapshot::materialization_root() const noexcept {
    return materialization_root_;
}

bool ProductRealizationResult::ok() const noexcept {
    return snapshot && diagnostics.empty();
}

ProductRealizationResult realize_product_scene_impl(
    native_scene::NativeSceneArchive archive,
    Identity accepted_source_identity,
    const ProductRealizationLimits& limits) {
    ProductRealizationResult result;
    const std::string source_hash = native_scene::scene_ir_semantic_hash(archive);
    const Identity semantic_source_identity = identity_from_hash(source_hash);
    const bool has_accepted_identity = std::ranges::any_of(
        accepted_source_identity, [](std::uint8_t value) { return value != 0; });
    const Identity source_identity = has_accepted_identity
                                         ? accepted_source_identity
                                         : semantic_source_identity;
    const auto validation =
        native_scene::validate_scene_ir_archive(archive, limits.validation);
    if (!validation.ok()) {
        const auto& cause = validation.diagnostics.front();
        add_diagnostic(result, ProductSceneDiagnosticDomain::Schema,
                       kInvalidArchive, cause.code, cause.path, cause.message,
                       "fix the scene schema or field value");
        return result;
    }
    std::vector<ProductFeatureRecord> dispositions;
    add_disposition(dispositions, "ure.scene.native",
                    native_scene::RequirementLevel::Required,
                    ProductFeatureDisposition::Executed);
    if (archive.package_manifest) {
        if (archive.package_semantic_hash.empty()) {
            add_diagnostic(result, ProductSceneDiagnosticDomain::Package,
                           kPackageInvalid, "URE-PRV2-PACKAGE-001", "/package",
                           "Package semantic identity is unavailable",
                           "rebuild the package with the current scene tool");
            return result;
        }
        add_disposition(dispositions, "ure.scene.package",
                        native_scene::RequirementLevel::Required,
                        ProductFeatureDisposition::Executed);
        if (!archive.package_manifest->dependencies.empty()) {
            add_diagnostic(
                result, ProductSceneDiagnosticDomain::Package,
                kPackageInvalid, "URE-PRV2-PACKAGE-DEPENDENCY-001",
                "/package/dependencies",
                "External package dependencies are not self-contained",
                "pack every required dependency into one self-contained package");
            return result;
        }
        if (!archive.package_manifest->caches.empty())
            add_disposition(
                dispositions, "ure.scene.compiled-cache",
                native_scene::RequirementLevel::Optional,
                ProductFeatureDisposition::PreservedForTooling,
                kRequiredFeatureUnavailable);
    }
    const auto procedural_requirement = feature_requirement(
        archive, kProceduralFeature,
        archive.procedural_graph ? native_scene::RequirementLevel::Required
                                 : native_scene::RequirementLevel::Optional);
    if (archive.procedural_graph) {
        auto built = native_scene::build_procedural_scene(archive);
        if (!built.ok() || !built.value) {
            if (procedural_requirement ==
                native_scene::RequirementLevel::Required) {
                const auto cause = built.diagnostics.empty()
                                       ? native_scene::ValidationDiagnostic{}
                                       : built.diagnostics.front();
                add_diagnostic(
                    result, ProductSceneDiagnosticDomain::Procedural,
                    kProceduralBuildFailed,
                    cause.code.empty() ? "URE-PRV2-PROCEDURAL-001"
                                       : cause.code,
                    cause.path.empty() ? "/procedural_graph" : cause.path,
                    cause.message.empty()
                        ? "Procedural graph realization failed"
                        : cause.message,
                    "fix the procedural graph or its declared resource",
                    procedural_requirement);
                return result;
            }
            add_disposition(dispositions, std::string(kProceduralFeature),
                            procedural_requirement,
                            ProductFeatureDisposition::PreservedForTooling,
                            kProceduralBuildFailed);
        } else {
            archive.scene = std::move(built.value->scene);
            archive.source_ids = std::move(built.value->source_ids);
            archive.procedural_graph.reset();
            for (auto& resource : built.value->generated_resources) {
                resource.source_uri = resource.descriptor.uri;
                if (std::ranges::find(archive.document.resources,
                                      resource.descriptor.id,
                                      &native_scene::ResourceDescriptor::id) ==
                    archive.document.resources.end())
                    archive.document.resources.push_back(resource.descriptor);
                archive.packaged_resources.push_back(std::move(resource));
            }
            native_scene::assign_deterministic_object_uuids(archive);
            add_disposition(dispositions,
                            std::string(kProceduralFeature),
                            procedural_requirement,
                            ProductFeatureDisposition::Executed);
        }
    } else if (procedural_requirement ==
               native_scene::RequirementLevel::Required) {
        add_diagnostic(result, ProductSceneDiagnosticDomain::Feature,
                       kRequiredFeatureUnavailable,
                       "URE-PRV2-FEATURE-001", "/features/ure.scene.procedural",
                       "Required procedural feature has no graph payload",
                       "add the declared graph or make the feature optional",
                       procedural_requirement);
        return result;
    }
    std::optional<RenderConfig> solver;
    const auto solver_requirement = feature_requirement(
        archive, native_scene::kSolverContractFeature,
        archive.solver_contract ? native_scene::RequirementLevel::Required
                                : native_scene::RequirementLevel::Optional);
    if (archive.solver_contract) {
        auto compiled = native_scene::compile_solver_contract(
            *archive.solver_contract, product_solver_capabilities());
        if (!compiled.ok()) {
            if (solver_requirement == native_scene::RequirementLevel::Required) {
                const auto cause = compiled.diagnostics.empty()
                                       ? native_scene::ValidationDiagnostic{}
                                       : compiled.diagnostics.front();
                add_diagnostic(
                    result, ProductSceneDiagnosticDomain::Solver,
                    kSolverCompileFailed,
                    cause.code.empty() ? "URE-PRV2-SOLVER-001" : cause.code,
                    cause.path.empty() ? "/solver" : cause.path,
                    cause.message.empty() ? "Solver contract is not applicable"
                                          : cause.message,
                    "select a supported solver contract or product runtime",
                    solver_requirement);
                return result;
            }
            add_disposition(dispositions, native_scene::kSolverContractFeature,
                            solver_requirement,
                            ProductFeatureDisposition::PreservedForTooling,
                            kSolverCompileFailed);
        } else {
            solver = std::move(compiled.config);
            if (!valid_spectral_packet_lane_count(
                    solver->spectral_packet_lanes)) {
                add_diagnostic(
                    result, ProductSceneDiagnosticDomain::Solver,
                    kSolverCompileFailed,
                    "URE-PRV2-SOLVER-SPECTRAL-001",
                    "/solver/spectral/packet_lanes",
                    "Solver packet width is not executable by the product renderer",
                    "select one lane or a packet width from 8 through 32",
                    solver_requirement);
                return result;
            }
            solver->backend.kind = BackendKind::Cuda;
            solver->acceleration.provider =
                AccelerationProviderKind::SelfCompute;
            add_disposition(dispositions, native_scene::kSolverContractFeature,
                            solver_requirement,
                            ProductFeatureDisposition::Executed);
        }
    } else if (solver_requirement == native_scene::RequirementLevel::Required) {
        add_diagnostic(result, ProductSceneDiagnosticDomain::Feature,
                       kRequiredFeatureUnavailable,
                       "URE-PRV2-FEATURE-002", "/features/ure.render.solver",
                       "Required solver feature has no contract payload",
                       "add the solver contract or make the feature optional",
                       solver_requirement);
        return result;
    }
    std::optional<PhysicsConfig> simulation;
    const auto simulation_requirement = feature_requirement(
        archive, native_scene::kSimulationFeature,
        archive.simulation_contract
            ? native_scene::RequirementLevel::Required
            : native_scene::RequirementLevel::Optional);
    if (archive.simulation_contract) {
        const bool required_domain = std::ranges::any_of(
            archive.simulation_contract->domains, [](const auto& domain) {
                return domain.requirement ==
                       native_scene::RequirementLevel::Required;
            });
        const bool required_coupling = std::ranges::any_of(
            archive.simulation_contract->coupling, [](const auto& coupling) {
                return coupling.requirement ==
                       native_scene::RequirementLevel::Required;
            });
        if (required_domain || required_coupling) {
            add_diagnostic(
                result, ProductSceneDiagnosticDomain::Simulation,
                kSimulationUnavailable, "URE-PRV2-SIMULATION-001",
                "/simulation",
                "Required dynamic simulation cannot be executed by the Preview scene realizer",
                "make the domain optional or use the later stateful-session capability",
                simulation_requirement);
            return result;
        }
        native_scene::SimulationCapabilityRegistry capabilities;
        const auto compiled = native_scene::compile_simulation_contract(
            *archive.simulation_contract, capabilities);
        if (!compiled.ok()) {
            add_diagnostic(result,
                           ProductSceneDiagnosticDomain::Simulation,
                           kSimulationUnavailable,
                           "URE-PRV2-SIMULATION-002", "/simulation",
                           "Simulation time plan is invalid",
                           "fix the time plan or unsupported coupling",
                           simulation_requirement);
            return result;
        }
        simulation = compiled.physics;
        add_disposition(dispositions, native_scene::kSimulationFeature,
                        simulation_requirement,
                        archive.simulation_contract->domains.empty() &&
                                archive.simulation_contract->coupling.empty()
                            ? ProductFeatureDisposition::Executed
                            : ProductFeatureDisposition::PreservedForTooling,
                        archive.simulation_contract->domains.empty() &&
                                archive.simulation_contract->coupling.empty()
                            ? 0
                            : kSimulationUnavailable);
    } else if (simulation_requirement ==
               native_scene::RequirementLevel::Required) {
        add_diagnostic(result, ProductSceneDiagnosticDomain::Feature,
                       kRequiredFeatureUnavailable,
                       "URE-PRV2-FEATURE-003", "/features/ure.scene.simulation",
                       "Required simulation feature has no contract payload",
                       "add the simulation contract or make the feature optional",
                       simulation_requirement);
        return result;
    }
    for (const auto& feature : archive.document.features) {
        if (feature.name == kProceduralFeature ||
            feature.name == native_scene::kResourceCatalogFeature ||
            feature.name == native_scene::kSolverContractFeature ||
            feature.name == native_scene::kSimulationFeature)
            continue;
        if (feature.name == native_scene::kScriptBuildFeature) {
            if (feature.requirement ==
                native_scene::RequirementLevel::Required) {
                add_diagnostic(
                    result, ProductSceneDiagnosticDomain::Feature,
                    kRequiredFeatureUnavailable,
                    "URE-PRV2-SCRIPT-001", "/features/ure.scene.script-build",
                    "Ambient script execution is forbidden in the product runtime",
                    "build the script explicitly and submit its native output",
                    feature.requirement);
                return result;
            }
            add_disposition(dispositions, feature.name, feature.requirement,
                            ProductFeatureDisposition::PreservedForTooling,
                            kRequiredFeatureUnavailable);
            continue;
        }
        if (feature.requirement == native_scene::RequirementLevel::Required) {
            add_diagnostic(result, ProductSceneDiagnosticDomain::Feature,
                           kRequiredFeatureUnavailable,
                           "URE-PRV2-FEATURE-004", "/features/" + feature.name,
                           "Required feature has no product execution owner",
                           "remove the feature or install its versioned extension",
                           feature.requirement);
            return result;
        }
        add_disposition(dispositions, feature.name, feature.requirement,
                        ProductFeatureDisposition::PreservedForTooling,
                        kRequiredFeatureUnavailable);
    }
    for (const auto& extension : archive.document.extensions) {
        const std::string id = "extension/" + extension.name;
        if (extension.requirement == native_scene::RequirementLevel::Required) {
            add_diagnostic(result, ProductSceneDiagnosticDomain::Feature,
                           kRequiredFeatureUnavailable,
                           "URE-PRV2-EXTENSION-001",
                           "/extensions/" + extension.name,
                           "Required extension has no product execution owner",
                           "install the owning extension or make it optional",
                           extension.requirement);
            return result;
        }
        add_disposition(dispositions, id, extension.requirement,
                        ProductFeatureDisposition::PreservedForTooling,
                        kRequiredFeatureUnavailable);
    }
    for (const auto& chunk : archive.preserved_optional_chunks) {
        add_disposition(dispositions, "chunk/" + chunk.id, chunk.requirement,
                        ProductFeatureDisposition::PreservedForTooling,
                        kRequiredFeatureUnavailable);
    }
    const auto realized_validation =
        native_scene::validate_scene_ir_archive(archive, limits.validation);
    if (!realized_validation.ok()) {
        const auto& cause = realized_validation.diagnostics.front();
        add_diagnostic(result, ProductSceneDiagnosticDomain::Procedural,
                       kProceduralBuildFailed, cause.code, cause.path,
                       cause.message,
                       "fix the realized scene or procedural output");
        return result;
    }
    auto resolution = native_scene::resolve_native_scene_resources(
        archive, limits.validation);
    if (!resolution.ok()) {
        const auto& cause = resolution.diagnostics.front();
        add_diagnostic(result, ProductSceneDiagnosticDomain::Resource,
                       resource_detail(cause.code), cause.code, cause.path,
                       cause.message,
                       "supply the content-addressed resource and matching hash");
        return result;
    }
    const std::uint64_t output_width = static_cast<std::uint64_t>(
        archive.scene.width > 0 ? archive.scene.width : 64);
    const std::uint64_t output_height = static_cast<std::uint64_t>(
        archive.scene.height > 0 ? archive.scene.height : 64);
    if (output_width > std::numeric_limits<std::uint64_t>::max() /
                           output_height / 3 / sizeof(float)) {
        add_diagnostic(result, ProductSceneDiagnosticDomain::Resource,
                       kTemporaryBudgetExceeded,
                       "URE-PRV2-OUTPUT-BUDGET-001", "/scene/output",
                       "Product output memory size overflowed",
                       "reduce the requested output dimensions");
        return result;
    }
    resolution.budget.output_bytes =
        output_width * output_height * 3 * sizeof(float);
    if (archive.resource_catalog ||
        std::ranges::any_of(archive.document.features, [](const auto& feature) {
            return feature.name == native_scene::kResourceCatalogFeature;
        })) {
        add_disposition(
            dispositions, std::string(native_scene::kResourceCatalogFeature),
            feature_requirement(
                archive, native_scene::kResourceCatalogFeature,
                native_scene::RequirementLevel::Required),
            ProductFeatureDisposition::Executed);
    }
    for (const auto& resource : resolution.resources)
        add_disposition(dispositions, "resource/" + resource.logical_id,
                        native_scene::RequirementLevel::Required,
                        ProductFeatureDisposition::Executed);
    try {
        std::map<std::string, std::filesystem::path> resolved_paths;
        auto owner = materialize_resources(
            resolution.resources, limits.temporary_bytes,
            resolution.budget, resolved_paths);
        const auto resolve = [&resolved_paths](std::string_view value) {
            if (value.empty())
                return std::string{};
            const auto found = resolved_paths.find(std::string(value));
            if (found == resolved_paths.end())
                throw std::invalid_argument(
                    "Scene resource has no content-identity resolution");
            return found->second.string();
        };
        auto render = clone_scene(archive.scene, resolve);
        result.snapshot = ProductSnapshotBuilder::build(
            std::move(archive), std::move(resolution.resources),
            resolution.budget, std::move(dispositions), std::move(solver),
            std::move(simulation), std::move(owner), std::move(render),
            source_identity);
    } catch (const std::length_error& error) {
        add_diagnostic(result, ProductSceneDiagnosticDomain::Resource,
                       kTemporaryBudgetExceeded,
                       "URE-PRV2-RESOURCE-BUDGET-004", "/resources",
                       error.what(), "increase the temporary resource budget");
    } catch (const std::exception& error) {
        add_diagnostic(result, ProductSceneDiagnosticDomain::Resource,
                       kResourceResolutionFailed,
                       "URE-PRV2-RESOURCE-MATERIALIZE-001", "/resources",
                       error.what(),
                       "fix the resource URI or content binding");
    }
    return result;
}

ProductRealizationResult realize_product_scene(
    native_scene::NativeSceneArchive archive,
    const ProductRealizationLimits& limits) {
    return realize_product_scene_impl(std::move(archive), {}, limits);
}

ProductRealizationResult realize_product_scene(
    native_scene::NativeSceneArchive archive,
    Identity accepted_source_identity,
    const ProductRealizationLimits& limits) {
    return realize_product_scene_impl(std::move(archive),
                                      accepted_source_identity, limits);
}

}
