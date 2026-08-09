#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <ure/native_scene_container.hpp>
#include <ure/native_resource_catalog.hpp>
#include <ure/native_solver_contract.hpp>
#include <ure/native_simulation_contract.hpp>
#include <ure/scene_ir.hpp>

namespace ure::native_scene {

struct ProceduralGraph;

struct NamedResourcePayload {
    std::string id;
    ResourceDescriptor descriptor;
    std::vector<std::uint8_t> payload;
};

struct NativeSceneSourceIds {
    std::vector<std::string> materials;
    std::vector<std::string> meshes;
    std::vector<std::string> images;
    std::vector<std::string> textures;
    std::vector<std::string> instances;
    std::vector<std::string> spheres;
    std::vector<std::string> quad_lights;
};

struct NativeSceneObjectUuids {
    std::vector<Uuid> materials;
    std::vector<Uuid> meshes;
    std::vector<Uuid> images;
    std::vector<Uuid> textures;
    std::vector<Uuid> instances;
    std::vector<Uuid> spheres;
    std::vector<Uuid> quad_lights;
    Uuid camera;
    Uuid environment;
};

struct CanonicalCamera {
    std::array<double, 16> world_from_camera{};
    double sensor_width_m = 0.036;
    double sensor_height_m = 0.024;
    double focal_length_m = 0.02897056274847714;
    double aperture_diameter_m = 0.0;
    double focus_distance_m = 10.0;
    double lens_shift_x_m = 0.0;
    double lens_shift_y_m = 0.0;
    double shutter_open_s = 0.0;
    double shutter_close_s = 0.0;
    double exposure_scale = 1.0;
    auto operator<=>(const CanonicalCamera&) const = default;
};

struct NativeSceneArchive {
    SceneDocument document;
    scene_ir::SceneIR scene;
    NativeSceneSourceIds source_ids;
    NativeSceneObjectUuids object_uuids;
    CanonicalCamera canonical_camera;
    std::shared_ptr<const ProceduralGraph> procedural_graph;
    std::shared_ptr<const NativeResourceCatalog> resource_catalog;
    std::shared_ptr<const NativeSolverContract> solver_contract;
    std::shared_ptr<const NativeSimulationContract> simulation_contract;
    std::vector<ContainerChunk> preserved_optional_chunks;
};

struct ExplodedSceneArchive {
    std::string manifest;
    std::vector<NamedResourcePayload> resources;
};

NativeSceneArchive make_native_scene_archive(SceneDocument document,
                                             const scene_ir::SceneIR& scene);
void assign_deterministic_object_uuids(NativeSceneArchive& archive);
CanonicalCamera canonical_camera_from_scene(const Camera& camera);
void apply_canonical_camera(const CanonicalCamera& source, Camera& camera);
bool valid_canonical_camera(const CanonicalCamera& camera) noexcept;

std::vector<std::uint8_t> write_scene_ir_binary(const NativeSceneArchive& archive);
LoadResult<NativeSceneArchive> read_scene_ir_binary(
    std::span<const std::uint8_t> bytes,
    const CapabilityRegistry& registry,
    const ValidationLimits& limits = {});

ExplodedSceneArchive write_scene_ir_text(const NativeSceneArchive& archive);
LoadResult<NativeSceneArchive> read_scene_ir_text(
    const ExplodedSceneArchive& archive,
    const CapabilityRegistry& registry,
    const ValidationLimits& limits = {});

std::string scene_ir_semantic_hash(const NativeSceneArchive& archive);
ValidationReport validate_scene_ir_archive(const NativeSceneArchive& archive,
                                           const ValidationLimits& limits = {});

void save_native_scene(const std::filesystem::path& path,
                       const NativeSceneArchive& archive);
LoadResult<NativeSceneArchive> load_native_scene(
    const std::filesystem::path& path,
    const CapabilityRegistry& registry,
    const ValidationLimits& limits = {});

}
