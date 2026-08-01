#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <locale>
#include <map>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#endif

#include <ure/native_adapter.hpp>
#include <ure/native_scene_hash.hpp>

namespace ure::native_scene {
namespace {

std::string number(double value) {
    if (!std::isfinite(value)) {
        throw std::runtime_error(
            "USDA export encountered a non-finite number");
    }
    std::array<char, 64> buffer{};
    const auto result = std::to_chars(
        buffer.data(),
        buffer.data() + buffer.size(),
        value,
        std::chars_format::general,
        std::numeric_limits<double>::max_digits10);
    if (result.ec != std::errc{}) {
        throw std::runtime_error(
            "USDA numeric formatting failed");
    }
    return std::string(buffer.data(), result.ptr);
}

std::string number(float value) {
    if (!std::isfinite(value)) {
        throw std::runtime_error(
            "USDA export encountered a non-finite number");
    }
    std::array<char, 48> buffer{};
    const auto result = std::to_chars(
        buffer.data(),
        buffer.data() + buffer.size(),
        value,
        std::chars_format::general,
        std::numeric_limits<float>::max_digits10);
    if (result.ec != std::errc{}) {
        throw std::runtime_error(
            "USDA numeric formatting failed");
    }
    return std::string(buffer.data(), result.ptr);
}

std::string quoted(std::string_view value) {
    std::string result = "\"";
    for (const unsigned char character : value) {
        switch (character) {
        case '\\': result += "\\\\"; break;
        case '"': result += "\\\""; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (character < 0x20) {
                throw std::runtime_error(
                    "USDA string contains an unsupported control character");
            }
            result += static_cast<char>(character);
            break;
        }
    }
    result += '"';
    return result;
}

std::string index_name(
    char prefix,
    std::size_t index) {
    std::string digits = std::to_string(index);
    return std::string(1, prefix) + "_" +
        std::string(
            digits.size() < 6
                ? 6 - digits.size()
                : 0,
            '0') +
        digits;
}

std::string vec2(const core::Vec2f& value) {
    return "(" + number(value.x) + ", " +
        number(value.y) + ")";
}

std::string vec3(const core::Vec3f& value) {
    return "(" + number(value.x) + ", " +
        number(value.y) + ", " +
        number(value.z) + ")";
}

std::string quat(const core::Quat& value) {
    return "(" + number(value.w) + ", " +
        number(value.x) + ", " +
        number(value.y) + ", " +
        number(value.z) + ")";
}

std::string material_model(
    scene_ir::MaterialModel model) {
    switch (model) {
    case scene_ir::MaterialModel::Lambertian:
        return "lambertian";
    case scene_ir::MaterialModel::Metal:
        return "metal";
    case scene_ir::MaterialModel::Dielectric:
        return "dielectric";
    case scene_ir::MaterialModel::Light:
        return "light";
    case scene_ir::MaterialModel::Cloth:
        return "cloth";
    }
    throw std::runtime_error(
        "USDA export encountered an unknown material model");
}

void write_rigid_body(
    std::ostringstream& stream,
    const RigidBodyConfig& rigid_body,
    std::string_view indent) {
    if (!rigid_body.enabled) {
        return;
    }
    stream << indent
           << "custom bool ure:physics:enabled = true\n"
           << indent
           << "custom float ure:physics:mass = "
           << number(rigid_body.mass) << "\n"
           << indent
           << "custom float ure:physics:friction = "
           << number(rigid_body.friction) << "\n"
           << indent
           << "custom float ure:physics:restitution = "
           << number(rigid_body.restitution) << "\n"
           << indent
           << "custom float ure:physics:linearDamping = "
           << number(rigid_body.linear_damping) << "\n"
           << indent
           << "custom float ure:physics:angularDamping = "
           << number(rigid_body.angular_damping) << "\n"
           << indent
           << "custom float3 ure:physics:velocity = "
           << vec3(rigid_body.velocity) << "\n"
           << indent
           << "custom token ure:physics:colliderType = "
           << quoted(rigid_body.collider_type) << "\n"
           << indent
           << "custom float3 ure:physics:colliderSize = "
           << vec3(rigid_body.collider_size) << "\n"
           << indent
           << "custom float ure:physics:colliderRadius = "
           << number(rigid_body.collider_radius) << "\n"
           << indent
           << "custom int ure:physics:materialId = "
           << rigid_body.material_id << "\n";
}

void write_mesh_geometry(
    std::ostringstream& stream,
    const Mesh& mesh,
    std::string_view indent) {
    stream << indent << "point3f[] points = [";
    for (std::size_t index = 0;
         index < mesh.vertices.size();
         ++index) {
        if (index != 0) {
            stream << ", ";
        }
        stream << vec3(mesh.vertices[index].position);
    }
    stream << "]\n" << indent
           << "int[] faceVertexCounts = [";
    for (std::size_t index = 0;
         index < mesh.indices.size() / 3;
         ++index) {
        if (index != 0) {
            stream << ", ";
        }
        stream << '3';
    }
    stream << "]\n" << indent
           << "int[] faceVertexIndices = [";
    for (std::size_t index = 0;
         index < mesh.indices.size();
         ++index) {
        if (index != 0) {
            stream << ", ";
        }
        stream << mesh.indices[index];
    }
    stream << "]\n" << indent
           << "normal3f[] normals = [";
    for (std::size_t index = 0;
         index < mesh.vertices.size();
         ++index) {
        if (index != 0) {
            stream << ", ";
        }
        stream << vec3(mesh.vertices[index].normal);
    }
    stream << "] (\n" << indent
           << "    interpolation = \"vertex\"\n"
           << indent << ")\n" << indent
           << "vector3f[] primvars:tangents = [";
    for (std::size_t index = 0;
         index < mesh.vertices.size();
         ++index) {
        if (index != 0) {
            stream << ", ";
        }
        stream << vec3(mesh.vertices[index].tangent);
    }
    stream << "] (\n" << indent
           << "    interpolation = \"vertex\"\n"
           << indent << ")\n" << indent
           << "texCoord2f[] primvars:st = [";
    for (std::size_t index = 0;
         index < mesh.vertices.size();
         ++index) {
        if (index != 0) {
            stream << ", ";
        }
        stream << vec2(mesh.vertices[index].uv);
    }
    stream << "] (\n" << indent
           << "    interpolation = \"vertex\"\n"
           << indent << ")\n" << indent
           << "uniform token subdivisionScheme = \"none\"\n";
}

std::string material_path(
    const std::map<const scene_ir::MaterialNode*,
                   std::string>& materials,
    const std::shared_ptr<scene_ir::MaterialNode>& material) {
    if (!material) {
        return {};
    }
    const auto found = materials.find(material.get());
    if (found == materials.end()) {
        throw std::runtime_error(
            "Visible native primitive references a material outside the scene material table");
    }
    return found->second;
}

void write_material_binding(
    std::ostringstream& stream,
    const std::string& path,
    std::string_view indent) {
    if (!path.empty()) {
        stream << indent
               << "rel material:binding = <"
               << path << ">\n";
    }
}

void write_instance(
    std::ostringstream& stream,
    const scene_ir::InstanceNode& instance,
    std::size_t index,
    const std::map<const scene_ir::MeshResource*,
                   std::string>& meshes,
    const std::map<const scene_ir::MaterialNode*,
                   std::string>& materials,
    std::vector<UsdExportMapping>& mappings) {
    if (!instance.mesh || !instance.mesh->mesh) {
        throw std::runtime_error(
            "Native instance has no mesh resource");
    }
    const auto mesh_path = meshes.find(
        instance.mesh.get());
    if (mesh_path == meshes.end()) {
        throw std::runtime_error(
            "Native instance references a mesh outside the scene mesh table");
    }
    const std::string name = index_name('i', index);
    const std::string path = "/URE/World/" + name;
    mappings.push_back({
        "/scene/instances/" + std::to_string(index),
        path});
    stream << "        def Xform " << quoted(name)
           << " (\n"
           << "            prepend references = <"
           << mesh_path->second << ">\n"
           << "            instanceable = true\n"
           << "        )\n        {\n"
           << "            custom string ure:nativeName = "
           << quoted(instance.name) << "\n"
           << "            float3 xformOp:translate = "
           << vec3(instance.position) << "\n"
           << "            quatf xformOp:orient = "
           << quat(instance.rotation) << "\n"
           << "            float3 xformOp:scale = "
           << vec3(instance.scale) << "\n"
           << "            uniform token[] xformOpOrder = [\"xformOp:translate\", \"xformOp:orient\", \"xformOp:scale\"]\n"
           << "            token visibility = \"inherited\"\n";
    write_material_binding(
        stream,
        material_path(materials, instance.material),
        "            ");
    write_rigid_body(
        stream,
        instance.rigid_body,
        "            ");
    stream << "        }\n";
}

void write_sphere(
    std::ostringstream& stream,
    const scene_ir::SphereNode& sphere,
    std::size_t index,
    const std::map<const scene_ir::MaterialNode*,
                   std::string>& materials,
    std::vector<UsdExportMapping>& mappings) {
    const std::string name = index_name('s', index);
    const std::string path = "/URE/World/" + name;
    mappings.push_back({
        "/scene/spheres/" + std::to_string(index),
        path});
    stream << "        def Sphere " << quoted(name)
           << "\n        {\n"
           << "            custom string ure:nativeName = "
           << quoted(sphere.name) << "\n"
           << "            double radius = "
           << number(sphere.radius) << "\n"
           << "            double3 xformOp:translate = "
           << vec3(sphere.center) << "\n"
           << "            uniform token[] xformOpOrder = [\"xformOp:translate\"]\n";
    write_material_binding(
        stream,
        material_path(materials, sphere.material),
        "            ");
    stream << "        }\n";
}

void write_quad_light(
    std::ostringstream& stream,
    const scene_ir::QuadLightNode& light,
    std::size_t index,
    const std::map<const scene_ir::MaterialNode*,
                   std::string>& materials,
    std::vector<UsdExportMapping>& mappings) {
    const std::string name = index_name('l', index);
    const std::string path = "/URE/World/" + name;
    mappings.push_back({
        "/scene/quad_lights/" +
            std::to_string(index),
        path});
    const core::Vec3f p0 = light.corner;
    const core::Vec3f p1 = light.corner + light.edge_u;
    const core::Vec3f p2 = p1 + light.edge_v;
    const core::Vec3f p3 = light.corner + light.edge_v;
    stream << "        def Mesh " << quoted(name)
           << "\n        {\n"
           << "            custom string ure:nativeName = "
           << quoted(light.name) << "\n"
           << "            custom bool ure:light:quad = true\n"
           << "            point3f[] points = ["
           << vec3(p0) << ", " << vec3(p1) << ", "
           << vec3(p2) << ", " << vec3(p3) << "]\n"
           << "            int[] faceVertexCounts = [3, 3]\n"
           << "            int[] faceVertexIndices = [0, 1, 2, 0, 2, 3]\n"
           << "            uniform token subdivisionScheme = \"none\"\n";
    write_material_binding(
        stream,
        material_path(materials, light.material),
        "            ");
    stream << "        }\n";
}

std::array<std::array<double, 4>, 4>
camera_transform(const Camera& camera) {
    const core::Vec3f forward =
        (camera.look_at - camera.position).normalize();
    const core::Vec3f right =
        forward.cross(camera.up).normalize();
    const core::Vec3f up =
        right.cross(forward).normalize();
    return {{
        {right.x, right.y, right.z, 0.0},
        {up.x, up.y, up.z, 0.0},
        {-forward.x, -forward.y, -forward.z, 0.0},
        {camera.position.x,
         camera.position.y,
         camera.position.z,
         1.0}}};
}

void write_camera(
    std::ostringstream& stream,
    const Camera& camera,
    std::vector<UsdExportMapping>& mappings) {
    const double vertical_aperture = 20.955;
    const double focal_length =
        vertical_aperture /
        (2.0 * std::tan(
            static_cast<double>(camera.fov) *
            0.0087266462599716478846));
    const double focal_world = focal_length / 10.0;
    const double f_stop = camera.aperture > 0.0f
        ? focal_world /
              (2.0 * static_cast<double>(camera.aperture))
        : 0.0;
    const auto transform = camera_transform(camera);
    mappings.push_back({
        "/scene/camera",
        "/URE/Camera"});
    stream << "    def Camera \"Camera\"\n"
           << "    {\n"
           << "        token projection = \"perspective\"\n"
           << "        float verticalAperture = "
           << number(vertical_aperture) << "\n"
           << "        float horizontalAperture = "
           << number(vertical_aperture * camera.aspect_ratio)
           << "\n"
           << "        float focalLength = "
           << number(focal_length) << "\n"
           << "        float focusDistance = "
           << number(camera.focus_dist) << "\n"
           << "        float fStop = "
           << number(f_stop) << "\n"
           << "        matrix4d xformOp:transform = (";
    for (std::size_t row = 0; row < 4; ++row) {
        if (row != 0) {
            stream << ", ";
        }
        stream << '(';
        for (std::size_t column = 0;
             column < 4;
             ++column) {
            if (column != 0) {
                stream << ", ";
            }
            stream << number(transform[row][column]);
        }
        stream << ')';
    }
    stream << ")\n"
           << "        uniform token[] xformOpOrder = [\"xformOp:transform\"]\n"
           << "    }\n";
}

std::string build_usda(
    const NativeSceneArchive& archive,
    std::vector<UsdExportMapping>& mappings) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << "#usda 1.0\n(\n"
           << "    defaultPrim = \"URE\"\n"
           << "    metersPerUnit = 1\n"
           << "    upAxis = \"Y\"\n"
           << "    customLayerData = {\n"
           << "        string ureAdapterIdentity = \"ure.adapter.usda-export/1.0\"\n"
           << "        string ureNativeDocumentId = "
           << quoted(archive.document.id) << "\n"
           << "        string ureNativeSemanticHash = "
           << quoted(scene_ir_semantic_hash(archive))
           << "\n    }\n)\n\n"
           << "def Xform \"URE\"\n{\n"
           << "    custom color3f ure:backgroundColor = "
           << vec3(archive.scene.background_color)
           << "\n    custom int ure:render:width = "
           << archive.scene.width
           << "\n    custom int ure:render:height = "
           << archive.scene.height
           << "\n    custom int ure:render:spp = "
           << archive.scene.spp
           << "\n    def Scope \"Materials\"\n    {\n";

