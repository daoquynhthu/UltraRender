#ifndef ULTRARENDER_SCENE_ADAPTER_HPP
#define ULTRARENDER_SCENE_ADAPTER_HPP

#include "runtime_objects.hpp"

#include <array>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <ure/native_scene_ir.hpp>

namespace ure::contract {

struct SceneRevisionData {
    native_scene::NativeSceneArchive archive;
    std::array<std::uint8_t, 32> revision_identity{};
    std::array<std::uint8_t, 32> blob_digest{};
    std::array<std::uint8_t, 32> semantic_digest{};
    std::array<std::uint8_t, 32> resource_manifest_digest{};
    std::string selected_package_scene;
    std::uint64_t revision{};
    std::uint64_t resource_count{};
    std::uint64_t object_count{};
    std::uint32_t source_schema_major{};
    std::uint32_t source_schema_minor{};
    std::uint32_t reset_reason{};
    std::uint32_t warning_count{};
    std::uint32_t loss_count{};
};

struct SceneObject final : Object {
    std::mutex mutex;
    std::shared_ptr<InstanceObject> instance;
    std::shared_ptr<const SceneRevisionData> current;
};

struct LoadedSceneData {
    std::shared_ptr<SceneRevisionData> revision;
    std::vector<native_scene::ValidationDiagnostic> diagnostics;
};

LoadedSceneData load_scene_blob(const ure_native_scene_blob_t &blob);
std::shared_ptr<SceneRevisionData> finalize_scene_revision(
    native_scene::NativeSceneArchive archive,
    const std::array<std::uint8_t, 32> &blob_digest,
    std::uint64_t revision,
    std::uint32_t reset_reason);
ure_result_t URE_CALL apply_scene_transaction(
    ure_handle_t scene,
    const ure_scene_transaction_t *transaction,
    ure_scene_transaction_result_t *result,
    ure_handle_t *error) noexcept;

std::shared_ptr<const SceneRevisionData>
scene_revision(ure_handle_t scene, ure_handle_t *error) noexcept;
void write_scene_revision(const SceneRevisionData &source,
                          ure_scene_revision_info_t &output) noexcept;

}

#endif
