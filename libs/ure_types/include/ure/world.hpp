#pragma once

#include "ure/core/vector.hpp"
#include "ure/core/quaternion.hpp"
#include "ure/render_config.hpp"
#include "ure/ure_api.hpp"
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace ure {

// Phase P.4: Entity-Component data model.
// Component pools use SoA layout (separate vectors per component type).
// Physics/Audio component types remain lightweight handles;
// concrete physics/audio configs live in ure_physics.

using EntityId = uint32_t;

inline constexpr EntityId kInvalidEntity = ~EntityId(0);

// ── Components ──────────────────────────────────────────────────────

struct TransformComponent {
    core::Vec3f position  = {0, 0, 0};
    core::Quat  rotation  = {};  // default = identity
    core::Vec3f scale     = {1, 1, 1};

    core::Matrix4x4f to_matrix() const;
};

struct GeometryComponent {
    std::shared_ptr<Mesh> mesh;
    int material_index = 0;
};

struct PhysicsComponent {
    int config_id = -1;  // index into external physics config array
};

struct AudioComponent {
    int material_id = -1;
    int modal_body_id = -1;
};

// ── World ───────────────────────────────────────────────────────────

struct World {
    // Entity bookkeeping
    std::vector<EntityId> entities;
    EntityId next_id = 1;  // 0 reserved for invalid
    std::unordered_map<EntityId, size_t> entity_to_index;

    // SoA component pools (indexed by entity index, not EntityId)
    std::vector<TransformComponent> transforms;
    std::vector<GeometryComponent>  geometries;
    std::vector<PhysicsComponent>   physics;
    std::vector<AudioComponent>     audio;

    // Material table (indexed by GeometryComponent::material_index)
    std::vector<std::shared_ptr<Material>> material_table;

    // Physics config (mirrors Scene::physics)
    PhysicsConfig physics_config;

    // Global state
    RenderConfig render_config;
    Camera camera;
    core::Vec3f background_color = {0,0,0};
    int width = 0;
    int height = 0;
    int spp = 0;

    // ── Entity lifecycle ──

    EntityId create_entity();

    // Remove entity and its components. Slower: O(N) compaction.
    // For high-frequency removal, mark-as-deleted + batch sweep later.
    void remove_entity(EntityId id);

    // ── Accessors ──

    size_t index_of(EntityId id) const {
        auto it = entity_to_index.find(id);
        return it != entity_to_index.end() ? it->second : SIZE_MAX;
    }

    bool has_entity(EntityId id) const {
        return entity_to_index.contains(id);
    }

    // ── Convenience ──

    // Number of live entities
    size_t entity_count() const { return entities.size(); }

    // Check if all component pools have the same size (debug invariant)
    bool invariant() const {
        size_t n = entities.size();
        return transforms.size() == n
            && geometries.size()  == n
            && physics.size()     == n
            && audio.size()       == n;
    }
};

// ── Inline implementations ──────────────────────────────────────────

inline core::Matrix4x4f TransformComponent::to_matrix() const {
    core::Matrix4x4f m = rotation.to_matrix();
    // Apply scale
    m.m[0][0] *= scale.x; m.m[0][1] *= scale.x; m.m[0][2] *= scale.x;
    m.m[1][0] *= scale.y; m.m[1][1] *= scale.y; m.m[1][2] *= scale.y;
    m.m[2][0] *= scale.z; m.m[2][1] *= scale.z; m.m[2][2] *= scale.z;
    // Apply translation
    m.m[0][3] = position.x;
    m.m[1][3] = position.y;
    m.m[2][3] = position.z;
    return m;
}

inline EntityId World::create_entity() {
    EntityId id = next_id++;
    entities.push_back(id);
    entity_to_index[id] = entities.size() - 1;
    transforms.emplace_back();
    geometries.emplace_back();
    physics.emplace_back();
    audio.emplace_back();
    return id;
}

inline void World::remove_entity(EntityId id) {
    auto it = entity_to_index.find(id);
    if (it == entity_to_index.end()) return;
    size_t idx = it->second;
    size_t last = entities.size() - 1;

    // Swap with last element (O(1) removal, maintains contiguous pools)
    if (idx < last) {
        EntityId last_id = entities[last];
        entities[idx] = last_id;
        transforms[idx] = std::move(transforms[last]);
        geometries[idx]  = std::move(geometries[last]);
        physics[idx]     = std::move(physics[last]);
        audio[idx]       = std::move(audio[last]);
        entity_to_index[last_id] = idx;
    }

    entities.pop_back();
    transforms.pop_back();
    geometries.pop_back();
    physics.pop_back();
    audio.pop_back();
    entity_to_index.erase(id);
}

} // namespace ure
