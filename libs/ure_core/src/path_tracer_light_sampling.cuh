#pragma once

static __device__ inline float sphere_light_solid_angle_pdf(
    const GpuSphere& light_sphere,
    const GpuVec3& reference_point,
    int light_count
) {
    if (light_count <= 0) return 0.0f;
    GpuVec3 wc = light_sphere.center - reference_point;
    float dist_sq = wc.length_sq();
    float radius_sq = light_sphere.radius * light_sphere.radius;
    if (dist_sq <= radius_sq) return 0.0f;
    float cos_theta_max = sqrtf(fmaxf(0.0f, 1.0f - radius_sq / dist_sq));
    float solid_angle = 6.2831853f * (1.0f - cos_theta_max);
    if (solid_angle <= 1e-8f) return 0.0f;
    return 1.0f / (solid_angle * float(light_count));
}

static __device__ inline float sphere_light_solid_angle_pdf_only(
    const GpuSphere& light_sphere,
    const GpuVec3& reference_point
) {
    GpuVec3 wc = light_sphere.center - reference_point;
    float dist_sq = wc.length_sq();
    float radius_sq = light_sphere.radius * light_sphere.radius;
    if (dist_sq <= radius_sq) return 0.0f;
    float cos_theta_max = sqrtf(fmaxf(0.0f, 1.0f - radius_sq / dist_sq));
    float solid_angle = 6.2831853f * (1.0f - cos_theta_max);
    if (solid_angle <= 1e-8f) return 0.0f;
    return 1.0f / solid_angle;
}

static __device__ inline float light_selection_pdf(const GpuScene& scene, int light_list_index) {
    if (scene.light_count <= 0 || light_list_index < 0 || light_list_index >= scene.light_count) return 0.0f;
    if (scene.light_selection_pmf) return fmaxf(0.0f, scene.light_selection_pmf[light_list_index]);
    if (!scene.light_selection_cdf) return 1.0f / float(scene.light_count);
    float upper = scene.light_selection_cdf[light_list_index];
    float lower = light_list_index == 0 ? 0.0f : scene.light_selection_cdf[light_list_index - 1];
    return fmaxf(0.0f, upper - lower);
}

static __device__ inline float light_tree_node_importance(
    const GpuScene& scene,
    int node_index,
    const GpuVec3& reference_point
) {
    if (!scene.light_tree_nodes || node_index < 0 || node_index >= scene.light_tree_node_count) return 0.0f;
    const GpuLightTreeNode node = scene.light_tree_nodes[node_index];
    const float weight = fmaxf(node.weight, 0.0f);
    if (weight <= 0.0f) return 0.0f;

    const GpuVec3 extent = node.bounds_max - node.bounds_min;
    if (extent.x > 1.0e18f || extent.y > 1.0e18f || extent.z > 1.0e18f) {
        return weight;
    }

    const GpuVec3 center = (node.bounds_min + node.bounds_max) * 0.5f;
    const GpuVec3 half_extent = extent * 0.5f;
    const float radius_sq = fmaxf(half_extent.length_sq(), 1.0e-8f);
    const float dist_sq = (center - reference_point).length_sq();
    if (dist_sq <= radius_sq) return weight;
    return weight / fmaxf(dist_sq - radius_sq, 1.0e-6f);
}

