#pragma once
#include "ure/gpu_structs.hpp"
#include <vector>

namespace ure::gpu {

struct MaterialLibrary {
    static GpuMaterial create_lambertian(const GpuVec3& albedo) {
        return {MaterialType::Lambertian, GpuSpectrum::from_rgb(albedo), 0.0f, 0.0f, GpuSpectrum(0.0f), GpuSpectrum(0.0f), 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, GpuSpectrum(0.0f), GpuSpectrum(0.0f), GpuSpectrum::from_rgb(GpuVec3(0,0,0))};
    }

    static GpuMaterial create_metal(const GpuVec3& albedo, float roughness, float ior = 0.0f, const GpuVec3& extinction = GpuVec3(0,0,0)) {
        return {MaterialType::Metal, GpuSpectrum::from_rgb(albedo), roughness, ior, GpuSpectrum(0.0f), GpuSpectrum::from_rgb(extinction), 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, GpuSpectrum(0.0f), GpuSpectrum(0.0f), GpuSpectrum::from_rgb(GpuVec3(0,0,0))};
    }

    static GpuMaterial create_dielectric(float ior, float dispersion = 0.0f) {
        return {MaterialType::Dielectric, GpuSpectrum::from_rgb(GpuVec3(1.0f, 1.0f, 1.0f)), 0.0f, ior, GpuSpectrum(0.0f), GpuSpectrum(0.0f), dispersion, 0.0f, 1.0f, 0.0f, 0.0f, GpuSpectrum(0.0f), GpuSpectrum(0.0f), GpuSpectrum::from_rgb(GpuVec3(0,0,0))};
    }

    static GpuMaterial create_light(const GpuVec3& emission) {
        return {MaterialType::Light, GpuSpectrum::from_rgb(GpuVec3(1.0f, 1.0f, 1.0f)), 0.0f, 0.0f, GpuSpectrum(0.0f), GpuSpectrum(0.0f), 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, GpuSpectrum(0.0f), GpuSpectrum(0.0f), GpuSpectrum::from_rgb(emission)};
    }
    
    // Preset Materials
    static GpuMaterial ground_grey() { return create_lambertian(GpuVec3(0.4f, 0.4f, 0.4f)); }
    static GpuMaterial glass_clear() { return create_dielectric(1.5f); }
    static GpuMaterial glass_diamond() { return create_dielectric(2.4f, 0.15f); } // Diamond exaggerated dispersion (0.044 -> 0.15)
    
    static GpuMaterial metal_gold_fuzzy() { 
        // Gold n, k approx
        return create_metal(GpuVec3(1.0f, 0.85f, 0.5f), 0.05f, 0.17f, GpuVec3(3.1f, 2.7f, 1.9f)); 
    }
    
    static GpuMaterial metal_copper() {
        GpuMaterial mat = create_metal(GpuVec3(0.95f, 0.64f, 0.54f), 0.02f, 0.27f, GpuVec3(3.61f, 2.62f, 2.29f));
        mat.metal_eta = GpuSpectrum::from_rgb(GpuVec3(0.20f, 0.924f, 1.102f));
        return mat;
    }

    static GpuMaterial metal_aluminum() {
        return create_metal(GpuVec3(0.91f, 0.92f, 0.92f), 0.01f, 1.2f, GpuVec3(7.0f, 6.0f, 5.0f));
    }

    static GpuMaterial red_matte() { return create_lambertian(GpuVec3(0.9f, 0.1f, 0.1f)); }
    static GpuMaterial blue_mesh() { return create_lambertian(GpuVec3(0.2f, 0.4f, 0.8f)); }
    // Increased intensity to 300.0 (was 200.0) -> Reduced to 100.0 -> Reduced to 50.0 for User Request
    static GpuMaterial bright_light() { return create_light(GpuVec3(50.0f, 50.0f, 50.0f)); } 
    // Procedural Grey Cloth
    static GpuMaterial cloth_grey_procedural() { 
        return {MaterialType::Cloth, GpuSpectrum::from_rgb(GpuVec3(0.5f, 0.5f, 0.5f)), 0.0f, 0.0f, GpuSpectrum(0.0f), GpuSpectrum(0.0f), 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, GpuSpectrum(0.0f), GpuSpectrum(0.0f), GpuSpectrum::from_rgb(GpuVec3(0,0,0))};
    }
};

}
