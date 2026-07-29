#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/vec2d.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/vt/array.h>
#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/meshUtil.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/pxOsd/tokens.h>

#include <ure/scene_ir.hpp>

#include "mesh_rprim.hpp"
#include "render_param.hpp"

PXR_NAMESPACE_OPEN_SCOPE
namespace {

constexpr HdDirtyBits kMeshDirtyBits =
    HdChangeTracker::DirtyPoints |
    HdChangeTracker::DirtyPrimvar |
    HdChangeTracker::DirtyMaterialId |
    HdChangeTracker::DirtyTopology |
    HdChangeTracker::DirtyTransform |
    HdChangeTracker::DirtyVisibility |
    HdChangeTracker::DirtyNormals |
    HdChangeTracker::DirtyDoubleSided |
    HdChangeTracker::DirtyInstancer |
    HdChangeTracker::DirtyRenderTag;

struct Primvar {
    bool present = false;
    HdInterpolation interpolation =
        HdInterpolationConstant;
    VtValue value;
    VtIntArray indices;
};

bool finite(double value) {
    return std::isfinite(value);
}

std::vector<ure::core::Vec3f>
vec3_values(const VtValue& value) {
    std::vector<ure::core::Vec3f> result;
    if (value.IsHolding<VtVec3fArray>()) {
        const auto& source =
            value.UncheckedGet<VtVec3fArray>();
        result.reserve(source.size());
        for (const auto& item : source) {
            result.push_back(
                {item[0], item[1], item[2]});
        }
    } else if (value.IsHolding<VtVec3dArray>()) {
        const auto& source =
            value.UncheckedGet<VtVec3dArray>();
        result.reserve(source.size());
        for (const auto& item : source) {
            result.push_back({
                static_cast<float>(item[0]),
                static_cast<float>(item[1]),
                static_cast<float>(item[2])});
        }
    } else {
        throw std::runtime_error(
            "Hydra vec3 primvar has an unsupported type");
    }
    if (!std::ranges::all_of(
            result,
            [](const auto& item) {
                return finite(item.x) &&
                       finite(item.y) &&
                       finite(item.z);
            })) {
        throw std::runtime_error(
            "Hydra vec3 primvar contains non-finite data");
    }
    return result;
}

std::vector<ure::core::Vec2f>
vec2_values(const VtValue& value) {
    std::vector<ure::core::Vec2f> result;
    if (value.IsHolding<VtVec2fArray>()) {
        const auto& source =
            value.UncheckedGet<VtVec2fArray>();
        result.reserve(source.size());
        for (const auto& item : source) {
            result.push_back({item[0], item[1]});
        }
    } else if (value.IsHolding<VtVec2dArray>()) {
        const auto& source =
            value.UncheckedGet<VtVec2dArray>();
        result.reserve(source.size());
        for (const auto& item : source) {
            result.push_back({
                static_cast<float>(item[0]),
                static_cast<float>(item[1])});
        }
    } else {
        throw std::runtime_error(
            "Hydra vec2 primvar has an unsupported type");
    }
    if (!std::ranges::all_of(
            result,
            [](const auto& item) {
                return finite(item.x) &&
                       finite(item.y);
            })) {
        throw std::runtime_error(
            "Hydra vec2 primvar contains non-finite data");
    }
    return result;
}

template <typename T>
std::vector<T> resolve_indices(
    const std::vector<T>& values,
    const VtIntArray& indices) {
    if (indices.empty()) {
        return values;
    }
    std::vector<T> result;
    result.reserve(indices.size());
    for (const int index : indices) {
        if (index < 0 ||
            static_cast<std::size_t>(index) >=
                values.size()) {
            throw std::runtime_error(
                "Hydra primvar index is out of range");
        }
        result.push_back(
            values[static_cast<std::size_t>(index)]);
    }
    return result;
}

Primvar read_primvar(
    HdUREMesh& mesh,
    HdSceneDelegate& delegate,
    const TfToken& name) {
    for (const HdInterpolation interpolation : {
             HdInterpolationVertex,
             HdInterpolationVarying,
             HdInterpolationFaceVarying,
             HdInterpolationUniform,
             HdInterpolationConstant}) {
        const auto descriptors =
            mesh.GetPrimvarDescriptors(
                &delegate,
                interpolation);
        if (std::ranges::none_of(
                descriptors,
                [&](const auto& descriptor) {
                    return descriptor.name == name;
                })) {
            continue;
        }
        Primvar result;
        result.present = true;
        result.interpolation = interpolation;
        result.value = mesh.GetIndexedPrimvar(
            &delegate,
            name,
            &result.indices);
        return result;
    }
    return {};
}

template <typename T>
std::vector<T> triangulate_face_varying(
    const HdMeshUtil& utility,
    const std::vector<T>& source,
    HdType type) {
    if (source.size() >
        static_cast<std::size_t>(
            std::numeric_limits<int>::max())) {
        throw std::runtime_error(
            "Hydra face-varying primvar exceeds the native index domain");
    }
    VtValue triangulated;
    if (!utility.ComputeTriangulatedFaceVaryingPrimvar(
            source.data(),
            static_cast<int>(source.size()),
            type,
            &triangulated)) {
        throw std::runtime_error(
            "Hydra face-varying primvar could not be triangulated");
    }
    std::vector<T> result;
    if constexpr (
        std::is_same_v<T, ure::core::Vec3f>) {
        result = vec3_values(triangulated);
    } else {
        result = vec2_values(triangulated);
    }
    return result;
}

template <typename T>
T sample_primvar(
    const Primvar& primvar,
    const std::vector<T>& values,
    const std::vector<T>& face_varying,
    int point_index,
    int face_index,
    std::size_t triangle_corner) {
    std::size_t index = 0;
    switch (primvar.interpolation) {
    case HdInterpolationConstant:
        index = 0;
        break;
    case HdInterpolationUniform:
        index = static_cast<std::size_t>(face_index);
        break;
    case HdInterpolationVertex:
    case HdInterpolationVarying:
        index = static_cast<std::size_t>(point_index);
        break;
    case HdInterpolationFaceVarying:
        if (triangle_corner >= face_varying.size()) {
            throw std::runtime_error(
                "Hydra triangulated primvar is incomplete");
        }
        return face_varying[triangle_corner];
    case HdInterpolationInstance:
    case HdInterpolationCount:
        throw std::runtime_error(
            "Hydra mesh primvar interpolation is unsupported");
    }
    if (index >= values.size()) {
        throw std::runtime_error(
            "Hydra primvar domain is incomplete");
    }
    return values[index];
}

std::array<double, 16> matrix_values(
    const GfMatrix4d& matrix) {
    std::array<double, 16> result;
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0;
             column < 4;
            ++column) {
            const double value =
                matrix[static_cast<int>(row)]
                      [static_cast<int>(column)];
            if (!finite(value)) {
                throw std::runtime_error(
                    "Hydra transform contains non-finite data");
            }
            result[row * 4 + column] = value;
        }
    }
    if (std::abs(result[3]) > 1.0e-12 ||
        std::abs(result[7]) > 1.0e-12 ||
        std::abs(result[11]) > 1.0e-12 ||
        std::abs(result[15] - 1.0) > 1.0e-12) {
        throw std::runtime_error(
            "Hydra projective transforms are unsupported");
    }
    return result;
}

