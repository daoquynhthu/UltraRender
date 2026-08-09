#define NOMINMAX

#include "scene_adapter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/verifier.h>
#include <ure/native_scene_hash.hpp>
#include <ure/native_scene_uuid.hpp>

#include "ure_scene_candidate_generated.h"

namespace ure::contract {
namespace {

namespace fb = ultrarender::contract::candidate;
using native_scene::Uuid;
using Digest = std::array<std::uint8_t, 32>;

Digest digest(std::span<const std::uint8_t> bytes) {
    const std::string text = native_scene::sha256_hex(bytes);
    Digest output{};
    const auto nibble = [](char value) -> std::uint8_t {
        if (value >= '0' && value <= '9')
            return static_cast<std::uint8_t>(value - '0');
        return static_cast<std::uint8_t>(value - 'a' + 10);
    };
    for (std::size_t index = 0; index < output.size(); ++index)
        output[index] = static_cast<std::uint8_t>(
            (nibble(text[index * 2]) << 4U) | nibble(text[index * 2 + 1]));
    return output;
}

bool equal(const ure_digest256_t &left, const Digest &right) noexcept {
    return std::memcmp(left.bytes, right.data(), right.size()) == 0;
}

void store(ure_digest256_t &output, const Digest &value) noexcept {
    std::memcpy(output.bytes, value.data(), value.size());
}

Uuid uuid(const fb::UuidValueT *value) {
    if (!value || value->bytes.size() != 16)
        throw std::invalid_argument("scene transaction UUID is missing");
    Uuid output;
    std::ranges::copy(value->bytes, output.bytes.begin());
    if (!native_scene::is_rfc9562_uuid(output))
        throw std::invalid_argument("scene transaction UUID is not RFC 9562");
    return output;
}

Uuid uuid(const ure_uuid_t &value) {
    Uuid output;
    std::ranges::copy(value.bytes, output.bytes.begin());
    if (!native_scene::is_rfc9562_uuid(output))
        throw std::invalid_argument("scene transaction envelope UUID is invalid");
    return output;
}

std::unique_ptr<fb::UuidValueT> fb_uuid(const Uuid &value) {
    auto output = std::make_unique<fb::UuidValueT>();
    output->bytes.assign(value.bytes.begin(), value.bytes.end());
    return output;
}

core::Vec3f vector(const fb::EditVec3 *value) {
    if (!value || !std::isfinite(value->x()) || !std::isfinite(value->y()) ||
        !std::isfinite(value->z()))
        throw std::invalid_argument("scene edit vector is invalid");
    return {static_cast<float>(value->x()), static_cast<float>(value->y()),
            static_cast<float>(value->z())};
}

core::Quat quaternion(const fb::EditQuat *value) {
    if (!value || !std::isfinite(value->w()) || !std::isfinite(value->x()) ||
        !std::isfinite(value->y()) || !std::isfinite(value->z()))
        throw std::invalid_argument("scene edit quaternion is invalid");
    const double norm = value->w() * value->w() + value->x() * value->x() +
                        value->y() * value->y() + value->z() * value->z();
    if (std::abs(norm - 1.0) > 1.0e-6)
        throw std::invalid_argument("scene edit quaternion is not normalized");
    return {static_cast<float>(value->w()), static_cast<float>(value->x()),
            static_cast<float>(value->y()), static_cast<float>(value->z())};
}

native_scene::NativeSceneArchive clone_archive(
    const native_scene::NativeSceneArchive &source) {
    auto output = native_scene::make_native_scene_archive(source.document,
                                                           source.scene);
    output.source_ids = source.source_ids;
    output.object_uuids = source.object_uuids;
    output.canonical_camera = source.canonical_camera;
    output.procedural_graph = source.procedural_graph;
    output.resource_catalog = source.resource_catalog;
    output.solver_contract = source.solver_contract;
    output.simulation_contract = source.simulation_contract;
    output.preserved_optional_chunks = source.preserved_optional_chunks;
    return output;
}

template <class T>
std::size_t index_of(const std::vector<Uuid> &identities,
                     const std::vector<T> &values, const Uuid &identity) {
    if (identities.size() != values.size())
        throw std::invalid_argument("scene UUID registry is inconsistent");
    const auto found = std::ranges::find(identities, identity);
    if (found == identities.end())
        throw std::invalid_argument("scene edit target UUID does not exist");
    return static_cast<std::size_t>(found - identities.begin());
}

template <class T>
std::shared_ptr<T> referenced(
    const std::vector<Uuid> &identities,
    const std::vector<std::shared_ptr<T>> &values, const Uuid &identity) {
    return values[index_of(identities, values, identity)];
}

bool contains_uuid(const native_scene::NativeSceneArchive &archive,
                   const Uuid &identity) {
    const auto contains = [&](const std::vector<Uuid> &values) {
        return std::ranges::find(values, identity) != values.end();
    };
    return contains(archive.object_uuids.materials) ||
           contains(archive.object_uuids.meshes) ||
           contains(archive.object_uuids.images) ||
           contains(archive.object_uuids.textures) ||
           contains(archive.object_uuids.instances) ||
           contains(archive.object_uuids.spheres) ||
           contains(archive.object_uuids.quad_lights) ||
           archive.object_uuids.camera == identity ||
           archive.object_uuids.environment == identity;
}

void apply_transform(native_scene::NativeSceneArchive &archive,
                     const Uuid &target, const fb::TransformEditT *edit) {
    if (!edit || !edit->position || !edit->scale || !edit->rotation)
        throw std::invalid_argument("transform edit is incomplete");
    const std::size_t index = index_of(archive.object_uuids.instances,
                                       archive.scene.instances, target);
    archive.scene.instances[index].position = vector(edit->position.get());
    archive.scene.instances[index].scale = vector(edit->scale.get());
    archive.scene.instances[index].rotation = quaternion(edit->rotation.get());
}

void apply_camera(native_scene::NativeSceneArchive &archive, const Uuid &target,
                  const fb::CameraEditT *edit) {
    if (target != archive.object_uuids.camera || !edit ||
        edit->world_from_camera.size() != 16)
        throw std::invalid_argument("camera edit target or transform is invalid");
    native_scene::CanonicalCamera camera;
    std::ranges::copy(edit->world_from_camera,
                      camera.world_from_camera.begin());
    camera.sensor_width_m = edit->sensor_width_m;
    camera.sensor_height_m = edit->sensor_height_m;
    camera.focal_length_m = edit->focal_length_m;
    camera.aperture_diameter_m = edit->aperture_diameter_m;
    camera.focus_distance_m = edit->focus_distance_m;
    camera.lens_shift_x_m = edit->lens_shift_x_m;
    camera.lens_shift_y_m = edit->lens_shift_y_m;
    camera.shutter_open_s = edit->shutter_open_s;
    camera.shutter_close_s = edit->shutter_close_s;
    camera.exposure_scale = edit->exposure_scale;
    native_scene::apply_canonical_camera(camera, archive.scene.camera);
    archive.canonical_camera = camera;
}

void apply_reference(native_scene::NativeSceneArchive &archive,
                     const Uuid &target, const fb::ReferenceEditT *edit,
                     bool mesh) {
    if (!edit)
        throw std::invalid_argument("reference edit is incomplete");
    const Uuid reference = uuid(edit->referenced_uuid.get());
    if (mesh) {
        const std::size_t index = index_of(archive.object_uuids.instances,
                                           archive.scene.instances, target);
        archive.scene.instances[index].mesh = referenced(
            archive.object_uuids.meshes, archive.scene.meshes, reference);
        return;
    }
    const auto material = referenced(archive.object_uuids.materials,
                                     archive.scene.materials, reference);
    const auto instance = std::ranges::find(archive.object_uuids.instances, target);
    if (instance != archive.object_uuids.instances.end()) {
        archive.scene.instances[instance - archive.object_uuids.instances.begin()]
            .material = material;
        return;
    }
    const auto sphere = std::ranges::find(archive.object_uuids.spheres, target);
    if (sphere != archive.object_uuids.spheres.end()) {
        archive.scene.spheres[sphere - archive.object_uuids.spheres.begin()]
            .material = material;
        return;
    }
    const auto light = std::ranges::find(archive.object_uuids.quad_lights, target);
    if (light == archive.object_uuids.quad_lights.end())
        throw std::invalid_argument("material reference target is invalid");
    archive.scene.quad_lights[light - archive.object_uuids.quad_lights.begin()]
        .material = material;
}

void apply_payload(native_scene::NativeSceneArchive &archive,
                   const Uuid &target, const fb::PayloadReplaceEditT *edit) {
    if (!edit || !edit->payload.empty() || edit->uri_utf8.empty())
        throw std::invalid_argument("payload replacement requires full reload");
    const std::size_t index = index_of(archive.object_uuids.images,
                                       archive.scene.images, target);
    archive.scene.images[index]->uri = edit->uri_utf8;
}

void apply_light(native_scene::NativeSceneArchive &archive, const Uuid &target,
                 const fb::LightEditT *edit) {
    if (!edit || !edit->corner || !edit->edge_u || !edit->edge_v)
        throw std::invalid_argument("light edit is incomplete");
    const std::size_t index = index_of(archive.object_uuids.quad_lights,
                                       archive.scene.quad_lights, target);
    auto &light = archive.scene.quad_lights[index];
    light.corner = vector(edit->corner.get());
    light.edge_u = vector(edit->edge_u.get());
    light.edge_v = vector(edit->edge_v.get());
    light.material = referenced(archive.object_uuids.materials,
                                archive.scene.materials,
                                uuid(edit->material_uuid.get()));
}

void apply_environment(native_scene::NativeSceneArchive &archive,
                       const Uuid &target, const fb::EnvironmentEditT *edit) {
    if (target != archive.object_uuids.environment || !edit ||
        !edit->background || !edit->medium_scattering ||
        !edit->medium_absorption)
        throw std::invalid_argument("environment edit is incomplete");
    archive.scene.background_color = vector(edit->background.get());
    archive.scene.medium_density = static_cast<float>(edit->medium_density);
    archive.scene.medium_anisotropy =
        static_cast<float>(edit->medium_anisotropy);
    archive.scene.medium_scattering = vector(edit->medium_scattering.get());
    archive.scene.medium_absorption = vector(edit->medium_absorption.get());
    archive.scene.medium_max_distance =
        static_cast<float>(edit->medium_max_distance);
}

template <class T>
void erase_at(std::vector<T> &values, std::size_t index) {
    values.erase(values.begin() + static_cast<std::ptrdiff_t>(index));
}

void apply_remove(native_scene::NativeSceneArchive &archive,
                  const Uuid &target) {
    auto remove = [&](std::vector<Uuid> &identities, auto &values,
                      std::vector<std::string> &aliases) {
        const auto found = std::ranges::find(identities, target);
        if (found == identities.end())
            return false;
        const std::size_t index = found - identities.begin();
        erase_at(identities, index);
        erase_at(values, index);
        erase_at(aliases, index);
        return true;
    };
    if (remove(archive.object_uuids.instances, archive.scene.instances,
               archive.source_ids.instances) ||
        remove(archive.object_uuids.spheres, archive.scene.spheres,
               archive.source_ids.spheres) ||
        remove(archive.object_uuids.quad_lights, archive.scene.quad_lights,
               archive.source_ids.quad_lights))
        return;
    throw std::invalid_argument("object removal requires full reload");
}

void apply_add(native_scene::NativeSceneArchive &archive, const Uuid &target,
               const fb::ObjectEditT *edit) {
    if (!edit || edit->alias.empty() || contains_uuid(archive, target))
        throw std::invalid_argument("object insertion identity is invalid");
    if (edit->object_kind == fb::SceneObjectKind::Sphere) {
        if (!edit->geometry_a || !std::isfinite(edit->scalar_a) ||
            edit->scalar_a <= 0.0)
            throw std::invalid_argument("sphere insertion is incomplete");
        archive.scene.spheres.push_back({
            edit->name, vector(edit->geometry_a.get()),
            static_cast<float>(edit->scalar_a),
            referenced(archive.object_uuids.materials, archive.scene.materials,
                       uuid(edit->material_uuid.get()))});
        archive.source_ids.spheres.push_back(edit->alias);
        archive.object_uuids.spheres.push_back(target);
        return;
    }
    if (edit->object_kind == fb::SceneObjectKind::Instance) {
        if (!edit->transform || !edit->transform->position ||
            !edit->transform->scale || !edit->transform->rotation)
            throw std::invalid_argument("instance insertion is incomplete");
        scene_ir::InstanceNode instance;
        instance.name = edit->name;
        instance.position = vector(edit->transform->position.get());
        instance.scale = vector(edit->transform->scale.get());
        instance.rotation = quaternion(edit->transform->rotation.get());
        instance.mesh = referenced(archive.object_uuids.meshes,
                                   archive.scene.meshes,
                                   uuid(edit->mesh_uuid.get()));
        instance.material = referenced(archive.object_uuids.materials,
                                       archive.scene.materials,
                                       uuid(edit->material_uuid.get()));
        archive.scene.instances.push_back(std::move(instance));
        archive.source_ids.instances.push_back(edit->alias);
        archive.object_uuids.instances.push_back(target);
        return;
    }
    if (edit->object_kind == fb::SceneObjectKind::QuadLight) {
        if (!edit->geometry_a || !edit->geometry_b || !edit->geometry_c)
            throw std::invalid_argument("quad-light insertion is incomplete");
        archive.scene.quad_lights.push_back({
            edit->name, vector(edit->geometry_a.get()),
            vector(edit->geometry_b.get()), vector(edit->geometry_c.get()),
            referenced(archive.object_uuids.materials, archive.scene.materials,
                       uuid(edit->material_uuid.get()))});
        archive.source_ids.quad_lights.push_back(edit->alias);
        archive.object_uuids.quad_lights.push_back(target);
        return;
    }
    throw std::invalid_argument("object insertion requires full reload");
}

std::uint32_t apply_operations(
    native_scene::NativeSceneArchive &archive,
    const fb::SceneTransactionRequestT &request,
    std::vector<Uuid> &rebuilt_objects,
    std::vector<std::string> &rebuilt_resources) {
    std::uint32_t strategy = URE_SCENE_UPDATE_HOT_UPDATE;
    for (const auto &operation : request.operations) {
        if (!operation)
            throw std::invalid_argument("scene transaction contains a null edit");
        const Uuid target = uuid(operation->target_uuid.get());
        bool rebuilt = false;
        switch (operation->kind) {
            case fb::SceneEditKind::Transform:
                apply_transform(archive, target, operation->transform.get());
                break;
            case fb::SceneEditKind::Camera:
                apply_camera(archive, target, operation->camera.get());
                break;
            case fb::SceneEditKind::MaterialReference:
                apply_reference(archive, target, operation->reference.get(), false);
                strategy = URE_SCENE_UPDATE_PARTIAL_REBUILD;
                rebuilt = true;
                break;
            case fb::SceneEditKind::MeshReference:
                apply_reference(archive, target, operation->reference.get(), true);
                strategy = URE_SCENE_UPDATE_PARTIAL_REBUILD;
                rebuilt = true;
                break;
            case fb::SceneEditKind::PayloadReplace:
                apply_payload(archive, target, operation->payload_replace.get());
                strategy = URE_SCENE_UPDATE_PARTIAL_REBUILD;
                rebuilt_resources.push_back(native_scene::format_uuid(target));
                rebuilt = true;
                break;
            case fb::SceneEditKind::AddObject:
                apply_add(archive, target, operation->object.get());
                strategy = URE_SCENE_UPDATE_PARTIAL_REBUILD;
                rebuilt = true;
                break;
            case fb::SceneEditKind::RemoveObject:
                apply_remove(archive, target);
                strategy = URE_SCENE_UPDATE_PARTIAL_REBUILD;
                rebuilt = true;
                break;
            case fb::SceneEditKind::Light:
                apply_light(archive, target, operation->light.get());
                strategy = URE_SCENE_UPDATE_PARTIAL_REBUILD;
                rebuilt = true;
                break;
            case fb::SceneEditKind::Environment:
                apply_environment(archive, target, operation->environment.get());
                strategy = URE_SCENE_UPDATE_PARTIAL_REBUILD;
                rebuilt = true;
                break;
            case fb::SceneEditKind::Visibility:
                throw std::invalid_argument("visibility edit requires full reload");
            case fb::SceneEditKind::MeshReplace:
                throw std::invalid_argument("mesh payload replacement requires full reload");
            default:
                throw std::invalid_argument("scene edit kind is unsupported");
        }
        if (rebuilt)
            rebuilt_objects.push_back(target);
    }
    return strategy;
}

ure_scene_budget_t fallback_budget(const fb::SceneBudgetT &source) {
    ure_scene_budget_t output{};
    output.header = {URE_STRUCTURE_SCENE_BUDGET, sizeof(output), nullptr};
    output.max_content_bytes = source.max_content_bytes;
    output.max_uncompressed_bytes = source.max_uncompressed_bytes;
    output.max_resident_bytes = source.max_resident_bytes;
    output.max_resource_count = source.max_resource_count;
    output.max_object_count = source.max_object_count;
    output.max_nesting_depth = source.max_nesting_depth;
    output.max_decompression_ratio = source.max_decompression_ratio;
    return output;
}

LoadedSceneData load_fallback(const fb::NativeSceneRequestT &source) {
    if (!source.budget || source.source_kind != fb::SceneSourceKind::Memory)
        throw std::invalid_argument("full-reload fallback must be in memory");
    ure_native_scene_blob_t blob{};
    blob.header = {URE_STRUCTURE_NATIVE_SCENE_BLOB, sizeof(blob), nullptr};
    blob.source_kind = static_cast<std::uint32_t>(source.source_kind);
    blob.format = static_cast<std::uint32_t>(source.format);
    blob.bytes = {source.content.data(), source.content.size()};
    blob.package_scene_id = {source.package_scene_id.data(),
                             source.package_scene_id.size()};
    blob.schema_min_major = source.schema_min_major;
    blob.schema_min_minor = source.schema_min_minor;
    blob.schema_max_major = source.schema_max_major;
    blob.schema_max_minor = source.schema_max_minor;
    blob.budget = fallback_budget(*source.budget);
    return load_scene_blob(blob);
}

std::vector<std::uint8_t> result_payload(
    const Uuid &transaction_id, std::uint64_t scene_id,
    std::uint64_t base_revision,
    const SceneRevisionData &revision, std::uint32_t strategy,
    std::uint32_t applied, const std::vector<Uuid> &rebuilt_objects,
    const std::vector<std::string> &rebuilt_resources,
    const std::vector<std::string> &warnings) {
    fb::PublicScenePayloadT payload;
    payload.kind = fb::ScenePayloadKind::SceneTransactionResult;
    payload.transaction_result = std::make_unique<fb::SceneTransactionResultT>();
    auto &result = *payload.transaction_result;
    result.transaction_uuid = fb_uuid(transaction_id);
    result.scene_id = scene_id;
    result.base_revision = base_revision;
    result.resulting_revision = revision.revision;
    result.strategy = strategy;
    result.reset_reason = strategy == URE_SCENE_UPDATE_REJECTED
                              ? 0
                              : revision.reset_reason;
    for (const Uuid &identity : rebuilt_objects)
        result.rebuilt_object_uuids.push_back(fb_uuid(identity));
    result.rebuilt_resource_ids = rebuilt_resources;
    result.warnings = warnings;
    result.revision_identity.assign(revision.revision_identity.begin(),
                                    revision.revision_identity.end());
    result.semantic_digest.assign(revision.semantic_digest.begin(),
                                  revision.semantic_digest.end());
    result.applied_operation_count = applied;
    result.retry_base_revision = revision.revision;
    result.accumulation_reset = strategy != URE_SCENE_UPDATE_REJECTED;
    result.renderer_rebuild = strategy == URE_SCENE_UPDATE_PARTIAL_REBUILD ||
                              strategy == URE_SCENE_UPDATE_FULL_RELOAD;
    flatbuffers::FlatBufferBuilder builder;
    fb::FinishPublicScenePayloadBuffer(
        builder, fb::CreatePublicScenePayload(builder, &payload));
    return {builder.GetBufferPointer(),
            builder.GetBufferPointer() + builder.GetSize()};
}

void write_result(ure_scene_transaction_result_t &output,
                  const Uuid &transaction_id, const SceneRevisionData &revision,
                  std::uint64_t base_revision, std::uint32_t strategy,
                  std::uint32_t applied, std::uint32_t warning_count,
                  std::span<const std::uint8_t> payload) {
    std::ranges::copy(transaction_id.bytes, output.transaction_id.bytes);
    output.strategy = strategy;
    output.reset_reason = strategy == URE_SCENE_UPDATE_REJECTED
                              ? 0
                              : revision.reset_reason;
    output.base_revision = base_revision;
    output.resulting_revision = revision.revision;
    store(output.revision_identity, revision.revision_identity);
    store(output.semantic_digest, revision.semantic_digest);
    output.applied_operation_count = applied;
    output.warning_count = warning_count;
    output.result_required = payload.size();
    output.result_written = std::min(output.result_payload.size,
                                     static_cast<std::uint64_t>(payload.size()));
    if (output.result_written != 0)
        std::memcpy(output.result_payload.data, payload.data(),
                    static_cast<std::size_t>(output.result_written));
}

bool valid_result(ure_scene_transaction_result_t *result) noexcept {
    return valid_output(result, URE_STRUCTURE_SCENE_TRANSACTION_RESULT) &&
           result->reserved[0] == 0 && result->reserved[1] == 0 &&
           (result->result_payload.size == 0 || result->result_payload.data);
}

ure_result_t apply_transaction_impl(
    ure_handle_t scene_handle, const ure_scene_transaction_t *transaction,
    ure_scene_transaction_result_t *result, ure_handle_t *error) {
    clear_error(error);
    const auto scene = handles().get<SceneObject>(scene_handle, ObjectType::Scene);
    if (!scene)
        return make_error(URE_RESULT_INVALID_HANDLE, 420,
                          "invalid scene handle", error);
    if (!valid_input(transaction, URE_STRUCTURE_SCENE_TRANSACTION) ||
        !valid_result(result) || transaction->header.next ||
        transaction->reserved[0] != 0 || transaction->reserved[1] != 0 ||
        transaction->payload_schema != URE_PAYLOAD_SCENE_TRANSACTION ||
        transaction->payload_version_major != 1 ||
        transaction->payload_version_minor != 0 ||
        transaction->max_operation_count == 0 ||
        transaction->max_operation_count > 100000 ||
        transaction->max_payload_bytes == 0 ||
        transaction->payload.size > transaction->max_payload_bytes ||
        transaction->payload.size > std::numeric_limits<std::size_t>::max() ||
        !transaction->payload.data)
        return make_error(URE_RESULT_INVALID_ARGUMENT, 421,
                          "invalid scene transaction envelope", error);
    const std::span<const std::uint8_t> bytes(
        transaction->payload.data,
        static_cast<std::size_t>(transaction->payload.size));
    if (!equal(transaction->payload_digest, digest(bytes)))
        return make_error(URE_RESULT_MALFORMED_DATA, 422,
                          "scene transaction digest differs", error);
    flatbuffers::Verifier verifier(bytes.data(), bytes.size(), 64,
                                   transaction->max_operation_count * 32U);
    if (!fb::VerifyPublicScenePayloadBuffer(verifier))
        return make_error(URE_RESULT_MALFORMED_DATA, 423,
                          "scene transaction payload is malformed", error);
    std::unique_ptr<fb::PublicScenePayloadT> payload(
        fb::GetPublicScenePayload(bytes.data())->UnPack());
    if (!payload || payload->kind != fb::ScenePayloadKind::SceneTransactionRequest ||
        !payload->transaction_request)
        return make_error(URE_RESULT_MALFORMED_DATA, 424,
                          "scene transaction payload kind is invalid", error);
    const auto &request = *payload->transaction_request;
    Uuid transaction_id;
    try {
        transaction_id = uuid(request.transaction_uuid.get());
        if (transaction_id != uuid(transaction->transaction_id) ||
            request.base_revision != transaction->base_revision ||
            request.operations.empty() ||
            request.operations.size() > transaction->max_operation_count ||
            request.max_operation_count != transaction->max_operation_count ||
            request.max_payload_bytes != transaction->max_payload_bytes ||
            request.required_capabilities.empty() ||
            !std::ranges::all_of(request.required_capabilities,
                                [](std::uint32_t capability) {
                                    return capability >= URE_CAPABILITY_BOOTSTRAP &&
                                           capability <=
                                               URE_CAPABILITY_RENDER_SESSION;
                                }) ||
            request.client_id.empty() || request.client_id.size() > 255 ||
            request.client_metadata.size() > 65536)
            throw std::invalid_argument("scene transaction metadata is inconsistent");
    } catch (const std::invalid_argument &exception) {
        return make_error(URE_RESULT_INVALID_ARGUMENT, 425, exception.what(), error);
    }

    std::scoped_lock lock(scene->mutex);
    if (scene->current->revision != request.base_revision) {
        const std::vector<std::string> warnings{
            "scene base revision is stale; retry from current revision"};
        const auto encoded = result_payload(
            transaction_id, request.scene_id, request.base_revision,
            *scene->current,
            URE_SCENE_UPDATE_REJECTED, 0, {}, {}, warnings);
        write_result(*result, transaction_id, *scene->current,
                     request.base_revision, URE_SCENE_UPDATE_REJECTED, 0,
                     static_cast<std::uint32_t>(warnings.size()), encoded);
        return make_error(URE_RESULT_REVISION_CONFLICT, 426,
                          "scene base revision is stale; retry from current revision",
                          error);
    }
    std::vector<Uuid> rebuilt_objects;
    std::vector<std::string> rebuilt_resources;
    std::vector<std::string> warnings;
    std::shared_ptr<SceneRevisionData> revision;
    std::uint32_t strategy = URE_SCENE_UPDATE_HOT_UPDATE;
    try {
        if (scene->current->archive.document.schema_version.major < 2)
            throw std::invalid_argument(
                "UUID scene transactions require SceneIR schema 2");
        auto candidate = clone_archive(scene->current->archive);
        try {
            strategy = apply_operations(candidate, request, rebuilt_objects,
                                        rebuilt_resources);
        } catch (const std::invalid_argument &) {
            if (!request.fallback_scene)
                throw;
            auto fallback = load_fallback(*request.fallback_scene);
            if (!fallback.revision)
                throw std::invalid_argument("full-reload fallback is invalid");
            candidate = std::move(fallback.revision->archive);
            warnings.push_back("transaction used explicit full-reload fallback");
            strategy = URE_SCENE_UPDATE_FULL_RELOAD;
            rebuilt_objects.clear();
            rebuilt_resources.clear();
            const auto append_identities = [&](const std::vector<Uuid> &values) {
                rebuilt_objects.insert(rebuilt_objects.end(), values.begin(),
                                       values.end());
            };
            append_identities(candidate.object_uuids.materials);
            append_identities(candidate.object_uuids.meshes);
            append_identities(candidate.object_uuids.images);
            append_identities(candidate.object_uuids.textures);
            append_identities(candidate.object_uuids.instances);
            append_identities(candidate.object_uuids.spheres);
            append_identities(candidate.object_uuids.quad_lights);
            rebuilt_objects.push_back(candidate.object_uuids.camera);
            rebuilt_objects.push_back(candidate.object_uuids.environment);
            std::ranges::transform(
                candidate.document.resources,
                std::back_inserter(rebuilt_resources),
                [](const native_scene::ResourceDescriptor &resource) {
                    return resource.id;
                });
        }
        const auto validation = native_scene::validate_scene_ir_archive(candidate);
        if (candidate.document.id != scene->current->archive.document.id ||
            candidate.document.schema_version.major < 2)
            throw std::invalid_argument(
                "scene transaction cannot change scene identity or UUID schema");
        if (!validation.ok())
            throw std::invalid_argument(validation.diagnostics.front().message);
        const auto binary = native_scene::write_scene_ir_binary(candidate);
        revision = finalize_scene_revision(
            std::move(candidate), digest(binary), scene->current->revision + 1,
            URE_SCENE_RESET_EXPLICIT);
    } catch (const std::invalid_argument &exception) {
        result->strategy = URE_SCENE_UPDATE_REJECTED;
        result->base_revision = request.base_revision;
        result->resulting_revision = scene->current->revision;
        return make_error(URE_RESULT_CAPABILITY_UNAVAILABLE, 427,
                          exception.what(), error);
    } catch (const std::exception &exception) {
        return make_error(URE_RESULT_MALFORMED_DATA, 428, exception.what(), error);
    }
    const auto encoded = result_payload(
        transaction_id, request.scene_id, request.base_revision, *revision,
        strategy,
        static_cast<std::uint32_t>(request.operations.size()), rebuilt_objects,
        rebuilt_resources, warnings);
    write_result(*result, transaction_id, *revision, request.base_revision,
                 strategy, static_cast<std::uint32_t>(request.operations.size()),
                 static_cast<std::uint32_t>(warnings.size()), encoded);
    if (result->result_written != result->result_required)
        return make_error(URE_RESULT_BUFFER_TOO_SMALL, 429,
                          "scene transaction result buffer is too small", error);
    scene->current = revision;
    emit_event(scene->instance, URE_EVENT_SCENE_TRANSACTION_COMMITTED, nullptr);
    return URE_RESULT_SUCCESS;
}

}

ure_result_t URE_CALL apply_scene_transaction(
    ure_handle_t scene, const ure_scene_transaction_t *transaction,
    ure_scene_transaction_result_t *result, ure_handle_t *error) noexcept {
    return guard_entry(error, [&] {
        return apply_transaction_impl(scene, transaction, result, error);
    });
}

}