static __device__ inline float light_selection_pdf_at(
    const GpuScene& scene,
    int light_list_index,
    const GpuVec3& reference_point
) {
    if (scene.light_count <= 0 || light_list_index < 0 || light_list_index >= scene.light_count) return 0.0f;
    if (scene.light_tree_nodes &&
        scene.light_tree_leaf_nodes &&
        scene.light_tree_root >= 0 &&
        scene.light_tree_root < scene.light_tree_node_count) {
        int node_index = scene.light_tree_leaf_nodes[light_list_index];
        if (node_index < 0 || node_index >= scene.light_tree_node_count) return 0.0f;

        float pdf = 1.0f;
        for (int depth = 0; depth < 32; ++depth) {
            const GpuLightTreeNode node = scene.light_tree_nodes[node_index];
            const int parent_index = node.parent;
            if (parent_index < 0) return pdf;
            if (parent_index >= scene.light_tree_node_count) return 0.0f;
            const GpuLightTreeNode parent = scene.light_tree_nodes[parent_index];
            const float left_importance = light_tree_node_importance(scene, parent.left, reference_point);
            const float right_importance = light_tree_node_importance(scene, parent.right, reference_point);
            const float total = left_importance + right_importance;
            if (total <= 0.0f) return 0.0f;
            const float branch_pdf = node_index == parent.left ? left_importance / total : right_importance / total;
            pdf *= fmaxf(0.0f, branch_pdf);
            node_index = parent_index;
        }
        return 0.0f;
    }
    return light_selection_pdf(scene, light_list_index);
}

static __device__ inline float path_guiding_light_total(const GpuScene& scene) {
    if (!scene.path_guiding_light_weights ||
        scene.path_guiding_light_count != scene.light_count ||
        scene.light_count <= 0) {
        return 0.0f;
    }
    float total = 0.0f;
    for (int i = 0; i < scene.light_count; ++i) {
        total += fmaxf(0.0f, scene.path_guiding_light_weights[i]);
    }
    return total;
}

static __device__ inline void path_guiding_grid_dimensions(int cell_count, int& nx, int& ny, int& nz) {
    nx = 1;
    while ((nx + 1) * (nx + 1) * (nx + 1) <= cell_count) ++nx;
    const int remaining = (cell_count + nx - 1) / nx;
    ny = 1;
    while ((ny + 1) * (ny + 1) <= remaining) ++ny;
    nz = (cell_count + nx * ny - 1) / (nx * ny);
}

static __device__ inline int path_guiding_spatial_cell(const GpuScene& scene, const GpuVec3& p) {
    if (scene.path_guiding_spatial_cell_count <= 1) return 0;
    const GpuVec3 extent = scene.path_guiding_bounds_max - scene.path_guiding_bounds_min;
    if (extent.x <= 1.0e-6f || extent.y <= 1.0e-6f || extent.z <= 1.0e-6f) return 0;
    int grid_x = 1;
    int grid_y = 1;
    int grid_z = 1;
    path_guiding_grid_dimensions(scene.path_guiding_spatial_cell_count, grid_x, grid_y, grid_z);
    const float normalized_x = fminf(0.99999994f, fmaxf(0.0f, (p.x - scene.path_guiding_bounds_min.x) / extent.x));
    const float normalized_y = fminf(0.99999994f, fmaxf(0.0f, (p.y - scene.path_guiding_bounds_min.y) / extent.y));
    const float normalized_z = fminf(0.99999994f, fmaxf(0.0f, (p.z - scene.path_guiding_bounds_min.z) / extent.z));
    const int ix = min(int(normalized_x * float(grid_x)), grid_x - 1);
    const int iy = min(int(normalized_y * float(grid_y)), grid_y - 1);
    const int iz = min(int(normalized_z * float(grid_z)), grid_z - 1);
    return min(ix + grid_x * (iy + grid_y * iz), scene.path_guiding_spatial_cell_count - 1);
}

static __device__ inline int path_guiding_direction_bin(const GpuScene& scene, const GpuVec3& direction) {
    if (scene.path_guiding_directional_bin_count <= 1) return 0;
    const float len_sq = direction.length_sq();
    if (len_sq <= 1.0e-12f) return 0;
    const GpuVec3 d = direction * rsqrtf(len_sq);
    float azimuth = atan2f(d.z, d.x);
    if (azimuth < 0.0f) azimuth += 6.28318530717958647692f;
    const float u = fminf(0.99999994f, fmaxf(0.0f, azimuth * 0.15915494309189533577f));
    const float v = fminf(0.99999994f, fmaxf(0.0f, 0.5f * (d.y + 1.0f)));
    int azimuth_bins = 1;
    while (azimuth_bins * azimuth_bins < scene.path_guiding_directional_bin_count) ++azimuth_bins;
    const int elevation_bins =
        (scene.path_guiding_directional_bin_count + azimuth_bins - 1) / azimuth_bins;
    const int azimuth_bin = min(int(u * float(azimuth_bins)), azimuth_bins - 1);
    const int elevation_bin = min(int(v * float(elevation_bins)), elevation_bins - 1);
    return min(elevation_bin * azimuth_bins + azimuth_bin,
               scene.path_guiding_directional_bin_count - 1);
}

