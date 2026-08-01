#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/vec3d.h>

#include "scene_snapshot.hpp"

PXR_NAMESPACE_OPEN_SCOPE
namespace {

bool included(
    const SdfPath& path,
    const HdRprimCollection& collection) {
    bool rooted = false;
    for (const auto& root :
         collection.GetRootPaths()) {
        if (path.HasPrefix(root)) {
            rooted = true;
            break;
        }
    }
    if (!rooted) {
        return false;
    }
    for (const auto& excluded :
         collection.GetExcludePaths()) {
        if (path.HasPrefix(excluded)) {
            return false;
        }
    }
    return true;
}

GfMatrix4d matrix_value(
    const std::array<double, 16>& source) {
    GfMatrix4d result;
    for (int row = 0; row < 4; ++row) {
        for (int column = 0;
             column < 4;
             ++column) {
            result[row][column] =
                source[static_cast<std::size_t>(
                    row * 4 + column)];
        }
    }
    return result;
}

ure::core::Vec3f vector_value(
    const GfVec3d& value,
    const char* context) {
    if (!std::isfinite(value[0]) ||
        !std::isfinite(value[1]) ||
        !std::isfinite(value[2])) {
        throw std::runtime_error(
            std::string(context) +
            " produced non-finite geometry");
    }
    return {
        static_cast<float>(value[0]),
        static_cast<float>(value[1]),
        static_cast<float>(value[2])};
}

GfVec3d normalize(
    const GfVec3d& value,
    const char* context) {
    const double length = value.GetLength();
    if (!std::isfinite(length) ||
        length <= 1.0e-12) {
        throw std::runtime_error(
            std::string(context) +
            " collapsed under the Hydra transform");
    }
    return value / length;
}

std::shared_ptr<ure::scene_ir::MeshResource>
bake_geometry(const HdUREMeshRecord& record) {
    if (!record.geometry ||
        !record.geometry->mesh) {
        throw std::runtime_error(
            "Hydra retained mesh has no native geometry");
    }
    const GfMatrix4d transform =
        matrix_value(record.transform);
    const double determinant =
        transform.GetDeterminant3();
    if (!std::isfinite(determinant) ||
        std::abs(determinant) <= 1.0e-12) {
        throw std::runtime_error(
            "Hydra affine transform is singular");
    }
    const GfMatrix4d normal_transform =
        transform.GetInverse().GetTranspose();
    auto mesh = std::make_shared<ure::Mesh>(
        *record.geometry->mesh);
    for (auto& vertex : mesh->vertices) {
        const GfVec3d position =
            transform.Transform(
                GfVec3d(
                    vertex.position.x,
                    vertex.position.y,
                    vertex.position.z));
        const GfVec3d normal = normalize(
            normal_transform.TransformDir(
                GfVec3d(
                    vertex.normal.x,
                    vertex.normal.y,
                    vertex.normal.z)),
            "Hydra normal");
        GfVec3d tangent =
            transform.TransformDir(
                GfVec3d(
                    vertex.tangent.x,
                    vertex.tangent.y,
                    vertex.tangent.z));
        tangent -= normal *
            GfDot(tangent, normal);
        tangent = normalize(
            tangent,
            "Hydra tangent");
        vertex.position = vector_value(
            position,
            "Hydra position");
        vertex.normal = vector_value(
            normal,
            "Hydra normal");
        vertex.tangent = vector_value(
            tangent,
            "Hydra tangent");
    }
    if (determinant < 0.0) {
        for (std::size_t index = 0;
             index + 2 < mesh->indices.size();
             index += 3) {
            std::swap(
                mesh->indices[index + 1],
                mesh->indices[index + 2]);
        }
    }
    auto resource =
        std::make_shared<
            ure::scene_ir::MeshResource>();
    resource->name = record.path;
    resource->mesh = std::move(mesh);
    return resource;
}

}

HdURESceneSnapshot BuildSceneSnapshot(
    const HdURERetainedScene& retained,
    const HdRprimCollection& collection,
    const ure::Camera& camera,
    int width,
    int height) {
    if (width <= 0 || height <= 0) {
        throw std::runtime_error(
            "Hydra viewport dimensions must be positive");
    }
    if (!collection.GetMaterialTag().IsEmpty()) {
        throw std::runtime_error(
            "Hydra material-tag collections require an explicit native partition");
    }
    if (retained.rejected_mesh_count != 0 ||
        retained.rejected_material_count != 0) {
        throw std::runtime_error(
            retained.last_error.empty()
                ? "Hydra retained scene contains rejected prims"
                : retained.last_error);
    }
    HdURESceneSnapshot result;
    result.revision = retained.revision;
    result.scene.camera = camera;
    result.scene.width = width;
    result.scene.height = height;
    result.scene.spp = 1;

    std::map<
        std::string,
        std::shared_ptr<ure::scene_ir::MaterialNode>>
        materials;
    for (const auto& source : retained.materials) {
        if (!source.material) {
            throw std::runtime_error(
                "Hydra retained material is empty");
        }
        auto material =
            std::make_shared<
                ure::scene_ir::MaterialNode>(
                *source.material);
        materials.emplace(source.path, material);
        result.scene.materials.push_back(
            std::move(material));
        for (const auto& loss :
             source.loss_report) {
            result.loss_report.push_back(
                source.path + ": " +
                loss.code + ": " +
                loss.message);
        }
    }
    std::shared_ptr<ure::scene_ir::MaterialNode>
        fallback_material;
    for (const auto& source : retained.meshes) {
        const SdfPath path(source.path);
        if (!path.IsAbsolutePath() ||
            !included(path, collection) ||
            !source.visible) {
            continue;
        }
        std::shared_ptr<ure::scene_ir::MaterialNode>
            material;
        if (source.material_path.empty()) {
            if (!fallback_material) {
                fallback_material =
                    std::make_shared<
                        ure::scene_ir::MaterialNode>();
                fallback_material->name =
                    "/__ureFallbackMaterial";
                result.scene.materials.push_back(
                    fallback_material);
            }
            material = fallback_material;
        } else {
            const auto found = materials.find(
                source.material_path);
            if (found == materials.end()) {
                throw std::runtime_error(
                    "Hydra mesh references an unsynchronized material: " +
                    source.material_path);
            }
            material = found->second;
        }
        auto geometry = bake_geometry(source);
        result.scene.meshes.push_back(geometry);
        ure::scene_ir::InstanceNode instance;
        instance.name = source.path;
        instance.mesh = std::move(geometry);
        instance.material = std::move(material);
        result.scene.instances.push_back(
            std::move(instance));
        if (!source.double_sided) {
            result.loss_report.push_back(
                source.path +
                ": native traversal currently evaluates both triangle sides");
        }
    }
    if (result.scene.instances.empty()) {
        throw std::runtime_error(
            "Hydra render collection contains no visible supported meshes");
    }
    return result;
}

PXR_NAMESPACE_CLOSE_SCOPE
