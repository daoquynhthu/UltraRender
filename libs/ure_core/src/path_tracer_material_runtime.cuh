#pragma once

static __device__ float sample_spectral_texture_resource(const GpuTexture& tex, int texel_index, float lambda) {
    if (tex.spectral_kind != SpectralTextureResourceKind::SourceSampleGrid ||
        !tex.spectral_source_values ||
        tex.spectral_sample_count <= 0) {
        return 0.0f;
    }

    if (tex.spectral_sample_count == 1 || tex.spectral_lambda_max <= tex.spectral_lambda_min) {
        return tex.spectral_source_values[static_cast<size_t>(texel_index) * static_cast<size_t>(tex.spectral_sample_count)];
    }

    const float normalized = fminf(1.0f, fmaxf(0.0f,
        (lambda - tex.spectral_lambda_min) / (tex.spectral_lambda_max - tex.spectral_lambda_min)));
    const float sample_pos = normalized * float(tex.spectral_sample_count - 1);
    const int s0 = min(static_cast<int>(floorf(sample_pos)), tex.spectral_sample_count - 1);
    const int s1 = min(s0 + 1, tex.spectral_sample_count - 1);
    const float ds = sample_pos - float(s0);
    const size_t base = static_cast<size_t>(texel_index) * static_cast<size_t>(tex.spectral_sample_count);
    const float v0 = tex.spectral_source_values[base + static_cast<size_t>(s0)];
    const float v1 = tex.spectral_source_values[base + static_cast<size_t>(s1)];
    return v0 * (1.0f - ds) + v1 * ds;
}

static __device__ SpectralPacket sample_texture(const GpuScene& scene, int tex_idx, float u, float v, const float* wavelengths, int num_spec) {
    if (tex_idx < 0 || tex_idx >= scene.texture_count) return rgb_to_spectrum(GpuVec3(1,0,1), wavelengths, num_spec);

    GpuTexture tex = scene.textures[tex_idx];

    if (tex.texture_object) {
        float4 val = tex2D<float4>(tex.texture_object, u, v);
        return rgb_to_spectrum(GpuVec3(val.x, val.y, val.z), wavelengths, num_spec);
    }

    if (tex.spectral_kind != SpectralTextureResourceKind::SourceSampleGrid || !tex.spectral_source_values) {
        return rgb_to_spectrum(GpuVec3(0,0,0), wavelengths, num_spec);
    }

    u = u - floorf(u);
    v = v - floorf(v);

    float x = u * (tex.width - 1);
    float y = v * (tex.height - 1);

    int x0 = (int)x;
    int y0 = (int)y;
    int x1 = min(x0 + 1, tex.width - 1);
    int y1 = min(y0 + 1, tex.height - 1);

    float dx = x - x0;
    float dy = y - y0;

    const int idx00 = y0 * tex.width + x0;
    const int idx10 = y0 * tex.width + x1;
    const int idx01 = y1 * tex.width + x0;
    const int idx11 = y1 * tex.width + x1;

    SpectralPacket result;
    for (int c = 0; c < num_spec; ++c) {
        const float lambda = wavelengths[c];
        float v00 = sample_spectral_texture_resource(tex, idx00, lambda);
        float v10 = sample_spectral_texture_resource(tex, idx10, lambda);
        float v01 = sample_spectral_texture_resource(tex, idx01, lambda);
        float v11 = sample_spectral_texture_resource(tex, idx11, lambda);
        float v0 = v00 * (1.0f - dx) + v10 * dx;
        float v1 = v01 * (1.0f - dx) + v11 * dx;
        result.values[c] = v0 * (1.0f - dy) + v1 * dy;
        result.wavelengths[c] = lambda;
    }
    return result;
}

