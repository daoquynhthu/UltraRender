#include <algorithm>
#include <cmath>
#include <format>
#include <span>
#include <string>
#include <utility>

#include <ure/native_scene_hash.hpp>

#include "native_procedural_internal.hpp"

namespace ure::native_scene::detail {
namespace {

template <typename T>
LoadResult<T> failure(std::string path, std::string message) {
    LoadResult<T> result;
    result.diagnostics.push_back({"URE-Q4-SCATTER-001", DiagnosticSeverity::Error,
                                  std::move(path), std::move(message), {}});
    return result;
}

core::Vec3f subtract(core::Vec3f a, core::Vec3f b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
core::Vec3f add(core::Vec3f a, core::Vec3f b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
core::Vec3f multiply(core::Vec3f a, float value) { return {a.x * value, a.y * value, a.z * value}; }
core::Vec3f cross(core::Vec3f a, core::Vec3f b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float dot(core::Vec3f a, core::Vec3f b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float length(core::Vec3f value) { return std::sqrt(dot(value, value)); }
core::Vec3f normalized(core::Vec3f value) { const float size = length(value); return multiply(value, 1.0f / size); }

float random_unit(const EvaluationContext& context, const ProceduralGraphNode& node,
                  std::uint64_t salt, std::size_t index, std::uint32_t lane) {
    const std::string key = std::format("ure.q4.prf.v1|{}|{}|{}|{}|{}|{}|{}",
        context.graph.seed_high, context.graph.seed_low, context.source_hash,
        node.id, salt, index, lane);
    const auto bytes = std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(key.data()), key.size());
    const std::string hash = sha256_hex(bytes);
    std::uint32_t bits = 0;
    for (std::size_t i = 0; i < 6; ++i) {
        const char character = hash[i];
        bits = (bits << 4) | static_cast<std::uint32_t>(character <= '9' ? character - '0' : character - 'a' + 10);
    }
    return static_cast<float>(bits) * 0x1p-24f;
}

core::Quat align_y_to(core::Vec3f normal) {
    const core::Vec3f y{0.0f, 1.0f, 0.0f};
    const float cosine = std::clamp(dot(y, normal), -1.0f, 1.0f);
    if (cosine > 0.999999f) return {};
    if (cosine < -0.999999f) return core::Quat::from_axis_angle({1.0f, 0.0f, 0.0f}, 3.14159265358979323846f);
    const core::Vec3f axis = cross(y, normal);
    return core::Quat(1.0f + cosine, axis.x, axis.y, axis.z).normalized();
}

}

LoadResult<TransformSet> evaluate_scatter(const EvaluationContext& context,
                                          const ProceduralGraphNode& node,
                                          const MeshReference& mesh) {
    const auto& data = std::get<ScatterSurfaceNode>(node.payload);
    auto count_value = resolve_binding(context, data.count, ParameterValueKind::Integer, node.id + "/count");
    auto offset_value = resolve_binding(context, data.offset, ParameterValueKind::Vec3, node.id + "/offset");
    auto scale_min_value = resolve_binding(context, data.scale_min, ParameterValueKind::Vec3, node.id + "/scale_min");
    auto scale_max_value = resolve_binding(context, data.scale_max, ParameterValueKind::Vec3, node.id + "/scale_max");
    auto yaw_min_value = resolve_binding(context, data.yaw_min, ParameterValueKind::Scalar, node.id + "/yaw_min");
    auto yaw_max_value = resolve_binding(context, data.yaw_max, ParameterValueKind::Scalar, node.id + "/yaw_max");
    if (!count_value.value) return failure<TransformSet>(node.id, "Invalid count binding");
    if (!offset_value.value || !scale_min_value.value || !scale_max_value.value || !yaw_min_value.value || !yaw_max_value.value) return failure<TransformSet>(node.id, "Invalid transform binding");
    const std::int64_t count = count_value.value->integer;
    const auto scale_min = scale_min_value.value->vec3;
    const auto scale_max = scale_max_value.value->vec3;
    if (count <= 0 || static_cast<std::uint64_t>(count) > context.options.limits.max_transforms ||
        scale_min.x <= 0.0f || scale_min.y <= 0.0f || scale_min.z <= 0.0f ||
        scale_max.x < scale_min.x || scale_max.y < scale_min.y || scale_max.z < scale_min.z ||
        yaw_max_value.value->scalar < yaw_min_value.value->scalar || !mesh.value || !mesh.value->mesh) {
        return failure<TransformSet>(node.id, "Scatter parameter domain is invalid");
    }
    const auto& source = *mesh.value->mesh;
    if (source.indices.empty() || source.indices.size() % 3 != 0) return failure<TransformSet>(node.id, "Scatter mesh topology is invalid");
    struct Triangle { core::Vec3f a; core::Vec3f b; core::Vec3f c; core::Vec3f normal; double cumulative; };
    std::vector<Triangle> triangles;
    double total = 0.0;
    for (std::size_t index = 0; index < source.indices.size(); index += 3) {
        const int ia = source.indices[index]; const int ib = source.indices[index + 1]; const int ic = source.indices[index + 2];
        if (ia < 0 || ib < 0 || ic < 0 || static_cast<std::size_t>(ia) >= source.vertices.size() ||
            static_cast<std::size_t>(ib) >= source.vertices.size() || static_cast<std::size_t>(ic) >= source.vertices.size()) return failure<TransformSet>(node.id, "Scatter mesh index is invalid");
        const auto a = source.vertices[ia].position; const auto b = source.vertices[ib].position; const auto c = source.vertices[ic].position;
        const auto area_vector = cross(subtract(b, a), subtract(c, a));
        const float twice_area = length(area_vector);
        if (twice_area == 0.0f) continue;
        total += static_cast<double>(twice_area) * 0.5;
        triangles.push_back({a, b, c, normalized(area_vector), total});
    }
    if (triangles.empty() || !std::isfinite(total) || total <= 0.0) return failure<TransformSet>(node.id, "Scatter mesh has no finite area");
    TransformSet output;
    output.values.reserve(static_cast<std::size_t>(count));
    for (std::size_t index = 0; index < static_cast<std::size_t>(count); ++index) {
        const double target = static_cast<double>(random_unit(context, node, data.seed_salt, index, 0)) * total;
        const auto triangle = std::ranges::lower_bound(triangles, target, {}, &Triangle::cumulative);
        float u = random_unit(context, node, data.seed_salt, index, 1);
        float v = random_unit(context, node, data.seed_salt, index, 2);
        if (u + v > 1.0f) { u = 1.0f - u; v = 1.0f - v; }
        const float w = 1.0f - u - v;
        GeneratedTransform transform;
        transform.position = add(add(multiply(triangle->a, w), multiply(triangle->b, u)), multiply(triangle->c, v));
        transform.position = add(transform.position, offset_value.value->vec3);
        transform.scale = {
            scale_min.x + (scale_max.x - scale_min.x) * random_unit(context, node, data.seed_salt, index, 3),
            scale_min.y + (scale_max.y - scale_min.y) * random_unit(context, node, data.seed_salt, index, 4),
            scale_min.z + (scale_max.z - scale_min.z) * random_unit(context, node, data.seed_salt, index, 5)};
        const float yaw = static_cast<float>(yaw_min_value.value->scalar +
            (yaw_max_value.value->scalar - yaw_min_value.value->scalar) * random_unit(context, node, data.seed_salt, index, 6));
        const core::Quat base = data.alignment == ScatterAlignment::SurfaceNormal ? align_y_to(triangle->normal) : core::Quat{};
        transform.rotation = (base * core::Quat::from_axis_angle({0.0f, 1.0f, 0.0f}, yaw)).normalized();
        output.values.push_back(transform);
    }
    LoadResult<TransformSet> result; result.value = std::move(output); return result;
}

}
