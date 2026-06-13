#pragma once
#include "ure/gpu_structs.hpp"
#include <vector>

namespace ure::gpu {

struct MaterialLibrary {
    static GpuMaterialData create_lambertian(const GpuVec3& albedo) {
        return {
            {MaterialType::Lambertian, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, -1, -1, -1},
            GpuSpectrum(albedo.x, albedo.y, albedo.z), GpuSpectrum(0.0f), GpuSpectrum(0.0f),
            GpuSpectrum(0.0f), GpuSpectrum(0.0f), GpuSpectrum(0.0f)
        };
    }

    static GpuMaterialData create_metal(const GpuVec3& albedo, float roughness, float ior = 0.0f, const GpuVec3& extinction = GpuVec3(0,0,0)) {
        GpuMaterialData d;
        d.header = {MaterialType::Metal, roughness, ior, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, -1, -1, -1};
        d.albedo = GpuSpectrum(albedo.x, albedo.y, albedo.z);
        d.extinction = GpuSpectrum(extinction.x, extinction.y, extinction.z);
        d.metal_eta = GpuSpectrum(0.0f);
        d.medium_scattering = GpuSpectrum(0.0f);
        d.medium_absorption = GpuSpectrum(0.0f);
        d.emission = GpuSpectrum(0.0f);
        return d;
    }

    static GpuMaterialData create_dielectric(float ior, float dispersion = 0.0f) {
        return {
            {MaterialType::Dielectric, 0.0f, ior, dispersion, 0.0f, 1.0f, 0.0f, 0.0f, -1, -1, -1},
            GpuSpectrum(1.0f, 1.0f, 1.0f), GpuSpectrum(0.0f), GpuSpectrum(0.0f),
            GpuSpectrum(0.0f), GpuSpectrum(0.0f), GpuSpectrum(0.0f)
        };
    }

    static GpuMaterialData create_light(const GpuVec3& emission) {
        return {
            {MaterialType::Light, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, -1, -1, -1},
            GpuSpectrum(1.0f, 1.0f, 1.0f), GpuSpectrum(0.0f), GpuSpectrum(0.0f),
            GpuSpectrum(0.0f), GpuSpectrum(0.0f), GpuSpectrum(emission.x, emission.y, emission.z)
        };
    }

    // Preset Materials
    static GpuMaterialData ground_grey() { return create_lambertian(GpuVec3(0.4f, 0.4f, 0.4f)); }
    static GpuMaterialData glass_clear() { return create_dielectric(1.5f); }
    static GpuMaterialData glass_diamond() { return create_dielectric(2.4f, 0.15f); }

    static GpuMaterialData metal_gold_fuzzy() {
        return create_metal(GpuVec3(1.0f, 0.85f, 0.5f), 0.05f, 0.17f, GpuVec3(3.1f, 2.7f, 1.9f));
    }

    static GpuMaterialData metal_copper() {
        GpuMaterialData d = create_metal(GpuVec3(0.95f, 0.64f, 0.54f), 0.02f, 0.27f, GpuVec3(3.61f, 2.62f, 2.29f));
        d.metal_eta = GpuSpectrum(0.20f, 0.924f, 1.102f);
        return d;
    }

    static GpuMaterialData metal_aluminum() {
        return create_metal(GpuVec3(0.91f, 0.92f, 0.92f), 0.01f, 1.2f, GpuVec3(7.0f, 6.0f, 5.0f));
    }

    static GpuMaterialData red_matte() { return create_lambertian(GpuVec3(0.9f, 0.1f, 0.1f)); }
    static GpuMaterialData blue_mesh() { return create_lambertian(GpuVec3(0.2f, 0.4f, 0.8f)); }
    static GpuMaterialData bright_light() { return create_light(GpuVec3(50.0f, 50.0f, 50.0f)); }

    static GpuMaterialData cloth_grey_procedural() {
        return {
            {MaterialType::Cloth, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, -1, -1, -1},
            GpuSpectrum(0.5f, 0.5f, 0.5f), GpuSpectrum(0.0f), GpuSpectrum(0.0f),
            GpuSpectrum(0.0f), GpuSpectrum(0.0f), GpuSpectrum(0.0f)
        };
    }
};

}