static __device__ inline int path_guiding_spatial_directional_index(
    const GpuScene& scene,
    int light_list_index,
    int cell,
    int direction_bin
) {
    if (!scene.path_guiding_spatial_directional_weights ||
        scene.path_guiding_spatial_cell_count <= 0 ||
        scene.path_guiding_directional_bin_count <= 0 ||
        light_list_index < 0 ||
        light_list_index >= scene.light_count ||
        cell < 0 ||
        cell >= scene.path_guiding_spatial_cell_count ||
        direction_bin < 0 ||
        direction_bin >= scene.path_guiding_directional_bin_count) {
        return -1;
    }
    return ((cell * scene.light_count) + light_list_index) * scene.path_guiding_directional_bin_count + direction_bin;
}

static __device__ inline float path_guiding_spatial_directional_light_weight(
    const GpuScene& scene,
    int light_list_index,
    const GpuVec3& reference_point
) {
    if (!scene.path_guiding_spatial_directional_weights) return 0.0f;
    const GpuLightRecord record = scene.lights[light_list_index];
    const int cell = path_guiding_spatial_cell(scene, reference_point);
    const int direction_bin = path_guiding_direction_bin(scene, record.centroid - reference_point);
    const int guide_index = path_guiding_spatial_directional_index(scene, light_list_index, cell, direction_bin);
    return guide_index >= 0 ? fmaxf(0.0f, scene.path_guiding_spatial_directional_weights[guide_index]) : 0.0f;
}

static __device__ inline float path_guiding_spatial_directional_total(const GpuScene& scene, const GpuVec3& reference_point) {
    if (!scene.path_guiding_spatial_directional_weights ||
        scene.path_guiding_light_count != scene.light_count ||
        scene.light_count <= 0) {
        return 0.0f;
    }
    float total = 0.0f;
    for (int i = 0; i < scene.light_count; ++i) {
        total += path_guiding_spatial_directional_light_weight(scene, i, reference_point);
    }
    return total;
}

static __device__ inline float path_guiding_effective_mixture(const GpuScene& scene, float guide_total) {
    if (guide_total <= fmaxf(scene.path_guiding_min_weight, 1e-12f)) return 0.0f;
    return fminf(0.95f, fmaxf(0.0f, scene.path_guiding_light_mixture));
}

struct PathGuidingProductMetadata {
    float luminance = 0.0f;
    float wavelength_nm = 0.0f;
};

static __device__ inline PathGuidingProductMetadata path_guiding_product_metadata(
    const SpectralPacket& product,
    int num_spec,
    int spectral_mode,
    int active_channel,
    float wavelength_pdf
) {
    PathGuidingProductMetadata metadata;
    const GpuVec3 xyz = spectral_mode_is_sampled(spectral_mode)
        ? spectral_sample_to_xyz(product, num_spec, active_channel, wavelength_pdf, spectral_mode)
        : spectrum_to_xyz(product, num_spec);
    metadata.luminance = fmaxf(0.0f, xyz.y);
    if (spectral_mode_is_sampled(spectral_mode) && active_channel >= 0 && active_channel < num_spec) {
        metadata.wavelength_nm = product.wavelengths[active_channel];
        return metadata;
    }
    float weighted_lambda = 0.0f;
    float weight_sum = 0.0f;
    for (int c = 0; c < num_spec; ++c) {
        const float weight = fmaxf(0.0f, product.values[c]);
        weighted_lambda += weight * product.wavelengths[c];
        weight_sum += weight;
    }
    metadata.wavelength_nm = weight_sum > 0.0f ? weighted_lambda / weight_sum : 0.0f;
    return metadata;
}

