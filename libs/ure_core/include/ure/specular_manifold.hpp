#pragma once

#include <cstdint>

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

enum class PathVertexMeasure {
    Discrete,
    Area,
    Volume
};

struct BidirectionalPdfVertex {
    double forward_directional_pdf = 0.0;
    double reverse_directional_pdf = 0.0;
    double forward_measure_pdf = 0.0;
    double reverse_measure_pdf = 0.0;
    PathVertexMeasure measure = PathVertexMeasure::Area;
    bool delta = false;
};

struct BidirectionalTechnique {
    int light_vertices = 0;
    int camera_vertices = 0;
    double probability = 0.0;
    bool valid = false;
};

struct BidirectionalPdfEdge {
    double forward_measure_pdf = 0.0;
    double reverse_measure_pdf = 0.0;
    bool from_delta = false;
    bool to_delta = false;
};

double solid_angle_to_area_pdf(double directional_pdf,
                               double distance_squared,
                               double target_abs_cosine);
double solid_angle_to_volume_pdf(double directional_pdf,
                                 double distance_squared);
double bidirectional_power_heuristic(const double* technique_probabilities,
                                     int count,
                                     int selected);
int enumerate_bidirectional_techniques(int light_vertex_count,
                                       int camera_vertex_count,
                                       BidirectionalTechnique* output,
                                       int capacity);
int reconstruct_bidirectional_strategy_probabilities(
    const BidirectionalPdfEdge* edges,
    int edge_count,
    double light_endpoint_pdf,
    double camera_endpoint_pdf,
    double* probabilities,
    int capacity);
double progressive_surface_merge_radius(double initial_radius,
                                        double alpha,
                                        std::uint64_t iteration);
double progressive_volume_merge_radius(double initial_radius,
                                       double alpha,
                                       std::uint64_t iteration);
double surface_merge_kernel_normalization(double radius);
double volume_merge_kernel_normalization(double radius);
double vcm_merge_power_heuristic(double merge_density,
                                 double connection_density);

} // namespace ure::integrator
