#pragma once

#include "ure/integrator/bidirectional.cuh"

namespace ure::gpu {

inline constexpr int kMaxGpuManifoldVariables = 8;

struct GpuManifoldLinearSolveResult {
    float solution[kMaxGpuManifoldVariables] = {};
    float determinant = 0.0f;
    int valid = 0;
};

enum class GpuManifoldPrimitiveKind : int {
    Sphere = 0,
    Triangle = 1
};

struct GpuManifoldPrimitive {
    GpuManifoldPrimitiveKind kind = GpuManifoldPrimitiveKind::Sphere;
    GpuVec3 p0 = {};
    GpuVec3 p1 = {};
    GpuVec3 p2 = {};
    float radius = 0.0f;
};

struct GpuManifoldSurfacePoint {
    GpuVec3 position = {};
    GpuVec3 normal = {};
    GpuVec3 dp_du = {};
    GpuVec3 dp_dv = {};
    int valid = 0;
};

struct GpuSingleManifoldSolveResult {
    GpuManifoldSurfacePoint surface = {};
    float u = 0.0f;
    float v = 0.0f;
    float residual = 0.0f;
    float determinant = 0.0f;
    int iterations = 0;
    int total_internal_reflection = 0;
    int valid = 0;
};

struct GpuManifoldChainEvent {
    GpuManifoldPrimitive primitive = {};
    float u = 0.0f;
    float v = 0.0f;
    float eta_i = 1.0f;
    float eta_t = 1.0f;
    int transmission = 0;
};

struct GpuManifoldChainSolveResult {
    GpuManifoldSurfacePoint surfaces[4] = {};
    float parameters[kMaxGpuManifoldVariables] = {};
    float residual = 0.0f;
    float determinant = 0.0f;
    int iterations = 0;
    int failed_event = -1;
    int total_internal_reflection = 0;
    int valid = 0;
};

enum class GpuManifoldRejectReason : int {
    None = 0,
    NoTrailingChain = 1,
    UnsupportedMaterial = 2,
    InvalidPrimitive = 3,
    InvalidInitialState = 4,
    Singular = 5,
    TotalInternalReflection = 6,
    Residual = 7,
    Stale = 8
};

struct GpuManifoldPathSolution {
    GpuManifoldSurfacePoint surfaces[4] = {};
    float parameters[kMaxGpuManifoldVariables] = {};
    float determinant = 0.0f;
    float residual = 0.0f;
    std::uint32_t scene_epoch = 0;
    int anchor_camera_vertex = -1;
    int light_vertex = -1;
    int event_count = 0;
    int iterations = 0;
    GpuManifoldRejectReason reject_reason =
        GpuManifoldRejectReason::NoTrailingChain;
    int valid = 0;
};

struct GpuManifoldTelemetry {
    std::uint32_t attempted = 0;
    std::uint32_t converged = 0;
    std::uint32_t rejected_no_chain = 0;
    std::uint32_t rejected_material = 0;
    std::uint32_t rejected_primitive = 0;
    std::uint32_t rejected_singular = 0;
    std::uint32_t rejected_tir = 0;
    std::uint32_t rejected_residual = 0;
    std::uint32_t rejected_stale = 0;
    std::uint64_t total_iterations = 0;
};

static __device__ inline void project_gpu_manifold_parameters(
    GpuManifoldPrimitiveKind kind,
    float& u,
    float& v);

static __device__ inline bool extract_gpu_manifold_primitive(
    const GpuScene& scene,
    int geometry_type,
    int geometry_index,
    int primitive_index,
    GpuManifoldPrimitive& primitive) {
    primitive = {};
    if (geometry_type == 0) {
        if (primitive_index < 0 || primitive_index >= scene.sphere_count ||
            !scene.spheres) return false;
        const GpuSphere sphere = scene.spheres[primitive_index];
        if (!(sphere.radius > 0.0f)) return false;
        primitive.kind = GpuManifoldPrimitiveKind::Sphere;
        primitive.p0 = sphere.center;
        primitive.radius = sphere.radius;
        return true;
    }
    int mesh_index = geometry_index;
    const GpuInstance* instance = nullptr;
    if (geometry_type == 2) {
        if (geometry_index < 0 || geometry_index >= scene.instance_count ||
            !scene.instances) return false;
        instance = &scene.instances[geometry_index];
        mesh_index = instance->mesh_index;
    } else if (geometry_type != 1) {
        return false;
    }
    if (mesh_index < 0 || mesh_index >= scene.mesh_count || !scene.meshes) {
        return false;
    }
    const GpuMesh mesh = scene.meshes[mesh_index];
    if (primitive_index < 0 || primitive_index >= mesh.triangle_count ||
        !mesh.vertices || !mesh.indices) return false;
    const int i0 = mesh.indices[primitive_index * 3];
    const int i1 = mesh.indices[primitive_index * 3 + 1];
    const int i2 = mesh.indices[primitive_index * 3 + 2];
    if (i0 < 0 || i1 < 0 || i2 < 0) return false;
    primitive.kind = GpuManifoldPrimitiveKind::Triangle;
    primitive.p0 = mesh.vertices[i0];
    primitive.p1 = mesh.vertices[i1];
    primitive.p2 = mesh.vertices[i2];
    if (instance) {
        primitive.p0 = instance->transform.transform_point(primitive.p0);
        primitive.p1 = instance->transform.transform_point(primitive.p1);
        primitive.p2 = instance->transform.transform_point(primitive.p2);
    }
    return (primitive.p1 - primitive.p0).cross(
               primitive.p2 - primitive.p0).length_sq() > 1e-16f;
}

static __device__ inline bool initialize_gpu_manifold_parameters(
    const GpuManifoldPrimitive& primitive,
    const GpuVec3& position,
    float& u,
    float& v) {
    if (primitive.kind == GpuManifoldPrimitiveKind::Sphere) {
        const GpuVec3 offset = position - primitive.p0;
        if (offset.length_sq() <= 1e-16f) return false;
        const GpuVec3 normal = offset.normalize();
        u = acosf(fminf(1.0f, fmaxf(-1.0f, normal.y))) /
            3.14159265358979323846f;
        v = atan2f(normal.z, normal.x) / 6.2831853071795864769f;
        if (v < 0.0f) v += 1.0f;
        return true;
    }
    const GpuVec3 edge_u = primitive.p1 - primitive.p0;
    const GpuVec3 edge_v = primitive.p2 - primitive.p0;
    const GpuVec3 offset = position - primitive.p0;
    const float uu = edge_u.dot(edge_u);
    const float uv = edge_u.dot(edge_v);
    const float vv = edge_v.dot(edge_v);
    const float wu = offset.dot(edge_u);
    const float wv = offset.dot(edge_v);
    const float determinant = uu * vv - uv * uv;
    if (fabsf(determinant) <= 1e-16f) return false;
    u = (wu * vv - wv * uv) / determinant;
    v = (wv * uu - wu * uv) / determinant;
    project_gpu_manifold_parameters(primitive.kind, u, v);
    return true;
}

static __device__ inline GpuManifoldSurfacePoint
evaluate_gpu_manifold_surface(
    const GpuManifoldPrimitive& primitive,
    float u,
    float v) {
    GpuManifoldSurfacePoint surface = {};
    if (!isfinite(u) || !isfinite(v)) return surface;
    if (primitive.kind == GpuManifoldPrimitiveKind::Sphere) {
        if (!(primitive.radius > 0.0f) || u <= 0.0f || u >= 1.0f ||
            v < 0.0f || v >= 1.0f) return surface;
        const float theta = 3.14159265358979323846f * u;
        const float phi = 6.2831853071795864769f * v;
        const float sin_theta = sinf(theta);
        const float cos_theta = cosf(theta);
        const float sin_phi = sinf(phi);
        const float cos_phi = cosf(phi);
        surface.normal = GpuVec3(
            sin_theta * cos_phi, cos_theta, sin_theta * sin_phi);
        surface.position = primitive.p0 + surface.normal * primitive.radius;
        surface.dp_du = GpuVec3(
            cos_theta * cos_phi, -sin_theta, cos_theta * sin_phi) *
            (primitive.radius * 3.14159265358979323846f);
        surface.dp_dv = GpuVec3(
            -sin_theta * sin_phi, 0.0f, sin_theta * cos_phi) *
            (primitive.radius * 6.2831853071795864769f);
    } else {
        if (u < 0.0f || v < 0.0f || u + v > 1.0f) return surface;
        surface.dp_du = primitive.p1 - primitive.p0;
        surface.dp_dv = primitive.p2 - primitive.p0;
        const GpuVec3 raw_normal = surface.dp_du.cross(surface.dp_dv);
        if (raw_normal.length_sq() <= 1e-16f) return surface;
        surface.normal = raw_normal.normalize();
        surface.position = primitive.p0 + surface.dp_du * u +
            surface.dp_dv * v;
    }
    if (surface.dp_du.length_sq() <= 1e-16f ||
        surface.dp_dv.length_sq() <= 1e-16f) return {};
    surface.valid = 1;
    return surface;
}

static __device__ inline bool gpu_manifold_constraint(
    const GpuManifoldPrimitive& primitive,
    const GpuVec3& previous,
    const GpuVec3& next,
    float u,
    float v,
    int transmission,
    float eta_i,
    float eta_t,
    float* residual) {
    if (!residual || !(eta_i > 0.0f) || !(eta_t > 0.0f)) return false;
    const GpuManifoldSurfacePoint surface =
        evaluate_gpu_manifold_surface(primitive, u, v);
    if (!surface.valid) return false;
    const GpuVec3 to_previous = previous - surface.position;
    const GpuVec3 to_next = next - surface.position;
    if (to_previous.length_sq() <= 1e-16f ||
        to_next.length_sq() <= 1e-16f) return false;
    const GpuVec3 previous_direction = to_previous.normalize();
    const GpuVec3 next_direction = to_next.normalize();
    const float previous_side = previous_direction.dot(surface.normal);
    const float next_side = next_direction.dot(surface.normal);
    if ((!transmission &&
         (!(previous_side > 0.0f) || !(next_side > 0.0f))) ||
        (transmission && previous_side * next_side >= 0.0f)) return false;
    GpuVec3 generalized_half = transmission
        ? previous_direction * eta_i + next_direction * eta_t
        : previous_direction + next_direction;
    if (generalized_half.length_sq() <= 1e-16f) return false;
    generalized_half = generalized_half.normalize();
    const GpuVec3 tangent_u = surface.dp_du.normalize();
    GpuVec3 tangent_v = surface.normal.cross(tangent_u);
    if (tangent_v.length_sq() <= 1e-16f) return false;
    tangent_v = tangent_v.normalize();
    residual[0] = generalized_half.dot(tangent_u);
    residual[1] = generalized_half.dot(tangent_v);
    return isfinite(residual[0]) && isfinite(residual[1]);
}

static __device__ inline void project_gpu_manifold_parameters(
    GpuManifoldPrimitiveKind kind,
    float& u,
    float& v) {
    if (kind == GpuManifoldPrimitiveKind::Sphere) {
        u = fminf(1.0f - 1e-5f, fmaxf(1e-5f, u));
        v = v - floorf(v);
        if (v < 0.0f) v += 1.0f;
    } else {
        u = fmaxf(0.0f, u);
        v = fmaxf(0.0f, v);
        const float sum = u + v;
        if (sum > 1.0f) {
            u /= sum;
            v /= sum;
        }
    }
}

static __device__ inline GpuManifoldLinearSolveResult
solve_gpu_manifold_linear_system(
    const float* matrix,
    const float* right_hand_side,
    int dimension,
    float pivot_tolerance) {
    GpuManifoldLinearSolveResult result = {};
    if (!matrix || !right_hand_side || dimension <= 0 ||
        dimension > kMaxGpuManifoldVariables || !(pivot_tolerance > 0.0f)) {
        return result;
    }
    float augmented[kMaxGpuManifoldVariables]
                   [kMaxGpuManifoldVariables + 1] = {};
    for (int row = 0; row < dimension; ++row) {
        for (int column = 0; column < dimension; ++column) {
            const float value = matrix[row * dimension + column];
            if (!isfinite(value)) return result;
            augmented[row][column] = value;
        }
        if (!isfinite(right_hand_side[row])) return result;
        augmented[row][dimension] = right_hand_side[row];
    }
    float determinant = 1.0f;
    int determinant_sign = 1;
    for (int column = 0; column < dimension; ++column) {
        int pivot_row = column;
        float pivot_magnitude = fabsf(augmented[column][column]);
        for (int row = column + 1; row < dimension; ++row) {
            const float magnitude = fabsf(augmented[row][column]);
            if (magnitude > pivot_magnitude) {
                pivot_magnitude = magnitude;
                pivot_row = row;
            }
        }
        if (!(pivot_magnitude > pivot_tolerance)) return result;
        if (pivot_row != column) {
            for (int entry = column; entry <= dimension; ++entry) {
                const float temporary = augmented[column][entry];
                augmented[column][entry] = augmented[pivot_row][entry];
                augmented[pivot_row][entry] = temporary;
            }
            determinant_sign = -determinant_sign;
        }
        const float pivot = augmented[column][column];
        determinant *= pivot;
        for (int row = column + 1; row < dimension; ++row) {
            const float factor = augmented[row][column] / pivot;
            augmented[row][column] = 0.0f;
            for (int entry = column + 1; entry <= dimension; ++entry) {
                augmented[row][entry] -= factor * augmented[column][entry];
            }
        }
    }
    for (int row = dimension - 1; row >= 0; --row) {
        float value = augmented[row][dimension];
        for (int column = row + 1; column < dimension; ++column) {
            value -= augmented[row][column] * result.solution[column];
        }
        result.solution[row] = value / augmented[row][row];
        if (!isfinite(result.solution[row])) return GpuManifoldLinearSolveResult{};
    }
    result.determinant = determinant * float(determinant_sign);
    result.valid = isfinite(result.determinant) ? 1 : 0;
    return result;
}

static __device__ inline GpuManifoldLinearSolveResult
solve_gpu_manifold_newton_step(
    const float* jacobian,
    const float* residual,
    int dimension,
    float pivot_tolerance) {
    float right_hand_side[kMaxGpuManifoldVariables] = {};
    if (!residual || dimension <= 0 ||
        dimension > kMaxGpuManifoldVariables) return {};
    for (int index = 0; index < dimension; ++index) {
        right_hand_side[index] = -residual[index];
    }
    return solve_gpu_manifold_linear_system(
        jacobian, right_hand_side, dimension, pivot_tolerance);
}

static __device__ inline GpuSingleManifoldSolveResult
solve_gpu_single_manifold_vertex(
    const GpuManifoldPrimitive& primitive,
    const GpuVec3& previous,
    const GpuVec3& next,
    float initial_u,
    float initial_v,
    int transmission,
    float eta_i,
    float eta_t,
    float tolerance,
    int max_iterations) {
    GpuSingleManifoldSolveResult result = {};
    if (!(tolerance > 0.0f) || max_iterations <= 0 ||
        max_iterations > 64) return result;
    float u = initial_u;
    float v = initial_v;
    project_gpu_manifold_parameters(primitive.kind, u, v);
    constexpr float kDifferenceStep = 1e-4f;
    constexpr float kPivotTolerance = 1e-8f;
    for (int iteration = 0; iteration < max_iterations; ++iteration) {
        if (transmission) {
            const GpuManifoldSurfacePoint candidate_surface =
                evaluate_gpu_manifold_surface(primitive, u, v);
            const GpuVec3 to_previous = previous - candidate_surface.position;
            if (!candidate_surface.valid ||
                to_previous.length_sq() <= 1e-16f) return result;
            const float cosine = fabsf(
                to_previous.normalize().dot(candidate_surface.normal));
            const float sine_squared = fmaxf(
                0.0f, 1.0f - cosine * cosine);
            const float eta = eta_i / eta_t;
            if (eta * eta * sine_squared >= 1.0f) {
                result.total_internal_reflection = 1;
                return result;
            }
        }
        float residual[2] = {};
        if (!gpu_manifold_constraint(
                primitive, previous, next, u, v, transmission,
                eta_i, eta_t, residual)) return result;
        const float norm = sqrtf(
            residual[0] * residual[0] + residual[1] * residual[1]);
        result.iterations = iteration;
        if (norm <= tolerance) {
            result.surface = evaluate_gpu_manifold_surface(primitive, u, v);
            result.u = u;
            result.v = v;
            result.residual = norm;
            if (transmission) {
                GpuVec3 incident = previous - result.surface.position;
                if (incident.length_sq() <= 1e-16f) return {};
                incident = incident.normalize();
                const float cosine = fabsf(incident.dot(result.surface.normal));
                const float sine_squared = fmaxf(0.0f, 1.0f - cosine * cosine);
                const float eta = eta_i / eta_t;
                if (eta * eta * sine_squared >= 1.0f) {
                    result.total_internal_reflection = 1;
                    result.valid = 0;
                    return result;
                }
            }
            result.valid = result.surface.valid;
            return result;
        }
        float jacobian[4] = {};
        for (int variable = 0; variable < 2; ++variable) {
            float plus_u = u;
            float plus_v = v;
            float minus_u = u;
            float minus_v = v;
            if (variable == 0) {
                plus_u += kDifferenceStep;
                minus_u -= kDifferenceStep;
            } else {
                plus_v += kDifferenceStep;
                minus_v -= kDifferenceStep;
            }
            project_gpu_manifold_parameters(primitive.kind, plus_u, plus_v);
            project_gpu_manifold_parameters(primitive.kind, minus_u, minus_v);
            float plus_residual[2] = {};
            float minus_residual[2] = {};
            if (!gpu_manifold_constraint(
                    primitive, previous, next, plus_u, plus_v, transmission,
                    eta_i, eta_t, plus_residual) ||
                !gpu_manifold_constraint(
                    primitive, previous, next, minus_u, minus_v, transmission,
                    eta_i, eta_t, minus_residual)) return result;
            const float parameter_delta = variable == 0
                ? plus_u - minus_u : plus_v - minus_v;
            if (fabsf(parameter_delta) <= 1e-8f) return result;
            jacobian[variable] =
                (plus_residual[0] - minus_residual[0]) / parameter_delta;
            jacobian[2 + variable] =
                (plus_residual[1] - minus_residual[1]) / parameter_delta;
        }
        const GpuManifoldLinearSolveResult step =
            solve_gpu_manifold_newton_step(
                jacobian, residual, 2, kPivotTolerance);
        if (!step.valid) return result;
        result.determinant = step.determinant;
        bool accepted = false;
        float damping = 1.0f;
        for (int line_search = 0; line_search < 10; ++line_search) {
            float candidate_u = u + damping * step.solution[0];
            float candidate_v = v + damping * step.solution[1];
            project_gpu_manifold_parameters(
                primitive.kind, candidate_u, candidate_v);
            float candidate_residual[2] = {};
            if (gpu_manifold_constraint(
                    primitive, previous, next, candidate_u, candidate_v,
                    transmission, eta_i, eta_t, candidate_residual)) {
                const float candidate_norm = sqrtf(
                    candidate_residual[0] * candidate_residual[0] +
                    candidate_residual[1] * candidate_residual[1]);
                if (candidate_norm < norm) {
                    u = candidate_u;
                    v = candidate_v;
                    accepted = true;
                    break;
                }
            }
            damping *= 0.5f;
        }
        if (!accepted) return result;
    }
    float residual[2] = {};
    if (!gpu_manifold_constraint(
            primitive, previous, next, u, v, transmission,
            eta_i, eta_t, residual)) return result;
    result.surface = evaluate_gpu_manifold_surface(primitive, u, v);
    result.u = u;
    result.v = v;
    result.residual = sqrtf(
        residual[0] * residual[0] + residual[1] * residual[1]);
    result.iterations = max_iterations;
    result.valid = result.surface.valid && result.residual <= tolerance;
    return result;
}

static __device__ inline bool evaluate_gpu_manifold_chain_residual(
    const GpuManifoldChainEvent* events,
    int event_count,
    const GpuVec3& camera_endpoint,
    const GpuVec3& light_endpoint,
    const float* parameters,
    float* residual,
    GpuManifoldSurfacePoint* surfaces,
    int* failed_event,
    int* total_internal_reflection) {
    if (!events || !parameters || !residual || !surfaces ||
        event_count <= 0 || event_count > 4) return false;
    for (int event = 0; event < event_count; ++event) {
        surfaces[event] = evaluate_gpu_manifold_surface(
            events[event].primitive, parameters[event * 2],
            parameters[event * 2 + 1]);
        if (!surfaces[event].valid) {
            if (failed_event) *failed_event = event;
            return false;
        }
    }
    for (int event = 0; event < event_count; ++event) {
        const GpuVec3 previous = event == 0
            ? camera_endpoint : surfaces[event - 1].position;
        const GpuVec3 next = event + 1 == event_count
            ? light_endpoint : surfaces[event + 1].position;
        if (events[event].transmission) {
            const GpuVec3 incident = previous - surfaces[event].position;
            if (incident.length_sq() <= 1e-16f) {
                if (failed_event) *failed_event = event;
                return false;
            }
            const float cosine = fabsf(
                incident.normalize().dot(surfaces[event].normal));
            const float sine_squared = fmaxf(
                0.0f, 1.0f - cosine * cosine);
            const float eta = events[event].eta_i / events[event].eta_t;
            if (eta * eta * sine_squared >= 1.0f) {
                if (failed_event) *failed_event = event;
                if (total_internal_reflection) *total_internal_reflection = 1;
                return false;
            }
        }
        if (!gpu_manifold_constraint(
                events[event].primitive, previous, next,
                parameters[event * 2], parameters[event * 2 + 1],
                events[event].transmission, events[event].eta_i,
                events[event].eta_t, residual + event * 2)) {
            if (failed_event) *failed_event = event;
            return false;
        }
    }
    return true;
}

static __device__ inline float gpu_manifold_residual_norm(
    const float* residual,
    int dimension) {
    float squared = 0.0f;
    for (int index = 0; index < dimension; ++index) {
        squared += residual[index] * residual[index];
    }
    return sqrtf(squared);
}

static __device__ inline GpuManifoldChainSolveResult
solve_gpu_manifold_chain(
    const GpuManifoldChainEvent* events,
    int event_count,
    const GpuVec3& camera_endpoint,
    const GpuVec3& light_endpoint,
    float tolerance,
    int max_iterations) {
    GpuManifoldChainSolveResult result = {};
    if (!events || event_count <= 0 || event_count > 4 ||
        !(tolerance > 0.0f) || max_iterations <= 0 ||
        max_iterations > 64) return result;
    const int dimension = event_count * 2;
    float parameters[kMaxGpuManifoldVariables] = {};
    for (int event = 0; event < event_count; ++event) {
        parameters[event * 2] = events[event].u;
        parameters[event * 2 + 1] = events[event].v;
        project_gpu_manifold_parameters(
            events[event].primitive.kind, parameters[event * 2],
            parameters[event * 2 + 1]);
    }
    constexpr float kDifferenceStep = 1e-4f;
    for (int iteration = 0; iteration < max_iterations; ++iteration) {
        float residual[kMaxGpuManifoldVariables] = {};
        GpuManifoldSurfacePoint surfaces[4] = {};
        int failed_event = -1;
        int total_internal_reflection = 0;
        if (!evaluate_gpu_manifold_chain_residual(
                events, event_count, camera_endpoint, light_endpoint,
                parameters, residual, surfaces, &failed_event,
                &total_internal_reflection)) {
            result.failed_event = failed_event;
            result.total_internal_reflection = total_internal_reflection;
            return result;
        }
        const float norm = gpu_manifold_residual_norm(residual, dimension);
        result.iterations = iteration;
        if (norm <= tolerance) {
            for (int event = 0; event < event_count; ++event) {
                result.surfaces[event] = surfaces[event];
                result.parameters[event * 2] = parameters[event * 2];
                result.parameters[event * 2 + 1] =
                    parameters[event * 2 + 1];
            }
            result.residual = norm;
            result.valid = 1;
            return result;
        }
        float jacobian[kMaxGpuManifoldVariables *
                       kMaxGpuManifoldVariables] = {};
        for (int variable = 0; variable < dimension; ++variable) {
            float plus[kMaxGpuManifoldVariables] = {};
            float minus[kMaxGpuManifoldVariables] = {};
            for (int index = 0; index < dimension; ++index) {
                plus[index] = parameters[index];
                minus[index] = parameters[index];
            }
            plus[variable] += kDifferenceStep;
            minus[variable] -= kDifferenceStep;
            const int event = variable / 2;
            project_gpu_manifold_parameters(
                events[event].primitive.kind, plus[event * 2],
                plus[event * 2 + 1]);
            project_gpu_manifold_parameters(
                events[event].primitive.kind, minus[event * 2],
                minus[event * 2 + 1]);
            float plus_residual[kMaxGpuManifoldVariables] = {};
            float minus_residual[kMaxGpuManifoldVariables] = {};
            GpuManifoldSurfacePoint scratch[4] = {};
            if (!evaluate_gpu_manifold_chain_residual(
                    events, event_count, camera_endpoint, light_endpoint,
                    plus, plus_residual, scratch, nullptr, nullptr) ||
                !evaluate_gpu_manifold_chain_residual(
                    events, event_count, camera_endpoint, light_endpoint,
                    minus, minus_residual, scratch, nullptr, nullptr)) {
                result.failed_event = event;
                return result;
            }
            const float delta = plus[variable] - minus[variable];
            if (fabsf(delta) <= 1e-8f) return result;
            for (int row = 0; row < dimension; ++row) {
                jacobian[row * dimension + variable] =
                    (plus_residual[row] - minus_residual[row]) / delta;
            }
        }
        const GpuManifoldLinearSolveResult step =
            solve_gpu_manifold_newton_step(
                jacobian, residual, dimension, 1e-8f);
        if (!step.valid) return result;
        result.determinant = step.determinant;
        bool accepted = false;
        float damping = 1.0f;
        for (int line_search = 0; line_search < 10; ++line_search) {
            float candidate[kMaxGpuManifoldVariables] = {};
            for (int index = 0; index < dimension; ++index) {
                candidate[index] = parameters[index] +
                    damping * step.solution[index];
            }
            for (int event = 0; event < event_count; ++event) {
                project_gpu_manifold_parameters(
                    events[event].primitive.kind, candidate[event * 2],
                    candidate[event * 2 + 1]);
            }
            float candidate_residual[kMaxGpuManifoldVariables] = {};
            GpuManifoldSurfacePoint candidate_surfaces[4] = {};
            if (evaluate_gpu_manifold_chain_residual(
                    events, event_count, camera_endpoint, light_endpoint,
                    candidate, candidate_residual, candidate_surfaces,
                    nullptr, nullptr) &&
                gpu_manifold_residual_norm(candidate_residual, dimension) <
                    norm) {
                for (int index = 0; index < dimension; ++index) {
                    parameters[index] = candidate[index];
                }
                accepted = true;
                break;
            }
            damping *= 0.5f;
        }
        if (!accepted) return result;
    }
    result.iterations = max_iterations;
    return result;
}

}
