#ifndef ULTRARENDER_SCENE_ADAPTER_HPP
#define ULTRARENDER_SCENE_ADAPTER_HPP

#include "runtime_objects.hpp"

#include <array>
#include <memory>
#include <mutex>
#include <string>

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

std::shared_ptr<const SceneRevisionData>
scene_revision(ure_handle_t scene, ure_handle_t *error) noexcept;
void write_scene_revision(const SceneRevisionData &source,
                          ure_scene_revision_info_t &output) noexcept;

}

#endif