    std::map<const scene_ir::MaterialNode*,
             std::string> materials;
    for (std::size_t index = 0;
         index < archive.scene.materials.size();
         ++index) {
        const auto& material =
            archive.scene.materials[index];
        if (!material) {
            throw std::runtime_error(
                "Native scene material table contains a null entry");
        }
        const std::string name = index_name('m', index);
        const std::string path =
            "/URE/Materials/" + name;
        if (!materials.emplace(
                 material.get(),
                 path).second) {
            throw std::runtime_error(
                "Native material table contains duplicate resource identity");
        }
        mappings.push_back({
            "/scene/materials/" +
                std::to_string(index),
            path});
        const float metallic =
            material->model ==
                scene_ir::MaterialModel::Metal
            ? 1.0f
            : 0.0f;
        stream << "        def Material "
               << quoted(name) << "\n        {\n"
               << "            custom string ure:nativeName = "
               << quoted(material->name) << "\n"
               << "            custom token ure:nativeModel = "
               << quoted(material_model(material->model))
               << "\n"
               << "            token outputs:surface.connect = <"
               << path
               << "/PreviewSurface.outputs:surface>\n"
               << "            def Shader \"PreviewSurface\"\n"
               << "            {\n"
               << "                uniform token info:id = \"UsdPreviewSurface\"\n"
               << "                color3f inputs:diffuseColor = "
               << vec3(material->base_color) << "\n"
               << "                float inputs:roughness = "
               << number(material->roughness) << "\n"
               << "                float inputs:metallic = "
               << number(metallic) << "\n"
               << "                float inputs:ior = "
               << number(material->ior) << "\n"
               << "                color3f inputs:emissiveColor = "
               << vec3(material->emission) << "\n"
               << "                token outputs:surface\n"
               << "            }\n        }\n";
    }
    stream << "    }\n"
           << "    def Scope \"Prototypes\"\n"
           << "    {\n"
           << "        token visibility = \"invisible\"\n";
    std::map<const scene_ir::MeshResource*,
             std::string> meshes;
    for (std::size_t index = 0;
         index < archive.scene.meshes.size();
         ++index) {
        const auto& mesh = archive.scene.meshes[index];
        if (!mesh || !mesh->mesh) {
            throw std::runtime_error(
                "Native scene mesh table contains a null entry");
        }
        const std::string name = index_name('p', index);
        const std::string path =
            "/URE/Prototypes/" + name;
        if (!meshes.emplace(
                 mesh.get(),
                 path).second) {
            throw std::runtime_error(
                "Native mesh table contains duplicate resource identity");
        }
        mappings.push_back({
            "/scene/meshes/" + std::to_string(index),
            path + "/Geometry"});
        stream << "        def Xform " << quoted(name)
               << "\n        {\n"
               << "            custom string ure:nativeName = "
               << quoted(mesh->name) << "\n"
               << "            def Mesh \"Geometry\"\n"
               << "            {\n";
        write_mesh_geometry(
            stream,
            *mesh->mesh,
            "                ");
        stream << "            }\n        }\n";
    }
    stream << "    }\n"
           << "    def Scope \"World\"\n    {\n";
    for (std::size_t index = 0;
         index < archive.scene.instances.size();
         ++index) {
        write_instance(
            stream,
            archive.scene.instances[index],
            index,
            meshes,
            materials,
            mappings);
    }
    for (std::size_t index = 0;
         index < archive.scene.spheres.size();
         ++index) {
        write_sphere(
            stream,
            archive.scene.spheres[index],
            index,
            materials,
            mappings);
    }
    for (std::size_t index = 0;
         index < archive.scene.quad_lights.size();
         ++index) {
        write_quad_light(
            stream,
            archive.scene.quad_lights[index],
            index,
            materials,
            mappings);
    }
    stream << "    }\n";
    write_camera(stream, archive.scene.camera, mappings);
    stream << "}\n";
    return stream.str();
}

void append_validation(
    std::vector<ValidationDiagnostic>& target,
    const ValidationReport& source) {
    target.insert(
        target.end(),
        source.diagnostics.begin(),
        source.diagnostics.end());
}

bool has_errors(
    const std::vector<ValidationDiagnostic>& diagnostics) {
    return std::ranges::any_of(
        diagnostics,
        [](const auto& diagnostic) {
            return diagnostic.severity ==
                DiagnosticSeverity::Error;
        });
}

void atomic_write(
    const std::filesystem::path& path,
    std::string_view content) {
    if (path.empty()) {
        throw std::invalid_argument(
            "USDA output path is empty");
    }
    std::error_code error;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(
            path.parent_path(),
            error);
        if (error) {
            throw std::runtime_error(
                "Cannot create USDA output directory: " +
                error.message());
        }
    }
    static std::atomic<std::uint64_t> sequence = 0;
    const std::filesystem::path temporary =
        path.string() + ".tmp." +
        std::to_string(
            std::chrono::steady_clock::now()
                .time_since_epoch().count()) +
        "." + std::to_string(
            sequence.fetch_add(
                1,
                std::memory_order_relaxed));
    {
        std::ofstream stream(
            temporary,
            std::ios::binary | std::ios::trunc);
        if (!stream) {
            throw std::runtime_error(
                "Cannot open temporary USDA output");
        }
        stream.write(
            content.data(),
            static_cast<std::streamsize>(
                content.size()));
        stream.flush();
        if (!stream) {
            throw std::runtime_error(
                "Cannot write temporary USDA output");
        }
    }
#if defined(_WIN32)
    if (!MoveFileExW(
            temporary.c_str(),
            path.c_str(),
            MOVEFILE_REPLACE_EXISTING |
                MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(temporary);
        throw std::runtime_error(
            "Cannot atomically publish USDA output");
    }
#else
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(temporary);
        throw std::runtime_error(
            "Cannot atomically publish USDA output: " +
            error.message());
    }
#endif
}

}

