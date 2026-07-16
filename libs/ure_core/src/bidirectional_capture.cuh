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
