#pragma once

static __device__ void capture_bidirectional_surface_vertex(
    const GpuScene& scene,
    const RayQueue& queue,
    int queue_index,
    int depth,
    const GpuVec3& position,
    const GpuVec3& geometric_normal,
    const GpuVec3& shading_normal,
    const GpuVec3& incoming,
    const GpuVec3& outgoing,
    const GpuVec2& uv,
    const SpectralPacket& throughput,
    float forward_pdf,
    float reverse_pdf,
    int material_index,
    int geometry_type,
    int geometry_index,
    int primitive_index,
    bool delta) {
    if (!scene.bidirectional_camera_vertices ||
        !scene.bidirectional_camera_path_lengths || depth < 0 ||
        depth >= scene.bidirectional_max_camera_vertices) return;
    const std::uint32_t path_index = queue.path_indices[queue_index];
    if (path_index >= static_cast<std::uint32_t>(
            scene.bidirectional_camera_path_capacity)) return;
    GpuBidirectionalPathVertex* path =
        scene.bidirectional_camera_vertices +
        static_cast<size_t>(path_index) *
            scene.bidirectional_max_camera_vertices;
    if (depth > 0 && path[depth - 1].valid) {
        const GpuVec3 edge = position - path[depth - 1].position;
        const float distance_squared = edge.length_sq();
        if (distance_squared > 1e-12f) {
            const GpuVec3 direction = edge * rsqrtf(distance_squared);
            path[depth - 1].forward_measure_pdf =
                path_solid_angle_to_area_pdf(
                    path[depth - 1].forward_directional_pdf,
                    distance_squared,
                    fabsf(geometric_normal.dot(-direction)));
            path[depth - 1].reverse_measure_pdf =
                path[depth - 1].measure == GpuPathVertexMeasure::Volume
                ? path_solid_angle_to_volume_pdf(
                    reverse_pdf, distance_squared)
                : path_solid_angle_to_area_pdf(
                    reverse_pdf, distance_squared,
                    fabsf(path[depth - 1].geometric_normal.dot(direction)));
        }
    }
    GpuBidirectionalPathVertex& vertex = path[depth];
    vertex.position = position;
    vertex.geometric_normal = geometric_normal;
    vertex.shading_normal = shading_normal;
    vertex.incoming = incoming;
    vertex.outgoing = outgoing;
    vertex.uv = uv;
    vertex.throughput = throughput;
    for (int channel = 0; channel < queue.num_spectral_channels; ++channel) {
        const StokesVector stokes = load_stokes(queue, queue_index, channel);
        vertex.stokes_i.values[channel] = stokes.I;
        vertex.stokes_q.values[channel] = stokes.Q;
        vertex.stokes_u.values[channel] = stokes.U;
        vertex.stokes_v.values[channel] = stokes.V;
        vertex.stokes_i.wavelengths[channel] = throughput.wavelengths[channel];
        vertex.stokes_q.wavelengths[channel] = throughput.wavelengths[channel];
        vertex.stokes_u.wavelengths[channel] = throughput.wavelengths[channel];
        vertex.stokes_v.wavelengths[channel] = throughput.wavelengths[channel];
    }
    vertex.wavelength_pdf = queue.wavelength_pdfs[queue_index];
    vertex.forward_directional_pdf = forward_pdf;
    vertex.reverse_directional_pdf = reverse_pdf;
    vertex.spectral_mode = queue.spectral_modes[queue_index];
    vertex.active_channel = queue.active_channels[queue_index];
    vertex.geometry_type = geometry_type;
    vertex.geometry_index = geometry_index;
    vertex.primitive_index = primitive_index;
    vertex.material_index = material_index;
    vertex.medium_index = queue.medium_indices[queue_index];
    vertex.measure = GpuPathVertexMeasure::Area;
    vertex.transport_mode = GpuPathTransportMode::Radiance;
    vertex.sample_index = queue.sample_indices[queue_index];
    vertex.scene_epoch = scene.bidirectional_scene_epoch;
    vertex.delta = delta ? 1 : 0;
    vertex.valid = 1;
    scene.bidirectional_camera_path_lengths[path_index] =
        max(scene.bidirectional_camera_path_lengths[path_index], depth + 1);
    if (scene.bidirectional_telemetry) {
        atomicAdd(&scene.bidirectional_telemetry->camera_vertices, 1u);
    }
}