static __device__ SpectralPacket sample_expression_texture(const GpuScene& scene,
                                                    int tex_idx,
                                                    SpectralExpressionSemantic semantic,
                                                    float u,
                                                    float v,
                                                    const float* wavelengths,
                                                    int num_spec) {
    if (tex_idx < 0 || tex_idx >= scene.texture_count) return SpectralPacket(0.0f);
    const GpuTexture tex = scene.textures[tex_idx];
    if (!tex.texture_object ||
        tex.spectral_kind == SpectralTextureResourceKind::SourceSampleGrid) {
        return sample_texture(scene, tex_idx, u, v, wavelengths, num_spec);
    }
    const float4 value = tex2D<float4>(tex.texture_object, u, v);
    const GpuVec3 rgb(value.x, value.y, value.z);
    SpectralPacket result;
    for (int c = 0; c < num_spec; ++c) {
        result.wavelengths[c] = wavelengths[c];
        if (semantic == SpectralExpressionSemantic::OpticalConstant) {
            result.values[c] = rgb_coeff_to_spectrum_value(rgb, wavelengths[c]);
        } else if (semantic == SpectralExpressionSemantic::Scalar) {
            result.values[c] = 0.2126f * rgb.x + 0.7152f * rgb.y + 0.0722f * rgb.z;
        } else {
            result.values[c] = rgb_to_spectrum_value(rgb, wavelengths[c]);
        }
    }
    return result;
}

static __device__ SpectralPacket eval_material_expression(
    const GpuScene& scene,
    const GpuMaterial& mat,
    int root,
    float u,
    float v,
    const float* wavelengths,
    int num_spec
) {
    SpectralPacket zero(0.0f);
    if (!scene.material_expression_nodes ||
        root < 0 ||
        mat.expression_node_start < 0 ||
        mat.expression_node_count <= 0 ||
        mat.expression_node_count > kMaxMaterialExpressionNodes) {
        return zero;
    }

    const int start = mat.expression_node_start;
    const int count = mat.expression_node_count;
    const int local_root = root - start;
    if (local_root < 0 || local_root >= count || start + count > scene.material_expression_node_count) {
        return zero;
    }

    SpectralPacket values[kMaxMaterialExpressionNodes];
    auto procedural_noise = [](float x, float y) {
        int xi = int(floorf(x));
        int yi = int(floorf(y));
        float fx = x - float(xi);
        float fy = y - float(yi);
        fx = fx * fx * (3.0f - 2.0f * fx);
        fy = fy * fy * (3.0f - 2.0f * fy);
        auto hash = [](int px, int py) {
            unsigned int h = unsigned(px) * 0x8da6b343u ^ unsigned(py) * 0xd8163841u;
            h ^= h >> 13;
            h *= 0x85ebca6bu;
            h ^= h >> 16;
            return float(h & 0x00ffffffu) * (1.0f / 16777215.0f);
        };
        float x0 = hash(xi, yi) * (1.0f - fx) + hash(xi + 1, yi) * fx;
        float x1 = hash(xi, yi + 1) * (1.0f - fx) + hash(xi + 1, yi + 1) * fx;
        return x0 * (1.0f - fy) + x1 * fy;
    };
    for (int i = 0; i < count; ++i) {
        const SpectralExpressionNode& node = scene.material_expression_nodes[start + i];
        SpectralPacket result(0.0f);
        switch (node.kind) {
            case SpectralExpressionNodeKind::Resource:
                for (int c = 0; c < num_spec; ++c) {
                    const float lambda = wavelengths[c];
                    result.wavelengths[c] = lambda;
                    result.values[c] = eval_spectral_resource(node.resource, lambda);
                }
                break;
            case SpectralExpressionNodeKind::Texture:
                result = sample_expression_texture(scene, node.texture_index, node.semantic, u, v, wavelengths, num_spec);
                break;
            case SpectralExpressionNodeKind::Add: {
                int a = node.input_a - start;
                int b = node.input_b - start;
                if (a >= 0 && a < i && b >= 0 && b < i) {
                    result = values[a] + values[b];
                }
                break;
            }
            case SpectralExpressionNodeKind::Multiply: {
                int a = node.input_a - start;
                int b = node.input_b - start;
                if (a >= 0 && a < i && b >= 0 && b < i) {
                    result = values[a] * values[b];
                }
                break;
            }
            case SpectralExpressionNodeKind::Mix: {
                int a = node.input_a - start;
                int b = node.input_b - start;
                int f = node.input_factor - start;
                if (a >= 0 && a < i && b >= 0 && b < i && f >= 0 && f < i) {
                    for (int c = 0; c < num_spec; ++c) {
                        float t = fminf(1.0f, fmaxf(0.0f, values[f].values[c]));
                        result.wavelengths[c] = wavelengths[c];
                        result.values[c] = values[a].values[c] * (1.0f - t) + values[b].values[c] * t;
                    }
                }
                break;
            }
            case SpectralExpressionNodeKind::Checker2D:
            case SpectralExpressionNodeKind::Noise2D: {
                int a = node.input_a - start;
                int b = node.input_b - start;
                int s = node.input_factor - start;
                if (a >= 0 && a < i && b >= 0 && b < i && s >= 0 && s < i) {
                    float scale = fmaxf(1e-6f, fabsf(values[s].values[0]));
                    float t = node.kind == SpectralExpressionNodeKind::Checker2D
                        ? float((int(floorf(u * scale)) + int(floorf(v * scale))) & 1)
                        : procedural_noise(u * scale, v * scale);
                    for (int c = 0; c < num_spec; ++c) {
                        result.wavelengths[c] = wavelengths[c];
                        result.values[c] = values[a].values[c] * (1.0f - t) + values[b].values[c] * t;
                    }
                }
                break;
            }
            case SpectralExpressionNodeKind::None:
            default:
                break;
        }
        values[i] = result;
    }
    return values[local_root];
}