std::shared_ptr<const ure::scene_ir::MeshResource>
build_geometry(
    HdUREMesh& source,
    HdSceneDelegate& delegate,
    HdMeshTopology topology) {
    if (topology.GetScheme() !=
        PxOsdOpenSubdivTokens->none) {
        throw std::runtime_error(
            "Hydra subdivision surfaces require an exact tessellation boundary");
    }

    const auto points = vec3_values(
        source.GetPoints(&delegate));
    if (points.empty()) {
        throw std::runtime_error(
            "Hydra mesh has no points");
    }

    HdMeshUtil utility(&topology, source.GetId());
    VtVec3iArray triangles;
    VtIntArray primitive_params;
    utility.ComputeTriangleIndices(
        &triangles,
        &primitive_params);
    if (triangles.empty() ||
        primitive_params.size() != triangles.size()) {
        throw std::runtime_error(
            "Hydra mesh triangulation is empty or incomplete");
    }
    if (triangles.size() >
        static_cast<std::size_t>(
            std::numeric_limits<int>::max()) /
            3) {
        throw std::runtime_error(
            "Hydra mesh exceeds the native index domain");
    }

    const Primvar normal_primvar =
        read_primvar(
            source,
            delegate,
            HdTokens->normals);
    const Primvar st_primvar =
        read_primvar(
            source,
            delegate,
            TfToken("st"));
    const Primvar uv_primvar =
        st_primvar.present
        ? st_primvar
        : read_primvar(
              source,
              delegate,
              TfToken("uv"));

    std::vector<ure::core::Vec3f> normals;
    std::vector<ure::core::Vec3f>
        triangle_normals;
    if (normal_primvar.present) {
        normals = resolve_indices(
            vec3_values(normal_primvar.value),
            normal_primvar.indices);
        if (normal_primvar.interpolation ==
            HdInterpolationFaceVarying) {
            triangle_normals =
                triangulate_face_varying(
                    utility,
                    normals,
                    HdTypeFloatVec3);
        }
    }

    std::vector<ure::core::Vec2f> texcoords;
    std::vector<ure::core::Vec2f>
        triangle_texcoords;
    if (uv_primvar.present) {
        texcoords = resolve_indices(
            vec2_values(uv_primvar.value),
            uv_primvar.indices);
        if (uv_primvar.interpolation ==
            HdInterpolationFaceVarying) {
            triangle_texcoords =
                triangulate_face_varying(
                    utility,
                    texcoords,
                    HdTypeFloatVec2);
        }
    }

    auto mesh = std::make_shared<ure::Mesh>();
    mesh->vertices.reserve(triangles.size() * 3);
    mesh->indices.reserve(triangles.size() * 3);
    for (std::size_t triangle_index = 0;
         triangle_index < triangles.size();
         ++triangle_index) {
        const auto& triangle =
            triangles[triangle_index];
        const int face_index =
            HdMeshUtil::
                DecodeFaceIndexFromCoarseFaceParam(
                    primitive_params[triangle_index]);
        std::array<ure::core::Vec3f, 3>
            triangle_positions;
        for (std::size_t corner = 0;
             corner < 3;
             ++corner) {
            const int point_index =
                triangle[static_cast<int>(corner)];
            if (point_index < 0 ||
                static_cast<std::size_t>(point_index) >=
                    points.size()) {
                throw std::runtime_error(
                    "Hydra triangle index is out of range");
            }
            triangle_positions[corner] =
                points[static_cast<std::size_t>(
                    point_index)];
        }
        const ure::core::Vec3f face_normal =
            (triangle_positions[1] -
             triangle_positions[0])
                .cross(
                    triangle_positions[2] -
                    triangle_positions[0]);
        if (!finite(face_normal.x) ||
            !finite(face_normal.y) ||
            !finite(face_normal.z) ||
            face_normal.length_sq() <= 1.0e-20f) {
            throw std::runtime_error(
                "Hydra mesh contains a degenerate triangle");
        }

        for (std::size_t corner = 0;
             corner < 3;
             ++corner) {
            const int point_index =
                triangle[static_cast<int>(corner)];
            const std::size_t triangle_corner =
                triangle_index * 3 + corner;
            ure::Vertex vertex;
            vertex.position =
                triangle_positions[corner];
            vertex.normal = normal_primvar.present
                ? sample_primvar(
                      normal_primvar,
                      normals,
                      triangle_normals,
                      point_index,
                      face_index,
                      triangle_corner)
                      .normalize()
                : face_normal.normalize();
            if (!finite(vertex.normal.x) ||
                !finite(vertex.normal.y) ||
                !finite(vertex.normal.z) ||
                vertex.normal.length_sq() <= 0.0f) {
                throw std::runtime_error(
                    "Hydra mesh normal is invalid");
            }
            if (uv_primvar.present) {
                vertex.uv = sample_primvar(
                    uv_primvar,
                    texcoords,
                    triangle_texcoords,
                    point_index,
                    face_index,
                    triangle_corner);
            }
            mesh->indices.push_back(
                static_cast<int>(
                    mesh->vertices.size()));
            mesh->vertices.push_back(vertex);
        }
    }

    auto geometry =
        std::make_shared<ure::scene_ir::MeshResource>();
    geometry->name =
        source.GetId().GetString();
    geometry->mesh = std::move(mesh);
    return geometry;
}

}