static __device__ inline float guided_light_selection_pdf(const GpuScene& scene, int light_list_index, float guide_total) {
    if (!scene.path_guiding_light_weights ||
        scene.path_guiding_light_count != scene.light_count ||
        light_list_index < 0 ||
        light_list_index >= scene.light_count ||
        guide_total <= fmaxf(scene.path_guiding_min_weight, 1e-12f)) {
        return 0.0f;
    }
    return fmaxf(0.0f, scene.path_guiding_light_weights[light_list_index]) / guide_total;
}

static __device__ inline int sample_base_light_list_index(const GpuScene& scene, float r) {
    if (scene.light_count <= 0) return -1;
    if (scene.light_tree_nodes && scene.light_tree_root >= 0 && scene.light_tree_root < scene.light_tree_node_count) {
        int node_index = scene.light_tree_root;
        float u = fminf(fmaxf(r, 0.0f), 0.99999994f);
        for (int depth = 0; depth < 32; ++depth) {
            const GpuLightTreeNode node = scene.light_tree_nodes[node_index];
            if (node.light_index >= 0) {
                return node.light_index < scene.light_count ? node.light_index : scene.light_count - 1;
            }
            const int left = node.left;
            const int right = node.right;
            if (left < 0 || right < 0 || left >= scene.light_tree_node_count || right >= scene.light_tree_node_count) break;
            const float left_weight = fmaxf(scene.light_tree_nodes[left].weight, 0.0f);
            const float right_weight = fmaxf(scene.light_tree_nodes[right].weight, 0.0f);
            const float total = left_weight + right_weight;
            if (total <= 0.0f) break;
            const float left_probability = left_weight / total;
            if (u < left_probability) {
                node_index = left;
                u = left_probability > 1e-8f ? u / left_probability : 0.0f;
            } else {
                node_index = right;
                const float right_probability = fmaxf(1e-8f, 1.0f - left_probability);
                u = (u - left_probability) / right_probability;
            }
        }
    }
    if (scene.light_alias_prob && scene.light_alias_index) {
        float scaled = fminf(fmaxf(r, 0.0f), 0.99999994f) * float(scene.light_count);
        int column = min(int(scaled), scene.light_count - 1);
        float coin = scaled - float(column);
        int alias = scene.light_alias_index[column];
        if (alias < 0 || alias >= scene.light_count) alias = column;
        return coin <= scene.light_alias_prob[column] ? column : alias;
    }
    if (!scene.light_selection_cdf) {
        return min(int(r * scene.light_count), scene.light_count - 1);
    }
    for (int i = 0; i < scene.light_count; ++i) {
        if (r <= scene.light_selection_cdf[i]) {
            return i;
        }
    }
    return scene.light_count - 1;
}

static __device__ inline int sample_base_light_list_index_at(
    const GpuScene& scene,
    const GpuVec3& reference_point,
    float r
) {
    if (scene.light_count <= 0) return -1;
    if (scene.light_tree_nodes && scene.light_tree_root >= 0 && scene.light_tree_root < scene.light_tree_node_count) {
        int node_index = scene.light_tree_root;
        float u = fminf(fmaxf(r, 0.0f), 0.99999994f);
        for (int depth = 0; depth < 32; ++depth) {
            const GpuLightTreeNode node = scene.light_tree_nodes[node_index];
            if (node.light_index >= 0) {
                return node.light_index < scene.light_count ? node.light_index : scene.light_count - 1;
            }
            const int left = node.left;
            const int right = node.right;
            if (left < 0 || right < 0 || left >= scene.light_tree_node_count || right >= scene.light_tree_node_count) break;
            const float left_importance = light_tree_node_importance(scene, left, reference_point);
            const float right_importance = light_tree_node_importance(scene, right, reference_point);
            const float total = left_importance + right_importance;
            if (total <= 0.0f) break;
            const float left_probability = left_importance / total;
            if (u < left_probability) {
                node_index = left;
                u = left_probability > 1e-8f ? u / left_probability : 0.0f;
            } else {
                node_index = right;
                const float right_probability = fmaxf(1e-8f, 1.0f - left_probability);
                u = (u - left_probability) / right_probability;
            }
        }
    }
    return sample_base_light_list_index(scene, r);
}