struct ResolvedMaterialBsdfLobe {
    GpuMaterial material;
    GpuMaterialSoA spectra;
    SpectralPacket dielectric_ior;
};

#ifndef URE_MATERIAL_TARGET_ONLY
static __device__ inline void rotate_stokes_into_boundary_frame(StokesVector& s, const GpuVec3& ray_dir, const GpuVec3& boundary_normal);
static __device__ inline void rotate_stokes_from_boundary_frame(StokesVector& s, const GpuVec3& ray_dir, const GpuVec3& boundary_normal);
#endif

static __device__ float material_expression_scalar(const GpuScene& scene,
                                            const GpuMaterial& material,
                                            int root,
                                            const GpuVec2& uv,
                                            const float* wavelengths) {
    SpectralPacket value = eval_material_expression(
        scene, material, root, uv.u, uv.v, wavelengths, scene.num_spectral_channels);
    float sum = 0.0f;
    for (int c = 0; c < scene.num_spectral_channels; ++c) sum += value.values[c];
    return sum / fmaxf(1.0f, float(scene.num_spectral_channels));
}

static __device__ ResolvedMaterialBsdfLobe resolve_material_bsdf_lobe(
    const GpuScene& scene,
    const GpuMaterial& parent,
    int lobe_index,
    const GpuVec2& uv,
    const float* wavelengths
) {
    ResolvedMaterialBsdfLobe resolved = {};
    const GpuMaterialBsdfLobe& source =
        scene.material_bsdf_lobes[parent.bsdf_lobe_start + lobe_index];
    resolved.material.type = source.type;
    resolved.material.roughness = source.roughness;
    resolved.material.ior = source.ior;
    resolved.material.dispersion = source.dispersion;
    resolved.material.thin_film_thickness = source.thin_film_thickness;
    resolved.material.thin_film_ior = source.thin_film_ior;
    resolved.material.ior_expression_root = source.ior_expression_root;
    resolved.spectra.albedo = eval_material_expression(
        scene, parent, source.albedo_expression_root, uv.u, uv.v, wavelengths, scene.num_spectral_channels);
    if (source.roughness_expression_root >= 0) {
        resolved.material.roughness = fminf(1.0f, fmaxf(0.001f, material_expression_scalar(
            scene, parent, source.roughness_expression_root, uv, wavelengths)));
    }
    if (source.metal_eta_expression_root >= 0) {
        resolved.spectra.metal_eta = eval_material_expression(
            scene, parent, source.metal_eta_expression_root, uv.u, uv.v, wavelengths, scene.num_spectral_channels);
    }
    if (source.extinction_expression_root >= 0) {
        resolved.spectra.extinction = eval_material_expression(
            scene, parent, source.extinction_expression_root, uv.u, uv.v, wavelengths, scene.num_spectral_channels);
    }
    resolved.dielectric_ior = SpectralPacket(source.ior);
    for (int c = 0; c < scene.num_spectral_channels; ++c) {
        resolved.dielectric_ior.wavelengths[c] = wavelengths[c];
    }
    if (source.ior_expression_root >= 0) {
        resolved.dielectric_ior = eval_material_expression(
            scene, parent, source.ior_expression_root, uv.u, uv.v, wavelengths, scene.num_spectral_channels);
    }
    return resolved;
}