UsdExportResult export_usda_native(
    const NativeSceneArchive& archive,
    UsdExportPolicy policy,
    const ValidationLimits& limits) {
    UsdExportResult result;
    result.policy = policy;
    result.loss_report = assess_native_export(
        archive,
        AdapterFormat::Usd);
    append_validation(
        result.diagnostics,
        validate_scene_ir_archive(archive, limits));
    if (!result.loss_report.exportable()) {
        result.diagnostics.push_back({
            "URE-U6-EXPORT-001",
            DiagnosticSeverity::Error,
            "/",
            "Native scene contains semantics that cannot be represented by the USDA adapter",
            "Retain the native source or remove the unsupported semantics"});
    } else if (!result.loss_report.lossless() &&
               policy == UsdExportPolicy::Strict) {
        result.diagnostics.push_back({
            "URE-U6-EXPORT-002",
            DiagnosticSeverity::Error,
            "/",
            "USDA export would be lossy under the strict policy",
            "Use AllowDocumentedLoss and retain the structured loss report"});
    }
    if (has_errors(result.diagnostics)) {
        return result;
    }
    try {
        result.usda = build_usda(
            archive,
            result.mappings);
    } catch (const std::exception& error) {
        result.usda.clear();
        result.mappings.clear();
        result.diagnostics.push_back({
            "URE-U6-EXPORT-003",
            DiagnosticSeverity::Error,
            "/",
            error.what(),
            "Repair the native scene before exporting"});
    }
    return result;
}

void save_usda_native(
    const std::filesystem::path& path,
    const NativeSceneArchive& archive,
    UsdExportPolicy policy,
    const std::filesystem::path& loss_report_path,
    const ValidationLimits& limits) {
    if (path.extension() != ".usda") {
        throw std::invalid_argument(
            "USD adapter output must use the .usda extension");
    }
    const UsdExportResult result =
        export_usda_native(
            archive,
            policy,
            limits);
    if (!result.ok()) {
        const std::string message =
            result.diagnostics.empty()
            ? "USDA export failed"
            : result.diagnostics.front().message;
        throw std::runtime_error(message);
    }
    if (!result.loss_report.lossless() &&
        loss_report_path.empty()) {
        throw std::invalid_argument(
            "Lossy USDA export requires an explicit loss-report path");
    }
    if (!loss_report_path.empty()) {
        atomic_write(
            loss_report_path,
            write_adapter_loss_report(
                result.loss_report));
    }
    atomic_write(path, result.usda);
}

}