static __device__ inline int sample_guided_light_list_index(const GpuScene& scene, float r, float guide_total) {
    if (guide_total <= fmaxf(scene.path_guiding_min_weight, 1e-12f)) {
        return sample_base_light_list_index(scene, r);
    }
    const float target = fminf(fmaxf(r, 0.0f), 0.99999994f) * guide_total;
    float running = 0.0f;
    for (int i = 0; i < scene.light_count; ++i) {
        running += fmaxf(0.0f, scene.path_guiding_light_weights[i]);
        if (target <= running) return i;
    }
    return scene.light_count - 1;
}

static __device__ inline int sample_guided_light_list_index_at(
    const GpuScene& scene,
    const GpuVec3& reference_point,
    float r,
    float guide_total
) {
    if (guide_total <= fmaxf(scene.path_guiding_min_weight, 1e-12f)) {
        return sample_base_light_list_index_at(scene, reference_point, r);
    }
    const float target = fminf(fmaxf(r, 0.0f), 0.99999994f) * guide_total;
    float running = 0.0f;
    for (int i = 0; i < scene.light_count; ++i) {
        running += path_guiding_spatial_directional_light_weight(scene, i, reference_point);
        if (target <= running) return i;
    }
    return scene.light_count - 1;
}

static __device__ inline int sample_light_list_index(const GpuScene& scene, float r) {
    if (scene.light_count <= 0) return -1;
    float guide_total = path_guiding_light_total(scene);
    float mixture = path_guiding_effective_mixture(scene, guide_total);
    if (mixture > 0.0f && r < mixture) {
        return sample_guided_light_list_index(scene, r / mixture, guide_total);
    }
    float base_u = mixture < 1.0f ? (r - mixture) / fmaxf(1e-6f, 1.0f - mixture) : r;
    return sample_base_light_list_index(scene, base_u);
}

static __device__ inline int sample_light_list_index_at(
    const GpuScene& scene,
    const GpuVec3& reference_point,
    float r
) {
    if (scene.light_count <= 0) return -1;
    float guide_total = path_guiding_spatial_directional_total(scene, reference_point);
    if (guide_total <= fmaxf(scene.path_guiding_min_weight, 1e-12f)) {
        guide_total = path_guiding_light_total(scene);
    }
    float mixture = path_guiding_effective_mixture(scene, guide_total);
    if (mixture > 0.0f && r < mixture) {
        if (scene.path_guiding_spatial_directional_weights &&
            path_guiding_spatial_directional_total(scene, reference_point) > fmaxf(scene.path_guiding_min_weight, 1e-12f)) {
            return sample_guided_light_list_index_at(scene, reference_point, r / mixture, guide_total);
        }
        return sample_guided_light_list_index(scene, r / mixture, guide_total);
    }
    float base_u = mixture < 1.0f ? (r - mixture) / fmaxf(1e-6f, 1.0f - mixture) : r;
    return sample_base_light_list_index_at(scene, reference_point, base_u);
}

static __device__ inline float guided_mixture_light_selection_pdf(const GpuScene& scene, int light_list_index) {
    const float base_pdf = light_selection_pdf(scene, light_list_index);
    const float guide_total = path_guiding_light_total(scene);
    const float mixture = path_guiding_effective_mixture(scene, guide_total);
    if (mixture <= 0.0f) return base_pdf;
    const float guide_pdf = guided_light_selection_pdf(scene, light_list_index, guide_total);
    return (1.0f - mixture) * base_pdf + mixture * guide_pdf;
}