static __device__ float composite_material_mix_factor(const GpuScene& scene,
                                               const GpuMaterial& material,
                                               const GpuVec2& uv,
                                               const float* wavelengths) {
    return fminf(1.0f, fmaxf(0.0f, material_expression_scalar(
        scene, material, material.bsdf_mix_expression_root, uv, wavelengths)));
}

struct ResolvedLayeredMaterial {
    ResolvedMaterialBsdfLobe coating;
    ResolvedMaterialBsdfLobe substrate;
    SpectralPacket absorption;
    float thickness = 0.0f;
};

static __device__ ResolvedLayeredMaterial resolve_layered_material(
    const GpuScene& scene,
    const GpuMaterial& parent,
    const GpuVec2& uv,
    const float* wavelengths
) {
    ResolvedLayeredMaterial layer = {};
    layer.coating = resolve_material_bsdf_lobe(scene, parent, 0, uv, wavelengths);
    layer.substrate = resolve_material_bsdf_lobe(scene, parent, 1, uv, wavelengths);
    if (parent.layer_thickness_expression_root >= 0) {
        layer.thickness = fmaxf(0.0f, material_expression_scalar(
            scene, parent, parent.layer_thickness_expression_root, uv, wavelengths));
    }
    if (parent.layer_absorption_expression_root >= 0) {
        layer.absorption = eval_material_expression(
            scene, parent, parent.layer_absorption_expression_root,
            uv.u, uv.v, wavelengths, scene.num_spectral_channels);
    } else {
        layer.absorption = SpectralPacket(0.0f);
    }
    return layer;
}

static __device__ bool refract_smooth(const GpuVec3& wi, const GpuVec3& n, float eta_i, float eta_t, GpuVec3& wt) {
    float cos_i = fminf(1.0f, fmaxf(0.0f, (-wi).dot(n)));
    float eta = eta_i / eta_t;
    GpuVec3 perp = eta * (wi + cos_i * n);
    float sin_t2 = perp.length_sq();
    if (sin_t2 >= 1.0f) return false;
    GpuVec3 para = -sqrtf(fmaxf(0.0f, 1.0f - sin_t2)) * n;
    wt = (perp + para).normalize();
    return true;
}

