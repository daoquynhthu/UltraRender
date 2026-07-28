#include "ure/backend.hpp"

#include <cuda_runtime.h>

#include <algorithm>
#include <array>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

#if defined(UR_ENABLE_VULKAN)
#include "ure/vulkan_runtime.hpp"
#endif

namespace ure {

namespace {

constexpr std::array<BackendFeature, 16> kBackendFeatures = {
    BackendFeature::Compute,
    BackendFeature::Subgroup,
    BackendFeature::Int64,
    BackendFeature::FloatAtomics,
    BackendFeature::TextureSampling,
    BackendFeature::MultiAdapter,
    BackendFeature::SpectralTransport,
    BackendFeature::Polarization,
    BackendFeature::PathGuiding,
    BackendFeature::Restir,
    BackendFeature::Bidirectional,
    BackendFeature::Mlt,
    BackendFeature::WaveReference,
    BackendFeature::SelfComputeTraversal,
    BackendFeature::RayQuery,
    BackendFeature::RayTracingPipeline
};

std::string cuda_error_message(cudaError_t error, const char* operation) {
    return std::string(operation) + ": " + cudaGetErrorString(error);
}

std::string cuda_adapter_id(const cudaDeviceProp& properties) {
    std::ostringstream stream;
    stream << "cuda:";
    for (const auto byte : properties.uuid.bytes) {
        stream << std::hex << std::setw(2) << std::setfill('0')
               << static_cast<unsigned int>(
                      static_cast<unsigned char>(byte));
    }
    return stream.str();
}

std::string cuda_driver_identity() {
    int version = 0;
    const auto error = cudaDriverGetVersion(&version);
    if (error != cudaSuccess) {
        throw std::runtime_error(cuda_error_message(
            error, "cudaDriverGetVersion"));
    }
    return "CUDA driver " + std::to_string(version / 1000) + "." +
           std::to_string((version % 1000) / 10);
}

std::string cuda_compiler_identity() {
    return "CUDA toolkit " + std::to_string(CUDART_VERSION / 1000) + "." +
           std::to_string((CUDART_VERSION % 1000) / 10);
}

BackendFeatureSet cuda_features(int device_count) {
    BackendFeatureSet features = 0;
    for (const auto feature : kBackendFeatures) {
        if (feature == BackendFeature::RayQuery ||
            feature == BackendFeature::RayTracingPipeline) {
            continue;
        }
        if (feature != BackendFeature::MultiAdapter || device_count > 1) {
            features |= backend_feature_bit(feature);
        }
    }
    return features;
}

BackendAdapterInfo query_cuda_adapter(int ordinal, int device_count) {
    cudaDeviceProp properties{};
    auto error = cudaGetDeviceProperties(&properties, ordinal);
    if (error != cudaSuccess) {
        throw std::runtime_error(cuda_error_message(
            error, "cudaGetDeviceProperties"));
    }
    int previous_device = 0;
    error = cudaGetDevice(&previous_device);
    if (error != cudaSuccess) {
        throw std::runtime_error(cuda_error_message(error, "cudaGetDevice"));
    }
    error = cudaSetDevice(ordinal);
    if (error != cudaSuccess) {
        throw std::runtime_error(cuda_error_message(error, "cudaSetDevice"));
    }
    std::size_t available = 0;
    std::size_t total = 0;
    error = cudaMemGetInfo(&available, &total);
    const auto restore_error = cudaSetDevice(previous_device);
    if (error != cudaSuccess) {
        throw std::runtime_error(cuda_error_message(error, "cudaMemGetInfo"));
    }
    if (restore_error != cudaSuccess) {
        throw std::runtime_error(cuda_error_message(
            restore_error, "cudaSetDevice restore"));
    }

    BackendAdapterInfo info;
    int gpu_pci_identity = 0;
    error = cudaDeviceGetAttribute(
        &gpu_pci_identity,
        cudaDevAttrGpuPciDeviceId,
        ordinal);
    if (error != cudaSuccess) {
        throw std::runtime_error(cuda_error_message(
            error, "cudaDevAttrGpuPciDeviceId"));
    }
    if (gpu_pci_identity == 0) {
        throw std::runtime_error(
            "cudaDevAttrGpuPciDeviceId returned zero");
    }
    info.kind = BackendKind::Cuda;
    info.adapter_id = cuda_adapter_id(properties);
    info.ordinal = static_cast<std::uint32_t>(ordinal);
    info.vendor_id =
        static_cast<std::uint32_t>(
            gpu_pci_identity) &
        0xffffu;
    info.device_id =
        static_cast<std::uint32_t>(
            gpu_pci_identity) >>
        16u;
    info.name = properties.name;
    info.features = cuda_features(device_count);
    info.limits.max_workgroup_threads =
        static_cast<std::uint32_t>(properties.maxThreadsPerBlock);
    info.limits.subgroup_size =
        static_cast<std::uint32_t>(properties.warpSize);
    info.limits.max_grid_dimension_x =
        static_cast<std::uint32_t>(properties.maxGridSize[0]);
    info.limits.max_grid_dimension_y =
        static_cast<std::uint32_t>(properties.maxGridSize[1]);
    info.limits.max_grid_dimension_z =
        static_cast<std::uint32_t>(properties.maxGridSize[2]);
    info.limits.max_shared_memory_per_workgroup =
        static_cast<std::uint64_t>(properties.sharedMemPerBlock);
    info.limits.max_spectral_packet_lanes = 32;
    info.memory.total_bytes = static_cast<std::uint64_t>(total);
    info.memory.available_bytes = static_cast<std::uint64_t>(available);
    info.driver_identity = cuda_driver_identity();
    info.compiler_identity = cuda_compiler_identity();
    return info;
}

}

const char* backend_kind_name(BackendKind kind) {
    switch (kind) {
    case BackendKind::Auto: return "auto";
    case BackendKind::Cuda: return "cuda";
    case BackendKind::Vulkan: return "vulkan";
    case BackendKind::D3D12: return "d3d12";
    }
    return "invalid";
}

std::optional<BackendKind> parse_backend_kind(std::string_view name) {
    if (name == "auto") return BackendKind::Auto;
    if (name == "cuda") return BackendKind::Cuda;
    if (name == "vulkan") return BackendKind::Vulkan;
    if (name == "d3d12") return BackendKind::D3D12;
    return std::nullopt;
}

const char* backend_feature_name(BackendFeature feature) {
    switch (feature) {
    case BackendFeature::Compute: return "compute";
    case BackendFeature::Subgroup: return "subgroup";
    case BackendFeature::Int64: return "int64";
    case BackendFeature::FloatAtomics: return "float_atomics";
    case BackendFeature::TextureSampling: return "texture_sampling";
    case BackendFeature::MultiAdapter: return "multi_adapter";
    case BackendFeature::SpectralTransport: return "spectral_transport";
    case BackendFeature::Polarization: return "polarization";
    case BackendFeature::PathGuiding: return "path_guiding";
    case BackendFeature::Restir: return "restir";
    case BackendFeature::Bidirectional: return "bidirectional";
    case BackendFeature::Mlt: return "mlt";
    case BackendFeature::WaveReference: return "wave_reference";
    case BackendFeature::SelfComputeTraversal: return "self_compute_traversal";
    case BackendFeature::RayQuery: return "ray_query";
    case BackendFeature::RayTracingPipeline:
        return "ray_tracing_pipeline";
    }
    return "invalid";
}

std::optional<BackendFeature> parse_backend_feature(std::string_view name) {
    for (const auto feature : kBackendFeatures) {
        if (name == backend_feature_name(feature)) return feature;
    }
    return std::nullopt;
}

BackendFeatureSet required_backend_features(const RenderConfig& config) {
    BackendFeatureSet features =
        backend_feature_bit(BackendFeature::Compute) |
        backend_feature_bit(BackendFeature::Subgroup) |
        backend_feature_bit(BackendFeature::Int64) |
        backend_feature_bit(BackendFeature::FloatAtomics) |
        backend_feature_bit(BackendFeature::TextureSampling) |
        backend_feature_bit(BackendFeature::SpectralTransport) |
        backend_feature_bit(BackendFeature::Polarization) |
        backend_feature_bit(BackendFeature::SelfComputeTraversal);
    if (config.num_gpus_to_use > 1) {
        features |= backend_feature_bit(BackendFeature::MultiAdapter);
    }
    if (config.path_guiding.enabled ||
        config.integrator.mode == IntegratorMode::PathGuided) {
        features |= backend_feature_bit(BackendFeature::PathGuiding);
    }
    if (config.restir_di.enabled || config.restir_pt.enabled ||
        config.integrator.mode == IntegratorMode::RestirDI ||
        config.integrator.mode == IntegratorMode::RestirPT) {
        features |= backend_feature_bit(BackendFeature::Restir);
    }
    if (config.bidirectional.enabled || config.vcm.enabled ||
        config.specular_manifold.enabled ||
        config.integrator.mode == IntegratorMode::BDPT ||
        config.integrator.mode == IntegratorMode::VCM ||
        config.integrator.mode == IntegratorMode::SpecularManifold) {
        features |= backend_feature_bit(BackendFeature::Bidirectional);
    }
    if (config.mlt.enabled ||
        config.integrator.mode == IntegratorMode::MLT) {
        features |= backend_feature_bit(BackendFeature::Mlt);
    }
    return features | config.backend.required_features;
}

std::vector<BackendAdapterInfo> enumerate_backend_adapters(BackendKind kind) {
    if (kind == BackendKind::D3D12) {
        return {};
    }
    if (kind != BackendKind::Auto &&
        kind != BackendKind::Cuda &&
        kind != BackendKind::Vulkan) {
        throw std::invalid_argument("invalid backend kind");
    }
    std::vector<BackendAdapterInfo> adapters;
    if (kind == BackendKind::Vulkan) {
#if defined(UR_ENABLE_VULKAN)
        return vulkan::enumerate_vulkan_adapters();
#else
        return {};
#endif
    }
    int count = 0;
    const auto error = cudaGetDeviceCount(&count);
    if (error != cudaSuccess) {
        throw std::runtime_error(cuda_error_message(
            error, "cudaGetDeviceCount"));
    }
    adapters.reserve(static_cast<std::size_t>(count));
    for (int ordinal = 0; ordinal < count; ++ordinal) {
        adapters.push_back(query_cuda_adapter(ordinal, count));
    }
    if (kind == BackendKind::Auto) {
#if defined(UR_ENABLE_VULKAN)
        auto vulkan_adapters =
            vulkan::enumerate_vulkan_adapters();
        adapters.insert(
            adapters.end(),
            std::make_move_iterator(vulkan_adapters.begin()),
            std::make_move_iterator(vulkan_adapters.end()));
#endif
    }
    return adapters;
}

BackendSelection select_backend(const RenderConfig& config) {
    const BackendKind requested = config.backend.kind;
    if (requested == BackendKind::D3D12) {
        throw std::invalid_argument(
            std::string("requested backend is unavailable: ") +
            backend_kind_name(requested));
    }
    if (requested != BackendKind::Auto &&
        requested != BackendKind::Cuda &&
        requested != BackendKind::Vulkan) {
        throw std::invalid_argument("invalid backend kind");
    }
    const auto selected_kind = requested == BackendKind::Auto
        ? BackendKind::Cuda
        : requested;
    auto adapters = enumerate_backend_adapters(selected_kind);
    if (adapters.empty()) {
        throw std::runtime_error(
            std::string("no ") +
            backend_kind_name(selected_kind) +
            " adapter is available");
    }
    BackendAdapterInfo* selected = nullptr;
    if (!config.backend.adapter_id.empty()) {
        const auto found = std::find_if(
            adapters.begin(), adapters.end(), [&](const auto& adapter) {
                return adapter.adapter_id == config.backend.adapter_id;
            });
        if (found == adapters.end()) {
            throw std::invalid_argument(
                "requested backend adapter_id is unavailable");
        }
        selected = &*found;
    } else {
        if (config.backend.adapter_ordinal >= adapters.size()) {
            throw std::invalid_argument(
                "requested backend adapter ordinal is unavailable");
        }
        selected = &adapters[config.backend.adapter_ordinal];
    }
    const auto required = required_backend_features(config);
    if (!backend_has_features(selected->features, required)) {
        throw std::invalid_argument(
            "selected backend adapter lacks required features");
    }
    const auto explicit_budget = config.backend.memory_budget_bytes;
    if (explicit_budget > selected->memory.available_bytes) {
        throw std::invalid_argument(
            "backend memory budget exceeds currently available memory");
    }
    const auto automatic_budget = std::min(
        selected->memory.available_bytes -
            selected->memory.available_bytes / 5,
        selected->memory.total_bytes -
            selected->memory.total_bytes / 4);
    if (explicit_budget == 0 && automatic_budget == 0) {
        throw std::runtime_error(
            "selected backend adapter has no usable memory budget");
    }
    selected->memory.budget_bytes =
        explicit_budget > 0 ? explicit_budget : automatic_budget;
    if (selected->kind == BackendKind::Cuda) {
        const auto activate_error = cudaSetDevice(
            static_cast<int>(selected->ordinal));
        if (activate_error != cudaSuccess) {
            throw std::runtime_error(cuda_error_message(
                activate_error, "cudaSetDevice selected adapter"));
        }
    }
    return BackendSelection{
        *selected,
        required,
        selected->memory.budget_bytes
    };
}

}