static __device__ inline float guided_mixture_light_selection_pdf_at(
    const GpuScene& scene,
    int light_list_index,
    const GpuVec3& reference_point
) {
    const float base_pdf = light_selection_pdf_at(scene, light_list_index, reference_point);
    const float spatial_directional_total = path_guiding_spatial_directional_total(scene, reference_point);
    const float guide_total = spatial_directional_total > fmaxf(scene.path_guiding_min_weight, 1e-12f)
        ? spatial_directional_total
        : path_guiding_light_total(scene);
    const float mixture = path_guiding_effective_mixture(scene, guide_total);
    if (mixture <= 0.0f) return base_pdf;
    const float guide_pdf = spatial_directional_total > fmaxf(scene.path_guiding_min_weight, 1e-12f)
        ? path_guiding_spatial_directional_light_weight(scene, light_list_index, reference_point) / guide_total
        : guided_light_selection_pdf(scene, light_list_index, guide_total);
    return (1.0f - mixture) * base_pdf + mixture * guide_pdf;
}

static __device__ inline float selected_sphere_light_pdf(
    const GpuScene& scene,
    int light_list_index,
    const GpuSphere& light_sphere,
    const GpuVec3& reference_point
) {
    return sphere_light_solid_angle_pdf_only(light_sphere, reference_point) *
           guided_mixture_light_selection_pdf_at(scene, light_list_index, reference_point);
}

struct SelectedLightSample {
    GpuVec3 point;
    GpuVec3 direction;
    float max_dist = 0.0f;
    float pdf = 0.0f;
    int material_index = -1;
    GpuLightKind kind = GpuLightKind::Sphere;
    bool valid = false;
};

static __device__ inline GpuVec3 environment_sky_color(
    const GpuScene& scene,
    const GpuVec3& direction,
    int current_medium_idx
) {
    const GpuVec3 unit_direction = direction.normalize();
    const float t_sky = 0.5f * (unit_direction.y + 1.0f);
    GpuVec3 sky_color;
    if (scene.medium_density > 1e-6f || current_medium_idx != -1) {
        const float sky_luma = 0.05f + 0.15f * t_sky;
        sky_color = GpuVec3(sky_luma, sky_luma, sky_luma);
    } else {
        sky_color = (1.0f - t_sky) * GpuVec3(0.05f, 0.05f, 0.05f) + t_sky * GpuVec3(0.2f, 0.2f, 0.4f);
    }
    const float intensity = scene.environment_light_intensity > 0.0f ? scene.environment_light_intensity : 1.0f;
    return sky_color * intensity;
}

static __device__ inline SpectralPacket environment_radiance_spectrum(
    const GpuScene& scene,
    const GpuVec3& direction,
    int current_medium_idx,
    const float* wavelengths
) {
    return emission_to_spectrum(
        environment_sky_color(scene, direction, current_medium_idx),
        wavelengths,
        scene.num_spectral_channels);
}

static __device__ inline GpuLightRecord get_light_record(const GpuScene& scene, int light_list_index) {
    if (scene.lights) {
        return scene.lights[light_list_index];
    }
    GpuLightRecord record;
    record.kind = GpuLightKind::Sphere;
    record.primitive_index = scene.light_indices ? scene.light_indices[light_list_index] : -1;
    record.secondary_index = -1;
    if (record.primitive_index >= 0 && record.primitive_index < scene.sphere_count) {
        const GpuSphere& sphere = scene.spheres[record.primitive_index];
        record.material_index = sphere.material_index;
        record.area = 4.0f * 3.14159265358979323846f * sphere.radius * sphere.radius;
    }
    return record;
}