static __device__ SpectralPacket layered_substrate_transmittance(
    const ResolvedLayeredMaterial& layer,
    const GpuVec3& n,
    const GpuVec3& wo,
    const GpuVec3& wi,
    const float* wavelengths,
    int num_spec
) {
    SpectralPacket result(0.0f);
    for (int c = 0; c < num_spec; ++c) {
        result.wavelengths[c] = wavelengths[c];
        float eta = layer.coating.dielectric_ior.values[c];
        if (!isfinite(eta) || eta <= 0.0f) continue;
        GpuVec3 wo_inside;
        GpuVec3 wi_inside;
        if (!refract_smooth(-wo, n, 1.0f, eta, wo_inside)) continue;
        if (!refract_smooth(-wi, n, 1.0f, eta, wi_inside)) continue;
        float cos_o = fmaxf(1e-6f, fabsf(wo_inside.dot(n)));
        float cos_i = fmaxf(1e-6f, fabsf(wi_inside.dot(n)));
        DielectricSurfaceBoundary in_boundary = eval_dielectric_surface_boundary(
            wavelengths[c], 0.0f, 1.0f, 1.0f, eta, fmaxf(0.0f, wo.dot(n)));
        DielectricSurfaceBoundary out_boundary = eval_dielectric_surface_boundary(
            wavelengths[c], 0.0f, eta, 1.0f, 1.0f, fmaxf(0.0f, (-wi_inside).dot(n)));
        if (in_boundary.tir || out_boundary.tir) continue;
        float optical_path = layer.thickness * (1.0f / cos_o + 1.0f / cos_i);
        float absorb = expf(-fmaxf(0.0f, layer.absorption.values[c]) * optical_path);
        float t_in = 0.5f * (in_boundary.Ts + in_boundary.Tp);
        float t_out = 0.5f * (out_boundary.Ts + out_boundary.Tp) *
            select_boundary_transport_scale(
                out_boundary.radiance_scale,
                out_boundary.importance_scale,
                BoundaryTransportMode::Radiance);
        result.values[c] = fmaxf(0.0f, t_in * t_out * absorb);
    }
    return result;
}

static __device__ SpectralPacket eval_layered_bsdf(
    const ResolvedLayeredMaterial& layer,
    const GpuVec3& p,
    const GpuVec3& n,
    const GpuVec2& uv,
    const GpuVec3& wo,
    const GpuVec3& wi,
    const float* wavelengths,
    int num_spec
) {
    if (layer.substrate.material.type != MaterialType::Lambertian) return SpectralPacket(0.0f);
    if (n.dot(wo) <= 0.0f || n.dot(wi) <= 0.0f) return SpectralPacket(0.0f);
    SpectralPacket substrate = eval_bsdf(
        layer.substrate.material,
        layer.substrate.spectra.albedo,
        layer.substrate.spectra.extinction,
        layer.substrate.spectra.metal_eta,
        layer.substrate.dielectric_ior,
        p,
        n,
        uv,
        wo,
        wi,
        wavelengths,
        num_spec);
    return substrate * layered_substrate_transmittance(layer, n, wo, wi, wavelengths, num_spec);
}

static __device__ SpectralPacket pdf_layered_bsdf_spectral(
    const ResolvedLayeredMaterial& layer,
    const GpuVec3& n,
    const GpuVec2& uv,
    const GpuVec3& wo,
    const GpuVec3& wi,
    const float* wavelengths,
    int num_spec,
    float dispersion_clamp
) {
    if (layer.substrate.material.type != MaterialType::Lambertian) return SpectralPacket(0.0f);
    return pdf_bsdf_spectral(
        layer.substrate.material,
        layer.substrate.dielectric_ior,
        n,
        uv,
        wo,
        wi,
        wavelengths,
        num_spec,
        dispersion_clamp);
}

