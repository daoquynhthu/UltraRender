#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "ure/runtime/acceleration.hpp"
#include "ure/scene_ir.hpp"

namespace ure::test {

struct Float4 {
    float x;
    float y;
    float z;
    float w;
};

struct AccelerationInstanceData {
    std::array<Float4, 3> object_to_world;
    std::array<Float4, 3> world_to_object;
    std::array<Float4, 3> normal_transform;
    Float4 bounds_min;
    Float4 bounds_max;
    std::array<std::uint32_t, 4> ids;
};

struct AccelerationParityFixture {
    scene_ir::SceneIR scene;
    std::array<Float4, 4> vertices;
    std::array<Float4, 4> normals;
    std::array<Float4, 4> texcoords;
    std::array<Float4, 4> tangents;
    std::array<std::uint32_t, 6> indices;
    std::array<AccelerationInstanceData, 2> instances;
    std::array<runtime::AccelerationInstanceDesc, 2>
        acceleration_instances;
    std::array<runtime::AccelerationRay, 5> rays;
};

inline AccelerationParityFixture
make_acceleration_parity_fixture() {
    AccelerationParityFixture value;
    auto mesh = std::make_shared<Mesh>();
    mesh->vertices = {
        {{-1.0f, -1.0f, 0.0f},
         {0.0f, 0.0f, 1.0f},
         {0.0f, 0.0f},
         {1.0f, 0.0f, 0.0f}},
        {{1.0f, -1.0f, 0.0f},
         {0.0f, 0.0f, 1.0f},
         {1.0f, 0.0f},
         {0.8f, 0.6f, 0.0f}},
        {{1.0f, 1.0f, 0.0f},
         {0.0f, 0.0f, 1.0f},
         {1.0f, 1.0f},
         {0.6f, 0.8f, 0.0f}},
        {{-1.0f, 1.0f, 0.0f},
         {0.0f, 0.0f, 1.0f},
         {0.0f, 1.0f},
         {0.0f, 1.0f, 0.0f}}};
    mesh->indices = {0, 1, 2, 0, 2, 3};
    auto resource = std::make_shared<
        scene_ir::MeshResource>();
    resource->name = "phase_v7_quad";
    resource->mesh = mesh;
    value.scene.meshes.push_back(resource);
    for (std::uint32_t index = 0; index < 8; ++index) {
        auto material = std::make_shared<
            scene_ir::MaterialNode>();
        material->name =
            "phase_v7_material_" +
            std::to_string(index);
        value.scene.materials.push_back(material);
    }
    value.scene.instances = {
        {
            "phase_v7_left",
            resource,
            value.scene.materials[5],
            {-2.0f, 0.0f, 0.0f},
            {1.5f, 0.75f, 1.0f},
            {},
            {}},
        {
            "phase_v7_right",
            resource,
            value.scene.materials[7],
            {2.0f, 0.0f, 0.0f},
            {0.75f, 1.5f, 1.0f},
            {},
            {}}};
    for (std::size_t index = 0;
         index < mesh->vertices.size();
         ++index) {
        const auto& source = mesh->vertices[index];
        value.vertices[index] = {
            source.position.x,
            source.position.y,
            source.position.z,
            1.0f};
        value.normals[index] = {
            source.normal.x,
            source.normal.y,
            source.normal.z,
            0.0f};
        value.texcoords[index] = {
            source.uv.x,
            source.uv.y,
            0.0f,
            0.0f};
        value.tangents[index] = {
            source.tangent.x,
            source.tangent.y,
            source.tangent.z,
            1.0f};
    }
    for (std::size_t index = 0;
         index < mesh->indices.size();
         ++index) {
        value.indices[index] =
            static_cast<std::uint32_t>(
                mesh->indices[index]);
    }
    const std::array visibility_masks = {
        std::uint32_t{0xff},
        std::uint32_t{0xff}};
    for (std::size_t index = 0;
         index < value.scene.instances.size();
         ++index) {
        const auto& source =
            value.scene.instances[index];
        const auto material =
            source.material ==
                value.scene.materials[5]
            ? 5u
            : 7u;
        const float inverse_x =
            1.0f / source.scale.x;
        const float inverse_y =
            1.0f / source.scale.y;
        const float inverse_z =
            1.0f / source.scale.z;
        value.instances[index] = {
            {{
                {source.scale.x, 0.0f, 0.0f,
                 source.position.x},
                {0.0f, source.scale.y, 0.0f,
                 source.position.y},
                {0.0f, 0.0f, source.scale.z,
                 source.position.z}}},
            {{
                {inverse_x, 0.0f, 0.0f,
                 -source.position.x * inverse_x},
                {0.0f, inverse_y, 0.0f,
                 -source.position.y * inverse_y},
                {0.0f, 0.0f, inverse_z,
                 -source.position.z * inverse_z}}},
            {{
                {inverse_x, 0.0f, 0.0f, 0.0f},
                {0.0f, inverse_y, 0.0f, 0.0f},
                {0.0f, 0.0f, inverse_z, 0.0f}}},
            {
                source.position.x - source.scale.x,
                source.position.y - source.scale.y,
                -0.001f,
                0.0f},
            {
                source.position.x + source.scale.x,
                source.position.y + source.scale.y,
                0.001f,
                0.0f},
            {
                static_cast<std::uint32_t>(index),
                material,
                visibility_masks[index],
                0}};
        value.acceleration_instances[index].
            object_to_world = {
                source.scale.x, 0.0f, 0.0f,
                source.position.x,
                0.0f, source.scale.y, 0.0f,
                source.position.y,
                0.0f, 0.0f, source.scale.z,
                source.position.z};
        value.acceleration_instances[index].
            instance_index =
                static_cast<std::uint32_t>(index);
        value.acceleration_instances[index].
            material_index = material;
        value.acceleration_instances[index].
            visibility_mask =
                static_cast<std::uint8_t>(
                    visibility_masks[index]);
    }
    const std::array origins = {
        Float4{-2.75f, 0.1875f, 4.0f, 0.001f},
        Float4{2.3f, -0.45f, 4.0f, 0.001f},
        Float4{0.5f, 0.0f, 4.0f, 0.001f},
        Float4{4.0f, 0.0f, 4.0f, 0.001f},
        Float4{-2.75f, 0.1875f, 4.0f, 0.001f}};
    const std::array masks = {
        0xffu, 0xffu, 0xffu, 0xffu, 0xffu};
    for (std::size_t index = 0;
         index < value.rays.size();
         ++index) {
        value.rays[index].origin_tmin = {
            origins[index].x,
            origins[index].y,
            origins[index].z,
            origins[index].w};
        value.rays[index].direction_tmax = {
            0.0f, 0.0f, -1.0f, 100.0f};
        value.rays[index].mask_flags[0] =
            masks[index];
    }
    value.rays[4].mask_flags[1] = 1;
    return value;
}

static_assert(
    sizeof(AccelerationInstanceData) == 192);

}