static __device__ inline bool light_triangle_vertices(
    const GpuScene& scene,
    const GpuLightRecord& record,
    GpuVec3& v0,
    GpuVec3& v1,
    GpuVec3& v2
) {
    if (record.kind == GpuLightKind::MeshTriangle) {
        if (record.primitive_index < 0 || record.primitive_index >= scene.mesh_count) return false;
        const GpuMesh& mesh = scene.meshes[record.primitive_index];
        if (record.secondary_index < 0 || record.secondary_index >= mesh.triangle_count) return false;
        const int i0 = mesh.indices[record.secondary_index * 3 + 0];
        const int i1 = mesh.indices[record.secondary_index * 3 + 1];
        const int i2 = mesh.indices[record.secondary_index * 3 + 2];
        v0 = mesh.vertices[i0];
        v1 = mesh.vertices[i1];
        v2 = mesh.vertices[i2];
        return true;
    }
    if (record.kind == GpuLightKind::InstanceTriangle) {
        if (record.primitive_index < 0 || record.primitive_index >= scene.instance_count) return false;
        const GpuInstanceDesc& desc = scene.instance_descs[record.primitive_index];
        if (desc.mesh_index < 0 || desc.mesh_index >= scene.mesh_count) return false;
        const GpuMesh& mesh = scene.meshes[desc.mesh_index];
        if (record.secondary_index < 0 || record.secondary_index >= mesh.triangle_count) return false;
        const int i0 = mesh.indices[record.secondary_index * 3 + 0];
        const int i1 = mesh.indices[record.secondary_index * 3 + 1];
        const int i2 = mesh.indices[record.secondary_index * 3 + 2];
        const GpuInstanceTransform& xform = scene.instance_transforms[record.primitive_index];
        v0 = xform.transform.transform_point(mesh.vertices[i0]);
        v1 = xform.transform.transform_point(mesh.vertices[i1]);
        v2 = xform.transform.transform_point(mesh.vertices[i2]);
        return true;
    }
    return false;
}

static __device__ inline float selected_triangle_light_pdf(
    const GpuScene& scene,
    int light_list_index,
    const GpuLightRecord& record,
    const GpuVec3& reference_point,
    const GpuVec3& light_point
) {
    GpuVec3 v0, v1, v2;
    if (!light_triangle_vertices(scene, record, v0, v1, v2)) return 0.0f;
    const GpuVec3 normal = (v1 - v0).cross(v2 - v0).normalize();
    const GpuVec3 to_light = light_point - reference_point;
    const float dist_sq = to_light.length_sq();
    if (dist_sq <= 1e-12f) return 0.0f;
    const GpuVec3 dir = to_light * rsqrtf(dist_sq);
    const float cos_light = fabsf(normal.dot(-dir));
    const float area = record.area > 0.0f ? record.area : 0.5f * (v1 - v0).cross(v2 - v0).length();
    if (cos_light <= 1e-6f || area <= 0.0f) return 0.0f;
    return (dist_sq / (cos_light * area)) * guided_mixture_light_selection_pdf_at(scene, light_list_index, reference_point);
}

static __device__ inline float selected_light_hit_pdf(
    const GpuScene& scene,
    int light_list_index,
    const GpuVec3& reference_point,
    const GpuVec3& light_point
) {
    if (light_list_index < 0 || light_list_index >= scene.light_count) return 0.0f;
    const GpuLightRecord record = get_light_record(scene, light_list_index);
    if (record.kind == GpuLightKind::Sphere) {
        if (record.primitive_index < 0 || record.primitive_index >= scene.sphere_count) return 0.0f;
        return selected_sphere_light_pdf(scene, light_list_index, scene.spheres[record.primitive_index], reference_point);
    }
    return selected_triangle_light_pdf(scene, light_list_index, record, reference_point, light_point);
}

