#include "ure/physics/acoustic/acoustic_ray_tracer.hpp"
#include "ure/physics/rigid_body.hpp"
#include <random>
#include <numbers>
#include <cmath>
#include <algorithm>

namespace ure {
namespace acoustic {

AcousticRayTracer::AcousticRayTracer(ure::physics::ISpatialQuery* spatial_query)
    : spatial_query(spatial_query) {}

std::vector<AcousticPath> AcousticRayTracer::trace_paths(
    const ure::core::Vec3<float>& source_pos,
    const ure::core::Vec3<float>& listener_pos,
    int max_depth,
    int num_rays
) {
    std::vector<AcousticPath> paths;
    
    // 1. Direct Path
    AcousticPath direct_path;
    direct_path.points.push_back(source_pos);
    direct_path.points.push_back(listener_pos);
    direct_path.total_distance = (listener_pos - source_pos).length();
    
    // Check occlusion
    float visibility = check_visibility(source_pos, listener_pos);
    
    // 1.1 Diffraction / Occlusion Logic
    if (visibility < 0.99f) {
         // Occluded!
         // Simple Diffraction Approximation:
         // Attenuate based on how "deep" the shadow is? 
         // For now, we just give it a penalty.
         direct_path.attenuation = 0.25f; // -12dB for being in shadow
    } else {
        direct_path.attenuation = 1.0f;
    }
    
    paths.push_back(direct_path);
    
    if (max_depth <= 0) return paths;
    
    // 2. Reflections & Refraction (Monte Carlo)
    // Random generator
    static std::mt19937 rng(12345);
    std::uniform_real_distribution<float> dist_01(0.0f, 1.0f);
    
    for (int i = 0; i < num_rays; ++i) {
        // Sample random direction (Sphere)
        float theta = 2.0f * std::numbers::pi_v<float> * dist_01(rng);
        float phi = std::acos(1.0f - 2.0f * dist_01(rng));
        
        float x = std::sin(phi) * std::cos(theta);
        float y = std::sin(phi) * std::sin(theta);
        float z = std::cos(phi);
        
        ure::core::Vec3<float> dir = {x, y, z};
        ure::core::Rayf ray;
        ray.origin = source_pos;
        ray.direction = dir;
        
        // Ray cast
        ure::physics::RayCastHit hit;
        if (spatial_query->ray_cast(ray, hit, 100.0f)) {
            ure::core::Vec3<float> hit_point = hit.point;
            
            // --- A. Reflection (Next Event Estimation) ---
            {
                // Check visibility from hit point to listener
                ure::core::Vec3<float> to_listener = listener_pos - hit_point;
                float dist_to_listener = to_listener.length();
                
                if (dist_to_listener > 1e-4f) {
                    ure::core::Vec3<float> shadow_origin = hit_point + hit.normal * 0.01f; 
                    float shadow_vis = check_visibility(shadow_origin, listener_pos);
                    
                    if (shadow_vis > 0.9f) {
                        AcousticPath path;
                        path.points.push_back(source_pos);
                        path.points.push_back(hit_point);
                        path.points.push_back(listener_pos);
                        
                        path.total_distance = hit.t + dist_to_listener;
                        
                        // Look up material
                        float reflectivity = 0.7f; // Default
                        if (hit.body && materials.count(hit.body->material_id)) {
                            reflectivity = 1.0f - materials[hit.body->material_id].absorption_coeff;
                        }
                        
                        path.attenuation = reflectivity; // Wall reflectivity
                        paths.push_back(path);
                    }
                }
            }
            
            // --- B. Refraction (Transmission) ---
            bool is_transparent = false;
            float transmission = 0.0f;
            if (hit.body && materials.count(hit.body->material_id)) {
                transmission = materials[hit.body->material_id].transmission_coeff;
                if (transmission > 0.01f) is_transparent = true;
            } else {
                 // Fallback for demo glass
                 if (hit.body && hit.body->material_id == 3) {
                     is_transparent = true;
                     transmission = 0.9f;
                 }
            }
            
            if (is_transparent) {
                // Trace a ray THROUGH the object.
                ure::core::Vec3<float> normal = hit.normal;
                float cos_theta = -dir.dot(normal);
                float eta = 1.0f; // Ratio of indices
                
                if (cos_theta > 0) {
                    // Entering
                    eta = 343.0f / 2000.0f; // Air -> Solid (approx 2000m/s for generic solid)
                } else {
                    // Exiting
                    eta = 2000.0f / 343.0f;
                    normal = normal * -1.0f;
                    cos_theta = -dir.dot(normal);
                }
                
                // Refraction vector
                float k = 1.0f - eta * eta * (1.0f - cos_theta * cos_theta);
                if (k >= 0.0f) {
                     ure::core::Vec3<float> refract_dir = dir * eta + normal * (eta * cos_theta - std::sqrt(k));
                     refract_dir = refract_dir.normalize();
                     
                     // Trace transmission ray
                     ure::core::Rayf trans_ray(hit_point - normal * 0.02f, refract_dir);
                     ure::physics::RayCastHit trans_hit;
                     
                     if (spatial_query->ray_cast(trans_ray, trans_hit, 10.0f)) {
                         // Connect to listener from exit point
                         ure::core::Vec3<float> exit_point = trans_hit.point;
                         
                         // Check visibility to listener from exit point
                         ure::core::Vec3<float> to_listener_trans = listener_pos - exit_point;
                         float dist_trans = to_listener_trans.length();
                         
                         if (dist_trans > 1e-4f && check_visibility(exit_point + trans_hit.normal * 0.01f, listener_pos) > 0.9f) {
                             AcousticPath path;
                             path.points.push_back(source_pos);
                             path.points.push_back(hit_point);   // Enter
                             path.points.push_back(exit_point);  // Exit
                             path.points.push_back(listener_pos);
                             
                             float d_air1 = hit.t;
                             float d_solid = trans_hit.t;
                             float d_air2 = dist_trans;
                            
                             // Speed correction for delay
                             path.total_distance = d_air1 + d_solid * (343.0f/2000.0f) + d_air2;
                             path.attenuation = transmission; // Transmission loss
                             paths.push_back(path);
                        }
                     }
                }
            }
        }
    }
    
    return paths;
}

float AcousticRayTracer::check_visibility(const ure::core::Vec3<float>& p1, const ure::core::Vec3<float>& p2) {
    ure::core::Vec3<float> dir = p2 - p1;
    float dist = dir.length();
    if (dist < 1e-4f) return 1.0f;
    
    dir = dir / dist;
    
    ure::core::Rayf ray;
    ray.origin = p1 + dir * 0.01f; // Offset
    ray.direction = dir;
    
    ure::physics::RayCastHit hit;
    // Check slightly less than full distance to avoid hitting the listener itself
    if (spatial_query->ray_cast(ray, hit, dist - 0.02f)) {
        return 0.0f; // Blocked
    }
    
    return 1.0f; // Clear
}

} // namespace acoustic
} // namespace ure