HdUREMesh::HdUREMesh(const SdfPath& id)
    : HdMesh(id) {
}

HdUREMesh::~HdUREMesh() = default;

HdDirtyBits
HdUREMesh::GetInitialDirtyBitsMask() const {
    return kMeshDirtyBits;
}

void HdUREMesh::Sync(
    HdSceneDelegate* delegate,
    HdRenderParam* render_param,
    HdDirtyBits* dirty_bits,
    const TfToken& repr_token) {
    static_cast<void>(repr_token);
    auto* state =
        dynamic_cast<HdURERenderParam*>(
            render_param);
    if (!delegate || !state || !dirty_bits) {
        TF_CODING_ERROR(
            "HdURE mesh sync requires delegate, render param and dirty bits");
        return;
    }
    if ((*dirty_bits & kMeshDirtyBits) == 0) {
        return;
    }
    const HdDirtyBits bits = *dirty_bits;
    _UpdateInstancer(delegate, dirty_bits);
    _UpdateVisibility(delegate, dirty_bits);
    if (bits & HdChangeTracker::DirtyMaterialId) {
        SetMaterialId(
            delegate->GetMaterialId(GetId()));
    }
    UpdateRenderTag(delegate, render_param);
    if (!GetInstancerId().IsEmpty()) {
        state->RejectMesh(
            GetId().GetString(),
            "Hydra instanced meshes require a dedicated native instance mapping");
        *dirty_bits = HdChangeTracker::Clean;
        return;
    }
    try {
        const auto previous =
            state->FindMesh(GetId().GetString());
        HdUREMeshRecord record =
            previous.value_or(HdUREMeshRecord{});
        record.path = GetId().GetString();
        bool changed = !previous.has_value();
        constexpr HdDirtyBits geometry_bits =
            HdChangeTracker::DirtyPoints |
            HdChangeTracker::DirtyPrimvar |
            HdChangeTracker::DirtyNormals |
            HdChangeTracker::DirtyTopology;
        if (!previous ||
            (bits & geometry_bits) != 0) {
            auto topology =
                GetMeshTopology(delegate);
            record.geometry = build_geometry(
                *this,
                *delegate,
                topology);
            topology_ =
                std::make_shared<HdMeshTopology>(
                    std::move(topology));
            changed = true;
        }
        if (!previous ||
            (bits &
             HdChangeTracker::DirtyTransform) != 0) {
            record.transform = matrix_values(
                delegate->GetTransform(GetId()));
            changed = true;
        }
        if (!previous ||
            (bits &
             HdChangeTracker::DirtyMaterialId) != 0) {
            record.material_path =
                GetMaterialId().GetString();
            changed = true;
        }
        if (!previous ||
            (bits &
             HdChangeTracker::DirtyVisibility) != 0) {
            record.visible = IsVisible();
            changed = true;
        }
        if (!previous ||
            (bits &
             HdChangeTracker::DirtyDoubleSided) != 0) {
            record.double_sided =
                IsDoubleSided(delegate);
            changed = true;
        }
        if (changed) {
            state->UpdateMesh(std::move(record));
        }
    } catch (const std::exception& error) {
        state->RejectMesh(
            GetId().GetString(),
            error.what());
    }
    *dirty_bits = HdChangeTracker::Clean;
}

void HdUREMesh::Finalize(
    HdRenderParam* render_param) {
    if (auto* state =
            dynamic_cast<HdURERenderParam*>(
                render_param)) {
        state->RemoveMesh(GetId().GetString());
    }
    topology_.reset();
}

HdMeshTopologySharedPtr
HdUREMesh::GetTopology() const {
    return topology_;
}

HdDirtyBits HdUREMesh::_PropagateDirtyBits(
    HdDirtyBits bits) const {
    if (bits & HdChangeTracker::DirtyTopology) {
        bits |=
            HdChangeTracker::DirtyPoints |
            HdChangeTracker::DirtyPrimvar |
            HdChangeTracker::DirtyNormals;
    }
    return bits;
}

void HdUREMesh::_InitRepr(
    const TfToken& repr_token,
    HdDirtyBits* dirty_bits) {
    static_cast<void>(repr_token);
    if (dirty_bits) {
        *dirty_bits |= kMeshDirtyBits;
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