static __device__ void capture_bidirectional_volume_vertex(
    const GpuScene& scene,
    const RayQueue& queue,
    int queue_index,
    int depth,
    const GpuVec3& position,
    const GpuVec3& incoming,
    const GpuVec3& outgoing,
    const SpectralPacket& throughput,
    float phase_pdf,
    int medium_index) {
    if (!scene.bidirectional_camera_vertices ||
        !scene.bidirectional_camera_path_lengths || depth < 0 ||
        depth >= scene.bidirectional_max_camera_vertices) return;
    const std::uint32_t path_index = queue.path_indices[queue_index];
    if (path_index >= static_cast<std::uint32_t>(
            scene.bidirectional_camera_path_capacity)) return;
    GpuBidirectionalPathVertex* path =
        scene.bidirectional_camera_vertices +
        static_cast<size_t>(path_index) *
            scene.bidirectional_max_camera_vertices;
    if (depth > 0 && path[depth - 1].valid) {
        const GpuVec3 edge = position - path[depth - 1].position;
        const float distance_squared = edge.length_sq();
        if (distance_squared > 1e-12f) {
            const GpuVec3 direction = edge * rsqrtf(distance_squared);
            path[depth - 1].forward_measure_pdf =
                path_solid_angle_to_volume_pdf(
                    path[depth - 1].forward_directional_pdf,
                    distance_squared);
            const float previous_target =
                path[depth - 1].measure == GpuPathVertexMeasure::Area
                ? fabsf(path[depth - 1].geometric_normal.dot(direction))
                : 1.0f;
            path[depth - 1].reverse_measure_pdf =
                path_solid_angle_to_area_pdf(
                    phase_pdf, distance_squared, previous_target);
        }
    }
    GpuBidirectionalPathVertex& vertex = path[depth];
    vertex.position = position;
    vertex.incoming = incoming;
    vertex.outgoing = outgoing;
    vertex.throughput = throughput;
    for (int channel = 0; channel < queue.num_spectral_channels; ++channel) {
        const StokesVector stokes = load_stokes(queue, queue_index, channel);
        vertex.stokes_i.values[channel] = stokes.I;
        vertex.stokes_q.values[channel] = stokes.Q;
        vertex.stokes_u.values[channel] = stokes.U;
        vertex.stokes_v.values[channel] = stokes.V;
        vertex.stokes_i.wavelengths[channel] = throughput.wavelengths[channel];
        vertex.stokes_q.wavelengths[channel] = throughput.wavelengths[channel];
        vertex.stokes_u.wavelengths[channel] = throughput.wavelengths[channel];
        vertex.stokes_v.wavelengths[channel] = throughput.wavelengths[channel];
    }
    vertex.wavelength_pdf = queue.wavelength_pdfs[queue_index];
    vertex.forward_directional_pdf = phase_pdf;
    vertex.reverse_directional_pdf = phase_pdf;
    vertex.spectral_mode = queue.spectral_modes[queue_index];
    vertex.active_channel = queue.active_channels[queue_index];
    vertex.material_index = -1;
    vertex.medium_index = medium_index;
    vertex.measure = GpuPathVertexMeasure::Volume;
    vertex.transport_mode = GpuPathTransportMode::Radiance;
    vertex.sample_index = queue.sample_indices[queue_index];
    vertex.scene_epoch = scene.bidirectional_scene_epoch;
    vertex.valid = 1;
    scene.bidirectional_camera_path_lengths[path_index] =
        max(scene.bidirectional_camera_path_lengths[path_index], depth + 1);
    if (scene.bidirectional_telemetry) {
        atomicAdd(&scene.bidirectional_telemetry->camera_vertices, 1u);
    }
}