static __device__ inline bool sample_selected_light(
    const GpuScene& scene,
    int light_list_index,
    const GpuVec3& reference_point,
    float r1,
    float r2,
    SelectedLightSample& sample
) {
    if (light_list_index < 0 || light_list_index >= scene.light_count) return false;
    const GpuLightRecord record = get_light_record(scene, light_list_index);
    if (record.kind == GpuLightKind::Sphere) {
        if (record.primitive_index < 0 || record.primitive_index >= scene.sphere_count) return false;
        const GpuSphere& light_sphere = scene.spheres[record.primitive_index];
        GpuVec3 wc = light_sphere.center - reference_point;
        float dist_sq = wc.length_sq();
        float radius_sq = light_sphere.radius * light_sphere.radius;
        if (dist_sq <= radius_sq) return false;
        float dist = sqrtf(dist_sq);
        float cos_theta_max = sqrtf(fmaxf(0.0f, 1.0f - radius_sq / dist_sq));
        float cos_theta = 1.0f - r1 + r1 * cos_theta_max;
        float sin_theta = sqrtf(fmaxf(0.0f, 1.0f - cos_theta * cos_theta));
        float phi = 6.2831853f * r2;
        GpuVec3 w = wc * (1.0f / dist);
        GpuVec3 u = (fabsf(w.x) > 0.9f) ? GpuVec3(0, 1, 0) : GpuVec3(1, 0, 0);
        u = u.cross(w).normalize();
        GpuVec3 v = w.cross(u);
        sample.direction = (u * cosf(phi) * sin_theta + v * sinf(phi) * sin_theta + w * cos_theta).normalize();
        float M_dot_D = -wc.dot(sample.direction);
        float c = dist_sq - radius_sq;
        float discriminant_val = M_dot_D * M_dot_D - c;
        if (discriminant_val <= 0.0f) return false;
        sample.max_dist = -M_dot_D - sqrtf(discriminant_val);
        if (sample.max_dist <= 1e-4f) return false;
        sample.point = reference_point + sample.direction * sample.max_dist;
        sample.pdf = selected_sphere_light_pdf(scene, light_list_index, light_sphere, reference_point);
        sample.material_index = light_sphere.material_index;
        sample.kind = GpuLightKind::Sphere;
        sample.valid = sample.pdf > 0.0f;
        return sample.valid;
    }
    if (record.kind == GpuLightKind::Environment) {
        const float y = 1.0f - 2.0f * fminf(fmaxf(r1, 0.0f), 0.99999994f);
        const float radial = sqrtf(fmaxf(0.0f, 1.0f - y * y));
        const float phi = 6.2831853f * r2;
        sample.direction = GpuVec3(radial * cosf(phi), y, radial * sinf(phi)).normalize();
        sample.point = reference_point + sample.direction * 1.0e20f;
        sample.max_dist = scene.medium_max_distance > 0.0f ? scene.medium_max_distance : 1.0e20f;
        sample.pdf = guided_mixture_light_selection_pdf_at(scene, light_list_index, reference_point) * (1.0f / 12.566370614359172f);
        sample.material_index = -1;
        sample.kind = GpuLightKind::Environment;
        sample.valid = sample.pdf > 0.0f && scene.environment_light_direct_sampling != 0;
        return sample.valid;
    }

    GpuVec3 v0, v1, v2;
    if (!light_triangle_vertices(scene, record, v0, v1, v2)) return false;
    const float su = sqrtf(fminf(fmaxf(r1, 0.0f), 0.99999994f));
    const float b0 = 1.0f - su;
    const float b1 = su * (1.0f - r2);
    const float b2 = su * r2;
    sample.point = v0 * b0 + v1 * b1 + v2 * b2;
    const GpuVec3 to_light = sample.point - reference_point;
    const float dist_sq = to_light.length_sq();
    if (dist_sq <= 1e-12f) return false;
    sample.max_dist = sqrtf(dist_sq);
    sample.direction = to_light * (1.0f / sample.max_dist);
    sample.pdf = selected_triangle_light_pdf(scene, light_list_index, record, reference_point, sample.point);
    sample.material_index = record.material_index;
    sample.kind = record.kind;
    sample.valid = sample.pdf > 0.0f && sample.material_index >= 0;
    return sample.valid;
}

static __device__ inline bool reconstruct_restir_di_light_sample(
    const GpuScene& scene,
    const GpuRestirDISample& stored,
    const GpuVec3& reference_point,
    SelectedLightSample& sample) {
    if (stored.light_list_index < 0 || stored.light_list_index >= scene.light_count) return false;
    const GpuLightRecord record = get_light_record(scene, stored.light_list_index);
    if (record.primitive_index != stored.light_primitive_index ||
        record.secondary_index != stored.light_secondary_index ||
        record.material_index != stored.light_material_index) {
        return false;
    }
    return sample_selected_light(
        scene, stored.light_list_index, reference_point, stored.light_u, stored.light_v, sample);
}
