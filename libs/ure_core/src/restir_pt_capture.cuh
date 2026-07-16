#pragma once

static __device__ void update_restir_pt_edge_measure(
    GpuRestirPathSuffix& suffix,
    int depth,
    const GpuVec3& position,
    const GpuVec3& normal,
    bool surface_measure) {
    if (depth <= 0 || depth > suffix.vertex_count) return;
    GpuRestirPathVertex& previous = suffix.vertices[depth - 1];
    const GpuVec3 edge = position - previous.position;
    const float distance_squared = edge.length_sq();
    if (!(distance_squared > 1e-12f)) return;
    const GpuVec3 direction = edge * rsqrtf(distance_squared);
    const float cosine = surface_measure
        ? fabsf(normal.dot(-direction)) : 1.0f;
    previous.measure_jacobian = cosine / distance_squared;
}

static __device__ void capture_restir_pt_surface_vertex(
    const GpuScene& scene,
    int pixel,
    int depth,
    const GpuVec3& position,
    const GpuVec3& geometric_normal,
    const GpuVec3& incoming,
    const GpuVec3& outgoing,
    float forward_pdf,
    float reverse_pdf,
    int material_index,
    int geometry_type,
    int geometry_index,
    int primitive_index) {
    if (!scene.restir_pt_candidates || depth < 0 ||
        depth >= scene.restir_pt_max_reuse_depth ||
        depth >= GpuRestirPathSuffix::kMaxVertices) return;
    const MaterialType type = scene.materials[material_index].type;
    if (type != MaterialType::Lambertian) {
        if (scene.restir_pt_telemetry) {
            atomicAdd(&scene.restir_pt_telemetry->rejected_specular, 1u);
        }
        return;
    }
    GpuRestirPathSuffix& suffix = scene.restir_pt_candidates[pixel];
    update_restir_pt_edge_measure(
        suffix, depth, position, geometric_normal, true);
    GpuRestirPathVertex& vertex = suffix.vertices[depth];
    vertex.position = position;
    vertex.geometric_normal = geometric_normal;
    vertex.incoming = incoming;
    vertex.outgoing = outgoing;
    vertex.forward_pdf = forward_pdf;
    vertex.reverse_pdf = reverse_pdf;
    vertex.measure_jacobian = 1.0f;
    vertex.material_index = material_index;
    vertex.geometry_type = geometry_type;
    vertex.geometry_index = geometry_index;
    vertex.primitive_index = primitive_index;
    vertex.kind = GpuRestirPathVertexKind::Surface;
    vertex.scene_epoch = scene.restir_pt_scene_epoch;
    suffix.vertex_count = max(suffix.vertex_count, depth + 1);
    if (scene.restir_pt_telemetry) {
        atomicAdd(&scene.restir_pt_telemetry->surface_suffixes, 1u);
    }
}

static __device__ void capture_restir_pt_volume_vertex(
    const GpuScene& scene,
    int pixel,
    int depth,
    const GpuVec3& position,
    const GpuVec3& incoming,
    const GpuVec3& outgoing,
    float phase_pdf,
    int medium_index) {
    if (!scene.restir_pt_candidates || depth < 0 ||
        depth >= scene.restir_pt_max_reuse_depth ||
        depth >= GpuRestirPathSuffix::kMaxVertices) return;
    GpuRestirPathSuffix& suffix = scene.restir_pt_candidates[pixel];
    update_restir_pt_edge_measure(
        suffix, depth, position, GpuVec3(), false);
    GpuRestirPathVertex& vertex = suffix.vertices[depth];
    vertex.position = position;
    vertex.incoming = incoming;
    vertex.outgoing = outgoing;
    vertex.forward_pdf = phase_pdf;
    vertex.reverse_pdf = phase_pdf;
    vertex.measure_jacobian = 1.0f;
    vertex.medium_index = medium_index;
    vertex.kind = GpuRestirPathVertexKind::Volume;
    vertex.scene_epoch = scene.restir_pt_scene_epoch;
    suffix.vertex_count = max(suffix.vertex_count, depth + 1);
}

static __device__ void capture_restir_pt_terminal_vertex(
    const GpuScene& scene,
    int pixel,
    int depth,
    const GpuVec3& position,
    const GpuVec3& geometric_normal,
    GpuRestirPathVertexKind kind,
    int material_index,
    int geometry_type,
    int geometry_index,
    int primitive_index) {
    if (!scene.restir_pt_candidates || depth < 0 ||
        depth >= scene.restir_pt_max_reuse_depth ||
        depth >= GpuRestirPathSuffix::kMaxVertices) return;
    GpuRestirPathSuffix& suffix = scene.restir_pt_candidates[pixel];
    if (kind == GpuRestirPathVertexKind::Emitter) {
        update_restir_pt_edge_measure(
            suffix, depth, position, geometric_normal, true);
    }
    GpuRestirPathVertex& vertex = suffix.vertices[depth];
    vertex.position = position;
    vertex.material_index = material_index;
    vertex.geometry_type = geometry_type;
    vertex.geometry_index = geometry_index;
    vertex.primitive_index = primitive_index;
    vertex.kind = kind;
    vertex.scene_epoch = scene.restir_pt_scene_epoch;
    suffix.vertex_count = max(suffix.vertex_count, depth + 1);
}
