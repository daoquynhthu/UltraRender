#pragma once

#include "path_tracer_decl.cuh"
#include "path_tracer_intersect.cuh"
#include "path_tracer_polarization.cuh"
#include "path_tracer_bsdf.cuh"
#include "path_tracer_volume.cuh"
#include "ure/gpu_material_helpers.cuh"

__global__ __launch_bounds__(256) void extend_kernel(
    RayQueue ray_queue,
    HitQueue hit_queue,
    GpuScene scene
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int ray_count_local = *ray_queue.count;
    if (idx >= ray_count_local) return;

    GpuRay r;
    r.origin = ray_queue.origins[idx];
    r.direction = ray_queue.directions[idx];

    float t_min = 1e-4f;

    float t;
    GpuVec3 p, n, ng;
    GpuVec2 uv;
    int mat_idx;
    int hit_type;
    int hit_index;
    int hit_primitive_index;

    if (world_hit(scene, r, t_min, FLT_MAX, t, p, n, ng, uv, mat_idx, hit_type, hit_index, hit_primitive_index)) {
        hit_queue.t[idx] = t;
        hit_queue.p[idx] = p;
        hit_queue.n[idx] = n;
        hit_queue.ng[idx] = ng;
        hit_queue.uv[idx] = uv;
        hit_queue.mat_ids[idx] = mat_idx;
        hit_queue.hit_types[idx] = hit_type;
        hit_queue.hit_indices[idx] = hit_index;
        hit_queue.hit_primitive_indices[idx] = hit_primitive_index;
    } else {
        hit_queue.mat_ids[idx] = -1;
    }
}

__device__ inline GpuVec2 project_camera_screen(const GpuCamera& camera, const GpuVec3& p) {
    GpuVec3 h = camera.horizontal;
    GpuVec3 v = camera.vertical;
    GpuVec3 plane_origin = camera.lower_left_corner;
    GpuVec3 plane_normal = h.cross(v);
    GpuVec3 ray = p - camera.origin;
    float denom = ray.dot(plane_normal);
    float numer = (plane_origin - camera.origin).dot(plane_normal);
    if (fabsf(denom) < 1e-8f) {
        return GpuVec2(0.0f, 0.0f);
    }
    GpuVec3 q = camera.origin + ray * (numer / denom);
    GpuVec3 rel = q - plane_origin;
    float h_len_sq = fmaxf(1e-8f, h.dot(h));
    float v_len_sq = fmaxf(1e-8f, v.dot(v));
    return GpuVec2(rel.dot(h) / h_len_sq, rel.dot(v) / v_len_sq);
}

