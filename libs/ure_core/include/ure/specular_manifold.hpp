#pragma once

namespace ure::integrator {

enum class SpecularManifoldStatus {
    Disabled,
    Ready,
    TotalInternalReflection,
    InvalidInput
};

struct SpecularInterfaceConfig {
    double eta_i = 1.0;
    double eta_t = 1.5;
    double cos_theta_i = 1.0;
};

struct SpecularInterfaceConnection {
    SpecularManifoldStatus status = SpecularManifoldStatus::InvalidInput;
    double eta_i = 1.0;
    double eta_t = 1.0;
    double cos_theta_i = 0.0;
    double cos_theta_t = 0.0;
    double fresnel_reflectance = 1.0;
    double transmittance = 0.0;
    double forward_solid_angle_jacobian = 0.0;
    double reverse_solid_angle_jacobian = 0.0;
    double manifold_pdf = 0.0;
    double throughput_scale = 0.0;
};

bool is_ready(SpecularManifoldStatus status);
SpecularInterfaceConnection make_specular_interface_connection(const SpecularInterfaceConfig& config);

} // namespace ure::integrator