#ifndef URE_MATERIAL_TARGET_ONLY
static __device__ bool scatter_layered_material(
    const ResolvedLayeredMaterial& layer,
    const GpuRay& r_in,
    const GpuVec3& p,
    const GpuVec3& n,
    const GpuVec2& uv,
    const SpectralPacket& throughput,
    SpectralPacket& attenuation,
    GpuRay& scattered,
    StokesVector& stokes,
    float& out_pdf,
    int sample_index,
    int pixel_index,
    int depth,
    int num_spec,
    const RayQueue* sampling_queue = nullptr
) {
    if (layer.coating.material.type != MaterialType::Dielectric ||
        layer.substrate.material.type != MaterialType::Lambertian) return false;

    float r0 = sampling_queue
        ? sample_path_dimension(*sampling_queue, sample_index, pixel_index, depth, kPathDimBsdf0)
        : sample_path_dimension(sample_index, pixel_index, depth, kPathDimBsdf0);
    float r1 = sampling_queue
        ? sample_path_dimension(*sampling_queue, sample_index, pixel_index, depth, kPathDimBsdf1)
        : sample_path_dimension(sample_index, pixel_index, depth, kPathDimBsdf1);
    float r2 = sampling_queue
        ? sample_path_dimension(*sampling_queue, sample_index, pixel_index, depth, kPathDimBsdf2)
        : sample_path_dimension(sample_index, pixel_index, depth, kPathDimBsdf2);
    GpuVec3 unit_direction = r_in.direction.normalize();
    if (unit_direction.dot(n) >= 0.0f) return false;
    float cos_i = fminf(1.0f, fmaxf(0.0f, (-unit_direction).dot(n)));

    float reflect_prob = 0.0f;
    float reflectance[kMaxPacketLanes];
    for (int c = 0; c < num_spec; ++c) {
        float eta = layer.coating.dielectric_ior.values[c];
        if (!isfinite(eta) || eta <= 0.0f) return false;
        DielectricSurfaceBoundary boundary = eval_dielectric_surface_boundary(
            throughput.wavelengths[c], 0.0f, 1.0f, 1.0f, eta, cos_i);
        reflectance[c] = boundary.tir ? 1.0f : 0.5f * (boundary.Rs + boundary.Rp);
        reflect_prob += reflectance[c];
    }
    reflect_prob = fminf(1.0f, fmaxf(0.0f, reflect_prob / fmaxf(1.0f, float(num_spec))));

    if (r2 < reflect_prob) {
        scattered.direction = reflect(unit_direction, n).normalize();
        scattered.origin = p + n * 1e-4f;
        scattered.t_min = 1e-4f;
        scattered.t_max = FLT_MAX;
        float pdf = fmaxf(1e-6f, reflect_prob);
        for (int c = 0; c < num_spec; ++c) {
            attenuation.values[c] = reflectance[c] / pdf;
            attenuation.wavelengths[c] = throughput.wavelengths[c];
        }
        rotate_stokes_into_boundary_frame(stokes, r_in.direction, n);
        int channel = min(max(num_spec / 2, 0), num_spec - 1);
        float eta = layer.coating.dielectric_ior.values[channel];
        DielectricSurfaceBoundary boundary = eval_dielectric_surface_boundary(
            throughput.wavelengths[channel], 0.0f, 1.0f, 1.0f, eta, cos_i);
        apply_mueller_reflection_boundary(stokes, boundary.rs, boundary.rp, boundary.Rs, boundary.Rp);
        stokes = stokes * (1.0f / pdf);
        rotate_stokes_from_boundary_frame(stokes, scattered.direction, n);
        out_pdf = 0.0f;
        return true;
    }

    GpuVec3 sampled = n + sample_unit_vector_lds(r0, r1);
    if (sampled.length_sq() < 1e-16f) sampled = n;
    scattered.direction = sampled.normalize();
    scattered.origin = p + n * 1e-4f;
    scattered.t_min = 1e-4f;
    scattered.t_max = FLT_MAX;

    SpectralPacket trans = layered_substrate_transmittance(
        layer, n, -unit_direction, scattered.direction, throughput.wavelengths, num_spec);
    float transmit_prob = fmaxf(1e-6f, 1.0f - reflect_prob);
    for (int c = 0; c < num_spec; ++c) {
        attenuation.values[c] = layer.substrate.spectra.albedo.values[c] * trans.values[c] / transmit_prob;
        attenuation.wavelengths[c] = throughput.wavelengths[c];
    }
    stokes.Q = 0.0f;
    stokes.U = 0.0f;
    stokes.V = 0.0f;
    out_pdf = transmit_prob * fmaxf(1e-6f, scattered.direction.dot(n)) * 0.318309886f;
    return true;
}
#endif