__device__ float sample_spectral_texture_resource(const GpuTexture& tex, int texel_index, float lambda) {
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

__device__ SpectralPacket sample_texture(const GpuScene& scene, int tex_idx, float u, float v, const float* wavelengths, int num_spec) {
    if (tex_idx < 0 || tex_idx >= scene.texture_count) return rgb_to_spectrum(GpuVec3(1,0,1), wavelengths, num_spec);

    GpuTexture tex = scene.textures[tex_idx];

    if (tex.texObj) {
        float4 val = tex2D<float4>(tex.texObj, u, v);
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

__device__ SpectralPacket sample_expression_texture(const GpuScene& scene,
                                                    int tex_idx,
                                                    SpectralExpressionSemantic semantic,
                                                    float u,
                                                    float v,
                                                    const float* wavelengths,
                                                    int num_spec) {
    if (tex_idx < 0 || tex_idx >= scene.texture_count) return SpectralPacket(0.0f);
    const GpuTexture tex = scene.textures[tex_idx];
    if (!tex.texObj || tex.spectral_kind == SpectralTextureResourceKind::SourceSampleGrid) {
        return sample_texture(scene, tex_idx, u, v, wavelengths, num_spec);
    }
    const float4 value = tex2D<float4>(tex.texObj, u, v);
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

__device__ SpectralPacket eval_material_expression(
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

__device__ inline void rotate_stokes_into_boundary_frame(StokesVector& s, const GpuVec3& ray_dir, const GpuVec3& boundary_normal);
__device__ inline void rotate_stokes_from_boundary_frame(StokesVector& s, const GpuVec3& ray_dir, const GpuVec3& boundary_normal);

__device__ float material_expression_scalar(const GpuScene& scene,
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

__device__ ResolvedMaterialBsdfLobe resolve_material_bsdf_lobe(
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

__device__ float composite_material_mix_factor(const GpuScene& scene,
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

__device__ ResolvedLayeredMaterial resolve_layered_material(
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

__device__ bool refract_smooth(const GpuVec3& wi, const GpuVec3& n, float eta_i, float eta_t, GpuVec3& wt) {
    float cos_i = fminf(1.0f, fmaxf(0.0f, (-wi).dot(n)));
    float eta = eta_i / eta_t;
    GpuVec3 perp = eta * (wi + cos_i * n);
    float sin_t2 = perp.length_sq();
    if (sin_t2 >= 1.0f) return false;
    GpuVec3 para = -sqrtf(fmaxf(0.0f, 1.0f - sin_t2)) * n;
    wt = (perp + para).normalize();
    return true;
}

__device__ SpectralPacket layered_substrate_transmittance(
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

__device__ SpectralPacket eval_layered_bsdf(
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

__device__ SpectralPacket pdf_layered_bsdf_spectral(
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

__device__ bool scatter_layered_material(
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
    int num_spec
) {
    if (layer.coating.material.type != MaterialType::Dielectric ||
        layer.substrate.material.type != MaterialType::Lambertian) return false;

    float r0 = sample_path_dimension(sample_index, pixel_index, depth, kPathDimBsdf0);
    float r1 = sample_path_dimension(sample_index, pixel_index, depth, kPathDimBsdf1);
    float r2 = sample_path_dimension(sample_index, pixel_index, depth, kPathDimBsdf2);
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

__device__ bool any_hit_bvh(const GpuMesh& mesh, const GpuRay& r, float t_min, float t_max) {
    int stack[64];
    int stack_ptr = 0;
    stack[stack_ptr++] = 0;

    while (stack_ptr > 0) {
        int node_idx = stack[--stack_ptr];
        const GpuBvhNode& node = mesh.bvh_nodes[node_idx];

        if (!hit_aabb(r, node.min_pt, node.max_pt, t_min, t_max)) {
            continue;
        }

        if (node.primitive_count > 0) {
            int start_idx = node.child_or_primitive_index;
            int end_idx = start_idx + node.primitive_count;

            for (int i = start_idx; i < end_idx; ++i) {
                int i0 = mesh.indices[i * 3 + 0];
                int i1 = mesh.indices[i * 3 + 1];
                int i2 = mesh.indices[i * 3 + 2];

                GpuVec3 v0 = mesh.vertices[i0];
                GpuVec3 v1 = mesh.vertices[i1];
                GpuVec3 v2 = mesh.vertices[i2];

                const GpuVec3* n0_ptr = nullptr;
                const GpuVec3* n1_ptr = nullptr;
                const GpuVec3* n2_ptr = nullptr;

                float t_tri;
                GpuVec3 ng_tri, ns_tri;
                float u_tri, v_tri;

                float local_max = t_max;

                if (hit_triangle(r, v0, v1, v2, n0_ptr, n1_ptr, n2_ptr, t_min, local_max, t_tri, ng_tri, ns_tri, u_tri, v_tri)) {
                    return true;
                }
            }
        } else {
            int left_child = node_idx + 1;
            int right_child = node.child_or_primitive_index;

            if (stack_ptr < 64) {
                stack[stack_ptr++] = right_child;
                stack[stack_ptr++] = left_child;
            }
        }
    }
    return false;
}

__device__ bool any_hit(const GpuScene& scene, const GpuRay& r, float t_min, float t_max) {
    for (int i = 0; i < scene.sphere_count; ++i) {
        GpuVec3 oc = r.origin - scene.spheres[i].center;
        float a = r.direction.dot(r.direction);
        float b = oc.dot(r.direction);
        float c = oc.dot(oc) - scene.spheres[i].radius * scene.spheres[i].radius;
        float discriminant = b * b - a * c;

        if (discriminant > 0) {
            float temp = (-b - sqrtf(discriminant)) / a;
            if (temp < t_max && temp > t_min) return true;
            temp = (-b + sqrtf(discriminant)) / a;
            if (temp < t_max && temp > t_min) return true;
        }
    }

    for (int i = 0; i < scene.mesh_count; ++i) {
        GpuMesh& mesh = scene.meshes[i];

        if (!hit_aabb(r, mesh.min_pt, mesh.max_pt, t_min, t_max)) {
            continue;
        }

        if (mesh.bvh_node_count > 0) {
            if (any_hit_bvh(mesh, r, t_min, t_max)) return true;
        } else {
            for (int j = 0; j < mesh.triangle_count; ++j) {
                int i0 = mesh.indices[j * 3 + 0];
                int i1 = mesh.indices[j * 3 + 1];
                int i2 = mesh.indices[j * 3 + 2];

                GpuVec3 v0 = mesh.vertices[i0];
                GpuVec3 v1 = mesh.vertices[i1];
                GpuVec3 v2 = mesh.vertices[i2];

                GpuVec3 v0v1 = v1 - v0;
                GpuVec3 v0v2 = v2 - v0;
                GpuVec3 pvec = r.direction.cross(v0v2);
                float det = v0v1.dot(pvec);

                if (fabsf(det) < 1e-8f) continue;

                float invDet = 1.0f / det;
                GpuVec3 tvec = r.origin - v0;
                float u = tvec.dot(pvec) * invDet;

                if (u < 0.0f || u > 1.0f) continue;

                GpuVec3 qvec = tvec.cross(v0v1);
                float v = r.direction.dot(qvec) * invDet;

                if (v < 0.0f || u + v > 1.0f) continue;

                float t = v0v2.dot(qvec) * invDet;

                if (t < t_max && t > t_min) return true;
            }
        }
    }
    return false;
}

__device__ inline int reserve_ray_slot(RayQueue& q) {
    int out_idx = atomicAdd(q.count, 1);
    if (out_idx >= q.capacity) {
        atomicMin(q.count, q.capacity);
        if (q.overflow_count) {
            atomicAdd(q.overflow_count, 1);
        }
        DEVICE_LOG(5101, q.capacity, (unsigned long long)q.count, 0ULL, 0.0f);
        return -1;
    }
    return out_idx;
}

__device__ inline int reserve_shadow_slot(ShadowQueue& q) {
    int out_idx = atomicAdd(q.count, 1);
    if (out_idx >= q.capacity) {
        atomicMin(q.count, q.capacity);
        if (q.overflow_count) {
            atomicAdd(q.overflow_count, 1);
        }
        DEVICE_LOG(5102, q.capacity, (unsigned long long)q.count, 0ULL, 0.0f);
        return -1;
    }
    return out_idx;
}

__device__ inline void store_lane_throughput(RayQueue& q, int idx, const SpectralPacket& source, int channel, float value) {
    SpectralPacket t;
    for (int c = 0; c < q.num_spectral_channels; ++c) {
        t.values[c] = (c == channel) ? value : 0.0f;
        t.wavelengths[c] = source.wavelengths[c];
    }
    store_throughput(q, idx, t);
}

__device__ inline int next_dielectric_medium_index(
    int current_medium_idx,
    int material_idx,
    const GpuVec3& in_direction,
    const GpuVec3& out_direction,
    const GpuVec3& geometric_normal
) {
    if (in_direction.dot(geometric_normal) * out_direction.dot(geometric_normal) <= 0.0f) {
        return current_medium_idx;
    }
    if (current_medium_idx == -1) {
        return material_idx;
    }
    if (current_medium_idx == material_idx) {
        return -1;
    }
    return material_idx;
}

__device__ inline float sphere_light_solid_angle_pdf(
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

__device__ inline float sphere_light_solid_angle_pdf_only(
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

__device__ inline float light_selection_pdf(const GpuScene& scene, int light_list_index) {
    if (scene.light_count <= 0 || light_list_index < 0 || light_list_index >= scene.light_count) return 0.0f;
    if (scene.light_selection_pmf) return fmaxf(0.0f, scene.light_selection_pmf[light_list_index]);
    if (!scene.light_selection_cdf) return 1.0f / float(scene.light_count);
    float upper = scene.light_selection_cdf[light_list_index];
    float lower = light_list_index == 0 ? 0.0f : scene.light_selection_cdf[light_list_index - 1];
    return fmaxf(0.0f, upper - lower);
}

__device__ inline float light_tree_node_importance(
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

__device__ inline float light_selection_pdf_at(
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

__device__ inline float path_guiding_light_total(const GpuScene& scene) {
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

__device__ inline void path_guiding_grid_dimensions(int cell_count, int& nx, int& ny, int& nz) {
    nx = 1;
    while ((nx + 1) * (nx + 1) * (nx + 1) <= cell_count) ++nx;
    const int remaining = (cell_count + nx - 1) / nx;
    ny = 1;
    while ((ny + 1) * (ny + 1) <= remaining) ++ny;
    nz = (cell_count + nx * ny - 1) / (nx * ny);
}

__device__ inline int path_guiding_spatial_cell(const GpuScene& scene, const GpuVec3& p) {
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

__device__ inline int path_guiding_direction_bin(const GpuScene& scene, const GpuVec3& direction) {
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

__device__ inline int path_guiding_spatial_directional_index(
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

__device__ inline float path_guiding_spatial_directional_light_weight(
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

__device__ inline float path_guiding_spatial_directional_total(const GpuScene& scene, const GpuVec3& reference_point) {
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

__device__ inline float path_guiding_effective_mixture(const GpuScene& scene, float guide_total) {
    if (guide_total <= fmaxf(scene.path_guiding_min_weight, 1e-12f)) return 0.0f;
    return fminf(0.95f, fmaxf(0.0f, scene.path_guiding_light_mixture));
}

struct PathGuidingProductMetadata {
    float luminance = 0.0f;
    float wavelength_nm = 0.0f;
};

__device__ inline PathGuidingProductMetadata path_guiding_product_metadata(
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

__global__ void decay_path_guiding_weights_kernel(float* weights, size_t count, float decay) {
    const size_t index = static_cast<size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (index >= count) return;
    weights[index] *= decay;
}

__device__ inline float guided_light_selection_pdf(const GpuScene& scene, int light_list_index, float guide_total) {
    if (!scene.path_guiding_light_weights ||
        scene.path_guiding_light_count != scene.light_count ||
        light_list_index < 0 ||
        light_list_index >= scene.light_count ||
        guide_total <= fmaxf(scene.path_guiding_min_weight, 1e-12f)) {
        return 0.0f;
    }
    return fmaxf(0.0f, scene.path_guiding_light_weights[light_list_index]) / guide_total;
}

__device__ inline int sample_base_light_list_index(const GpuScene& scene, float r) {
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

__device__ inline int sample_base_light_list_index_at(
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

__device__ inline int sample_guided_light_list_index(const GpuScene& scene, float r, float guide_total) {
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

__device__ inline int sample_guided_light_list_index_at(
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

__device__ inline int sample_light_list_index(const GpuScene& scene, float r) {
    if (scene.light_count <= 0) return -1;
    float guide_total = path_guiding_light_total(scene);
    float mixture = path_guiding_effective_mixture(scene, guide_total);
    if (mixture > 0.0f && r < mixture) {
        return sample_guided_light_list_index(scene, r / mixture, guide_total);
    }
    float base_u = mixture < 1.0f ? (r - mixture) / fmaxf(1e-6f, 1.0f - mixture) : r;
    return sample_base_light_list_index(scene, base_u);
}

__device__ inline int sample_light_list_index_at(
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

__device__ inline float guided_mixture_light_selection_pdf(const GpuScene& scene, int light_list_index) {
    const float base_pdf = light_selection_pdf(scene, light_list_index);
    const float guide_total = path_guiding_light_total(scene);
    const float mixture = path_guiding_effective_mixture(scene, guide_total);
    if (mixture <= 0.0f) return base_pdf;
    const float guide_pdf = guided_light_selection_pdf(scene, light_list_index, guide_total);
    return (1.0f - mixture) * base_pdf + mixture * guide_pdf;
}

__device__ inline float guided_mixture_light_selection_pdf_at(
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

__device__ inline float selected_sphere_light_pdf(
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

__device__ inline GpuVec3 environment_sky_color(
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

__device__ inline SpectralPacket environment_radiance_spectrum(
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

__device__ inline GpuLightRecord get_light_record(const GpuScene& scene, int light_list_index) {
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

__device__ inline bool light_triangle_vertices(
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

__device__ inline float selected_triangle_light_pdf(
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

__device__ inline float selected_light_hit_pdf(
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

__device__ inline bool sample_selected_light(
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

__device__ inline bool reconstruct_restir_di_light_sample(
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

__device__ inline float selected_environment_light_pdf(const GpuScene& scene, const GpuVec3& reference_point) {
    if (!scene.environment_light_direct_sampling) return 0.0f;
    float selection_pdf = 0.0f;
    for (int i = 0; i < scene.light_count; ++i) {
        const GpuLightRecord record = get_light_record(scene, i);
        if (record.kind == GpuLightKind::Environment) {
            selection_pdf += guided_mixture_light_selection_pdf_at(scene, i, reference_point);
        }
    }
    return selection_pdf * (1.0f / 12.566370614359172f);
}

__device__ inline float rgb_luminance(const GpuVec3& rgb) {
    return fmaxf(0.0f, 0.2126f * rgb.x + 0.7152f * rgb.y + 0.0722f * rgb.z);
}

__device__ inline bool restir_di_ready(const GpuScene& scene, int pixel_index) {
    return scene.restir_di_enabled &&
           scene.restir_di_temporal_reuse &&
           !scene.restir_di_unbiased &&
           scene.restir_di_valid &&
           scene.restir_di_origins &&
           scene.restir_di_directions &&
           scene.restir_di_max_dist &&
           scene.restir_di_radiance_vals &&
           scene.restir_di_radiance_wavelengths &&
           scene.restir_di_lobe_pdfs &&
           scene.restir_di_wavelength_pdfs &&
           scene.restir_di_light_list_indices &&
           scene.restir_di_spectral_modes &&
           scene.restir_di_active_channels &&
           scene.restir_di_stokes_i &&
           scene.restir_di_stokes_q &&
           scene.restir_di_stokes_u &&
           scene.restir_di_stokes_v &&
           pixel_index >= 0 &&
           pixel_index < scene.restir_di_pixel_count &&
           scene.restir_di_valid[pixel_index] != 0;
}

__device__ inline void enqueue_restir_di_temporal_replay(
    const GpuScene& scene,
    ShadowQueue& shadow_queue,
    int pixel_index
) {
    if (!restir_di_ready(scene, pixel_index)) return;
    int s_idx = reserve_shadow_slot(shadow_queue);
    if (s_idx < 0) return;
    const int cap = shadow_queue.capacity;
    const int history = scene.restir_di_history_lengths && scene.restir_di_history_lengths[pixel_index] > 0
        ? scene.restir_di_history_lengths[pixel_index]
        : 1;
    const float replay_weight = 1.0f / float(history + 1);
    shadow_queue.origins[s_idx] = scene.restir_di_origins[pixel_index];
    shadow_queue.directions[s_idx] = scene.restir_di_directions[pixel_index];
    shadow_queue.max_dist[s_idx] = scene.restir_di_max_dist[pixel_index];
    for (int c = 0; c < scene.num_spectral_channels; ++c) {
        const int src = c * scene.restir_di_pixel_count + pixel_index;
        shadow_queue.radiance_vals[c * cap + s_idx] = scene.restir_di_radiance_vals[src] * replay_weight;
        shadow_queue.radiance_wavelengths[c * cap + s_idx] = scene.restir_di_radiance_wavelengths[src];
    }
    shadow_queue.pixel_indices[s_idx] = pixel_index;
    shadow_queue.spectral_modes[s_idx] = scene.restir_di_spectral_modes[pixel_index];
    shadow_queue.active_channels[s_idx] = scene.restir_di_active_channels[pixel_index];
    shadow_queue.wavelength_pdfs[s_idx] = scene.restir_di_wavelength_pdfs[pixel_index];
    shadow_queue.light_list_indices[s_idx] = scene.restir_di_light_list_indices[pixel_index];
    shadow_queue.bsdf_lobe_pdfs[s_idx] = scene.restir_di_lobe_pdfs[pixel_index];
    shadow_queue.guiding_product_luminance[s_idx] = 0.0f;
    shadow_queue.guiding_wavelength_nm[s_idx] = 0.0f;
    shadow_queue.guiding_epochs[s_idx] = scene.path_guiding_epoch;
    shadow_queue.stokes_i[s_idx] = scene.restir_di_stokes_i[pixel_index];
    shadow_queue.stokes_q[s_idx] = scene.restir_di_stokes_q[pixel_index];
    shadow_queue.stokes_u[s_idx] = scene.restir_di_stokes_u[pixel_index];
    shadow_queue.stokes_v[s_idx] = scene.restir_di_stokes_v[pixel_index];
    shadow_queue.restir_replay_flags[s_idx] = 1;
}

__device__ inline void store_restir_di_visible_candidate(
    const GpuScene& scene,
    const ShadowQueue& shadow_queue,
    int shadow_index,
    const GpuVec3& rgb
) {
    if (!scene.restir_di_enabled || scene.restir_di_unbiased) return;
    if (!scene.restir_di_valid || !scene.restir_di_radiance_vals ||
        !scene.restir_di_stokes_i || !scene.restir_di_stokes_q ||
        !scene.restir_di_stokes_u || !scene.restir_di_stokes_v) return;
    const int pixel_index = shadow_queue.pixel_indices[shadow_index];
    if (pixel_index < 0 || pixel_index >= scene.restir_di_pixel_count) return;
    const float target = rgb_luminance(rgb);
    if (!isfinite(target) || target <= scene.restir_di_min_target) return;
    const int cap = shadow_queue.capacity;
    scene.restir_di_origins[pixel_index] = shadow_queue.origins[shadow_index];
    scene.restir_di_directions[pixel_index] = shadow_queue.directions[shadow_index];
    scene.restir_di_max_dist[pixel_index] = shadow_queue.max_dist[shadow_index];
    for (int c = 0; c < scene.num_spectral_channels; ++c) {
        const int dst = c * scene.restir_di_pixel_count + pixel_index;
        scene.restir_di_radiance_vals[dst] = shadow_queue.radiance_vals[c * cap + shadow_index];
        scene.restir_di_radiance_wavelengths[dst] = shadow_queue.radiance_wavelengths[c * cap + shadow_index];
    }
    scene.restir_di_target_luminance[pixel_index] = target;
    scene.restir_di_lobe_pdfs[pixel_index] = shadow_queue.bsdf_lobe_pdfs[shadow_index];
    scene.restir_di_wavelength_pdfs[pixel_index] = shadow_queue.wavelength_pdfs[shadow_index];
    scene.restir_di_stokes_i[pixel_index] = shadow_queue.stokes_i[shadow_index];
    scene.restir_di_stokes_q[pixel_index] = shadow_queue.stokes_q[shadow_index];
    scene.restir_di_stokes_u[pixel_index] = shadow_queue.stokes_u[shadow_index];
    scene.restir_di_stokes_v[pixel_index] = shadow_queue.stokes_v[shadow_index];
    scene.restir_di_light_list_indices[pixel_index] = shadow_queue.light_list_indices[shadow_index];
    scene.restir_di_spectral_modes[pixel_index] = shadow_queue.spectral_modes[shadow_index];
    scene.restir_di_active_channels[pixel_index] = shadow_queue.active_channels[shadow_index];
    const int old_history = scene.restir_di_history_lengths[pixel_index];
    const int max_history = scene.restir_di_max_history > 0 ? scene.restir_di_max_history : 1;
    const int next_history = old_history + 1 < max_history ? old_history + 1 : max_history;
    scene.restir_di_history_lengths[pixel_index] = next_history > 0 ? next_history : 1;
    scene.restir_di_valid[pixel_index] = 1;
}

__device__ inline bool direct_light_direction_allowed(
    const GpuMaterial& mat,
    const GpuVec3& n,
    const GpuVec3& ng,
    const GpuVec3& wi
) {
    if (is_rough_dielectric_bsdf(mat)) {
        return fabsf(n.dot(wi)) > 1e-6f && fabsf(ng.dot(wi)) > 1e-6f;
    }
    return n.dot(wi) > 0.0f && ng.dot(wi) > 0.0f;
}

__device__ inline float direct_light_cosine_factor(
    const GpuMaterial& mat,
    const GpuVec3& n,
    const GpuVec3& wi
) {
    return is_rough_dielectric_bsdf(mat) ? fabsf(n.dot(wi)) : fmaxf(0.0f, n.dot(wi));
}

__device__ inline GpuVec3 direct_light_offset_normal(const GpuVec3& ng, const GpuVec3& wi) {
    return ng.dot(wi) >= 0.0f ? ng : -ng;
}

__device__ inline StokesVector load_packet_average_stokes(const RayQueue& q, int idx) {
    StokesVector s(0.0f, 0.0f, 0.0f, 0.0f);
    for (int c = 0; c < q.num_spectral_channels; ++c) {
        StokesVector lane = load_stokes(q, idx, c);
        s.I += lane.I;
        s.Q += lane.Q;
        s.U += lane.U;
        s.V += lane.V;
    }
    float inv_n = 1.0f / fmaxf(1.0f, float(q.num_spectral_channels));
    return s * inv_n;
}

__device__ inline void rotate_stokes_into_boundary_frame(StokesVector& s, const GpuVec3& ray_dir, const GpuVec3& boundary_normal) {
    GpuVec3 ref_in = get_reference_frame(ray_dir);
    GpuVec3 raw_s = ray_dir.cross(boundary_normal);
    float raw_len_sq = raw_s.length_sq();
    GpuVec3 s_axis = raw_len_sq < 1e-12f
        ? get_reference_frame(boundary_normal)
        : raw_s * (1.0f / sqrtf(raw_len_sq));
    float cos_phi = ref_in.dot(s_axis);
    float sin_phi = ref_in.cross(s_axis).dot(ray_dir);
    rotate_stokes(s, 2.0f * atan2f(sin_phi, cos_phi));
}

__device__ inline void rotate_stokes_from_boundary_frame(StokesVector& s, const GpuVec3& ray_dir, const GpuVec3& boundary_normal) {
    GpuVec3 ref_out = get_reference_frame(ray_dir);
    GpuVec3 raw_s = ray_dir.cross(boundary_normal);
    float raw_len_sq = raw_s.length_sq();
    GpuVec3 s_axis = raw_len_sq < 1e-12f
        ? get_reference_frame(boundary_normal)
        : raw_s * (1.0f / sqrtf(raw_len_sq));
    float cos_phi = s_axis.dot(ref_out);
    float sin_phi = s_axis.cross(ref_out).dot(ray_dir);
    rotate_stokes(s, 2.0f * atan2f(sin_phi, cos_phi));
}

__device__ inline void store_packet_scattered_stokes(
    const RayQueue& current_queue,
    RayQueue& next_queue,
    int in_idx,
    int out_idx,
    const GpuMaterial& mat,
    const GpuMaterialSoA& mat_soa,
    const SpectralPacket& dielectric_ior,
    const GpuRay& r_in,
    const GpuRay& scattered,
    const GpuVec3& n,
    const GpuVec2& uv,
    const SpectralPacket& throughput,
    float ior_outside,
    float dispersion_clamp,
    int sample_index,
    int pixel_index,
    int depth
) {
    if (mat.type == MaterialType::Lambertian || mat.type == MaterialType::Cloth) {
        for (int c = 0; c < current_queue.num_spectral_channels; ++c) {
            StokesVector s = load_stokes(current_queue, in_idx, c);
            s.Q = 0.0f;
            s.U = 0.0f;
            s.V = 0.0f;
            store_stokes(next_queue, out_idx, c, s);
        }
        return;
    }

    if (mat.type == MaterialType::Metal) {
        GpuVec3 V = (-r_in.direction).normalize();
        GpuVec3 L = scattered.direction.normalize();
        GpuVec3 N = n;
        if (V.dot(N) < 0.0f) N = -N;
        GpuVec3 H = (V + L);
        if (H.length_sq() < 1e-12f) {
            for (int c = 0; c < current_queue.num_spectral_channels; ++c) {
                store_stokes(next_queue, out_idx, c, load_stokes(current_queue, in_idx, c));
            }
            return;
        }
        H = H.normalize();
        float cos_theta_h = fmaxf(0.0f, V.dot(H));
        ConductorMaterialSemantics conductor = eval_conductor_material_semantics(
            mat_soa.metal_eta, mat_soa.extinction, current_queue.num_spectral_channels);
        float effective_thickness = mat.thin_film_thickness;
        if (effective_thickness > 0.0f) {
            effective_thickness = effective_thickness * (1.5f - uv.v);
        }

        for (int c = 0; c < current_queue.num_spectral_channels; ++c) {
            StokesVector s = load_stokes(current_queue, in_idx, c);
            rotate_stokes_into_boundary_frame(s, r_in.direction, H);
            if (!conductor.measured_conductor) {
                float eta_equiv = conductor_f0_eta_from_albedo(mat_soa.albedo.values[c]);
                if (effective_thickness > 0.0f) {
                    DielectricSurfaceBoundary surface = eval_dielectric_surface_boundary(
                        throughput.wavelengths[c], effective_thickness, 1.0f, mat.thin_film_ior, eta_equiv, cos_theta_h);
                    apply_mueller_reflection_boundary(s, surface.rs, surface.rp, surface.Rs, surface.Rp);
                } else {
                    float r = (1.0f - eta_equiv) / (1.0f + eta_equiv);
                    apply_mueller_reflection_boundary(s, c_make(r, 0.0f), c_make(-r, 0.0f), r * r, r * r);
                }
            } else {
                float eta_c = conductor_eta_for_channel(conductor, mat_soa.metal_eta, mat.ior, c);
                if (effective_thickness > 0.0f) {
                    ThinFilmBoundary film = eval_thin_film_conductor_boundary(
                        throughput.wavelengths[c], effective_thickness, 1.0f, mat.thin_film_ior, eta_c, mat_soa.extinction.values[c], cos_theta_h);
                    apply_mueller_reflection_boundary(s, film.rs, film.rp, film.Rs, film.Rp);
                } else {
                    ConductorBoundary boundary = eval_conductor_boundary(eta_c, mat_soa.extinction.values[c], cos_theta_h);
                    apply_mueller_reflection_boundary(s, boundary.rs, boundary.rp, boundary.Rs, boundary.Rp);
                }
            }
            rotate_stokes_from_boundary_frame(s, scattered.direction, H);
            store_stokes(next_queue, out_idx, c, s);
        }
        return;
    }

    if (mat.type == MaterialType::Dielectric) {
        float r_bsdf_1 = sample_path_dimension(sample_index, pixel_index, depth, kPathDimBsdf0);
        float r_bsdf_2 = sample_path_dimension(sample_index, pixel_index, depth, kPathDimBsdf1);
        GpuVec3 normal = r_in.direction.dot(n) < 0.0f ? n : -n;
        float jitter_scale = mat.roughness * 0.002f;
        if (jitter_scale > 0.0f) {
            normal = (normal + sample_unit_vector_lds(r_bsdf_1, r_bsdf_2) * jitter_scale).normalize();
        }
        GpuVec3 unit_direction = r_in.direction.normalize();
        GpuVec3 out_direction = scattered.direction.normalize();
        float cos_theta_i = fminf((-unit_direction).dot(normal), 1.0f);
        bool front_face = r_in.direction.dot(n) < 0.0f;
        bool is_reflection = unit_direction.dot(normal) * out_direction.dot(normal) < 0.0f;
        float effective_thickness = mat.thin_film_thickness;
        if (effective_thickness > 0.0f) {
            effective_thickness = effective_thickness * (1.5f - uv.v);
        }

        for (int c = 0; c < current_queue.num_spectral_channels; ++c) {
            float material_ior = mat.ior_expression_root != -1
                ? dielectric_ior.values[c]
                : dispersed_dielectric_ior(mat.ior, mat.dispersion, throughput.wavelengths[c], dispersion_clamp);
            float eta_i = front_face ? ior_outside : material_ior;
            float eta_t = front_face ? material_ior : ior_outside;
            DielectricSurfaceBoundary surface = eval_dielectric_surface_boundary(
                throughput.wavelengths[c], effective_thickness, eta_i, mat.thin_film_ior, eta_t, cos_theta_i);
            StokesVector s = load_stokes(current_queue, in_idx, c);
            rotate_stokes_into_boundary_frame(s, r_in.direction, normal);
            if (is_reflection || surface.tir) {
                apply_mueller_reflection_boundary(s, surface.rs, surface.rp, surface.Rs, surface.Rp);
            } else {
                apply_mueller_transmission_boundary(s, surface.ts, surface.tp, surface.Ts, surface.Tp, surface.eta_jacobian);
                s = s * surface.radiance_scale;
            }
            rotate_stokes_from_boundary_frame(s, scattered.direction, normal);
            store_stokes(next_queue, out_idx, c, s);
        }
        return;
    }

    for (int c = 0; c < current_queue.num_spectral_channels; ++c) {
        store_stokes(next_queue, out_idx, c, load_stokes(current_queue, in_idx, c));
    }
}

__device__ inline bool split_dispersive_dielectric_lanes(
    const RayQueue& current_queue,
    RayQueue& next_queue,
    int idx,
    const GpuMaterial& mat,
    const GpuMaterialSoA& mat_soa,
    const SpectralPacket& dielectric_ior,
    const GpuVec3& p,
    const GpuVec3& n,
    const GpuVec3& ng,
    const GpuVec2& uv,
    const SpectralPacket& throughput,
    int current_medium_idx,
    int mat_idx,
    int pixel_index,
    int depth,
    unsigned int seed,
    float dispersion_clamp,
    float ior_outside
) {
    if (mat.type != MaterialType::Dielectric) return false;
    if (is_rough_dielectric_bsdf(mat)) return false;
    if (spectral_mode_is_sampled(current_queue.spectral_modes[idx])) return false;

    float effective_thickness = mat.thin_film_thickness;
    if (effective_thickness > 0.0f) {
        effective_thickness = effective_thickness * (1.5f - uv.v);
    }

    if (mat.dispersion <= 0.0f && effective_thickness <= 0.0f && mat.ior_expression_root == -1) return false;

    GpuRay r_in;
    r_in.origin = current_queue.origins[idx];
    r_in.direction = current_queue.directions[idx];
    GpuVec3 unit_direction = r_in.direction.normalize();

    bool front_face = r_in.direction.dot(n) < 0.0f;
    GpuVec3 normal = front_face ? n : -n;
    float cos_theta_i = fminf((-unit_direction).dot(normal), 1.0f);

    for (int c = 0; c < current_queue.num_spectral_channels; ++c) {
        float lambda = throughput.wavelengths[c];
        float material_ior = mat.ior_expression_root != -1
            ? dielectric_ior.values[c]
            : dispersed_dielectric_ior(mat.ior, mat.dispersion, lambda, dispersion_clamp);
        float eta_i = front_face ? ior_outside : material_ior;
        float eta_t = front_face ? material_ior : ior_outside;

        StokesVector lane_stokes = load_stokes(current_queue, idx, c);
        float Is = stokes_s_intensity(lane_stokes);
        float Ip = stokes_p_intensity(lane_stokes);

        DielectricSurfaceBoundary surface = eval_dielectric_surface_boundary(
            lambda, effective_thickness, eta_i, mat.thin_film_ior, eta_t, cos_theta_i);

        float R = fminf(1.0f, fmaxf(0.0f, (surface.Rs * Is + surface.Rp * Ip) / (lane_stokes.I + 1e-6f)));
        float T = surface.tir ? 0.0f : fminf(1.0f, fmaxf(0.0f, (surface.Ts * Is + surface.Tp * Ip) / (lane_stokes.I + 1e-6f)));

        if (R > 1e-6f) {
            int out_idx = reserve_ray_slot(next_queue);
            if (out_idx >= 0) {
                GpuVec3 out_direction = reflect(unit_direction, normal).normalize();
                GpuVec3 offset = (out_direction.dot(normal) > 0.0f) ? normal : -normal;
                next_queue.origins[out_idx] = p + offset * 1e-4f;
                next_queue.directions[out_idx] = out_direction;
                float wavelength_pdf = current_queue.wavelength_pdfs[idx];
                store_lane_throughput(next_queue, out_idx, throughput, c, throughput.values[c] * R * wavelength_pdf);
                for (int s = 0; s < current_queue.num_spectral_channels; ++s) {
                    store_stokes(next_queue, out_idx, s, StokesVector(0.0f, 0.0f, 0.0f, 0.0f));
                }
                StokesVector reflected_stokes = lane_stokes;
                apply_mueller_reflection_boundary(reflected_stokes, surface.rs, surface.rp, surface.Rs, surface.Rp);
                store_stokes(next_queue, out_idx, c, reflected_stokes);
                next_queue.medium_indices[out_idx] = current_medium_idx;
                next_queue.seeds[out_idx] = seed + 1664525u * unsigned(c + 1);
                next_queue.pixel_indices[out_idx] = pixel_index;
                next_queue.depths[out_idx] = depth + 1;
                next_queue.flags[out_idx] = 1;
                next_queue.last_pdf[out_idx] = 1.0f;
                next_queue.spectral_modes[out_idx] = SpectralRayModeLane;
                next_queue.active_channels[out_idx] = c;
                next_queue.wavelength_pdfs[out_idx] = wavelength_pdf;
            }
        }

        if (T > 1e-6f) {
            float eta = eta_i / eta_t;
            GpuVec3 perp = eta * (unit_direction + cos_theta_i * normal);
            GpuVec3 para = -sqrtf(fmaxf(0.0f, 1.0f - perp.length_sq())) * normal;
            GpuVec3 out_direction = (perp + para).normalize();

            int out_idx = reserve_ray_slot(next_queue);
            if (out_idx >= 0) {
                GpuVec3 offset = (out_direction.dot(normal) > 0.0f) ? normal : -normal;
                float transport_weight = T *
                    select_boundary_transport_scale(surface.radiance_scale, surface.importance_scale, BoundaryTransportMode::Radiance);
                float wavelength_pdf = current_queue.wavelength_pdfs[idx];
                next_queue.origins[out_idx] = p + offset * 1e-4f;
                next_queue.directions[out_idx] = out_direction;
                store_lane_throughput(next_queue, out_idx, throughput, c, throughput.values[c] * transport_weight * wavelength_pdf);
                for (int s = 0; s < current_queue.num_spectral_channels; ++s) {
                    store_stokes(next_queue, out_idx, s, StokesVector(0.0f, 0.0f, 0.0f, 0.0f));
                }
                StokesVector transmitted_stokes = lane_stokes;
                apply_mueller_transmission_boundary(transmitted_stokes, surface.ts, surface.tp, surface.Ts, surface.Tp, surface.eta_jacobian);
                transmitted_stokes = transmitted_stokes *
                    select_boundary_transport_scale(surface.radiance_scale, surface.importance_scale, BoundaryTransportMode::Radiance);
                store_stokes(next_queue, out_idx, c, transmitted_stokes);

                next_queue.medium_indices[out_idx] = next_dielectric_medium_index(
                    current_medium_idx, mat_idx, r_in.direction, out_direction, ng);
                next_queue.seeds[out_idx] = seed + 22695477u * unsigned(c + 1);
                next_queue.pixel_indices[out_idx] = pixel_index;
                next_queue.depths[out_idx] = depth + 1;
                next_queue.flags[out_idx] = 1;
                next_queue.last_pdf[out_idx] = 1.0f;
                next_queue.spectral_modes[out_idx] = SpectralRayModeLane;
                next_queue.active_channels[out_idx] = c;
                next_queue.wavelength_pdfs[out_idx] = wavelength_pdf;
            }
        }
    }
    return true;
}

__device__ inline bool split_dispersive_dielectric_lanes(
    const RayQueue& current_queue,
    RayQueue& next_queue,
    int idx,
    const GpuMaterial& mat,
    const GpuMaterialSoA& mat_soa,
    const GpuVec3& p,
    const GpuVec3& n,
    const GpuVec3& ng,
    const GpuVec2& uv,
    const SpectralPacket& throughput,
    int current_medium_idx,
    int mat_idx,
    int pixel_index,
    int depth,
    unsigned int seed,
    float dispersion_clamp,
    float ior_outside
) {
    return split_dispersive_dielectric_lanes(
        current_queue,
        next_queue,
        idx,
        mat,
        mat_soa,
        SpectralPacket(mat.ior),
        p,
        n,
        ng,
        uv,
        throughput,
        current_medium_idx,
        mat_idx,
        pixel_index,
        depth,
        seed,
        dispersion_clamp,
        ior_outside);
}

__global__ __launch_bounds__(256) void extend_shadow_kernel(
    ShadowQueue shadow_queue,
    GpuVec3* accum_buffer,
    GpuScene scene,
    float dispersion_clamp
) {
    (void)dispersion_clamp;
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= *shadow_queue.count) return;

    GpuVec3 origin = shadow_queue.origins[idx];
    GpuVec3 direction = shadow_queue.directions[idx];
    float max_dist = shadow_queue.max_dist[idx];
    int pixel_index = shadow_queue.pixel_indices[idx];
    int spectral_mode = shadow_queue.spectral_modes ? shadow_queue.spectral_modes[idx] : SpectralRayModePacket;
    int active_channel = shadow_queue.active_channels ? shadow_queue.active_channels[idx] : -1;
    float wavelength_pdf = shadow_queue.wavelength_pdfs ? shadow_queue.wavelength_pdfs[idx] : 1.0f;
    SpectralPacket radiance;
    {
        const int cap = shadow_queue.capacity;
        for (int c = 0; c < scene.num_spectral_channels; ++c) {
            radiance.values[c] = shadow_queue.radiance_vals[c * cap + idx];
            radiance.wavelengths[c] = shadow_queue.radiance_wavelengths[c * cap + idx];
        }
    }

    GpuRay r(origin, direction, 1e-4f, max_dist);

    for (int pass = 0; pass < 8; ++pass) {
        float t;
        GpuVec3 p, n, ng;
        GpuVec2 uv;
        int mat_idx;
        int type_dummy; int index_dummy; int primitive_dummy;

        if (!world_hit(scene, r, 1e-4f, r.t_max, t, p, n, ng, uv, mat_idx, type_dummy, index_dummy, primitive_dummy, true)) {
            break;
        }

        GpuMaterial mat = scene.materials[mat_idx];

        if (mat.type == MaterialType::Light) {
            r.origin = p + r.direction * 1e-4f;
            r.t_max -= (t + 1e-4f);
            if (r.t_max <= 1e-4f) break;
            continue;
        }

        if (mat.type != MaterialType::Dielectric) {
            return;
        }

        return;
    }

    GpuVec3 xyz = spectral_mode_is_sampled(spectral_mode)
        ? spectral_sample_to_xyz(radiance, scene.num_spectral_channels, active_channel, wavelength_pdf, spectral_mode)
        : spectrum_to_xyz(radiance, scene.num_spectral_channels);
    GpuVec3 rgb = xyz_to_rgb(xyz);

    float max_val = 1000.0f;
    rgb.x = fminf(rgb.x, max_val);
    rgb.y = fminf(rgb.y, max_val);
    rgb.z = fminf(rgb.z, max_val);

    if (isfinite(rgb.x) && isfinite(rgb.y) && isfinite(rgb.z)) {
        atomicAdd(&accum_buffer[pixel_index].x, rgb.x);
        atomicAdd(&accum_buffer[pixel_index].y, rgb.y);
        atomicAdd(&accum_buffer[pixel_index].z, rgb.z);
        store_restir_di_visible_candidate(scene, shadow_queue, idx, rgb);
        if (scene.path_guiding_light_weights &&
            shadow_queue.light_list_indices &&
            scene.path_guiding_learning_rate > 0.0f) {
            int light_list_index = shadow_queue.light_list_indices[idx];
            if (light_list_index >= 0 && light_list_index < scene.path_guiding_light_count) {
                float luminance = shadow_queue.guiding_product_luminance[idx];
                if (isfinite(luminance) && luminance > scene.path_guiding_min_weight) {
                    if (shadow_queue.guiding_epochs[idx] != scene.path_guiding_epoch) return;
                    atomicAdd(&scene.path_guiding_light_weights[light_list_index],
                              scene.path_guiding_learning_rate * luminance);
                    const int cell = path_guiding_spatial_cell(scene, shadow_queue.origins[idx]);
                    const GpuVec3 guide_direction =
                        scene.lights[light_list_index].centroid - shadow_queue.origins[idx];
                    const int direction_bin = path_guiding_direction_bin(scene, guide_direction);
                    const int guide_index = path_guiding_spatial_directional_index(scene, light_list_index, cell, direction_bin);
                    if (guide_index >= 0) {
                        atomicAdd(&scene.path_guiding_spatial_directional_weights[guide_index],
                                  scene.path_guiding_learning_rate * luminance);
                    }
                }
            }
        }
    }
}

__global__ __launch_bounds__(256) void shade_kernel(
    RayQueue current_queue,
    HitQueue hit_queue,
    RayQueue next_queue,
    ShadowQueue shadow_queue,
    GpuVec3* accum_buffer,
    GpuVec3* normal_buffer,
    GpuVec3* albedo_buffer,
    float* depth_buffer,
    GpuVec2* uv_buffer,
    GpuVec2* motion_vector_buffer,
    GpuCamera current_camera,
    GpuCamera previous_camera,
    GpuScene scene,
    int sample_index,
    float dispersion_clamp,
    float rr_min_prob
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= *current_queue.count) return;

    int pixel_index = current_queue.pixel_indices[idx];
    int mat_idx = hit_queue.mat_ids[idx];
    SpectralPacket throughput = load_throughput(current_queue, idx);
    int depth = current_queue.depths[idx];
    unsigned int seed = current_queue.seeds[idx];
    int flag = current_queue.flags[idx];

    int current_medium_idx = current_queue.medium_indices[idx];
    float t_hit = (mat_idx != -1) ? hit_queue.t[idx] : 1e30f;
    int spectral_mode = current_queue.spectral_modes[idx];
    int active_channel = current_queue.active_channels[idx];
    if (spectral_mode_is_sampled(spectral_mode)) {
        if (active_channel < 0) active_channel = 0;
        if (active_channel >= scene.num_spectral_channels) active_channel = scene.num_spectral_channels - 1;
    } else {
        active_channel = 0;
    }

    float density = 0.0f;
    float anisotropy = 0.0f;
    VolumePhaseFunction phase_function = VolumePhaseFunction::HenyeyGreenstein;
    int phase_resource_index = -1;
    SpectralPacket sigma_s(0.0f);
    SpectralPacket sigma_a(0.0f);

    if (current_medium_idx == -1) {
        density = scene.medium_density;
        anisotropy = scene.medium_anisotropy;
        phase_function = static_cast<VolumePhaseFunction>(scene.medium_phase);
        phase_resource_index = scene.medium_phase_resource_index;
        sigma_s = scene.medium_scattering;
        sigma_a = scene.medium_absorption;
    } else {
        GpuMaterial med_mat = scene.materials[current_medium_idx];
        density = med_mat.medium_density;
        anisotropy = med_mat.medium_anisotropy;
        phase_function = static_cast<VolumePhaseFunction>(med_mat.medium_phase);
        phase_resource_index = med_mat.medium_phase_resource_index;
        GpuMaterialSoA med_soa = load_mat_spectra_6x(scene, current_medium_idx, throughput.wavelengths);
        sigma_s = med_soa.medium_scattering;
        sigma_a = med_soa.medium_absorption;
    }

    SpectralPacket sigma_t;
    if (phase_function == VolumePhaseFunction::Mie) {
        if (!load_mie_medium_cross_sections(scene, phase_resource_index, throughput.wavelengths,
                                             scene.num_spectral_channels, &sigma_s, &sigma_t)) {
            return;
        }
        sigma_t = sigma_t * density;
    } else {
        sigma_t = (sigma_s + sigma_a) * density;
    }
    float sigma_t_avg = 0.0f;
    for (int c = 0; c < scene.num_spectral_channels; ++c) {
        sigma_t_avg += sigma_t.values[c];
    }
    sigma_t_avg /= float(scene.num_spectral_channels);
    float sigma_t_proposal = spectral_mode_is_sampled(spectral_mode)
        ? sigma_t.values[active_channel]
        : sigma_t_avg;

    if (sigma_t_proposal > 0.0f) {
        float r_dist = sample_path_dimension(sample_index, pixel_index, depth, kPathDimVolumeDistance);
        float t_medium = -logf(1.0f - r_dist) / sigma_t_proposal;

        float max_allowed = scene.medium_max_distance > 0.0f ? scene.medium_max_distance : 1e30f;
        if (t_medium < t_hit && t_medium < max_allowed) {
            float tr_vals[kMaxPacketLanes];
            for (int c = 0; c < scene.num_spectral_channels; ++c) {
                tr_vals[c] = expf(-sigma_t.values[c] * t_medium);
            }

            float pdf_t = sigma_t_proposal * expf(-sigma_t_proposal * t_medium);

            for (int c = 0; c < scene.num_spectral_channels; ++c) {
                throughput.values[c] *= tr_vals[c] * sigma_s.values[c] * density * (1.0f / pdf_t);
            }

            if (scene.light_count > 0) {
                float r_light_pick = sample_path_dimension(sample_index, pixel_index, depth, kPathDimVolumeLightPick);

                GpuVec3 p_vol = current_queue.origins[idx] + current_queue.directions[idx] * t_medium;
                int light_idx_idx = sample_light_list_index_at(scene, p_vol, r_light_pick);
                float r1 = sample_path_dimension(sample_index, pixel_index, depth, kPathDimVolumeLightU);
                float r2 = sample_path_dimension(sample_index, pixel_index, depth, kPathDimVolumeLightV);
                SelectedLightSample light_sample;

                if (sample_selected_light(scene, light_idx_idx, p_vol, r1, r2, light_sample)) {
                    const float phase_cosine =
                        current_queue.directions[idx].dot(light_sample.direction);
                    float phase_values[kMaxPacketLanes];
                    float phase_val = 0.0f;
                    if (phase_function == VolumePhaseFunction::Mie) {
                        for (int c = 0; c < scene.num_spectral_channels; ++c) {
                            if (!lookup_mie_phase(scene, phase_resource_index,
                                                  throughput.wavelengths[c], phase_cosine,
                                                  &phase_values[c])) return;
                        }
                        if (!eval_mie_packet_phase_pdf(
                                scene, phase_resource_index, throughput.wavelengths,
                                scene.num_spectral_channels, spectral_mode, active_channel,
                                phase_cosine, &phase_val)) return;
                    } else {
                        bool phase_supported = false;
                        phase_val = eval_volume_phase(phase_function, phase_cosine,
                                                      anisotropy, &phase_supported);
                        if (!phase_supported) return;
                        for (int c = 0; c < scene.num_spectral_channels; ++c) {
                            phase_values[c] = phase_val;
                        }
                    }

                    if (light_sample.max_dist > 1e-4f) {
                         float tr_light_vals[kMaxPacketLanes];
                         for (int c = 0; c < scene.num_spectral_channels; ++c) {
                             tr_light_vals[c] = expf(-sigma_t.values[c] * light_sample.max_dist);
                         }

                         SpectralPacket L_e = light_sample.kind == GpuLightKind::Environment
                             ? environment_radiance_spectrum(scene, light_sample.direction, current_medium_idx, throughput.wavelengths)
                             : load_mat_emission_spectrum(scene, light_sample.material_index, throughput.wavelengths);
                         SpectralPacket contribution;
                         SpectralPacket guiding_product;
                         for (int c = 0; c < scene.num_spectral_channels; ++c) {
                             L_e.wavelengths[c] = throughput.wavelengths[c];
                             guiding_product.values[c] = L_e.values[c] * phase_values[c] * tr_light_vals[c];
                             guiding_product.wavelengths[c] = throughput.wavelengths[c];
                             contribution.values[c] = throughput.values[c] * L_e.values[c] * phase_values[c] * tr_light_vals[c] * (1.0f / light_sample.pdf);
                             contribution.wavelengths[c] = throughput.wavelengths[c];
                         }

                         int s_idx = reserve_shadow_slot(shadow_queue);
                         if (s_idx >= 0) {
                             const int cap = shadow_queue.capacity;
                             shadow_queue.origins[s_idx] = p_vol;
                             shadow_queue.directions[s_idx] = light_sample.direction;
                             shadow_queue.max_dist[s_idx] = light_sample.max_dist - 1e-4f;
                             for (int c = 0; c < scene.num_spectral_channels; ++c) {
                                 shadow_queue.radiance_vals[c * cap + s_idx] = contribution.values[c];
                                 shadow_queue.radiance_wavelengths[c * cap + s_idx] = contribution.wavelengths[c];
                             }
                             shadow_queue.pixel_indices[s_idx] = pixel_index;
                             shadow_queue.spectral_modes[s_idx] = spectral_mode;
                             shadow_queue.active_channels[s_idx] = current_queue.active_channels[idx];
                             shadow_queue.wavelength_pdfs[s_idx] = current_queue.wavelength_pdfs[idx];
                             shadow_queue.light_list_indices[s_idx] = light_idx_idx;
                             shadow_queue.bsdf_lobe_pdfs[s_idx] = phase_val;
                             PathGuidingProductMetadata guide_metadata = path_guiding_product_metadata(
                                 guiding_product,
                                 scene.num_spectral_channels,
                                 spectral_mode,
                                 current_queue.active_channels[idx],
                                 current_queue.wavelength_pdfs[idx]);
                             shadow_queue.guiding_product_luminance[s_idx] = guide_metadata.luminance;
                             shadow_queue.guiding_wavelength_nm[s_idx] = guide_metadata.wavelength_nm;
                             shadow_queue.guiding_epochs[s_idx] = scene.path_guiding_epoch;
                             StokesVector restir_stokes = spectral_mode_is_sampled(spectral_mode)
                                 ? load_stokes(current_queue, idx, active_channel)
                                 : load_packet_average_stokes(current_queue, idx);
                             restir_stokes = apply_volume_phase_polarization(
                                 phase_function, restir_stokes);
                             shadow_queue.stokes_i[s_idx] = restir_stokes.I;
                             shadow_queue.stokes_q[s_idx] = restir_stokes.Q;
                             shadow_queue.stokes_u[s_idx] = restir_stokes.U;
                             shadow_queue.stokes_v[s_idx] = restir_stokes.V;
                             shadow_queue.restir_replay_flags[s_idx] = 0;
                         }
                    }
                }
            }

            float r_phase_1 = sample_path_dimension(sample_index, pixel_index, depth, kPathDimVolumePhaseU);
            float r_phase_2 = sample_path_dimension(sample_index, pixel_index, depth, kPathDimVolumePhaseV);
            float phase_pdf = 0.0f;
            GpuVec3 new_dir;
            if (phase_function == VolumePhaseFunction::Mie) {
                if (!sample_mie_packet_phase_lds_pdf(
                        scene, phase_resource_index, current_queue.directions[idx],
                        throughput.wavelengths, scene.num_spectral_channels,
                        spectral_mode, active_channel, r_phase_1, r_phase_2,
                        &new_dir, &phase_pdf)) return;
                const int first_channel = spectral_mode_is_sampled(spectral_mode)
                    ? active_channel : 0;
                const int end_channel = spectral_mode_is_sampled(spectral_mode)
                    ? active_channel + 1 : scene.num_spectral_channels;
                for (int c = first_channel; c < end_channel; ++c) {
                    float lane_phase = 0.0f;
                    if (!lookup_mie_phase(scene, phase_resource_index,
                                          throughput.wavelengths[c],
                                          current_queue.directions[idx].dot(new_dir),
                                          &lane_phase)) return;
                    throughput.values[c] *= lane_phase / phase_pdf;
                }
            } else if (!sample_volume_phase_lds_pdf(
                           phase_function, current_queue.directions[idx], anisotropy,
                           r_phase_1, r_phase_2, &new_dir, &phase_pdf)) {
                return;
            }
            GpuVec3 new_origin = current_queue.origins[idx] + current_queue.directions[idx] * t_medium;

             int out_idx = reserve_ray_slot(next_queue);
             if (out_idx >= 0) {
                next_queue.origins[out_idx] = new_origin;
                next_queue.directions[out_idx] = new_dir;
                store_throughput(next_queue, out_idx, throughput);
                for (int c = 0; c < scene.num_spectral_channels; ++c) {
                    const StokesVector input_stokes = load_stokes(current_queue, idx, c);
                    const StokesVector output_stokes = apply_volume_phase_polarization(
                        phase_function, input_stokes);
                    store_stokes(next_queue, out_idx, c, output_stokes);
                }
                next_queue.medium_indices[out_idx] = current_medium_idx;
                next_queue.seeds[out_idx] = seed;
                next_queue.pixel_indices[out_idx] = pixel_index;
                next_queue.depths[out_idx] = depth + 1;
                next_queue.flags[out_idx] = 0;
                next_queue.last_pdf[out_idx] = phase_pdf;
                next_queue.spectral_modes[out_idx] = current_queue.spectral_modes[idx];
                next_queue.active_channels[out_idx] = current_queue.active_channels[idx];
                next_queue.wavelength_pdfs[out_idx] = current_queue.wavelength_pdfs[idx];
             }
             return;
        } else {
            float tr_vals[kMaxPacketLanes];
            float prob_no_scatter = expf(-sigma_t_proposal * t_hit);
            for (int c = 0; c < scene.num_spectral_channels; ++c) {
                tr_vals[c] = expf(-sigma_t.values[c] * t_hit);
            }

            if (prob_no_scatter > 1e-6f) {
                for (int c = 0; c < scene.num_spectral_channels; ++c) {
                    throughput.values[c] *= tr_vals[c] * (1.0f / prob_no_scatter);
                }
            } else {
                throughput = SpectralPacket(0.0f);
            }
        }
    }

    if (mat_idx == -1) {
        float mis_weight = 1.0f;
        if (depth > 0 && !(flag & 1) && scene.environment_light_direct_sampling) {
            const float pdf_nee = selected_environment_light_pdf(scene, current_queue.origins[idx]);
            if (pdf_nee > 0.0f) {
                const float last_pdf = current_queue.last_pdf[idx];
                mis_weight = (last_pdf * last_pdf) / (last_pdf * last_pdf + pdf_nee * pdf_nee);
            }
        }

        GpuVec3 sky_color = environment_sky_color(scene, current_queue.directions[idx], current_medium_idx);
        SpectralPacket sky_spectrum = environment_radiance_spectrum(scene, current_queue.directions[idx], current_medium_idx, throughput.wavelengths);
        SpectralPacket contribution = throughput * sky_spectrum * mis_weight;

        GpuVec3 xyz = spectral_sample_to_xyz(
            contribution,
            scene.num_spectral_channels,
            current_queue.active_channels[idx],
            current_queue.wavelength_pdfs[idx],
            current_queue.spectral_modes[idx]);
        GpuVec3 rgb = xyz_to_rgb(xyz);

        if (isfinite(rgb.x) && isfinite(rgb.y) && isfinite(rgb.z)) {
            atomicAdd(&accum_buffer[pixel_index].x, rgb.x);
            atomicAdd(&accum_buffer[pixel_index].y, rgb.y);
            atomicAdd(&accum_buffer[pixel_index].z, rgb.z);
        }

        if (depth == 0) {
            if (normal_buffer) normal_buffer[pixel_index] = GpuVec3(0, 0, 0);
            if (albedo_buffer) albedo_buffer[pixel_index] = sky_color;
            if (depth_buffer) depth_buffer[pixel_index] = 0.0f;
            if (uv_buffer) uv_buffer[pixel_index] = GpuVec2(0.0f, 0.0f);
            if (motion_vector_buffer) motion_vector_buffer[pixel_index] = GpuVec2(0.0f, 0.0f);
        }
        return;
    }

    GpuMaterial mat = scene.materials[mat_idx];
    const bool is_composite = mat.type == MaterialType::Composite;
    const bool is_layered = mat.type == MaterialType::Layered;
    GpuMaterialSoA mat_soa = load_mat_spectra_6x(scene, mat_idx, throughput.wavelengths);

    GpuVec2 hit_uv = hit_queue.uv[idx];
    if (mat.albedo_expression_root != -1) {
        mat_soa.albedo = eval_material_expression(scene, mat, mat.albedo_expression_root, hit_uv.u, hit_uv.v, throughput.wavelengths, scene.num_spectral_channels);
    }
    if (mat.roughness_expression_root != -1) {
        SpectralPacket graph_roughness = eval_material_expression(scene, mat, mat.roughness_expression_root, hit_uv.u, hit_uv.v, throughput.wavelengths, scene.num_spectral_channels);
        float roughness_value = 0.0f;
        for (int c = 0; c < scene.num_spectral_channels; ++c) {
            roughness_value += graph_roughness.values[c];
        }
        roughness_value /= float(scene.num_spectral_channels);
        mat.roughness = fminf(1.0f, fmaxf(0.001f, roughness_value));
    }
    if (mat.emission_expression_root != -1) {
        mat_soa.emission = eval_material_expression(scene, mat, mat.emission_expression_root, hit_uv.u, hit_uv.v, throughput.wavelengths, scene.num_spectral_channels);
    }
    if (mat.metal_eta_expression_root != -1) {
        mat_soa.metal_eta = eval_material_expression(scene, mat, mat.metal_eta_expression_root, hit_uv.u, hit_uv.v, throughput.wavelengths, scene.num_spectral_channels);
    }
    if (mat.extinction_expression_root != -1) {
        mat_soa.extinction = eval_material_expression(scene, mat, mat.extinction_expression_root, hit_uv.u, hit_uv.v, throughput.wavelengths, scene.num_spectral_channels);
    }
    SpectralPacket dielectric_ior(mat.ior);
    for (int c = 0; c < scene.num_spectral_channels; ++c) dielectric_ior.wavelengths[c] = throughput.wavelengths[c];
    if (mat.ior_expression_root != -1) {
        dielectric_ior = eval_material_expression(scene, mat, mat.ior_expression_root, hit_uv.u, hit_uv.v, throughput.wavelengths, scene.num_spectral_channels);
        for (int c = 0; c < scene.num_spectral_channels; ++c) {
            if (!isfinite(dielectric_ior.values[c]) || dielectric_ior.values[c] <= 0.0f) return;
        }
    }

    if (mat.texture_index != -1) {
        SpectralPacket tex_color = sample_texture(scene, mat.texture_index, hit_uv.u, hit_uv.v, throughput.wavelengths, scene.num_spectral_channels);
        mat_soa.albedo = mat_soa.albedo * tex_color;
    }
    if (mat.roughness_texture_index != -1) {
        SpectralPacket tex_roughness = sample_texture(scene, mat.roughness_texture_index, hit_uv.u, hit_uv.v, throughput.wavelengths, scene.num_spectral_channels);
        float tex_value = 0.0f;
        for (int c = 0; c < scene.num_spectral_channels; ++c) {
            tex_value += tex_roughness.values[c];
        }
        tex_value = fminf(1.0f, fmaxf(0.0f, tex_value / float(scene.num_spectral_channels)));
        mat.roughness = fminf(1.0f, fmaxf(0.001f, mat.roughness * tex_value));
    }
    if (mat.emission_texture_index != -1) {
        SpectralPacket tex_emission = sample_texture(scene, mat.emission_texture_index, hit_uv.u, hit_uv.v, throughput.wavelengths, scene.num_spectral_channels);
        mat_soa.emission = mat_soa.emission * tex_emission;
    }

    ResolvedLayeredMaterial resolved_material;
    float composite_mix = 0.0f;
    if (is_composite) {
        if (!scene.material_bsdf_lobes ||
            mat.bsdf_lobe_count != 2 ||
            mat.bsdf_lobe_start < 0 ||
            mat.bsdf_lobe_start + mat.bsdf_lobe_count > scene.material_bsdf_lobe_count ||
            mat.bsdf_mix_expression_root < 0) return;
        resolved_material.coating = resolve_material_bsdf_lobe(scene, mat, 0, hit_uv, throughput.wavelengths);
        resolved_material.substrate = resolve_material_bsdf_lobe(scene, mat, 1, hit_uv, throughput.wavelengths);
        composite_mix = composite_material_mix_factor(scene, mat, hit_uv, throughput.wavelengths);
        for (int c = 0; c < scene.num_spectral_channels; ++c) {
            mat_soa.albedo.values[c] = resolved_material.coating.spectra.albedo.values[c] * (1.0f - composite_mix) +
                resolved_material.substrate.spectra.albedo.values[c] * composite_mix;
            mat_soa.albedo.wavelengths[c] = throughput.wavelengths[c];
        }
    } else if (is_layered) {
        if (!scene.material_bsdf_lobes ||
            mat.bsdf_lobe_count != 2 ||
            mat.bsdf_lobe_start < 0 ||
            mat.bsdf_lobe_start + mat.bsdf_lobe_count > scene.material_bsdf_lobe_count ||
            mat.layer_thickness_expression_root < 0 ||
            mat.layer_absorption_expression_root < 0) return;
        resolved_material = resolve_layered_material(scene, mat, hit_uv, throughput.wavelengths);
        if (resolved_material.coating.material.type != MaterialType::Dielectric ||
            resolved_material.substrate.material.type != MaterialType::Lambertian) return;
        mat_soa.albedo = resolved_material.substrate.spectra.albedo;
    }

    GpuVec3 p = hit_queue.p[idx];
    GpuVec3 n = hit_queue.n[idx];
    GpuVec3 ng = hit_queue.ng[idx];

    if (depth == 0) {
        if (normal_buffer) normal_buffer[pixel_index] = n;
        if (albedo_buffer) albedo_buffer[pixel_index] = xyz_to_rgb(spectrum_to_xyz(mat_soa.albedo, scene.num_spectral_channels));
        if (depth_buffer) depth_buffer[pixel_index] = t_hit;
        if (uv_buffer) uv_buffer[pixel_index] = hit_uv;
        if (motion_vector_buffer) {
            GpuVec3 previous_p = p;
            int hit_type = hit_queue.hit_types[idx];
            int hit_index = hit_queue.hit_indices[idx];
            if (hit_type == 2 &&
                hit_index >= 0 &&
                hit_index < scene.instance_count &&
                scene.instance_transforms &&
                scene.previous_instance_transforms) {
                const GpuInstanceTransform& current_xform = scene.instance_transforms[hit_index];
                const GpuInstanceTransform& previous_xform = scene.previous_instance_transforms[hit_index];
                GpuVec3 local_p = current_xform.inverse_transform.transform_point(p);
                previous_p = previous_xform.transform.transform_point(local_p);
            }
            GpuVec2 current_screen = project_camera_screen(current_camera, p);
            GpuVec2 previous_screen = project_camera_screen(previous_camera, previous_p);
            motion_vector_buffer[pixel_index] = GpuVec2(
                current_screen.u - previous_screen.u,
                current_screen.v - previous_screen.v);
        }
    }

    GpuVec3 emission_rgb = xyz_to_rgb(spectrum_to_xyz(mat_soa.emission, scene.num_spectral_channels));
    if (emission_rgb.length_sq() > 0) {
        float mis_weight = 1.0f;

        if (depth > 0 && !(flag & 1) && scene.light_count > 0) {
             float pdf_nee = 0.0f;
             int hit_type = hit_queue.hit_types[idx];
             int hit_index = hit_queue.hit_indices[idx];
             int hit_primitive_index = hit_queue.hit_primitive_indices ? hit_queue.hit_primitive_indices[idx] : -1;

             for(int k=0; k<scene.light_count; ++k) {
                 const GpuLightRecord record = get_light_record(scene, k);
                 bool same_light = false;
                 if (record.kind == GpuLightKind::Sphere) {
                     same_light = hit_type == 0 && record.primitive_index == hit_index;
                 } else if (record.kind == GpuLightKind::MeshTriangle) {
                     same_light = hit_type == 1 &&
                                  record.primitive_index == hit_index &&
                                  record.secondary_index == hit_primitive_index;
                 } else if (record.kind == GpuLightKind::InstanceTriangle) {
                     same_light = hit_type == 2 &&
                                  record.primitive_index == hit_index &&
                                  record.secondary_index == hit_primitive_index;
                 }
                 if (!same_light || record.material_index != mat_idx) continue;
                 pdf_nee = selected_light_hit_pdf(scene, k, current_queue.origins[idx], p);
                 break;
             }

             if (pdf_nee > 0.0f) {
                 float last_pdf = current_queue.last_pdf[idx];
                 mis_weight = (last_pdf * last_pdf) / (last_pdf * last_pdf + pdf_nee * pdf_nee);
             } else {
                 mis_weight = 0.0f;
             }
        }

        if (mis_weight > 0.0f) {
            SpectralPacket emission_spectrum = mat_soa.emission;
            for (int c = 0; c < scene.num_spectral_channels; ++c) {
                emission_spectrum.wavelengths[c] = throughput.wavelengths[c];
            }
            SpectralPacket contribution = throughput * emission_spectrum * mis_weight;

            GpuVec3 xyz = spectral_sample_to_xyz(
                contribution,
                scene.num_spectral_channels,
                current_queue.active_channels[idx],
                current_queue.wavelength_pdfs[idx],
                current_queue.spectral_modes[idx]);
            GpuVec3 rgb = xyz_to_rgb(xyz);

            if (depth > 0) {
                float max_radiance = 1000.0f;
                if (rgb.x > max_radiance) rgb.x = max_radiance;
                if (rgb.y > max_radiance) rgb.y = max_radiance;
                 if (rgb.z > max_radiance) rgb.z = max_radiance;
            }

            if (isfinite(rgb.x) && isfinite(rgb.y) && isfinite(rgb.z)) {
                atomicAdd(&accum_buffer[pixel_index].x, rgb.x);
                atomicAdd(&accum_buffer[pixel_index].y, rgb.y);
                atomicAdd(&accum_buffer[pixel_index].z, rgb.z);
            }
        }
    }

    if (depth == 0) {
        enqueue_restir_di_temporal_replay(scene, shadow_queue, pixel_index);
    }

    if (depth >= 50) return;

    if (scene.light_count > 0 && (mat.type == MaterialType::Composite ||
                                  mat.type == MaterialType::Layered ||
                                  mat.type == MaterialType::Lambertian ||
                                  mat.type == MaterialType::Cloth ||
                                  (mat.type == MaterialType::Metal && mat.roughness > 0.02f) ||
                                  is_rough_dielectric_bsdf(mat))) {
        float r_light_pick = sample_path_dimension(sample_index, pixel_index, depth, kPathDimLightPick);
        float r_light_1 = sample_path_dimension(sample_index, pixel_index, depth, kPathDimLightU);
        float r_light_2 = sample_path_dimension(sample_index, pixel_index, depth, kPathDimLightV);

        int light_idx_idx = sample_light_list_index_at(scene, p, r_light_pick);
        SelectedLightSample light_sample;

        if (sample_selected_light(scene, light_idx_idx, p, r_light_1, r_light_2, light_sample)) {
            float cos_surf = (is_composite || is_layered)
                ? fmaxf(0.0f, n.dot(light_sample.direction))
                : direct_light_cosine_factor(mat, n, light_sample.direction);

            if (((is_composite || is_layered)
                    ? n.dot(light_sample.direction) > 1e-6f && ng.dot(light_sample.direction) > 1e-6f
                    : direct_light_direction_allowed(mat, n, ng, light_sample.direction))) {
                 float pdf = light_sample.pdf;
                 pdf = fmaxf(pdf, 1e-12f);

                 SpectralPacket L_e = light_sample.kind == GpuLightKind::Environment
                     ? environment_radiance_spectrum(scene, light_sample.direction, current_medium_idx, throughput.wavelengths)
                     : load_mat_emission_spectrum(scene, light_sample.material_index, throughput.wavelengths);
                 for (int c = 0; c < scene.num_spectral_channels; ++c) {
                     L_e.wavelengths[c] = throughput.wavelengths[c];
                 }

                 SpectralPacket f_r;
                 SpectralPacket pdf_mat;
                 if (is_composite) {
                     SpectralPacket f_a = eval_bsdf(
                         resolved_material.coating.material, resolved_material.coating.spectra.albedo, resolved_material.coating.spectra.extinction,
                         resolved_material.coating.spectra.metal_eta, resolved_material.coating.dielectric_ior, p, n, hit_uv,
                         -current_queue.directions[idx], light_sample.direction, throughput.wavelengths,
                         scene.num_spectral_channels);
                     SpectralPacket f_b = eval_bsdf(
                         resolved_material.substrate.material, resolved_material.substrate.spectra.albedo, resolved_material.substrate.spectra.extinction,
                         resolved_material.substrate.spectra.metal_eta, resolved_material.substrate.dielectric_ior, p, n, hit_uv,
                         -current_queue.directions[idx], light_sample.direction, throughput.wavelengths,
                         scene.num_spectral_channels);
                     SpectralPacket pdf_a = pdf_bsdf_spectral(
                         resolved_material.coating.material, resolved_material.coating.dielectric_ior, n, hit_uv,
                         -current_queue.directions[idx], light_sample.direction, throughput.wavelengths,
                         scene.num_spectral_channels, dispersion_clamp);
                     SpectralPacket pdf_b = pdf_bsdf_spectral(
                         resolved_material.substrate.material, resolved_material.substrate.dielectric_ior, n, hit_uv,
                         -current_queue.directions[idx], light_sample.direction, throughput.wavelengths,
                         scene.num_spectral_channels, dispersion_clamp);
                     for (int c = 0; c < scene.num_spectral_channels; ++c) {
                         f_r.values[c] = f_a.values[c] * (1.0f - composite_mix) + f_b.values[c] * composite_mix;
                         f_r.wavelengths[c] = throughput.wavelengths[c];
                         pdf_mat.values[c] = pdf_a.values[c] * (1.0f - composite_mix) + pdf_b.values[c] * composite_mix;
                         pdf_mat.wavelengths[c] = throughput.wavelengths[c];
                     }
                 } else if (is_layered) {
                     f_r = eval_layered_bsdf(
                        resolved_material, p, n, hit_uv, -current_queue.directions[idx],
                         light_sample.direction, throughput.wavelengths, scene.num_spectral_channels);
                     pdf_mat = pdf_layered_bsdf_spectral(
                        resolved_material, n, hit_uv, -current_queue.directions[idx],
                         light_sample.direction, throughput.wavelengths,
                         scene.num_spectral_channels, dispersion_clamp);
                 } else {
                     f_r = eval_bsdf(mat, mat_soa.albedo, mat_soa.extinction, mat_soa.metal_eta, dielectric_ior, p, n, hit_uv, -current_queue.directions[idx], light_sample.direction, throughput.wavelengths, scene.num_spectral_channels);
                     pdf_mat = pdf_bsdf_spectral(
                         mat, dielectric_ior, n, hit_uv, -current_queue.directions[idx],
                         light_sample.direction, throughput.wavelengths, scene.num_spectral_channels,
                         dispersion_clamp);
                 }

                 SpectralPacket guiding_product = L_e * f_r * cos_surf;
                 SpectralPacket contribution = throughput * guiding_product * (1.0f / pdf);
                 float lobe_pdf_for_reservoir = 0.0f;
                 for (int c = 0; c < scene.num_spectral_channels; ++c) {
                     float pdf_mat_c = pdf_mat.values[c];
                     lobe_pdf_for_reservoir += pdf_mat_c;
                     float mis_weight = (pdf * pdf) / (pdf * pdf + pdf_mat_c * pdf_mat_c);
                     contribution.values[c] *= mis_weight;
                 }
                 lobe_pdf_for_reservoir /= fmaxf(1.0f, float(scene.num_spectral_channels));

                    if (light_sample.max_dist > 1e-4f) {
                        if (sigma_t_avg > 1e-4f) {
                            float tr_vals[kMaxPacketLanes];
                            for (int c = 0; c < scene.num_spectral_channels; ++c) {
                                tr_vals[c] = expf(-sigma_t.values[c] * light_sample.max_dist);
                            }

                            for (int c = 0; c < scene.num_spectral_channels; ++c) {
                                contribution.values[c] *= tr_vals[c];
                                guiding_product.values[c] *= tr_vals[c];
                            }
                        }

                         int s_idx = reserve_shadow_slot(shadow_queue);
                         if (s_idx >= 0) {
                             const int cap = shadow_queue.capacity;
                             GpuVec3 offset_normal = direct_light_offset_normal(ng, light_sample.direction);
                             float adaptive_eps = 1e-4f / fmaxf(0.01f, fabsf(ng.dot(light_sample.direction)));
                             shadow_queue.origins[s_idx] = p + offset_normal * adaptive_eps;
                             shadow_queue.directions[s_idx] = light_sample.direction;
                             shadow_queue.max_dist[s_idx] = light_sample.max_dist - adaptive_eps;
                             for (int c = 0; c < scene.num_spectral_channels; ++c) {
                                 shadow_queue.radiance_vals[c * cap + s_idx] = contribution.values[c];
                                 shadow_queue.radiance_wavelengths[c * cap + s_idx] = contribution.wavelengths[c];
                             }
                             shadow_queue.pixel_indices[s_idx] = pixel_index;
                             shadow_queue.spectral_modes[s_idx] = spectral_mode;
                             shadow_queue.active_channels[s_idx] = current_queue.active_channels[idx];
                             shadow_queue.wavelength_pdfs[s_idx] = current_queue.wavelength_pdfs[idx];
                             shadow_queue.light_list_indices[s_idx] = light_idx_idx;
                             shadow_queue.bsdf_lobe_pdfs[s_idx] = lobe_pdf_for_reservoir;
                             PathGuidingProductMetadata guide_metadata = path_guiding_product_metadata(
                                 guiding_product,
                                 scene.num_spectral_channels,
                                 spectral_mode,
                                 current_queue.active_channels[idx],
                                 current_queue.wavelength_pdfs[idx]);
                             shadow_queue.guiding_product_luminance[s_idx] = guide_metadata.luminance;
                             shadow_queue.guiding_wavelength_nm[s_idx] = guide_metadata.wavelength_nm;
                             shadow_queue.guiding_epochs[s_idx] = scene.path_guiding_epoch;
                             StokesVector restir_stokes = spectral_mode_is_sampled(spectral_mode)
                                 ? load_stokes(current_queue, idx, active_channel)
                                 : load_packet_average_stokes(current_queue, idx);
                             shadow_queue.stokes_i[s_idx] = restir_stokes.I;
                             shadow_queue.stokes_q[s_idx] = restir_stokes.Q;
                             shadow_queue.stokes_u[s_idx] = restir_stokes.U;
                             shadow_queue.stokes_v[s_idx] = restir_stokes.V;
                             shadow_queue.restir_replay_flags[s_idx] = 0;
                         }
                    }
            }
        }
    }

    GpuRay r_in;
    r_in.origin = current_queue.origins[idx];
    r_in.direction = current_queue.directions[idx];

    GpuRay scattered;
    SpectralPacket attenuation;

    StokesVector current_stokes = spectral_mode_is_sampled(spectral_mode)
        ? load_stokes(current_queue, idx, active_channel)
        : load_packet_average_stokes(current_queue, idx);

    GpuVec2 uv = hit_queue.uv[idx];

            if (is_composite) {
                float lobe_sample = sample_path_dimension(
                    sample_index, pixel_index, depth, kPathDimBsdfLobe);
                const ResolvedMaterialBsdfLobe& selected = lobe_sample < composite_mix
                    ? resolved_material.substrate
                    : resolved_material.coating;
                mat = selected.material;
                mat_soa = selected.spectra;
                dielectric_ior = selected.dielectric_ior;
                for (int c = 0; c < scene.num_spectral_channels; ++c) {
                    if (mat.type == MaterialType::Dielectric &&
                        (!isfinite(dielectric_ior.values[c]) || dielectric_ior.values[c] <= 0.0f)) return;
                }
            }

            float pdf_val = 0.0f;
            float ior_outside = 1.0f;
            bool front_face = r_in.direction.dot(ng) < 0.0f;
            if (front_face && current_medium_idx >= 0) {
                ior_outside = scene.materials[current_medium_idx].ior;
            }
            if (!is_layered && split_dispersive_dielectric_lanes(
                    current_queue,
                    next_queue,
                    idx,
                    mat,
                    mat_soa,
                    dielectric_ior,
                    p,
                    n,
                    ng,
                    uv,
                    throughput,
                    current_medium_idx,
                    mat_idx,
                    pixel_index,
                    depth,
                    seed,
                    dispersion_clamp,
                    ior_outside)) {
                return;
            }
            bool scattered_ok = is_layered
                ? scatter_layered_material(
                    resolved_material,
                    r_in,
                    p,
                    n,
                    uv,
                    throughput,
                    attenuation,
                    scattered,
                    current_stokes,
                    pdf_val,
                    sample_index,
                    pixel_index,
                    depth,
                    scene.num_spectral_channels)
                : scatter(r_in, mat, mat_soa.albedo, mat_soa.extinction, mat_soa.metal_eta, dielectric_ior, p, n, uv, throughput, attenuation, scattered, current_stokes, seed, pdf_val, dispersion_clamp, sample_index, pixel_index, depth, scene.num_spectral_channels, ior_outside, scene.materials[mat_idx].ior, spectral_mode, active_channel);
            if (scattered_ok) {
                if (is_composite && pdf_val > 0.0f) {
                    SpectralPacket pdf_a = pdf_bsdf_spectral(
                        resolved_material.coating.material, resolved_material.coating.dielectric_ior, n, uv,
                        -r_in.direction, scattered.direction, throughput.wavelengths,
                        scene.num_spectral_channels, dispersion_clamp);
                    SpectralPacket pdf_b = pdf_bsdf_spectral(
                        resolved_material.substrate.material, resolved_material.substrate.dielectric_ior, n, uv,
                        -r_in.direction, scattered.direction, throughput.wavelengths,
                        scene.num_spectral_channels, dispersion_clamp);
                    pdf_val = 0.0f;
                    for (int c = 0; c < scene.num_spectral_channels; ++c) {
                        pdf_val += pdf_a.values[c] * (1.0f - composite_mix) +
                            pdf_b.values[c] * composite_mix;
                    }
                    pdf_val /= fmaxf(1.0f, float(scene.num_spectral_channels));
                }
                SpectralPacket new_throughput = throughput * attenuation;

                for (int c = 0; c < scene.num_spectral_channels; ++c) {
                    if (!isfinite(new_throughput.values[c])) {
                        return;
                    }
                }

                if (depth > 3) {
                    float prob = spectral_survival_probability(new_throughput, scene.num_spectral_channels, rr_min_prob);

                    float r_rr = sample_path_dimension(sample_index, pixel_index, depth, kPathDimRussianRoulette);
                    if (r_rr > prob) {
                        return;
                    }
                    new_throughput = new_throughput * (1.0f / prob);
                }

                int next_flag = 0;
                bool is_delta = scene.light_count == 0 ||
                    (mat.type == MaterialType::Metal && mat.roughness <= 0.02f) ||
                    (mat.type == MaterialType::Dielectric && pdf_val <= 0.0f) ||
                    (is_layered && pdf_val <= 0.0f);
                if (is_delta) {
                    next_flag = 1;
                }

        int out_idx = reserve_ray_slot(next_queue);
        if (out_idx >= 0) {
            next_queue.origins[out_idx] = scattered.origin;
            next_queue.directions[out_idx] = scattered.direction;
            store_throughput(next_queue, out_idx, new_throughput);
            if (spectral_mode_is_sampled(spectral_mode)) {
                for (int c = 0; c < scene.num_spectral_channels; ++c) {
                    store_stokes(next_queue, out_idx, c, StokesVector(0.0f, 0.0f, 0.0f, 0.0f));
                }
                store_stokes(next_queue, out_idx, active_channel, current_stokes);
            } else if (is_layered) {
                store_stokes_packet(next_queue, out_idx, current_stokes);
            } else {
                store_packet_scattered_stokes(
                    current_queue,
                    next_queue,
                    idx,
                    out_idx,
                    mat,
                    mat_soa,
                    dielectric_ior,
                    r_in,
                    scattered,
                    n,
                    uv,
                    throughput,
                    ior_outside,
                    dispersion_clamp,
                    sample_index,
                    pixel_index,
                    depth);
            }
            next_queue.last_pdf[out_idx] = pdf_val;

            int next_medium = current_medium_idx;
            if (!is_layered && mat.type == MaterialType::Dielectric) {
                next_medium = next_dielectric_medium_index(
                    current_medium_idx, mat_idx, r_in.direction, scattered.direction, ng);
            }
            next_queue.medium_indices[out_idx] = next_medium;

            next_queue.seeds[out_idx] = seed;
            next_queue.pixel_indices[out_idx] = pixel_index;
            next_queue.depths[out_idx] = depth + 1;
            next_queue.flags[out_idx] = next_flag;
            next_queue.spectral_modes[out_idx] = spectral_mode;
            next_queue.active_channels[out_idx] = current_queue.active_channels[idx];
            next_queue.wavelength_pdfs[out_idx] = current_queue.wavelength_pdfs[idx];
        }
    }
}
