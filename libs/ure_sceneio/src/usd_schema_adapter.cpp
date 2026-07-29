#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <map>
#include <memory>
#include <numbers>
#include <ranges>
#include <set>
#include <span>
#include <string_view>
#include <utility>

#include <ure/native_scene_hash.hpp>
#include <ure/native_scene_validation.hpp>
#include <ure/usd_schema_adapter.hpp>

namespace ure::usd {
namespace {

using native_scene::AdapterLossSeverity;
using native_scene::DiagnosticSeverity;

void add_diagnostic(
    UsdSchemaAdapterResult& result,
    std::string code,
    std::string path,
    std::string message,
    std::string guidance = {}) {
    result.native.diagnostics.push_back({
        std::move(code),
        DiagnosticSeverity::Error,
        std::move(path),
        std::move(message),
        std::move(guidance)});
}

void add_loss(
    UsdSchemaAdapterResult& result,
    std::string code,
    AdapterLossSeverity severity,
    std::string path,
    std::string feature,
    std::string message,
    std::string remediation) {
    result.native.loss_report.losses.push_back({
        std::move(code),
        severity,
        std::move(path),
        std::move(feature),
        std::move(message),
        std::move(remediation)});
}

bool finite(core::Vec2f value) {
    return std::isfinite(value.x) &&
           std::isfinite(value.y);
}

bool finite(core::Vec3f value) {
    return std::isfinite(value.x) &&
           std::isfinite(value.y) &&
           std::isfinite(value.z);
}

bool finite(core::Quat value) {
    return std::isfinite(value.w) &&
           std::isfinite(value.x) &&
           std::isfinite(value.y) &&
           std::isfinite(value.z);
}

std::string digest(std::string_view value) {
    return native_scene::sha256_hex(
        std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(
                value.data()),
            value.size()));
}

std::string native_id(
    std::string_view kind,
    std::string_view path) {
    return std::format(
        "usd/{}/{}",
        kind,
        digest(path).substr(0, 24));
}

std::string display_name(
    std::string_view path,
    std::string_view authored) {
    if (!authored.empty()) {
        return std::string(authored);
    }
    const std::size_t slash = path.find_last_of('/');
    return std::string(
        slash == std::string_view::npos
            ? path
            : path.substr(slash + 1));
}

core::Quat basis_rotation(UsdUpAxis axis) {
    if (axis == UsdUpAxis::Y) {
        return {};
    }
    return core::Quat::from_axis_angle(
        {1.0f, 0.0f, 0.0f},
        -0.5f * std::numbers::pi_v<float>);
}

core::Vec3f convert_direction(
    core::Vec3f value,
    UsdUpAxis axis) {
    return basis_rotation(axis).rotate(value);
}

core::Vec3f convert_position(
    core::Vec3f value,
    UsdUpAxis axis,
    float metres_per_unit) {
    return convert_direction(value, axis) *
           metres_per_unit;
}

core::Vec3f convert_scale(
    core::Vec3f value,
    UsdUpAxis axis) {
    return axis == UsdUpAxis::Y
        ? value
        : core::Vec3f(value.x, value.z, value.y);
}

core::Quat convert_rotation(
    core::Quat value,
    UsdUpAxis axis) {
    const core::Quat basis = basis_rotation(axis);
    return (basis * value.normalized() *
            basis.inverse())
        .normalized();
}

bool valid_prim_path(std::string_view path) {
    if (path.size() < 2 || path.front() != '/' ||
        path.back() == '/') {
        return false;
    }
    bool segment = false;
    for (const char character : path.substr(1)) {
        if (character == '/') {
            if (!segment) {
                return false;
            }
            segment = false;
        } else if (
            (character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') ||
            character == '_' || character == '-') {
            segment = true;
        } else {
            return false;
        }
    }
    return segment;
}

bool supported_schema(std::string_view schema) {
    return schema == kUsdSchemaAdapterIdentity ||
           schema == kUsdPhysicsApiSchema ||
           schema == kUsdSpectralMaterialApiSchema;
}

template <typename T>
std::vector<const T*> sorted_by_path(
    const std::vector<T>& values) {
    std::vector<const T*> result;
    result.reserve(values.size());
    for (const auto& value : values) {
        result.push_back(&value);
    }
    std::ranges::sort(
        result,
        {},
        [](const T* value) {
            return value->path;
        });
    return result;
}

std::shared_ptr<scene_ir::MaterialGraph>
make_preview_graph(
    const UsdMaterialPrim& source) {
    auto graph =
        std::make_shared<scene_ir::MaterialGraph>();
    const auto add = [&](scene_ir::MaterialGraphNode node) {
        return graph->add_node(std::move(node));
    };
    scene_ir::MaterialGraphNode color;
    color.kind =
        scene_ir::MaterialGraphNodeKind::ConstantColor;
    color.color = source.model ==
            scene_ir::MaterialModel::Light
        ? source.emission
        : source.base_color;
    const auto color_id = add(std::move(color));

    scene_ir::MaterialGraphNode roughness;
    roughness.kind =
        scene_ir::MaterialGraphNodeKind::ConstantFloat;
    roughness.value = source.roughness;
    const auto roughness_id = add(
        std::move(roughness));

    scene_ir::MaterialGraphNode ior;
    ior.kind =
        scene_ir::MaterialGraphNodeKind::ConstantFloat;
    ior.value = source.ior;
    const auto ior_id = add(std::move(ior));

    scene_ir::MaterialGraphNode bsdf;
    switch (source.model) {
    case scene_ir::MaterialModel::Lambertian:
    case scene_ir::MaterialModel::Cloth:
        bsdf.kind =
            scene_ir::MaterialGraphNodeKind::BsdfLambert;
        bsdf.inputs.push_back(
            scene_ir::material_graph_input(
                "base_color",
                color_id));
        bsdf.inputs.push_back(
            scene_ir::material_graph_input(
                "roughness",
                roughness_id));
        break;
    case scene_ir::MaterialModel::Metal:
        bsdf.kind =
            scene_ir::MaterialGraphNodeKind::BsdfMetal;
        bsdf.inputs.push_back(
            scene_ir::material_graph_input(
                "base_color",
                color_id));
        bsdf.inputs.push_back(
            scene_ir::material_graph_input(
                "roughness",
                roughness_id));
        break;
    case scene_ir::MaterialModel::Dielectric:
        bsdf.kind =
            scene_ir::MaterialGraphNodeKind::BsdfDielectric;
        bsdf.inputs.push_back(
            scene_ir::material_graph_input(
                "ior",
                ior_id));
        bsdf.inputs.push_back(
            scene_ir::material_graph_input(
                "roughness",
                roughness_id));
        break;
    case scene_ir::MaterialModel::Light:
        bsdf.kind =
            scene_ir::MaterialGraphNodeKind::BsdfLight;
        bsdf.inputs.push_back(
            scene_ir::material_graph_input(
                "emission",
                color_id));
        break;
    }
    const auto bsdf_id = add(std::move(bsdf));
    scene_ir::MaterialGraphNode output;
    output.kind =
        scene_ir::MaterialGraphNodeKind::OutputSurface;
    output.inputs.push_back(
        scene_ir::material_graph_input(
            "surface",
            bsdf_id));
    graph->output_node_id = add(std::move(output));
    graph->validate();
    return graph;
}

std::shared_ptr<Mesh> make_mesh(
    const UsdMeshPrim& source,
    UsdUpAxis axis,
    float metres_per_unit) {
    auto mesh = std::make_shared<Mesh>();
    mesh->vertices.resize(source.points.size());
    for (std::size_t index = 0;
         index < source.points.size();
         ++index) {
        mesh->vertices[index].position =
            convert_position(
                source.points[index],
                axis,
                metres_per_unit);
        if (!source.texcoords.empty()) {
            mesh->vertices[index].uv =
                source.texcoords[index];
        }
    }
    mesh->indices.reserve(
        source.face_vertex_indices.size());
    for (const std::uint32_t index :
         source.face_vertex_indices) {
        mesh->indices.push_back(
            static_cast<int>(index));
    }
    if (!source.normals.empty()) {
        for (std::size_t index = 0;
             index < source.normals.size();
             ++index) {
            mesh->vertices[index].normal =
                convert_direction(
                    source.normals[index],
                    axis)
                    .normalize();
        }
        return mesh;
    }
    for (std::size_t index = 0;
         index < mesh->indices.size();
         index += 3) {
        const int i0 = mesh->indices[index];
        const int i1 = mesh->indices[index + 1];
        const int i2 = mesh->indices[index + 2];
        const core::Vec3f normal =
            (mesh->vertices[i1].position -
             mesh->vertices[i0].position)
                .cross(
                    mesh->vertices[i2].position -
                    mesh->vertices[i0].position);
        mesh->vertices[i0].normal += normal;
        mesh->vertices[i1].normal += normal;
        mesh->vertices[i2].normal += normal;
    }
    for (auto& vertex : mesh->vertices) {
        vertex.normal = vertex.normal.normalize();
    }
    return mesh;
}

bool has_errors(
    const UsdSchemaAdapterResult& result) {
    return std::ranges::any_of(
        result.native.diagnostics,
        [](const auto& diagnostic) {
            return diagnostic.severity ==
                DiagnosticSeverity::Error;
        });
}

}

bool UsdSchemaAdapterResult::ok() const {
    return native.ok() &&
           native.loss_report.exportable();
}

UsdSchemaAdapterResult import_usd_schema_stage(
    const UsdStageSnapshot& stage,
    const UsdSchemaAdapterLimits& limits) {
    UsdSchemaAdapterResult result;
    result.native.loss_report.format =
        native_scene::AdapterFormat::Usd;
    if (stage.source_identifier.empty()) {
        add_diagnostic(
            result,
            "URE-U1-STAGE-001",
            "/stage/source_identifier",
            "USD source identity is empty");
    }
    if (!std::isfinite(stage.metres_per_unit) ||
        stage.metres_per_unit <= 0.0 ||
        stage.metres_per_unit > 1.0e6) {
        add_diagnostic(
            result,
            "URE-U1-STAGE-002",
            "/stage/metres_per_unit",
            "USD stage unit scale is invalid");
    }
    if (stage.authored_time_sample_count > 1) {
        add_diagnostic(
            result,
            "URE-U1-TIME-001",
            "/stage/time_samples",
            "Animated USD samples require the U.5 session adapter",
            "Bake one time sample or use the future Hydra session path");
    }
    for (const auto& schema :
         stage.required_schemas) {
        if (!supported_schema(schema)) {
            add_diagnostic(
                result,
                "URE-U1-SCHEMA-001",
                "/stage/required_schemas",
                "Unsupported required USD schema: " +
                    schema);
        }
    }
    for (const auto& schema :
         stage.optional_schemas) {
        if (!supported_schema(schema)) {
            add_loss(
                result,
                "URE-U1-SCHEMA-002",
                AdapterLossSeverity::Warning,
                "/stage/optional_schemas",
                schema,
                "Optional USD schema was retained only as adapter provenance",
                "Author required semantics with the URE USD schema API");
        }
    }

    std::size_t prim_count = 0;
    bool prim_count_overflow = false;
    for (const std::size_t count : {
             stage.materials.size(),
             stage.meshes.size(),
             stage.spheres.size(),
             stage.cameras.size()}) {
        if (count >
            std::numeric_limits<std::size_t>::max() -
                prim_count) {
            prim_count_overflow = true;
        } else {
            prim_count += count;
        }
    }
    if (prim_count_overflow ||
        prim_count > limits.max_prims) {
        add_diagnostic(
            result,
            "URE-U1-BUDGET-001",
            "/stage/prims",
            "USD prim count exceeds the adapter budget");
    }
    std::size_t vertex_count = 0;
    std::size_t index_count = 0;
    bool geometry_count_overflow = false;
    std::set<std::string> paths;
    const auto register_path =
        [&](std::string_view path,
            std::string_view kind) {
            if (!valid_prim_path(path)) {
                add_diagnostic(
                    result,
                    "URE-U1-PATH-001",
                    std::string(path),
                    "Invalid USD prim path");
            } else if (!paths.insert(
                           std::string(path))
                           .second) {
                add_diagnostic(
                    result,
                    "URE-U1-PATH-002",
                    std::string(path),
                    "Duplicate USD prim path");
            }
            if (kind == "mesh") {
                result.mappings.push_back({
                    std::string(path),
                    native_id("instance", path)});
            } else {
                result.mappings.push_back({
                    std::string(path),
                    native_id(kind, path)});
            }
        };

    for (const auto& material : stage.materials) {
        register_path(material.path, "material");
        if (material.model ==
                scene_ir::MaterialModel::Cloth ||
            !finite(material.base_color) ||
            !finite(material.emission) ||
            !std::isfinite(material.roughness) ||
            material.roughness < 0.0f ||
            material.roughness > 1.0f ||
            !std::isfinite(material.ior) ||
            material.ior <= 0.0f) {
            add_diagnostic(
                result,
                "URE-U1-MATERIAL-001",
                material.path,
                "USD material parameters or model are unsupported");
        }
    }
    for (const auto& mesh : stage.meshes) {
        register_path(mesh.path, "mesh");
        if (mesh.points.size() >
            std::numeric_limits<std::size_t>::max() -
                vertex_count) {
            geometry_count_overflow = true;
        } else {
            vertex_count += mesh.points.size();
        }
        if (mesh.face_vertex_indices.size() >
            std::numeric_limits<std::size_t>::max() -
                index_count) {
            geometry_count_overflow = true;
        } else {
            index_count +=
                mesh.face_vertex_indices.size();
        }
        if (mesh.points.empty() ||
            !std::ranges::all_of(
                mesh.points,
                [](auto value) {
                    return finite(value);
                }) ||
            (!mesh.normals.empty() &&
             (mesh.normals.size() !=
                  mesh.points.size() ||
              !std::ranges::all_of(
                  mesh.normals,
                  [](auto value) {
                      return finite(value) &&
                             value.length_sq() >
                                 0.0f;
                  }))) ||
            (!mesh.texcoords.empty() &&
             (mesh.texcoords.size() !=
                  mesh.points.size() ||
              !std::ranges::all_of(
                  mesh.texcoords,
                  [](auto value) {
                      return finite(value);
                  }))) ||
            mesh.face_vertex_counts.empty() ||
            !std::ranges::all_of(
                mesh.face_vertex_counts,
                [](std::uint32_t count) {
                    return count == 3;
                }) ||
            mesh.face_vertex_counts.size() >
                std::numeric_limits<std::size_t>::
                    max() / 3 ||
            mesh.face_vertex_indices.size() !=
                mesh.face_vertex_counts.size() * 3 ||
            !std::ranges::all_of(
                mesh.face_vertex_indices,
                [&](std::uint32_t index) {
                    return index <
                        mesh.points.size();
                }) ||
            !finite(mesh.transform.translation) ||
            !finite(mesh.transform.scale) ||
            !finite(mesh.transform.rotation) ||
            !mesh.transform.affine_trs_compatible ||
            mesh.transform.scale.x == 0.0f ||
            mesh.transform.scale.y == 0.0f ||
            mesh.transform.scale.z == 0.0f ||
            mesh.transform.rotation.w *
                    mesh.transform.rotation.w +
                    mesh.transform.rotation.x *
                        mesh.transform.rotation.x +
                    mesh.transform.rotation.y *
                        mesh.transform.rotation.y +
                    mesh.transform.rotation.z *
                        mesh.transform.rotation.z <
                1.0e-8f) {
            add_diagnostic(
                result,
                "URE-U1-MESH-001",
                mesh.path,
                "USD mesh must be finite, indexed, per-point attributed and triangulated");
        } else {
            for (std::size_t index = 0;
                 index <
                 mesh.face_vertex_indices.size();
                 index += 3) {
                const auto& first =
                    mesh.points[
                        mesh.face_vertex_indices[index]];
                const auto& second =
                    mesh.points[
                        mesh.face_vertex_indices[
                            index + 1]];
                const auto& third =
                    mesh.points[
                        mesh.face_vertex_indices[
                            index + 2]];
                if ((second - first)
                        .cross(third - first)
                        .length_sq() <= 1.0e-20f) {
                    add_diagnostic(
                        result,
                        "URE-U1-MESH-002",
                        mesh.path,
                        "USD mesh contains a degenerate triangle");
                    break;
                }
            }
        }
    }
    for (const auto& sphere : stage.spheres) {
        register_path(sphere.path, "sphere");
        if (!finite(sphere.center) ||
            !std::isfinite(sphere.radius) ||
            sphere.radius <= 0.0f ||
            sphere.rigid_body.enabled) {
            add_diagnostic(
                result,
                "URE-U1-SPHERE-001",
                sphere.path,
                "USD sphere parameters or physics binding are unsupported");
        }
    }
    for (const auto& camera : stage.cameras) {
        register_path(camera.path, "camera");
        if (!finite(camera.position) ||
            !finite(camera.look_at) ||
            !finite(camera.up) ||
            camera.up.length_sq() == 0.0f ||
            !std::isfinite(
                camera.vertical_fov_degrees) ||
            camera.vertical_fov_degrees <= 0.0f ||
            camera.vertical_fov_degrees >= 180.0f ||
            !std::isfinite(camera.aspect_ratio) ||
            camera.aspect_ratio <= 0.0f ||
            !std::isfinite(camera.aperture) ||
            camera.aperture < 0.0f ||
            !std::isfinite(camera.focus_distance) ||
            camera.focus_distance <= 0.0f) {
            add_diagnostic(
                result,
                "URE-U1-CAMERA-001",
                camera.path,
                "USD camera parameters are invalid");
        }
    }
    if (geometry_count_overflow ||
        vertex_count > limits.max_vertices ||
        index_count > limits.max_indices) {
        add_diagnostic(
            result,
            "URE-U1-BUDGET-002",
            "/stage/geometry",
            "USD geometry exceeds the adapter budget");
    }

    if (has_errors(result)) {
        return result;
    }

    scene_ir::SceneIR scene;
    std::map<std::string,
             std::shared_ptr<scene_ir::MaterialNode>>
        materials;
    native_scene::NativeResourceCatalog catalog;
    catalog.id = native_id(
        "resource-catalog",
        stage.source_identifier);
    catalog.schema_version = {1, 0};

    for (const auto* source :
         sorted_by_path(stage.materials)) {
        auto material =
            std::make_shared<scene_ir::MaterialNode>();
        material->name = display_name(
            source->path,
            source->display_name);
        material->model = source->model;
        material->base_color = source->base_color;
        material->roughness = source->roughness;
        material->ior = source->ior;
        material->emission = source->emission;
        material->graph = make_preview_graph(*source);
        scene.add_material(material);
        materials.emplace(source->path, material);

        std::vector<const UsdSpectralResourceBinding*>
            spectral_resources;
        spectral_resources.reserve(
            source->spectral_resources.size());
        for (const auto& binding :
             source->spectral_resources) {
            spectral_resources.push_back(&binding);
        }
        std::ranges::sort(
            spectral_resources,
            {},
            [](const auto* binding) {
                return binding->id;
            });
        std::vector<std::string> dependencies;
        for (const auto* binding :
             spectral_resources) {
            native_scene::NativeResourceEntry entry;
            entry.id = binding->id;
            entry.kind =
                native_scene::ResourceKind::SpectralTable;
            entry.schema_version = {1, 0};
            entry.schema_identity =
                "ure.spectral-resource/1.0";
            entry.content_hash =
                binding->content_hash;
            entry.payload_uri = binding->uri;
            entry.payload_bytes =
                binding->payload_bytes;
            entry.resident_bytes =
                binding->resident_bytes;
            entry.residency =
                binding->representation ==
                    native_scene::
                        SpectralRepresentation::Tiled
                ? native_scene::
                      ResourceResidency::Tiled
                : native_scene::
                      ResourceResidency::Resident;
            native_scene::SpectralResourceContract
                spectral;
            spectral.semantic = binding->semantic;
            spectral.representation =
                binding->representation;
            spectral.domain = {
                binding->wavelength_min_nm,
                binding->wavelength_max_nm,
                binding->domain_bins,
                binding->packet_lanes};
            spectral.sample_count =
                binding->sample_count;
            spectral.basis_count =
                binding->basis_count;
            spectral.tile_bins =
                binding->tile_bins;
            spectral.value_min =
                binding->value_min;
            spectral.value_max =
                binding->value_max;
            entry.spectral = spectral;
            catalog.resources.push_back(
                std::move(entry));
            dependencies.push_back(binding->id);
        }
        if (!dependencies.empty()) {
            native_scene::NativeResourceEntry graph;
            graph.id = native_id(
                "material-graph",
                source->path);
            graph.kind =
                native_scene::ResourceKind::MaterialGraph;
            graph.schema_version = {1, 0};
            graph.schema_identity =
                "ure.material-graph/1.0";
            std::string graph_seed = std::format(
                "{}|{}|{:.9g}|{:.9g}|{:.9g}|{:.9g}|{:.9g}|{:.9g}|{:.9g}|{:.9g}",
                source->path,
                static_cast<unsigned>(source->model),
                source->base_color.x,
                source->base_color.y,
                source->base_color.z,
                source->roughness,
                source->ior,
                source->emission.x,
                source->emission.y,
                source->emission.z);
            for (const auto& dependency :
                 dependencies) {
                graph_seed += std::format(
                    "|{}:{}",
                    dependency.size(),
                    dependency);
            }
            graph.content_hash = digest(graph_seed);
            graph.payload_uri =
                "content://sha256/" +
                graph.content_hash;
            graph.dependencies = dependencies;
            graph.material =
                native_scene::MaterialResourceContract{
                    native_id(
                        "material",
                        source->path),
                    dependencies};
            catalog.resources.push_back(
                std::move(graph));
        }
    }

    const float unit =
        static_cast<float>(stage.metres_per_unit);
    for (const auto* source :
         sorted_by_path(stage.meshes)) {
        const auto material =
            materials.find(source->material_path);
        if (material == materials.end()) {
            add_diagnostic(
                result,
                "URE-U1-BINDING-001",
                source->path,
                "USD mesh material binding is missing");
            continue;
        }
        auto mesh = scene.register_mesh(
            display_name(
                source->path,
                source->display_name),
            make_mesh(
                *source,
                stage.up_axis,
                unit));
        scene_ir::InstanceNode instance;
        instance.name = mesh->name;
        instance.mesh = mesh;
        instance.material = material->second;
        instance.position = convert_position(
            source->transform.translation,
            stage.up_axis,
            unit);
        instance.scale = convert_scale(
            source->transform.scale,
            stage.up_axis);
        instance.rotation = convert_rotation(
            source->transform.rotation,
            stage.up_axis);
        instance.rigid_body = source->rigid_body;
        scene.instances.push_back(
            std::move(instance));
    }
    for (const auto* source :
         sorted_by_path(stage.spheres)) {
        const auto material =
            materials.find(source->material_path);
        if (material == materials.end()) {
            add_diagnostic(
                result,
                "URE-U1-BINDING-002",
                source->path,
                "USD sphere material binding is missing");
            continue;
        }
        scene_ir::SphereNode sphere;
        sphere.name = display_name(
            source->path,
            source->display_name);
        sphere.center = convert_position(
            source->center,
            stage.up_axis,
            unit);
        sphere.radius = source->radius * unit;
        sphere.material = material->second;
        scene.spheres.push_back(std::move(sphere));
    }

    const auto cameras =
        sorted_by_path(stage.cameras);
    const UsdCameraPrim* selected = nullptr;
    if (!stage.camera_path.empty()) {
        const auto found = std::ranges::find(
            cameras,
            stage.camera_path,
            [](const UsdCameraPrim* camera) {
                return camera->path;
            });
        if (found == cameras.end()) {
            add_diagnostic(
                result,
                "URE-U1-CAMERA-002",
                "/stage/camera_path",
                "Selected USD camera does not exist");
        } else {
            selected = *found;
        }
    } else if (!cameras.empty()) {
        selected = cameras.front();
    }
    if (selected) {
        scene.camera.position = convert_position(
            selected->position,
            stage.up_axis,
            unit);
        scene.camera.look_at = convert_position(
            selected->look_at,
            stage.up_axis,
            unit);
        scene.camera.up = convert_direction(
            selected->up,
            stage.up_axis)
            .normalize();
        scene.camera.fov =
            selected->vertical_fov_degrees;
        scene.camera.aspect_ratio =
            selected->aspect_ratio;
        scene.camera.aperture =
            selected->aperture * unit;
        scene.camera.focus_dist =
            selected->focus_distance * unit;
        if (cameras.size() > 1) {
            add_loss(
                result,
                "URE-U1-CAMERA-003",
                AdapterLossSeverity::Warning,
                "/stage/cameras",
                "usd.camera.selection",
                "Only the selected USD camera enters the static SceneIR snapshot",
                "Use U.5 session camera switching for interactive stages");
        }
    }

    if (has_errors(result)) {
        return result;
    }

    native_scene::SceneDocument document;
    document.id = native_id(
        "scene",
        stage.source_identifier);
    document.schema_version =
        native_scene::kSceneSchemaVersion;
    document.features.push_back({
        std::string(kUsdSchemaAdapterIdentity),
        kUsdSchemaAdapterVersion,
        native_scene::RequirementLevel::Required,
        "ure_usd",
        {},
        "{}"});
    if (!catalog.resources.empty()) {
        document.features.push_back({
            std::string(
                native_scene::kResourceCatalogFeature),
            {1, 0},
            native_scene::RequirementLevel::Required,
            "ure",
            {},
            "{}"});
    }
    result.native.archive =
        native_scene::make_native_scene_archive(
            std::move(document),
            scene);
    result.native.archive.source_ids.materials.clear();
    for (const auto* source :
         sorted_by_path(stage.materials)) {
        result.native.archive.source_ids.materials.
            push_back(
                native_id("material", source->path));
    }
    result.native.archive.source_ids.meshes.clear();
    result.native.archive.source_ids.instances.clear();
    for (const auto* source :
         sorted_by_path(stage.meshes)) {
        result.native.archive.source_ids.meshes.
            push_back(native_id("mesh", source->path));
        result.native.archive.source_ids.instances.
            push_back(
                native_id("instance", source->path));
    }
    result.native.archive.source_ids.spheres.clear();
    for (const auto* source :
         sorted_by_path(stage.spheres)) {
        result.native.archive.source_ids.spheres.
            push_back(
                native_id("sphere", source->path));
    }
    if (!catalog.resources.empty()) {
        const auto resource_validation =
            native_scene::validate_resource_catalog(
                catalog,
                limits.native);
        result.native.diagnostics.insert(
            result.native.diagnostics.end(),
            resource_validation.diagnostics.begin(),
            resource_validation.diagnostics.end());
        if (resource_validation.ok()) {
            result.native.archive.resource_catalog =
                std::make_shared<
                    const native_scene::
                        NativeResourceCatalog>(
                    std::move(catalog));
        }
    }
    const auto archive_validation =
        native_scene::validate_scene_ir_archive(
            result.native.archive,
            limits.native);
    result.native.diagnostics.insert(
        result.native.diagnostics.end(),
        archive_validation.diagnostics.begin(),
        archive_validation.diagnostics.end());
    std::ranges::sort(
        result.mappings,
        {},
        &UsdPrimMapping::usd_path);
    return result;
}

}