static __device__ float capture_bidirectional_emitter_vertex(
    const GpuScene& scene,
    const RayQueue& queue,
    int queue_index,
    int depth,
    const GpuVec3& position,
    const GpuVec3& geometric_normal,
    const GpuVec2& uv,
    const SpectralPacket& throughput,
    int material_index,
    int geometry_type,
    int geometry_index,
    int primitive_index) {
    if (!scene.bidirectional_mis_partition || depth <= 0 ||
        !scene.bidirectional_camera_vertices ||
        !scene.bidirectional_camera_path_lengths || depth >=
            scene.bidirectional_max_camera_vertices) return 1.0f;
    const std::uint32_t path_index = queue.path_indices[queue_index];
    if (path_index >= static_cast<std::uint32_t>(
            scene.bidirectional_camera_path_capacity)) return 0.0f;
    int light_index = -1;
    GpuLightRecord record = {};
    for (int index = 0; index < scene.light_count; ++index) {
        const GpuLightRecord candidate = get_light_record(scene, index);
        const bool identity_matches =
            (candidate.kind == GpuLightKind::Sphere && geometry_type == 0 &&
             candidate.primitive_index == geometry_index) ||
            (candidate.kind == GpuLightKind::MeshTriangle &&
             geometry_type == 1 && candidate.primitive_index == geometry_index &&
             candidate.secondary_index == primitive_index) ||
            (candidate.kind == GpuLightKind::InstanceTriangle &&
             geometry_type == 2 && candidate.primitive_index == geometry_index &&
             candidate.secondary_index == primitive_index);
        if (identity_matches && candidate.material_index == material_index) {
            light_index = index;
            record = candidate;
            break;
        }
    }
    if (light_index < 0 || !(record.area > 0.0f)) return 0.0f;
    const float endpoint_pdf = light_selection_pdf(scene, light_index) /
        record.area;
    if (!(endpoint_pdf > 0.0f)) return 0.0f;
    GpuBidirectionalPathVertex* path =
        scene.bidirectional_camera_vertices +
        static_cast<size_t>(path_index) *
            scene.bidirectional_max_camera_vertices;
    if (!path[depth - 1].valid) return 0.0f;
    const GpuVec3 edge = position - path[depth - 1].position;
    const float distance_squared = edge.length_sq();
    if (!(distance_squared > 1e-12f)) return 0.0f;
    const GpuVec3 camera_direction = edge * rsqrtf(distance_squared);
    path[depth - 1].forward_measure_pdf =
        path_solid_angle_to_area_pdf(
            queue.last_pdf[queue_index], distance_squared,
            fabsf(geometric_normal.dot(-camera_direction)));
    const float emission_cosine = fmaxf(
        0.0f, geometric_normal.dot(-camera_direction));
    const float emission_directional_pdf =
        emission_cosine * 0.31830988618379067154f;
    const float previous_target =
        path[depth - 1].measure == GpuPathVertexMeasure::Area
        ? fabsf(path[depth - 1].geometric_normal.dot(camera_direction))
        : 1.0f;
    path[depth - 1].reverse_measure_pdf =
        path[depth - 1].measure == GpuPathVertexMeasure::Area
        ? path_solid_angle_to_area_pdf(
            emission_directional_pdf, distance_squared, previous_target)
        : path_solid_angle_to_volume_pdf(
            emission_directional_pdf, distance_squared);
    GpuBidirectionalPathVertex& terminal = path[depth];
    terminal.position = position;
    terminal.geometric_normal = geometric_normal;
    terminal.shading_normal = geometric_normal;
    terminal.incoming = -camera_direction;
    terminal.outgoing = -camera_direction;
    terminal.uv = uv;
    terminal.throughput = throughput;
    for (int channel = 0; channel < queue.num_spectral_channels; ++channel) {
        const StokesVector stokes = load_stokes(queue, queue_index, channel);
        terminal.stokes_i.values[channel] = stokes.I;
        terminal.stokes_q.values[channel] = stokes.Q;
        terminal.stokes_u.values[channel] = stokes.U;
        terminal.stokes_v.values[channel] = stokes.V;
        terminal.stokes_i.wavelengths[channel] = throughput.wavelengths[channel];
        terminal.stokes_q.wavelengths[channel] = throughput.wavelengths[channel];
        terminal.stokes_u.wavelengths[channel] = throughput.wavelengths[channel];
        terminal.stokes_v.wavelengths[channel] = throughput.wavelengths[channel];
    }
    terminal.wavelength_pdf = queue.wavelength_pdfs[queue_index];
    terminal.reverse_directional_pdf = emission_directional_pdf;
    terminal.endpoint_pdf = endpoint_pdf;
    terminal.spectral_mode = queue.spectral_modes[queue_index];
    terminal.active_channel = queue.active_channels[queue_index];
    terminal.geometry_type = geometry_type;
    terminal.geometry_index = geometry_index;
    terminal.primitive_index = primitive_index;
    terminal.material_index = material_index;
    terminal.measure = GpuPathVertexMeasure::Area;
    terminal.transport_mode = GpuPathTransportMode::Radiance;
    terminal.sample_index = queue.sample_indices[queue_index];
    terminal.scene_epoch = scene.bidirectional_scene_epoch;
    terminal.valid = 1;
    scene.bidirectional_camera_path_lengths[path_index] = depth + 1;
    if (scene.bidirectional_telemetry) {
        atomicAdd(&scene.bidirectional_telemetry->camera_vertices, 1u);
    }
    GpuBidirectionalPdfEdge edges[31] = {};
    for (int edge_index = 0; edge_index < depth; ++edge_index) {
        const int lower = depth - edge_index - 1;
        const int upper = lower + 1;
        edges[edge_index].forward_measure_pdf =
            path[lower].reverse_measure_pdf;
        edges[edge_index].reverse_measure_pdf =
            path[lower].forward_measure_pdf;
        edges[edge_index].from_delta = path[upper].delta;
        edges[edge_index].to_delta = path[lower].delta;
    }
    return bidirectional_strategy_mis_weight(
        edges, depth, 0, endpoint_pdf, 1.0f);
}
