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

struct PrimarySampleMutationConfig {
    double large_step_probability = 0.3;
    double small_step_sigma = 0.01;
    unsigned int seed = 1;
};

struct PrimarySampleMutation {
    double value = 0.0;
    double proposal_pdf_forward = 0.0;
    double proposal_pdf_reverse = 0.0;
    bool large_step = false;
    unsigned int seed = 0;
};

PrimarySampleMutation mutate_primary_sample(double current,
                                            int dimension,
                                            int mutation_index,
                                            const PrimarySampleMutationConfig& config);
double metropolis_acceptance(double current_contribution, double proposed_contribution);

} // namespace ure::integrator
