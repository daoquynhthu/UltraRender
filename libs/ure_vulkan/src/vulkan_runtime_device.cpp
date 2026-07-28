#define VOLK_IMPLEMENTATION
#include "volk.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "ure/vulkan_runtime.hpp"

namespace ure::vulkan {
namespace {

template <typename T>
T vk_structure(VkStructureType type, void* next = nullptr) {
    T value{};
    value.sType = type;
    value.pNext = next;
    return value;
}

runtime::ErrorCode result_code(VkResult result) {
    switch (result) {
    case VK_ERROR_OUT_OF_HOST_MEMORY:
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        return runtime::ErrorCode::OutOfMemory;
    case VK_TIMEOUT:
        return runtime::ErrorCode::Timeout;
    case VK_ERROR_FEATURE_NOT_PRESENT:
    case VK_ERROR_EXTENSION_NOT_PRESENT:
    case VK_ERROR_FORMAT_NOT_SUPPORTED:
        return runtime::ErrorCode::Unsupported;
    case VK_ERROR_DEVICE_LOST:
        return runtime::ErrorCode::DeviceLost;
    case VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS:
    case VK_ERROR_VALIDATION_FAILED_EXT:
        return runtime::ErrorCode::InvalidArgument;
    default:
        return runtime::ErrorCode::BackendFailure;
    }
}

std::string result_name(VkResult result) {
    switch (result) {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_NOT_READY: return "VK_NOT_READY";
    case VK_TIMEOUT: return "VK_TIMEOUT";
    case VK_ERROR_OUT_OF_HOST_MEMORY:
        return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED:
        return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST:
        return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED:
        return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT:
        return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT:
        return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT:
        return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER:
        return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS:
        return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED:
        return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_FRAGMENTED_POOL:
        return "VK_ERROR_FRAGMENTED_POOL";
    case VK_ERROR_UNKNOWN:
        return "VK_ERROR_UNKNOWN";
    case VK_ERROR_VALIDATION_FAILED_EXT:
        return "VK_ERROR_VALIDATION_FAILED_EXT";
    default:
        return "VkResult(" + std::to_string(result) + ")";
    }
}

void require_success(VkResult result, std::string_view operation) {
    if (result == VK_SUCCESS) return;
    throw runtime::Error(
        result_code(result),
        std::string(operation) + ": " + result_name(result));
}

bool has_extension(
    const std::vector<VkExtensionProperties>& extensions,
    std::string_view name) {
    return std::ranges::any_of(
        extensions,
        [&](const auto& value) {
            return name == value.extensionName;
        });
}

bool has_layer(
    const std::vector<VkLayerProperties>& layers,
    std::string_view name) {
    return std::ranges::any_of(
        layers,
        [&](const auto& value) {
            return name == value.layerName;
        });
}

std::string uuid_string(const std::uint8_t* bytes) {
    std::ostringstream stream;
    stream << "vulkan:";
    for (std::size_t index = 0; index < VK_UUID_SIZE; ++index) {
        stream << std::hex << std::setw(2) << std::setfill('0')
               << static_cast<unsigned int>(bytes[index]);
    }
    return stream.str();
}

VkFormat image_format(runtime::Format format) {
    switch (format) {
    case runtime::Format::R32Float:
        return VK_FORMAT_R32_SFLOAT;
    case runtime::Format::Rg32Float:
        return VK_FORMAT_R32G32_SFLOAT;
    case runtime::Format::Rgba16Float:
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    case runtime::Format::Rgba32Float:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case runtime::Format::R32Uint:
        return VK_FORMAT_R32_UINT;
    }
    throw runtime::Error(
        runtime::ErrorCode::Unsupported,
        "Vulkan image format is unsupported");
}

VkBufferUsageFlags buffer_usage(runtime::BufferUsage usage) {
    VkBufferUsageFlags flags = 0;
    if (runtime::has_usage(usage, runtime::BufferUsage::Storage)) {
        flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    }
    if (runtime::has_usage(usage, runtime::BufferUsage::Uniform)) {
        flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    }
    if (runtime::has_usage(
            usage, runtime::BufferUsage::TransferSource)) {
        flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    }
    if (runtime::has_usage(
            usage, runtime::BufferUsage::TransferDestination)) {
        flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }
    if (runtime::has_usage(usage, runtime::BufferUsage::Indirect)) {
        flags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    }
    if (runtime::has_usage(
            usage, runtime::BufferUsage::DeviceAddress)) {
        flags |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    }
    return flags;
}

VkImageUsageFlags image_usage(runtime::ImageUsage usage) {
    VkImageUsageFlags flags = 0;
    const auto bits = static_cast<std::uint32_t>(usage);
    if ((bits & static_cast<std::uint32_t>(
                    runtime::ImageUsage::Sampled)) != 0) {
        flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
    }
    if ((bits & static_cast<std::uint32_t>(
                    runtime::ImageUsage::Storage)) != 0) {
        flags |= VK_IMAGE_USAGE_STORAGE_BIT;
    }
    if ((bits & static_cast<std::uint32_t>(
                    runtime::ImageUsage::TransferSource)) != 0) {
        flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }
    if ((bits & static_cast<std::uint32_t>(
                    runtime::ImageUsage::TransferDestination)) != 0) {
        flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }
    return flags;
}

VkDescriptorType descriptor_type(runtime::BindingType type) {
    switch (type) {
    case runtime::BindingType::StorageBuffer:
    case runtime::BindingType::ReadOnlyStorageBuffer:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case runtime::BindingType::UniformBuffer:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case runtime::BindingType::SampledImage:
        return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    case runtime::BindingType::StorageImage:
        return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    case runtime::BindingType::AccelerationStructure:
        return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    }
    throw runtime::Error(
        runtime::ErrorCode::InvalidArgument,
        "Vulkan descriptor type is invalid");
}

VkSamplerAddressMode sampler_address(runtime::AddressMode mode) {
    switch (mode) {
    case runtime::AddressMode::Clamp:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case runtime::AddressMode::Repeat:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case runtime::AddressMode::Mirror:
        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case runtime::AddressMode::Border:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    }
    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
}

VkBuildAccelerationStructureFlagsKHR acceleration_build_flags(
    const runtime::AccelerationBuildConfig& config,
    bool allow_compaction) {
    VkBuildAccelerationStructureFlagsKHR flags =
        config.quality ==
                runtime::AccelerationBuildQuality::FastBuild
            ? VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR
            : VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    if (config.update_policy !=
        runtime::AccelerationUpdatePolicy::Static) {
        flags |=
            VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    }
    if (config.compact && allow_compaction) {
        flags |=
            VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
    }
    return flags;
}

struct AdapterRecord {
    BackendAdapterInfo info;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    std::uint32_t queue_family = 0;
    std::uint32_t queue_count = 0;
    bool shader_float_atomic = false;
    bool memory_budget = false;
    bool ray_query = false;
    bool ray_tracing_pipeline = false;
    std::uint32_t max_triangle_geometries = 0;
    std::uint32_t max_instances = 0;
    std::uint64_t scratch_alignment = 1;
    std::array<std::uint8_t, VK_UUID_SIZE>
        pipeline_cache_uuid{};
};

struct Environment {
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    bool validation_layer = false;
    std::mutex validation_mutex;
    std::vector<ValidationMessage> messages;
    std::vector<AdapterRecord> adapters;

    static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT,
        const VkDebugUtilsMessengerCallbackDataEXT* data,
        void* user_data) {
        auto* self = static_cast<Environment*>(user_data);
        if (!self || !data || !data->pMessage) return VK_FALSE;
        std::scoped_lock lock(self->validation_mutex);
        self->messages.push_back({
            (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0,
            data->pMessage});
        return VK_FALSE;
    }

    Environment() {
        require_success(volkInitialize(), "volkInitialize");
        std::uint32_t instance_version = VK_API_VERSION_1_0;
        if (vkEnumerateInstanceVersion) {
            require_success(
                vkEnumerateInstanceVersion(&instance_version),
                "vkEnumerateInstanceVersion");
        }
        if (instance_version < VK_API_VERSION_1_3) {
            throw runtime::Error(
                runtime::ErrorCode::Unsupported,
                "Vulkan 1.3 is required");
        }

        std::uint32_t extension_count = 0;
        require_success(
            vkEnumerateInstanceExtensionProperties(
                nullptr, &extension_count, nullptr),
            "vkEnumerateInstanceExtensionProperties");
        std::vector<VkExtensionProperties> extensions(extension_count);
        require_success(
            vkEnumerateInstanceExtensionProperties(
                nullptr, &extension_count, extensions.data()),
            "vkEnumerateInstanceExtensionProperties");

        std::uint32_t layer_count = 0;
        require_success(
            vkEnumerateInstanceLayerProperties(
                &layer_count, nullptr),
            "vkEnumerateInstanceLayerProperties");
        std::vector<VkLayerProperties> layers(layer_count);
        require_success(
            vkEnumerateInstanceLayerProperties(
                &layer_count, layers.data()),
            "vkEnumerateInstanceLayerProperties");

        const bool debug_utils =
            has_extension(extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        validation_layer = has_layer(
            layers, "VK_LAYER_KHRONOS_validation");
        const char* enabled_extension =
            VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        const char* enabled_layer =
            "VK_LAYER_KHRONOS_validation";

        auto debug_info =
            vk_structure<VkDebugUtilsMessengerCreateInfoEXT>(
                VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT);
        debug_info.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debug_info.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debug_info.pfnUserCallback = debug_callback;
        debug_info.pUserData = this;

        auto application = vk_structure<VkApplicationInfo>(
            VK_STRUCTURE_TYPE_APPLICATION_INFO);
        application.pApplicationName = "UltraRender";
        application.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        application.pEngineName = "UltraRender";
        application.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        application.apiVersion = VK_API_VERSION_1_3;

        auto create = vk_structure<VkInstanceCreateInfo>(
            VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
        create.pApplicationInfo = &application;
        if (debug_utils) {
            create.enabledExtensionCount = 1;
            create.ppEnabledExtensionNames = &enabled_extension;
            create.pNext = &debug_info;
        }
        if (validation_layer) {
            create.enabledLayerCount = 1;
            create.ppEnabledLayerNames = &enabled_layer;
        }
        require_success(
            vkCreateInstance(&create, nullptr, &instance),
            "vkCreateInstance");
        volkLoadInstanceOnly(instance);
        if (debug_utils) {
            require_success(
                vkCreateDebugUtilsMessengerEXT(
                    instance, &debug_info, nullptr, &messenger),
                "vkCreateDebugUtilsMessengerEXT");
        }
        query_adapters();
    }

    ~Environment() {
        if (messenger) {
            vkDestroyDebugUtilsMessengerEXT(
                instance, messenger, nullptr);
        }
        if (instance) vkDestroyInstance(instance, nullptr);
    }

    void query_adapters() {
        std::uint32_t physical_count = 0;
        require_success(
            vkEnumeratePhysicalDevices(
                instance, &physical_count, nullptr),
            "vkEnumeratePhysicalDevices");
        std::vector<VkPhysicalDevice> physical_devices(physical_count);
        require_success(
            vkEnumeratePhysicalDevices(
                instance,
                &physical_count,
                physical_devices.data()),
            "vkEnumeratePhysicalDevices");

        adapters.clear();
        adapters.reserve(physical_devices.size());
        for (std::uint32_t ordinal = 0;
             ordinal < physical_devices.size();
             ++ordinal) {
            const auto physical = physical_devices[ordinal];
            std::uint32_t device_extension_count = 0;
            require_success(
                vkEnumerateDeviceExtensionProperties(
                    physical,
                    nullptr,
                    &device_extension_count,
                    nullptr),
                "vkEnumerateDeviceExtensionProperties");
            std::vector<VkExtensionProperties> device_extensions(
                device_extension_count);
            require_success(
                vkEnumerateDeviceExtensionProperties(
                    physical,
                    nullptr,
                    &device_extension_count,
                    device_extensions.data()),
                "vkEnumerateDeviceExtensionProperties");
            const bool float_atomic = has_extension(
                device_extensions,
                VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME);
            const bool memory_budget = has_extension(
                device_extensions,
                VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
            const bool acceleration_extension =
                has_extension(
                    device_extensions,
                    VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) &&
                has_extension(
                    device_extensions,
                    VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
            const bool ray_query_extension =
                acceleration_extension &&
                has_extension(
                    device_extensions,
                    VK_KHR_RAY_QUERY_EXTENSION_NAME);
            const bool ray_pipeline_extension =
                acceleration_extension &&
                has_extension(
                    device_extensions,
                    VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);

            auto subgroup =
                vk_structure<VkPhysicalDeviceSubgroupProperties>(
                    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES);
            auto driver =
                vk_structure<VkPhysicalDeviceDriverProperties>(
                    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES,
                    &subgroup);
            auto id = vk_structure<VkPhysicalDeviceIDProperties>(
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES,
                &driver);
            auto properties =
                vk_structure<VkPhysicalDeviceProperties2>(
                    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
                    &id);
            auto acceleration_properties =
                vk_structure<
                    VkPhysicalDeviceAccelerationStructurePropertiesKHR>(
                    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR);
            if (acceleration_extension) {
                id.pNext = &driver;
                driver.pNext = &subgroup;
                subgroup.pNext = &acceleration_properties;
            }
            vkGetPhysicalDeviceProperties2(physical, &properties);

            auto atomics =
                vk_structure<
                    VkPhysicalDeviceShaderAtomicFloatFeaturesEXT>(
                    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT);
            auto features13 =
                vk_structure<VkPhysicalDeviceVulkan13Features>(
                    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES);
            auto acceleration_features =
                vk_structure<
                    VkPhysicalDeviceAccelerationStructureFeaturesKHR>(
                    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR);
            auto ray_query_features =
                vk_structure<VkPhysicalDeviceRayQueryFeaturesKHR>(
                    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR);
            auto ray_pipeline_features =
                vk_structure<
                    VkPhysicalDeviceRayTracingPipelineFeaturesKHR>(
                    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR);
            auto features12 =
                vk_structure<VkPhysicalDeviceVulkan12Features>(
                    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
                    &features13);
            auto features =
                vk_structure<VkPhysicalDeviceFeatures2>(
                    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                    &features12);
            void* feature_next = nullptr;
            if (float_atomic) {
                atomics.pNext = feature_next;
                feature_next = &atomics;
            }
            if (ray_pipeline_extension) {
                ray_pipeline_features.pNext = feature_next;
                feature_next = &ray_pipeline_features;
            }
            if (ray_query_extension) {
                ray_query_features.pNext = feature_next;
                feature_next = &ray_query_features;
            }
            if (acceleration_extension) {
                acceleration_features.pNext = feature_next;
                feature_next = &acceleration_features;
            }
            features13.pNext = feature_next;
            vkGetPhysicalDeviceFeatures2(physical, &features);
            const bool ray_query =
                ray_query_extension &&
                features12.bufferDeviceAddress &&
                acceleration_features.accelerationStructure &&
                ray_query_features.rayQuery;
            const bool ray_pipeline =
                ray_pipeline_extension &&
                features12.bufferDeviceAddress &&
                acceleration_features.accelerationStructure &&
                ray_pipeline_features.rayTracingPipeline;

            auto budget =
                vk_structure<VkPhysicalDeviceMemoryBudgetPropertiesEXT>(
                    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT);
            auto memory =
                vk_structure<VkPhysicalDeviceMemoryProperties2>(
                    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2);
            if (memory_budget) memory.pNext = &budget;
            vkGetPhysicalDeviceMemoryProperties2(physical, &memory);

            std::uint32_t family_count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties2(
                physical, &family_count, nullptr);
            std::vector<VkQueueFamilyProperties2> families(
                family_count);
            for (auto& family_info : families) {
                family_info =
                    vk_structure<VkQueueFamilyProperties2>(
                        VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2);
            }
            vkGetPhysicalDeviceQueueFamilyProperties2(
                physical, &family_count, families.data());
            const auto family = std::ranges::find_if(
                families,
                [](const auto& value) {
                    return value.queueFamilyProperties.queueCount > 0 &&
                        (value.queueFamilyProperties.queueFlags &
                         VK_QUEUE_COMPUTE_BIT) != 0;
                });
            if (family == families.end()) continue;
            if (properties.properties.apiVersion <
                    VK_API_VERSION_1_3 ||
                !features.features.shaderInt64 ||
                !features12.timelineSemaphore ||
                !features13.synchronization2 ||
                (subgroup.supportedStages &
                 VK_SHADER_STAGE_COMPUTE_BIT) == 0) {
                continue;
            }

            std::uint64_t total = 0;
            std::uint64_t available = 0;
            for (std::uint32_t heap = 0;
                 heap < memory.memoryProperties.memoryHeapCount;
                 ++heap) {
                if ((memory.memoryProperties.memoryHeaps[heap].flags &
                     VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) == 0) {
                    continue;
                }
                total += memory.memoryProperties.memoryHeaps[heap].size;
                available += memory_budget
                    ? std::min(
                          budget.heapBudget[heap],
                          budget.heapUsage[heap] <=
                                  budget.heapBudget[heap]
                              ? budget.heapBudget[heap] -
                                    budget.heapUsage[heap]
                              : 0)
                    : memory.memoryProperties.memoryHeaps[heap].size -
                          memory.memoryProperties.memoryHeaps[heap].size /
                              10;
            }
            if (available == 0) available = total;

            BackendFeatureSet backend_features = 0;
            if ((family->queueFamilyProperties.queueFlags &
                 VK_QUEUE_COMPUTE_BIT) != 0) {
                backend_features |=
                    backend_feature_bit(BackendFeature::Compute);
            }
            if ((subgroup.supportedStages &
                 VK_SHADER_STAGE_COMPUTE_BIT) != 0) {
                backend_features |=
                    backend_feature_bit(BackendFeature::Subgroup);
            }
            if (features.features.shaderInt64) {
                backend_features |=
                    backend_feature_bit(BackendFeature::Int64);
            }
            if (float_atomic && atomics.shaderBufferFloat32Atomics) {
                backend_features |=
                    backend_feature_bit(BackendFeature::FloatAtomics);
            }
            backend_features |=
                backend_feature_bit(BackendFeature::TextureSampling);
            backend_features |=
                backend_feature_bit(BackendFeature::SpectralTransport) |
                backend_feature_bit(BackendFeature::Polarization) |
                backend_feature_bit(BackendFeature::WaveReference);
            if (ray_query) {
                backend_features |=
                    backend_feature_bit(BackendFeature::RayQuery);
            }
            if (ray_pipeline) {
                backend_features |=
                    backend_feature_bit(
                        BackendFeature::RayTracingPipeline);
            }
            AdapterRecord record;
            record.physical = physical;
            record.queue_family = static_cast<std::uint32_t>(
                std::distance(families.begin(), family));
            record.queue_count =
                family->queueFamilyProperties.queueCount;
            record.shader_float_atomic =
                float_atomic && atomics.shaderBufferFloat32Atomics;
            record.memory_budget = memory_budget;
            record.ray_query = ray_query;
            record.ray_tracing_pipeline = ray_pipeline;
            record.max_triangle_geometries =
                acceleration_extension
                ? static_cast<std::uint32_t>(std::min<
                      std::uint64_t>(
                      acceleration_properties.maxGeometryCount,
                      std::numeric_limits<std::uint32_t>::max()))
                : 0;
            record.max_instances =
                acceleration_extension
                ? static_cast<std::uint32_t>(std::min<
                      std::uint64_t>(
                      acceleration_properties.maxInstanceCount,
                      std::numeric_limits<std::uint32_t>::max()))
                : 0;
            record.scratch_alignment =
                acceleration_extension
                ? acceleration_properties
                      .minAccelerationStructureScratchOffsetAlignment
                : 1;
            std::ranges::copy(
                properties.properties.pipelineCacheUUID,
                record.pipeline_cache_uuid.begin());
            record.info.kind = BackendKind::Vulkan;
            record.info.adapter_id = uuid_string(id.deviceUUID);
            record.info.ordinal = ordinal;
            record.info.vendor_id = properties.properties.vendorID;
            record.info.device_id = properties.properties.deviceID;
            record.info.name = properties.properties.deviceName;
            record.info.features = backend_features;
            record.info.limits.max_workgroup_threads =
                properties.properties.limits
                    .maxComputeWorkGroupInvocations;
            record.info.limits.subgroup_size = subgroup.subgroupSize;
            record.info.limits.max_grid_dimension_x =
                properties.properties.limits
                    .maxComputeWorkGroupCount[0];
            record.info.limits.max_grid_dimension_y =
                properties.properties.limits
                    .maxComputeWorkGroupCount[1];
            record.info.limits.max_grid_dimension_z =
                properties.properties.limits
                    .maxComputeWorkGroupCount[2];
            record.info.limits.max_shared_memory_per_workgroup =
                properties.properties.limits
                    .maxComputeSharedMemorySize;
            record.info.limits.max_spectral_packet_lanes = 32;
            record.info.memory.total_bytes = total;
            record.info.memory.available_bytes = available;
            record.info.driver_identity =
                std::string(driver.driverName) + " " +
                driver.driverInfo;
            record.info.compiler_identity =
                "SPIR-V 1.6 / Slang 2026.14";
            adapters.push_back(std::move(record));
        }
        if (adapters.size() > 1) {
            for (auto& record : adapters) {
                record.info.features |=
                    backend_feature_bit(
                        BackendFeature::MultiAdapter);
            }
        }
    }
};

std::shared_ptr<Environment> environment() {
    static const auto* value =
        new std::shared_ptr<Environment>(
            std::make_shared<Environment>());
    return *value;
}

}

struct VulkanRuntimeDevice::Impl {
    struct Queue {
        struct Pending {
            std::uint64_t value = 0;
            VkCommandPool command_pool = VK_NULL_HANDLE;
            VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
        };

        VkQueue queue = VK_NULL_HANDLE;
        std::uint32_t index = 0;
        VkSemaphore completion = VK_NULL_HANDLE;
        std::uint64_t submitted = 0;
        std::deque<Pending> pending;
        runtime::QueueDesc desc;
    };

    struct Fence {
        VkSemaphore semaphore = VK_NULL_HANDLE;
        std::uint64_t last_signaled = 0;
    };

    struct Event {
        VkEvent event = VK_NULL_HANDLE;
        bool recorded = false;
    };

    struct Buffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
        VkDeviceSize allocation_size = 0;
        runtime::BufferDesc desc;
    };

    struct Image {
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize allocation_size = 0;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        runtime::ImageDesc desc;
    };

    struct Sampler {
        VkSampler sampler = VK_NULL_HANDLE;
        runtime::SamplerDesc desc;
    };

    struct Module {
        VkShaderModule module = VK_NULL_HANDLE;
        runtime::ModuleDesc desc;
    };

    struct Pipeline {
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkDescriptorSetLayout descriptor_layout = VK_NULL_HANDLE;
        runtime::PipelineDesc desc;
    };

    struct NativeBuffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize allocation_size = 0;
    };

    struct Acceleration {
        std::vector<VkAccelerationStructureKHR> bottoms;
        VkAccelerationStructureKHR top =
            VK_NULL_HANDLE;
        std::vector<NativeBuffer> bottom_storage;
        NativeBuffer top_storage;
        runtime::AccelerationSceneDesc desc;
        std::vector<runtime::TriangleGeometryDesc>
            geometries;
        std::vector<runtime::AccelerationInstanceDesc>
            instances;
        runtime::AccelerationBuildStats stats;
    };

    Impl(
        BackendAdapterInfo selected,
        std::uint64_t budget,
        std::span<const std::byte> initial_cache)
        : env(environment()),
          adapter(std::move(selected)),
          memory_budget(budget) {
        const auto found = std::ranges::find_if(
            env->adapters,
            [&](const auto& record) {
                return record.info.adapter_id == adapter.adapter_id;
            });
        if (found == env->adapters.end()) {
            throw runtime::Error(
                runtime::ErrorCode::InvalidArgument,
                "Vulkan adapter identity is unavailable");
        }
        physical = found->physical;
        adapter = found->info;
        adapter.memory.budget_bytes = budget;
        queue_family = found->queue_family;
        queue_capacity = std::min(found->queue_count, 4u);
        ray_query = found->ray_query;
        max_triangle_geometries = found->max_triangle_geometries;
        max_instances = found->max_instances;
        scratch_alignment = found->scratch_alignment;
        pipeline_cache_uuid = found->pipeline_cache_uuid;
        validation_start = [&] {
            std::scoped_lock lock(env->validation_mutex);
            return env->messages.size();
        }();

        std::vector<float> priorities(queue_capacity, 1.0f);
        auto queue_create =
            vk_structure<VkDeviceQueueCreateInfo>(
                VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
        queue_create.queueFamilyIndex = queue_family;
        queue_create.queueCount = queue_capacity;
        queue_create.pQueuePriorities = priorities.data();

        auto atomics =
            vk_structure<
                VkPhysicalDeviceShaderAtomicFloatFeaturesEXT>(
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT);
        atomics.shaderBufferFloat32Atomics =
            found->shader_float_atomic ? VK_TRUE : VK_FALSE;
        auto features13 =
            vk_structure<VkPhysicalDeviceVulkan13Features>(
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES);
        features13.synchronization2 = VK_TRUE;
        auto acceleration_features =
            vk_structure<
                VkPhysicalDeviceAccelerationStructureFeaturesKHR>(
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR);
        acceleration_features.accelerationStructure =
            ray_query ? VK_TRUE : VK_FALSE;
        auto ray_query_features =
            vk_structure<VkPhysicalDeviceRayQueryFeaturesKHR>(
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR);
        ray_query_features.rayQuery =
            ray_query ? VK_TRUE : VK_FALSE;
        auto features12 =
            vk_structure<VkPhysicalDeviceVulkan12Features>(
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
                &features13);
        features12.timelineSemaphore = VK_TRUE;
        features12.bufferDeviceAddress =
            ray_query ? VK_TRUE : VK_FALSE;
        void* feature_next = nullptr;
        if (found->shader_float_atomic) {
            atomics.pNext = feature_next;
            feature_next = &atomics;
        }
        if (ray_query) {
            ray_query_features.pNext = feature_next;
            acceleration_features.pNext = &ray_query_features;
            feature_next = &acceleration_features;
        }
        features13.pNext = feature_next;
        auto features =
            vk_structure<VkPhysicalDeviceFeatures2>(
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                &features12);
        features.features.shaderInt64 = VK_TRUE;

        std::vector<const char*> device_extensions;
        if (found->shader_float_atomic) {
            device_extensions.push_back(
                VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME);
        }
        if (ray_query) {
            device_extensions.push_back(
                VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
            device_extensions.push_back(
                VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
            device_extensions.push_back(
                VK_KHR_RAY_QUERY_EXTENSION_NAME);
        }
        auto create = vk_structure<VkDeviceCreateInfo>(
            VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            &features);
        create.queueCreateInfoCount = 1;
        create.pQueueCreateInfos = &queue_create;
        if (!device_extensions.empty()) {
            create.enabledExtensionCount =
                static_cast<std::uint32_t>(
                    device_extensions.size());
            create.ppEnabledExtensionNames =
                device_extensions.data();
        }
        if (!initial_cache.empty()) {
            constexpr std::size_t kHeaderSize =
                sizeof(std::uint32_t) * 4 + VK_UUID_SIZE;
            if (initial_cache.size() < kHeaderSize) {
                throw runtime::Error(
                    runtime::ErrorCode::InvalidArgument,
                    "Vulkan pipeline cache header is truncated");
            }
            std::array<std::uint32_t, 4> header{};
            std::memcpy(
                header.data(),
                initial_cache.data(),
                sizeof(header));
            const auto cache_uuid =
                initial_cache.subspan(sizeof(header), VK_UUID_SIZE);
            const std::span expected_uuid{
                reinterpret_cast<const std::byte*>(
                    pipeline_cache_uuid.data()),
                pipeline_cache_uuid.size()};
            if (header[0] < kHeaderSize ||
                header[0] > initial_cache.size() ||
                header[1] !=
                    VK_PIPELINE_CACHE_HEADER_VERSION_ONE ||
                header[2] != adapter.vendor_id ||
                header[3] != adapter.device_id ||
                !std::ranges::equal(
                    cache_uuid, expected_uuid)) {
                throw runtime::Error(
                    runtime::ErrorCode::InvalidArgument,
                    "Vulkan pipeline cache identity is incompatible");
            }
        }
        require_success(
            vkCreateDevice(
                physical, &create, nullptr, &device),
            "vkCreateDevice");
        volkLoadDeviceTable(&vk, device);
        try {
            native_queues.resize(queue_capacity);
            for (std::uint32_t index = 0;
                 index < queue_capacity;
                 ++index) {
                vk.vkGetDeviceQueue(
                    device,
                    queue_family,
                    index,
                    &native_queues[index]);
            }
            queue_claimed.assign(queue_capacity, false);
            auto cache_create =
                vk_structure<VkPipelineCacheCreateInfo>(
                    VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO);
            cache_create.initialDataSize = initial_cache.size();
            cache_create.pInitialData = initial_cache.data();
            check(
                vk.vkCreatePipelineCache(
                    device,
                    &cache_create,
                    nullptr,
                    &pipeline_cache),
                "vkCreatePipelineCache");
        } catch (...) {
            vk.vkDestroyDevice(device, nullptr);
            device = VK_NULL_HANDLE;
            throw;
        }
    }

    ~Impl() {
        if (!device) return;
        static_cast<void>(vk.vkDeviceWaitIdle(device));
        for (auto& [id, acceleration] : accelerations) {
            static_cast<void>(id);
            destroy_acceleration(acceleration);
        }
        for (auto& [id, pipeline] : pipelines) {
            static_cast<void>(id);
            vk.vkDestroyPipeline(
                device, pipeline.pipeline, nullptr);
            vk.vkDestroyPipelineLayout(
                device, pipeline.layout, nullptr);
            vk.vkDestroyDescriptorSetLayout(
                device, pipeline.descriptor_layout, nullptr);
        }
        for (auto& [id, module] : modules) {
            static_cast<void>(id);
            vk.vkDestroyShaderModule(
                device, module.module, nullptr);
        }
        for (auto& [id, sampler] : samplers) {
            static_cast<void>(id);
            vk.vkDestroySampler(
                device, sampler.sampler, nullptr);
        }
        for (auto& [id, image] : images) {
            static_cast<void>(id);
            vk.vkDestroyImageView(
                device, image.view, nullptr);
            vk.vkDestroyImage(device, image.image, nullptr);
            vk.vkFreeMemory(device, image.memory, nullptr);
        }
        for (auto& [id, buffer] : buffers) {
            static_cast<void>(id);
            if (buffer.mapped) {
                vk.vkUnmapMemory(device, buffer.memory);
            }
            vk.vkDestroyBuffer(device, buffer.buffer, nullptr);
            vk.vkFreeMemory(device, buffer.memory, nullptr);
        }
        for (auto& [id, event] : events) {
            static_cast<void>(id);
            vk.vkDestroyEvent(device, event.event, nullptr);
        }
        for (auto& [id, fence] : fences) {
            static_cast<void>(id);
            vk.vkDestroySemaphore(
                device, fence.semaphore, nullptr);
        }
        for (auto& [id, queue] : queues) {
            static_cast<void>(id);
            destroy_pending(queue);
            vk.vkDestroySemaphore(
                device, queue.completion, nullptr);
        }
        if (pipeline_cache) {
            vk.vkDestroyPipelineCache(
                device, pipeline_cache, nullptr);
        }
        vk.vkDestroyDevice(device, nullptr);
        state.store(
            runtime::DeviceState::Shutdown,
            std::memory_order_release);
    }

    std::shared_ptr<Environment> env;
    BackendAdapterInfo adapter;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VolkDeviceTable vk{};
    std::uint32_t queue_family = 0;
    std::uint32_t queue_capacity = 0;
    bool ray_query = false;
    std::uint32_t max_triangle_geometries = 0;
    std::uint32_t max_instances = 0;
    std::uint64_t scratch_alignment = 1;
    std::vector<VkQueue> native_queues;
    std::vector<bool> queue_claimed;
    VkPipelineCache pipeline_cache = VK_NULL_HANDLE;
    std::array<std::uint8_t, VK_UUID_SIZE>
        pipeline_cache_uuid{};
    std::size_t validation_start = 0;
    std::atomic<runtime::DeviceState> state{
        runtime::DeviceState::Ready};
    std::optional<runtime::DeviceLossInfo> loss;
    mutable std::mutex mutex;
    std::uint64_t next_handle = 1;
    std::uint64_t next_submission = 1;
    std::uint64_t memory_budget = 0;
    std::uint64_t allocated = 0;
    std::uint64_t loss_epoch = 0;
    std::unordered_map<std::uint64_t, Queue> queues;
    std::unordered_map<std::uint64_t, Fence> fences;
    std::unordered_map<std::uint64_t, Event> events;
    std::unordered_map<std::uint64_t, Buffer> buffers;
    std::unordered_map<std::uint64_t, Image> images;
    std::unordered_map<std::uint64_t, Sampler> samplers;
    std::unordered_map<std::uint64_t, Module> modules;
    std::unordered_map<std::uint64_t, Pipeline> pipelines;
    std::unordered_map<std::uint64_t, Acceleration>
        accelerations;

    void ready() const {
        const auto current = state.load(std::memory_order_acquire);
        if (current == runtime::DeviceState::Lost) {
            throw runtime::Error(
                runtime::ErrorCode::DeviceLost,
                loss ? loss->reason : "Vulkan device is lost");
        }
        if (current == runtime::DeviceState::Shutdown) {
            throw runtime::Error(
                runtime::ErrorCode::BackendFailure,
                "Vulkan runtime device is shut down");
        }
    }

    void check(VkResult result, std::string_view operation) {
        if (result == VK_SUCCESS) return;
        const auto code = result_code(result);
        const auto message =
            std::string(operation) + ": " + result_name(result);
        if (code == runtime::ErrorCode::DeviceLost) {
            loss = runtime::DeviceLossInfo{
                ++loss_epoch,
                message,
                adapter.driver_identity};
            state.store(
                runtime::DeviceState::Lost,
                std::memory_order_release);
        }
        throw runtime::Error(code, message);
    }

    std::uint64_t handle() {
        if (next_handle ==
            std::numeric_limits<std::uint64_t>::max()) {
            throw runtime::Error(
                runtime::ErrorCode::Overflow,
                "Vulkan handle space exhausted");
        }
        return next_handle++;
    }

    runtime::SubmissionId submission() {
        if (next_submission ==
            std::numeric_limits<std::uint64_t>::max()) {
            throw runtime::Error(
                runtime::ErrorCode::Overflow,
                "Vulkan submission identity overflows");
        }
        return next_submission++;
    }

    template <typename Map>
    static auto& require(
        Map& values,
        std::uint64_t handle_value,
        const char* label) {
        const auto found = values.find(handle_value);
        if (handle_value == 0 || found == values.end()) {
            throw runtime::Error(
                runtime::ErrorCode::InvalidHandle,
                std::string("invalid Vulkan ") + label + " handle");
        }
        return found->second;
    }

    void reserve(std::uint64_t bytes) const {
        if (bytes > memory_budget ||
            allocated > memory_budget - bytes) {
            throw runtime::Error(
                runtime::ErrorCode::OutOfMemory,
                "Vulkan runtime memory budget exceeded");
        }
    }

    std::uint32_t memory_type(
        std::uint32_t allowed,
        VkMemoryPropertyFlags required) const {
        VkPhysicalDeviceMemoryProperties properties{};
        vkGetPhysicalDeviceMemoryProperties(physical, &properties);
        for (std::uint32_t index = 0;
             index < properties.memoryTypeCount;
             ++index) {
            if ((allowed & (1u << index)) != 0 &&
                (properties.memoryTypes[index].propertyFlags &
                 required) == required) {
                return index;
            }
        }
        throw runtime::Error(
            runtime::ErrorCode::Unsupported,
            "Vulkan memory type is unavailable");
    }

    NativeBuffer create_native_buffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties,
        bool device_address) {
        auto create = vk_structure<VkBufferCreateInfo>(
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
        create.size = size;
        create.usage = usage;
        create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        NativeBuffer value;
        check(
            vk.vkCreateBuffer(
                device, &create, nullptr, &value.buffer),
            "vkCreateBuffer acceleration");
        VkMemoryRequirements requirements{};
        vk.vkGetBufferMemoryRequirements(
            device, value.buffer, &requirements);
        try {
            reserve(requirements.size);
            auto flags =
                vk_structure<VkMemoryAllocateFlagsInfo>(
                    VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO);
            if (device_address) {
                flags.flags =
                    VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
            }
            auto allocate =
                vk_structure<VkMemoryAllocateInfo>(
                    VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                    device_address ? &flags : nullptr);
            allocate.allocationSize = requirements.size;
            allocate.memoryTypeIndex = memory_type(
                requirements.memoryTypeBits, properties);
            check(
                vk.vkAllocateMemory(
                    device,
                    &allocate,
                    nullptr,
                    &value.memory),
                "vkAllocateMemory acceleration");
            check(
                vk.vkBindBufferMemory(
                    device,
                    value.buffer,
                    value.memory,
                    0),
                "vkBindBufferMemory acceleration");
        } catch (...) {
            if (value.memory) {
                vk.vkFreeMemory(
                    device, value.memory, nullptr);
            }
            vk.vkDestroyBuffer(
                device, value.buffer, nullptr);
            throw;
        }
        value.allocation_size = requirements.size;
        allocated += requirements.size;
        return value;
    }

    void destroy_native_buffer(NativeBuffer& value) noexcept {
        if (value.buffer) {
            vk.vkDestroyBuffer(
                device, value.buffer, nullptr);
        }
        if (value.memory) {
            vk.vkFreeMemory(
                device, value.memory, nullptr);
        }
        allocated -= std::min<std::uint64_t>(
            allocated, value.allocation_size);
        value = {};
    }

    VkDeviceAddress buffer_address(VkBuffer buffer) const {
        auto info =
            vk_structure<VkBufferDeviceAddressInfo>(
                VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO);
        info.buffer = buffer;
        return vk.vkGetBufferDeviceAddress(device, &info);
    }

    void destroy_acceleration(Acceleration& value) noexcept {
        if (value.top) {
            vk.vkDestroyAccelerationStructureKHR(
                device, value.top, nullptr);
        }
        for (const auto bottom : value.bottoms) {
            vk.vkDestroyAccelerationStructureKHR(
                device, bottom, nullptr);
        }
        destroy_native_buffer(value.top_storage);
        for (auto& storage : value.bottom_storage) {
            destroy_native_buffer(storage);
        }
        value.top = VK_NULL_HANDLE;
        value.bottoms.clear();
        value.bottom_storage.clear();
    }

    void collect(Queue& queue) {
        std::uint64_t completed = 0;
        check(
            vk.vkGetSemaphoreCounterValue(
                device, queue.completion, &completed),
            "vkGetSemaphoreCounterValue");
        while (!queue.pending.empty() &&
               queue.pending.front().value <= completed) {
            const auto pending = queue.pending.front();
            queue.pending.pop_front();
            if (pending.descriptor_pool) {
                vk.vkDestroyDescriptorPool(
                    device,
                    pending.descriptor_pool,
                    nullptr);
            }
            vk.vkDestroyCommandPool(
                device, pending.command_pool, nullptr);
        }
    }

    void destroy_pending(Queue& queue) noexcept {
        for (const auto& pending : queue.pending) {
            if (pending.descriptor_pool) {
                vk.vkDestroyDescriptorPool(
                    device,
                    pending.descriptor_pool,
                    nullptr);
            }
            vk.vkDestroyCommandPool(
                device, pending.command_pool, nullptr);
        }
        queue.pending.clear();
    }

    void wait_idle_locked() {
        ready();
        check(vk.vkDeviceWaitIdle(device), "vkDeviceWaitIdle");
        for (auto& [id, queue] : queues) {
            static_cast<void>(id);
            collect(queue);
        }
    }
};

VulkanRuntimeDevice::VulkanRuntimeDevice(
    BackendAdapterInfo adapter,
    std::uint64_t memory_budget_bytes,
    std::span<const std::byte> pipeline_cache) {
    if (adapter.kind != BackendKind::Vulkan ||
        adapter.adapter_id.empty()) {
        throw runtime::Error(
            runtime::ErrorCode::InvalidArgument,
            "Vulkan adapter identity is invalid");
    }
    const auto adapters = enumerate_vulkan_adapters();
    const auto canonical = std::ranges::find_if(
        adapters,
        [&](const auto& value) {
            return value.adapter_id == adapter.adapter_id;
        });
    if (canonical == adapters.end()) {
        throw runtime::Error(
            runtime::ErrorCode::InvalidArgument,
            "Vulkan adapter identity is unavailable");
    }
    adapter = *canonical;
    const auto budget = memory_budget_bytes != 0
        ? memory_budget_bytes
        : (adapter.memory.budget_bytes != 0
               ? adapter.memory.budget_bytes
               : std::min(
                     adapter.memory.available_bytes -
                         adapter.memory.available_bytes / 5,
                     adapter.memory.total_bytes -
                         adapter.memory.total_bytes / 4));
    if (budget == 0 || budget > adapter.memory.available_bytes) {
        throw runtime::Error(
            runtime::ErrorCode::InvalidArgument,
            "Vulkan runtime memory budget is invalid");
    }
    adapter.memory.budget_bytes = budget;
    impl_ = std::make_unique<Impl>(
        std::move(adapter), budget, pipeline_cache);
}

VulkanRuntimeDevice::~VulkanRuntimeDevice() = default;

const BackendAdapterInfo&
VulkanRuntimeDevice::adapter() const noexcept {
    return impl_->adapter;
}

runtime::DeviceState
VulkanRuntimeDevice::state() const noexcept {
    return impl_->state.load(std::memory_order_acquire);
}

std::optional<runtime::DeviceLossInfo>
VulkanRuntimeDevice::loss_info() const {
    std::scoped_lock lock(impl_->mutex);
    return impl_->loss;
}

runtime::QueueHandle VulkanRuntimeDevice::create_queue(
    const runtime::QueueDesc& desc) {
    runtime::validate(desc);
    std::scoped_lock lock(impl_->mutex);
    impl_->ready();
    const auto id = impl_->handle();
    const auto free = std::ranges::find(
        impl_->queue_claimed, false);
    if (free == impl_->queue_claimed.end()) {
        throw runtime::Error(
            runtime::ErrorCode::Unsupported,
            "Vulkan compute queue capacity is exhausted");
    }
    const auto queue_index = static_cast<std::uint32_t>(
        std::distance(impl_->queue_claimed.begin(), free));
    auto timeline = vk_structure<VkSemaphoreTypeCreateInfo>(
        VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO);
    timeline.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    auto create = vk_structure<VkSemaphoreCreateInfo>(
        VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        &timeline);
    VkSemaphore completion = VK_NULL_HANDLE;
    impl_->check(
        impl_->vk.vkCreateSemaphore(
            impl_->device, &create, nullptr, &completion),
        "vkCreateSemaphore queue completion");
    try {
        impl_->queues.emplace(
            id,
            Impl::Queue{
                impl_->native_queues[queue_index],
                queue_index,
                completion,
                0,
                {},
                desc});
    } catch (...) {
        impl_->vk.vkDestroySemaphore(
            impl_->device, completion, nullptr);
        throw;
    }
    impl_->queue_claimed[queue_index] = true;
    return runtime::QueueHandle{id};
}

runtime::FenceHandle VulkanRuntimeDevice::create_fence(
    std::uint64_t initial_value) {
    std::scoped_lock lock(impl_->mutex);
    impl_->ready();
    const auto id = impl_->handle();
    auto timeline = vk_structure<VkSemaphoreTypeCreateInfo>(
        VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO);
    timeline.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    timeline.initialValue = initial_value;
    auto create = vk_structure<VkSemaphoreCreateInfo>(
        VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        &timeline);
    VkSemaphore semaphore = VK_NULL_HANDLE;
    impl_->check(
        impl_->vk.vkCreateSemaphore(
            impl_->device, &create, nullptr, &semaphore),
        "vkCreateSemaphore timeline");
    try {
        impl_->fences.emplace(
            id, Impl::Fence{semaphore, initial_value});
    } catch (...) {
        impl_->vk.vkDestroySemaphore(
            impl_->device, semaphore, nullptr);
        throw;
    }
    return runtime::FenceHandle{id};
}

runtime::EventHandle VulkanRuntimeDevice::create_event(
    std::string_view) {
    std::scoped_lock lock(impl_->mutex);
    impl_->ready();
    const auto id = impl_->handle();
    auto create = vk_structure<VkEventCreateInfo>(
        VK_STRUCTURE_TYPE_EVENT_CREATE_INFO);
    VkEvent event = VK_NULL_HANDLE;
    impl_->check(
        impl_->vk.vkCreateEvent(
            impl_->device, &create, nullptr, &event),
        "vkCreateEvent");
    try {
        impl_->events.emplace(
            id, Impl::Event{event, false});
    } catch (...) {
        impl_->vk.vkDestroyEvent(
            impl_->device, event, nullptr);
        throw;
    }
    return runtime::EventHandle{id};
}

runtime::BufferHandle VulkanRuntimeDevice::create_buffer(
    const runtime::BufferDesc& desc) {
    runtime::validate(desc);
    std::scoped_lock lock(impl_->mutex);
    impl_->ready();
    const auto id = impl_->handle();
    auto create = vk_structure<VkBufferCreateInfo>(
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
    create.size = desc.size_bytes;
    create.usage = buffer_usage(desc.usage);
    const bool acceleration_input = runtime::has_usage(
        desc.usage,
        runtime::BufferUsage::AccelerationInput);
    const bool device_address = runtime::has_usage(
        desc.usage,
        runtime::BufferUsage::DeviceAddress) ||
        (acceleration_input && impl_->ray_query);
    if (acceleration_input && impl_->ray_query) {
        create.usage |=
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    }
    create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer buffer = VK_NULL_HANDLE;
    impl_->check(
        impl_->vk.vkCreateBuffer(
            impl_->device, &create, nullptr, &buffer),
        "vkCreateBuffer");

    VkMemoryRequirements requirements{};
    impl_->vk.vkGetBufferMemoryRequirements(
        impl_->device, buffer, &requirements);
    try {
        impl_->reserve(requirements.size);
    } catch (...) {
        impl_->vk.vkDestroyBuffer(
            impl_->device, buffer, nullptr);
        throw;
    }
    const auto properties =
        desc.memory == runtime::MemoryClass::DeviceLocal
        ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        : VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    std::uint32_t memory_type = 0;
    try {
        memory_type = impl_->memory_type(
            requirements.memoryTypeBits, properties);
    } catch (...) {
        impl_->vk.vkDestroyBuffer(
            impl_->device, buffer, nullptr);
        throw;
    }
    auto flags =
        vk_structure<VkMemoryAllocateFlagsInfo>(
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO);
    if (device_address) {
        flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    }
    auto allocate = vk_structure<VkMemoryAllocateInfo>(
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        device_address ? &flags : nullptr);
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = memory_type;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    try {
        impl_->check(
            impl_->vk.vkAllocateMemory(
                impl_->device, &allocate, nullptr, &memory),
            "vkAllocateMemory buffer");
        impl_->check(
            impl_->vk.vkBindBufferMemory(
                impl_->device, buffer, memory, 0),
            "vkBindBufferMemory");
    } catch (...) {
        impl_->vk.vkDestroyBuffer(
            impl_->device, buffer, nullptr);
        if (memory) {
            impl_->vk.vkFreeMemory(
                impl_->device, memory, nullptr);
        }
        throw;
    }
    void* mapped = nullptr;
    if (desc.memory != runtime::MemoryClass::DeviceLocal) {
        try {
            impl_->check(
                impl_->vk.vkMapMemory(
                    impl_->device,
                    memory,
                    0,
                    desc.size_bytes,
                    0,
                    &mapped),
                "vkMapMemory");
        } catch (...) {
            impl_->vk.vkDestroyBuffer(
                impl_->device, buffer, nullptr);
            impl_->vk.vkFreeMemory(
                impl_->device, memory, nullptr);
            throw;
        }
    }
    try {
        impl_->buffers.emplace(
            id,
            Impl::Buffer{
                buffer,
                memory,
                mapped,
                requirements.size,
                desc});
    } catch (...) {
        if (mapped) {
            impl_->vk.vkUnmapMemory(
                impl_->device, memory);
        }
        impl_->vk.vkDestroyBuffer(
            impl_->device, buffer, nullptr);
        impl_->vk.vkFreeMemory(
            impl_->device, memory, nullptr);
        throw;
    }
    impl_->allocated += requirements.size;
    return runtime::BufferHandle{id};
}

runtime::ImageHandle VulkanRuntimeDevice::create_image(
    const runtime::ImageDesc& desc) {
    runtime::validate(desc);
    if (desc.dimension == runtime::ImageDimension::Three &&
        desc.array_layers != 1) {
        throw runtime::Error(
            runtime::ErrorCode::Unsupported,
            "Vulkan 3D image arrays are unsupported");
    }
    std::scoped_lock lock(impl_->mutex);
    impl_->ready();
    const auto id = impl_->handle();
    auto create = vk_structure<VkImageCreateInfo>(
        VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    create.imageType =
        desc.dimension == runtime::ImageDimension::One
        ? VK_IMAGE_TYPE_1D
        : (desc.dimension == runtime::ImageDimension::Two
               ? VK_IMAGE_TYPE_2D
               : VK_IMAGE_TYPE_3D);
    create.format = image_format(desc.format);
    create.extent = {desc.width, desc.height, desc.depth};
    create.mipLevels = desc.mip_levels;
    create.arrayLayers = desc.array_layers;
    create.samples = VK_SAMPLE_COUNT_1_BIT;
    create.tiling = VK_IMAGE_TILING_OPTIMAL;
    create.usage = image_usage(desc.usage);
    create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImage image = VK_NULL_HANDLE;
    impl_->check(
        impl_->vk.vkCreateImage(
            impl_->device, &create, nullptr, &image),
        "vkCreateImage");
    VkMemoryRequirements requirements{};
    impl_->vk.vkGetImageMemoryRequirements(
        impl_->device, image, &requirements);
    try {
        impl_->reserve(requirements.size);
    } catch (...) {
        impl_->vk.vkDestroyImage(
            impl_->device, image, nullptr);
        throw;
    }
    auto allocate = vk_structure<VkMemoryAllocateInfo>(
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
    allocate.allocationSize = requirements.size;
    try {
        allocate.memoryTypeIndex = impl_->memory_type(
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    } catch (...) {
        impl_->vk.vkDestroyImage(
            impl_->device, image, nullptr);
        throw;
    }
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    try {
        impl_->check(
            impl_->vk.vkAllocateMemory(
                impl_->device, &allocate, nullptr, &memory),
            "vkAllocateMemory image");
        impl_->check(
            impl_->vk.vkBindImageMemory(
                impl_->device, image, memory, 0),
            "vkBindImageMemory");
        auto view_create =
            vk_structure<VkImageViewCreateInfo>(
                VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
        view_create.image = image;
        view_create.viewType =
            desc.dimension == runtime::ImageDimension::One
            ? (desc.array_layers > 1
                   ? VK_IMAGE_VIEW_TYPE_1D_ARRAY
                   : VK_IMAGE_VIEW_TYPE_1D)
            : (desc.dimension == runtime::ImageDimension::Two
                   ? (desc.array_layers > 1
                          ? VK_IMAGE_VIEW_TYPE_2D_ARRAY
                          : VK_IMAGE_VIEW_TYPE_2D)
                   : VK_IMAGE_VIEW_TYPE_3D);
        view_create.format = create.format;
        view_create.subresourceRange.aspectMask =
            VK_IMAGE_ASPECT_COLOR_BIT;
        view_create.subresourceRange.levelCount = desc.mip_levels;
        view_create.subresourceRange.layerCount = desc.array_layers;
        impl_->check(
            impl_->vk.vkCreateImageView(
                impl_->device, &view_create, nullptr, &view),
            "vkCreateImageView");
    } catch (...) {
        if (view) {
            impl_->vk.vkDestroyImageView(
                impl_->device, view, nullptr);
        }
        impl_->vk.vkDestroyImage(
            impl_->device, image, nullptr);
        if (memory) {
            impl_->vk.vkFreeMemory(
                impl_->device, memory, nullptr);
        }
        throw;
    }
    try {
        impl_->images.emplace(
            id,
            Impl::Image{
                image,
                view,
                memory,
                requirements.size,
                VK_IMAGE_LAYOUT_UNDEFINED,
                desc});
    } catch (...) {
        impl_->vk.vkDestroyImageView(
            impl_->device, view, nullptr);
        impl_->vk.vkDestroyImage(
            impl_->device, image, nullptr);
        impl_->vk.vkFreeMemory(
            impl_->device, memory, nullptr);
        throw;
    }
    impl_->allocated += requirements.size;
    return runtime::ImageHandle{id};
}

runtime::SamplerHandle VulkanRuntimeDevice::create_sampler(
    const runtime::SamplerDesc& desc) {
    runtime::validate(desc);
    std::scoped_lock lock(impl_->mutex);
    impl_->ready();
    const auto id = impl_->handle();
    auto create = vk_structure<VkSamplerCreateInfo>(
        VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
    create.magFilter = desc.mag_filter == runtime::Filter::Linear
        ? VK_FILTER_LINEAR
        : VK_FILTER_NEAREST;
    create.minFilter = desc.min_filter == runtime::Filter::Linear
        ? VK_FILTER_LINEAR
        : VK_FILTER_NEAREST;
    create.mipmapMode = desc.min_filter == runtime::Filter::Linear
        ? VK_SAMPLER_MIPMAP_MODE_LINEAR
        : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    create.addressModeU = sampler_address(desc.address_u);
    create.addressModeV = sampler_address(desc.address_v);
    create.addressModeW = sampler_address(desc.address_w);
    create.minLod = desc.min_lod;
    create.maxLod = desc.max_lod;
    create.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    VkSampler sampler = VK_NULL_HANDLE;
    impl_->check(
        impl_->vk.vkCreateSampler(
            impl_->device, &create, nullptr, &sampler),
        "vkCreateSampler");
    try {
        impl_->samplers.emplace(
            id, Impl::Sampler{sampler, desc});
    } catch (...) {
        impl_->vk.vkDestroySampler(
            impl_->device, sampler, nullptr);
        throw;
    }
    return runtime::SamplerHandle{id};
}

runtime::ModuleHandle VulkanRuntimeDevice::create_module(
    const runtime::ModuleDesc& desc,
    std::span<const std::byte> code) {
    runtime::validate(desc, code);
    if (desc.format != runtime::ModuleFormat::Spirv ||
        code.size() % sizeof(std::uint32_t) != 0 ||
        code.size() < sizeof(std::uint32_t)) {
        throw runtime::Error(
            runtime::ErrorCode::Unsupported,
            "Vulkan runtime accepts aligned SPIR-V modules only");
    }
    std::uint32_t magic = 0;
    std::memcpy(&magic, code.data(), sizeof(magic));
    if (magic != 0x07230203u) {
        throw runtime::Error(
            runtime::ErrorCode::InvalidArgument,
            "Vulkan SPIR-V magic is invalid");
    }
    std::scoped_lock lock(impl_->mutex);
    impl_->ready();
    const auto id = impl_->handle();
    std::vector<std::uint32_t> words(
        code.size() / sizeof(std::uint32_t));
    std::memcpy(words.data(), code.data(), code.size());
    auto create = vk_structure<VkShaderModuleCreateInfo>(
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
    create.codeSize = code.size();
    create.pCode = words.data();
    VkShaderModule module = VK_NULL_HANDLE;
    impl_->check(
        impl_->vk.vkCreateShaderModule(
            impl_->device, &create, nullptr, &module),
        "vkCreateShaderModule");
    try {
        impl_->modules.emplace(
            id, Impl::Module{module, desc});
    } catch (...) {
        impl_->vk.vkDestroyShaderModule(
            impl_->device, module, nullptr);
        throw;
    }
    return runtime::ModuleHandle{id};
}

runtime::PipelineHandle VulkanRuntimeDevice::create_pipeline(
    const runtime::PipelineDesc& desc) {
    runtime::validate(desc);
    std::scoped_lock lock(impl_->mutex);
    impl_->ready();
    const auto id = impl_->handle();
    auto& module = Impl::require(
        impl_->modules, desc.module.value, "module");
    const auto threads =
        static_cast<std::uint64_t>(desc.workgroup_size[0]) *
        desc.workgroup_size[1] *
        desc.workgroup_size[2];
    if (threads > impl_->adapter.limits.max_workgroup_threads) {
        throw runtime::Error(
            runtime::ErrorCode::Unsupported,
            "Vulkan pipeline workgroup exceeds adapter limit");
    }

    std::vector<VkDescriptorSetLayoutBinding> bindings;
    bindings.reserve(desc.bindings.size());
    for (const auto& binding : desc.bindings) {
        bindings.push_back({
            binding.slot,
            descriptor_type(binding.type),
            1,
            VK_SHADER_STAGE_COMPUTE_BIT,
            nullptr});
    }
    std::ranges::sort(
        bindings,
        {},
        &VkDescriptorSetLayoutBinding::binding);
    auto descriptor_create =
        vk_structure<VkDescriptorSetLayoutCreateInfo>(
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    descriptor_create.bindingCount =
        static_cast<std::uint32_t>(bindings.size());
    descriptor_create.pBindings = bindings.data();
    VkDescriptorSetLayout descriptor_layout = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    try {
        impl_->check(
            impl_->vk.vkCreateDescriptorSetLayout(
                impl_->device,
                &descriptor_create,
                nullptr,
                &descriptor_layout),
            "vkCreateDescriptorSetLayout");
        auto layout_create =
            vk_structure<VkPipelineLayoutCreateInfo>(
                VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
        layout_create.setLayoutCount = 1;
        layout_create.pSetLayouts = &descriptor_layout;
        impl_->check(
            impl_->vk.vkCreatePipelineLayout(
                impl_->device,
                &layout_create,
                nullptr,
                &pipeline_layout),
            "vkCreatePipelineLayout");

        std::vector<VkSpecializationMapEntry> entries;
        std::vector<std::byte> data;
        entries.reserve(desc.specialization.size());
        for (const auto& constant : desc.specialization) {
            const auto offset = static_cast<std::uint32_t>(
                data.size());
            const auto* source =
                reinterpret_cast<const std::byte*>(&constant.value);
            data.insert(
                data.end(),
                source,
                source + constant.size_bytes);
            entries.push_back({
                constant.id,
                offset,
                constant.size_bytes});
        }
        VkSpecializationInfo specialization{};
        specialization.mapEntryCount =
            static_cast<std::uint32_t>(entries.size());
        specialization.pMapEntries = entries.data();
        specialization.dataSize = data.size();
        specialization.pData = data.data();

        auto stage =
            vk_structure<VkPipelineShaderStageCreateInfo>(
                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
        stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stage.module = module.module;
        stage.pName = desc.entry_point.c_str();
        if (!entries.empty()) {
            stage.pSpecializationInfo = &specialization;
        }
        auto pipeline_create =
            vk_structure<VkComputePipelineCreateInfo>(
                VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);
        pipeline_create.stage = stage;
        pipeline_create.layout = pipeline_layout;
        impl_->check(
            impl_->vk.vkCreateComputePipelines(
                impl_->device,
                impl_->pipeline_cache,
                1,
                &pipeline_create,
                nullptr,
                &pipeline),
            "vkCreateComputePipelines");
    } catch (...) {
        if (pipeline) {
            impl_->vk.vkDestroyPipeline(
                impl_->device, pipeline, nullptr);
        }
        if (pipeline_layout) {
            impl_->vk.vkDestroyPipelineLayout(
                impl_->device, pipeline_layout, nullptr);
        }
        if (descriptor_layout) {
            impl_->vk.vkDestroyDescriptorSetLayout(
                impl_->device, descriptor_layout, nullptr);
        }
        throw;
    }
    try {
        impl_->pipelines.emplace(
            id,
            Impl::Pipeline{
                pipeline,
                pipeline_layout,
                descriptor_layout,
                desc});
    } catch (...) {
        impl_->vk.vkDestroyPipeline(
            impl_->device, pipeline, nullptr);
        impl_->vk.vkDestroyPipelineLayout(
            impl_->device, pipeline_layout, nullptr);
        impl_->vk.vkDestroyDescriptorSetLayout(
            impl_->device, descriptor_layout, nullptr);
        throw;
    }
    return runtime::PipelineHandle{id};
}

void VulkanRuntimeDevice::destroy(
    runtime::QueueHandle handle) {
    std::scoped_lock lock(impl_->mutex);
    impl_->ready();
    auto& queue = Impl::require(
        impl_->queues, handle.value, "queue");
    impl_->check(
        impl_->vk.vkQueueWaitIdle(queue.queue),
        "vkQueueWaitIdle");
    impl_->collect(queue);
    impl_->destroy_pending(queue);
    impl_->vk.vkDestroySemaphore(
        impl_->device, queue.completion, nullptr);
    impl_->queue_claimed[queue.index] = false;
    impl_->queues.erase(handle.value);
}

void VulkanRuntimeDevice::destroy(
    runtime::FenceHandle handle) {
    std::scoped_lock lock(impl_->mutex);
    impl_->wait_idle_locked();
    auto& fence = Impl::require(
        impl_->fences, handle.value, "fence");
    impl_->vk.vkDestroySemaphore(
        impl_->device, fence.semaphore, nullptr);
    impl_->fences.erase(handle.value);
}

void VulkanRuntimeDevice::destroy(
    runtime::EventHandle handle) {
    std::scoped_lock lock(impl_->mutex);
    impl_->wait_idle_locked();
    auto& event = Impl::require(
        impl_->events, handle.value, "event");
    impl_->vk.vkDestroyEvent(
        impl_->device, event.event, nullptr);
    impl_->events.erase(handle.value);
}

void VulkanRuntimeDevice::destroy(
    runtime::BufferHandle handle) {
    std::scoped_lock lock(impl_->mutex);
    impl_->wait_idle_locked();
    auto& buffer = Impl::require(
        impl_->buffers, handle.value, "buffer");
    for (const auto& [id, acceleration] :
         impl_->accelerations) {
        static_cast<void>(id);
        for (const auto& geometry :
             acceleration.geometries) {
            if (geometry.vertices == handle ||
                geometry.indices == handle) {
                throw runtime::Error(
                    runtime::ErrorCode::InvalidArgument,
                    "Vulkan acceleration input is still in use");
            }
        }
    }
    if (buffer.mapped) {
        impl_->vk.vkUnmapMemory(
            impl_->device, buffer.memory);
    }
    impl_->vk.vkDestroyBuffer(
        impl_->device, buffer.buffer, nullptr);
    impl_->vk.vkFreeMemory(
        impl_->device, buffer.memory, nullptr);
    impl_->allocated -= buffer.allocation_size;
    impl_->buffers.erase(handle.value);
}

void VulkanRuntimeDevice::destroy(
    runtime::ImageHandle handle) {
    std::scoped_lock lock(impl_->mutex);
    impl_->wait_idle_locked();
    auto& image = Impl::require(
        impl_->images, handle.value, "image");
    impl_->vk.vkDestroyImageView(
        impl_->device, image.view, nullptr);
    impl_->vk.vkDestroyImage(
        impl_->device, image.image, nullptr);
    impl_->vk.vkFreeMemory(
        impl_->device, image.memory, nullptr);
    impl_->allocated -= image.allocation_size;
    impl_->images.erase(handle.value);
}

void VulkanRuntimeDevice::destroy(
    runtime::SamplerHandle handle) {
    std::scoped_lock lock(impl_->mutex);
    impl_->wait_idle_locked();
    auto& sampler = Impl::require(
        impl_->samplers, handle.value, "sampler");
    impl_->vk.vkDestroySampler(
        impl_->device, sampler.sampler, nullptr);
    impl_->samplers.erase(handle.value);
}

void VulkanRuntimeDevice::destroy(
    runtime::ModuleHandle handle) {
    std::scoped_lock lock(impl_->mutex);
    impl_->wait_idle_locked();
    auto& module = Impl::require(
        impl_->modules, handle.value, "module");
    for (const auto& [id, pipeline] : impl_->pipelines) {
        static_cast<void>(id);
        if (pipeline.desc.module == handle) {
            throw runtime::Error(
                runtime::ErrorCode::InvalidHandle,
                "Vulkan module still owns a pipeline");
        }
    }
    impl_->vk.vkDestroyShaderModule(
        impl_->device, module.module, nullptr);
    impl_->modules.erase(handle.value);
}

void VulkanRuntimeDevice::destroy(
    runtime::PipelineHandle handle) {
    std::scoped_lock lock(impl_->mutex);
    impl_->wait_idle_locked();
    auto& pipeline = Impl::require(
        impl_->pipelines, handle.value, "pipeline");
    impl_->vk.vkDestroyPipeline(
        impl_->device, pipeline.pipeline, nullptr);
    impl_->vk.vkDestroyPipelineLayout(
        impl_->device, pipeline.layout, nullptr);
    impl_->vk.vkDestroyDescriptorSetLayout(
        impl_->device, pipeline.descriptor_layout, nullptr);
    impl_->pipelines.erase(handle.value);
}

runtime::SubmissionId VulkanRuntimeDevice::submit(
    runtime::QueueHandle queue_handle,
    const runtime::DispatchGraph& graph,
    const runtime::SubmitInfo& info) {
    runtime::validate(graph);
    std::scoped_lock lock(impl_->mutex);
    impl_->ready();
    auto& queue = Impl::require(
        impl_->queues, queue_handle.value, "queue");
    impl_->collect(queue);

    std::unordered_map<
        std::uint32_t,
        const runtime::GraphNode*> nodes;
    for (const auto& node : graph.nodes) {
        nodes.emplace(node.id, &node);
    }
    std::unordered_map<std::uint64_t, bool> event_state;
    for (const auto& [id, event] : impl_->events) {
        event_state.emplace(id, event.recorded);
    }
    for (const auto point : info.waits) {
        if (point.fence.value == 0) {
            throw runtime::Error(
                runtime::ErrorCode::InvalidHandle,
                "invalid Vulkan fence handle");
        }
        auto& fence = Impl::require(
            impl_->fences, point.fence.value, "fence");
        if (point.value > fence.last_signaled) {
            throw runtime::Error(
                runtime::ErrorCode::InvalidArgument,
                "Vulkan timeline wait precedes signal");
        }
    }
    std::unordered_map<std::uint64_t, bool>
        unique_waits;
    for (const auto point : info.waits) {
        if (!unique_waits.emplace(
                point.fence.value, true).second) {
            throw runtime::Error(
                runtime::ErrorCode::InvalidArgument,
                "Vulkan timeline wait fence is duplicated");
        }
    }
    std::unordered_map<std::uint64_t, bool>
        unique_signals;
    for (const auto point : info.signals) {
        if (!unique_signals.emplace(
                point.fence.value, true).second) {
            throw runtime::Error(
                runtime::ErrorCode::InvalidArgument,
                "Vulkan timeline signal fence is duplicated");
        }
        auto& fence = Impl::require(
            impl_->fences, point.fence.value, "fence");
        if (point.value <= fence.last_signaled) {
            throw runtime::Error(
                runtime::ErrorCode::InvalidArgument,
                "Vulkan timeline signal is not monotonic");
        }
    }
    if (queue.submitted ==
        std::numeric_limits<std::uint64_t>::max()) {
        throw runtime::Error(
            runtime::ErrorCode::Overflow,
            "Vulkan queue timeline overflows");
    }

    std::map<VkDescriptorType, std::uint32_t>
        descriptor_counts;
    std::uint32_t descriptor_sets = 0;
    const std::array grid_limits = {
        impl_->adapter.limits.max_grid_dimension_x,
        impl_->adapter.limits.max_grid_dimension_y,
        impl_->adapter.limits.max_grid_dimension_z};
    for (const auto& node : graph.nodes) {
        std::visit(
            [&](const auto& command) {
                using Type =
                    std::decay_t<decltype(command)>;
                if constexpr (std::is_same_v<
                                  Type,
                                  runtime::DispatchCommand>) {
                    auto& pipeline = Impl::require(
                        impl_->pipelines,
                        command.pipeline.value,
                        "pipeline");
                    for (std::size_t axis = 0; axis < 3; ++axis) {
                        if (command.groups[axis] >
                            grid_limits[axis]) {
                            throw runtime::Error(
                                runtime::ErrorCode::Unsupported,
                                "Vulkan dispatch grid exceeds adapter limit");
                        }
                    }
                    if (command.bindings.size() !=
                        pipeline.desc.bindings.size()) {
                        throw runtime::Error(
                            runtime::ErrorCode::InvalidArgument,
                            "Vulkan dispatch binding layout is incomplete");
                    }
                    std::unordered_map<
                        std::uint32_t,
                        runtime::BindingType> expected;
                    for (const auto& binding :
                         pipeline.desc.bindings) {
                        auto& count = descriptor_counts[
                            descriptor_type(binding.type)];
                        if (count ==
                            std::numeric_limits<
                                std::uint32_t>::max()) {
                            throw runtime::Error(
                                runtime::ErrorCode::Overflow,
                                "Vulkan descriptor count overflows");
                        }
                        expected.emplace(
                            binding.slot, binding.type);
                        ++count;
                    }
                    if (descriptor_sets ==
                        std::numeric_limits<
                            std::uint32_t>::max()) {
                        throw runtime::Error(
                            runtime::ErrorCode::Overflow,
                            "Vulkan descriptor set count overflows");
                    }
                    ++descriptor_sets;
                    for (const auto& binding : command.bindings) {
                        std::visit(
                            [&](const auto& value) {
                                using Binding =
                                    std::decay_t<
                                        decltype(value)>;
                                const auto type =
                                    expected.find(value.slot);
                                if (type == expected.end()) {
                                    throw runtime::Error(
                                        runtime::ErrorCode::
                                            InvalidArgument,
                                        "Vulkan dispatch binding slot is unexpected");
                                }
                                if constexpr (std::is_same_v<
                                                  Binding,
                                                  runtime::
                                                      BufferBinding>) {
                                    if (type->second !=
                                            runtime::BindingType::
                                                StorageBuffer &&
                                        type->second !=
                                            runtime::BindingType::
                                                ReadOnlyStorageBuffer &&
                                        type->second !=
                                            runtime::BindingType::
                                                UniformBuffer) {
                                        throw runtime::Error(
                                            runtime::ErrorCode::
                                                InvalidArgument,
                                            "Vulkan buffer binding type is invalid");
                                    }
                                    auto& buffer = Impl::require(
                                        impl_->buffers,
                                        value.buffer.value,
                                        "buffer");
                                    const auto usage =
                                        type->second ==
                                                runtime::BindingType::
                                                    UniformBuffer
                                        ? runtime::BufferUsage::Uniform
                                        : runtime::BufferUsage::Storage;
                                    if (!runtime::has_usage(
                                            buffer.desc.usage,
                                            usage)) {
                                        throw runtime::Error(
                                            runtime::ErrorCode::
                                                InvalidArgument,
                                            "Vulkan buffer binding usage is invalid");
                                    }
                                    if (value.offset >
                                            buffer.desc.size_bytes ||
                                        value.size >
                                            buffer.desc.size_bytes -
                                                value.offset) {
                                        throw runtime::Error(
                                            runtime::ErrorCode::Overflow,
                                            "Vulkan buffer binding exceeds allocation");
                                    }
                                } else if constexpr (std::is_same_v<
                                                         Binding,
                                                         runtime::
                                                             ImageBinding>) {
                                    if (type->second !=
                                            runtime::BindingType::
                                                SampledImage &&
                                        type->second !=
                                            runtime::BindingType::
                                                StorageImage) {
                                        throw runtime::Error(
                                            runtime::ErrorCode::
                                                InvalidArgument,
                                            "Vulkan image binding type is invalid");
                                    }
                                    auto& image = Impl::require(
                                        impl_->images,
                                        value.image.value,
                                        "image");
                                    const auto usage_bits =
                                        static_cast<std::uint32_t>(
                                            image.desc.usage);
                                    const auto required =
                                        type->second ==
                                                runtime::BindingType::
                                                    SampledImage
                                        ? runtime::ImageUsage::Sampled
                                        : runtime::ImageUsage::Storage;
                                    if ((usage_bits &
                                         static_cast<std::uint32_t>(
                                             required)) == 0) {
                                        throw runtime::Error(
                                            runtime::ErrorCode::
                                                InvalidArgument,
                                            "Vulkan image binding usage is invalid");
                                    }
                                    if (type->second ==
                                        runtime::BindingType::
                                            SampledImage) {
                                        if (!value.sampler) {
                                            throw runtime::Error(
                                                runtime::ErrorCode::
                                                    InvalidArgument,
                                                "Vulkan sampled image requires a sampler");
                                        }
                                        Impl::require(
                                            impl_->samplers,
                                            value.sampler->value,
                                            "sampler");
                                    } else if (value.sampler) {
                                        throw runtime::Error(
                                            runtime::ErrorCode::
                                                InvalidArgument,
                                            "Vulkan storage image rejects a sampler");
                                    }
                                } else {
                                    if (type->second !=
                                        runtime::BindingType::
                                            AccelerationStructure) {
                                        throw runtime::Error(
                                            runtime::ErrorCode::
                                                InvalidArgument,
                                            "Vulkan acceleration binding type is invalid");
                                    }
                                    Impl::require(
                                        impl_->accelerations,
                                        value.scene.value,
                                        "acceleration scene");
                                }
                            },
                            binding);
                    }
                } else if constexpr (std::is_same_v<
                                         Type,
                                         runtime::
                                             CopyBufferCommand>) {
                    auto& source = Impl::require(
                        impl_->buffers,
                        command.source.value,
                        "buffer");
                    auto& destination = Impl::require(
                        impl_->buffers,
                        command.destination.value,
                        "buffer");
                    if (!runtime::has_usage(
                            source.desc.usage,
                            runtime::BufferUsage::TransferSource) ||
                        !runtime::has_usage(
                            destination.desc.usage,
                            runtime::BufferUsage::
                                TransferDestination)) {
                        throw runtime::Error(
                            runtime::ErrorCode::InvalidArgument,
                            "Vulkan buffer copy usage is invalid");
                    }
                    if (command.source_offset >
                            source.desc.size_bytes ||
                        command.size >
                            source.desc.size_bytes -
                                command.source_offset ||
                        command.destination_offset >
                            destination.desc.size_bytes ||
                        command.size >
                            destination.desc.size_bytes -
                                command.destination_offset) {
                        throw runtime::Error(
                            runtime::ErrorCode::Overflow,
                            "Vulkan buffer copy exceeds allocation");
                    }
                } else if constexpr (std::is_same_v<
                                         Type,
                                         runtime::
                                             BufferBarrierCommand>) {
                    Impl::require(
                        impl_->buffers,
                        command.buffer.value,
                        "buffer");
                } else if constexpr (std::is_same_v<
                                         Type,
                                         runtime::
                                             SetEventCommand>) {
                    Impl::require(
                        impl_->events,
                        command.event.value,
                        "event");
                } else {
                    Impl::require(
                        impl_->events,
                        command.event.value,
                        "event");
                    std::unordered_map<std::uint32_t, bool>
                        visited_dependencies;
                    std::function<bool(std::uint32_t)>
                        dependency_signals =
                            [&](std::uint32_t dependency_id) {
                                if (visited_dependencies[
                                        dependency_id]) {
                                    return false;
                                }
                                visited_dependencies[
                                    dependency_id] = true;
                                const auto* dependency =
                                    nodes.at(dependency_id);
                                if (const auto* set =
                                        std::get_if<
                                            runtime::
                                                SetEventCommand>(
                                            &dependency->command);
                                    set &&
                                    set->event == command.event) {
                                    return true;
                                }
                                return std::ranges::any_of(
                                    dependency->dependencies,
                                    dependency_signals);
                            };
                    const bool dependency_signal =
                        std::ranges::any_of(
                            node.dependencies,
                            dependency_signals);
                    if (!event_state[command.event.value] &&
                        !dependency_signal) {
                        throw runtime::Error(
                            runtime::ErrorCode::InvalidArgument,
                            "Vulkan event wait has no signal dependency");
                    }
                }
            },
            node.command);
    }

    auto pool_create =
        vk_structure<VkCommandPoolCreateInfo>(
            VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
    pool_create.flags =
        VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool_create.queueFamilyIndex = impl_->queue_family;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    impl_->check(
        impl_->vk.vkCreateCommandPool(
            impl_->device,
            &pool_create,
            nullptr,
            &command_pool),
        "vkCreateCommandPool");
    try {
        if (descriptor_sets > 0) {
            std::vector<VkDescriptorPoolSize> sizes;
            sizes.reserve(descriptor_counts.size());
            for (const auto& [type, count] :
                 descriptor_counts) {
                sizes.push_back({type, count});
            }
            auto descriptor_create =
                vk_structure<VkDescriptorPoolCreateInfo>(
                    VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
            descriptor_create.maxSets = descriptor_sets;
            descriptor_create.poolSizeCount =
                static_cast<std::uint32_t>(sizes.size());
            descriptor_create.pPoolSizes = sizes.data();
            impl_->check(
                impl_->vk.vkCreateDescriptorPool(
                    impl_->device,
                    &descriptor_create,
                    nullptr,
                    &descriptor_pool),
                "vkCreateDescriptorPool");
        }

        auto allocate =
            vk_structure<VkCommandBufferAllocateInfo>(
                VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
        allocate.commandPool = command_pool;
        allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate.commandBufferCount = 1;
        VkCommandBuffer command_buffer = VK_NULL_HANDLE;
        impl_->check(
            impl_->vk.vkAllocateCommandBuffers(
                impl_->device,
                &allocate,
                &command_buffer),
            "vkAllocateCommandBuffers");
        auto begin = vk_structure<VkCommandBufferBeginInfo>(
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
        begin.flags =
            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        impl_->check(
            impl_->vk.vkBeginCommandBuffer(
                command_buffer, &begin),
            "vkBeginCommandBuffer");

        std::unordered_map<std::uint32_t, std::uint8_t>
            visited;
        std::unordered_map<std::uint64_t, VkImageLayout>
            planned_layouts;
        std::vector<std::uint64_t> set_events;
        std::function<void(std::uint32_t)> execute =
            [&](std::uint32_t id) {
                if (visited[id] == 2) return;
                for (const auto dependency :
                     nodes.at(id)->dependencies) {
                    execute(dependency);
                }
                const auto& command = nodes.at(id)->command;
                std::visit(
                    [&](const auto& value) {
                        using Type =
                            std::decay_t<decltype(value)>;
                        if constexpr (std::is_same_v<
                                          Type,
                                          runtime::
                                              DispatchCommand>) {
                            auto& pipeline = Impl::require(
                                impl_->pipelines,
                                value.pipeline.value,
                                "pipeline");
                            auto descriptor_allocate =
                                vk_structure<
                                    VkDescriptorSetAllocateInfo>(
                                    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
                            descriptor_allocate.descriptorPool =
                                descriptor_pool;
                            descriptor_allocate.descriptorSetCount = 1;
                            descriptor_allocate.pSetLayouts =
                                &pipeline.descriptor_layout;
                            VkDescriptorSet set = VK_NULL_HANDLE;
                            impl_->check(
                                impl_->vk.vkAllocateDescriptorSets(
                                    impl_->device,
                                    &descriptor_allocate,
                                    &set),
                                "vkAllocateDescriptorSets");

                            std::unordered_map<
                                std::uint32_t,
                                runtime::BindingType> expected;
                            for (const auto& binding :
                                 pipeline.desc.bindings) {
                                expected.emplace(
                                    binding.slot, binding.type);
                            }
                            std::vector<VkDescriptorBufferInfo>
                                buffer_infos;
                            std::vector<VkDescriptorImageInfo>
                                image_infos;
                            std::vector<
                                VkWriteDescriptorSetAccelerationStructureKHR>
                                acceleration_infos;
                            std::vector<VkWriteDescriptorSet>
                                writes;
                            buffer_infos.reserve(
                                value.bindings.size());
                            image_infos.reserve(
                                value.bindings.size());
                            acceleration_infos.reserve(
                                value.bindings.size());
                            writes.reserve(value.bindings.size());
                            for (const auto& binding :
                                 value.bindings) {
                                std::visit(
                                    [&](const auto& item) {
                                        using Binding =
                                            std::decay_t<
                                                decltype(item)>;
                                        const auto type =
                                            expected.at(item.slot);
                                        auto write =
                                            vk_structure<
                                                VkWriteDescriptorSet>(
                                                VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
                                        write.dstSet = set;
                                        write.dstBinding = item.slot;
                                        write.descriptorCount = 1;
                                        write.descriptorType =
                                            descriptor_type(type);
                                        if constexpr (
                                            std::is_same_v<
                                                Binding,
                                                runtime::
                                                    BufferBinding>) {
                                            auto& buffer =
                                                Impl::require(
                                                    impl_->buffers,
                                                    item.buffer.value,
                                                    "buffer");
                                            buffer_infos.push_back({
                                                buffer.buffer,
                                                item.offset,
                                                item.size});
                                            write.pBufferInfo =
                                                &buffer_infos.back();
                                        } else if constexpr (
                                            std::is_same_v<
                                                Binding,
                                                runtime::
                                                    ImageBinding>) {
                                            auto& image =
                                                Impl::require(
                                                    impl_->images,
                                                    item.image.value,
                                                    "image");
                                            const auto current =
                                                planned_layouts
                                                    .contains(
                                                        item.image
                                                            .value)
                                                ? planned_layouts.at(
                                                      item.image
                                                          .value)
                                                : image.layout;
                                            if (current !=
                                                VK_IMAGE_LAYOUT_GENERAL) {
                                                auto barrier =
                                                    vk_structure<
                                                        VkImageMemoryBarrier2>(
                                                        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2);
                                                barrier.srcStageMask =
                                                    VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                                                barrier.srcAccessMask =
                                                    VK_ACCESS_2_MEMORY_WRITE_BIT;
                                                barrier.dstStageMask =
                                                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                                                barrier.dstAccessMask =
                                                    VK_ACCESS_2_SHADER_READ_BIT |
                                                    VK_ACCESS_2_SHADER_WRITE_BIT;
                                                barrier.oldLayout =
                                                    current;
                                                barrier.newLayout =
                                                    VK_IMAGE_LAYOUT_GENERAL;
                                                barrier.image =
                                                    image.image;
                                                barrier.subresourceRange
                                                    .aspectMask =
                                                    VK_IMAGE_ASPECT_COLOR_BIT;
                                                barrier.subresourceRange
                                                    .levelCount =
                                                    image.desc
                                                        .mip_levels;
                                                barrier.subresourceRange
                                                    .layerCount =
                                                    image.desc
                                                        .array_layers;
                                                auto dependency =
                                                    vk_structure<
                                                        VkDependencyInfo>(
                                                        VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
                                                dependency
                                                    .imageMemoryBarrierCount =
                                                    1;
                                                dependency
                                                    .pImageMemoryBarriers =
                                                    &barrier;
                                                impl_->vk
                                                    .vkCmdPipelineBarrier2(
                                                        command_buffer,
                                                        &dependency);
                                                planned_layouts[
                                                    item.image.value] =
                                                    VK_IMAGE_LAYOUT_GENERAL;
                                            }
                                            VkSampler sampler =
                                                VK_NULL_HANDLE;
                                            if (item.sampler) {
                                                sampler =
                                                    Impl::require(
                                                        impl_->
                                                            samplers,
                                                        item.sampler
                                                            ->value,
                                                        "sampler")
                                                        .sampler;
                                            }
                                            image_infos.push_back({
                                                sampler,
                                                image.view,
                                                VK_IMAGE_LAYOUT_GENERAL});
                                            write.pImageInfo =
                                                &image_infos.back();
                                        } else {
                                            auto& acceleration =
                                                Impl::require(
                                                    impl_->
                                                        accelerations,
                                                    item.scene.value,
                                                    "acceleration scene");
                                            auto acceleration_info =
                                                vk_structure<
                                                    VkWriteDescriptorSetAccelerationStructureKHR>(
                                                    VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR);
                                            acceleration_info
                                                .accelerationStructureCount =
                                                1;
                                            acceleration_info
                                                .pAccelerationStructures =
                                                &acceleration.top;
                                            acceleration_infos.push_back(
                                                acceleration_info);
                                            write.pNext =
                                                &acceleration_infos.back();
                                        }
                                        writes.push_back(write);
                                    },
                                    binding);
                            }
                            impl_->vk.vkUpdateDescriptorSets(
                                impl_->device,
                                static_cast<std::uint32_t>(
                                    writes.size()),
                                writes.data(),
                                0,
                                nullptr);
                            impl_->vk.vkCmdBindPipeline(
                                command_buffer,
                                VK_PIPELINE_BIND_POINT_COMPUTE,
                                pipeline.pipeline);
                            impl_->vk.vkCmdBindDescriptorSets(
                                command_buffer,
                                VK_PIPELINE_BIND_POINT_COMPUTE,
                                pipeline.layout,
                                0,
                                1,
                                &set,
                                0,
                                nullptr);
                            impl_->vk.vkCmdDispatch(
                                command_buffer,
                                value.groups[0],
                                value.groups[1],
                                value.groups[2]);
                        } else if constexpr (
                            std::is_same_v<
                                Type,
                                runtime::CopyBufferCommand>) {
                            auto& source = Impl::require(
                                impl_->buffers,
                                value.source.value,
                                "buffer");
                            auto& destination = Impl::require(
                                impl_->buffers,
                                value.destination.value,
                                "buffer");
                            auto region =
                                vk_structure<VkBufferCopy2>(
                                    VK_STRUCTURE_TYPE_BUFFER_COPY_2);
                            region.srcOffset =
                                value.source_offset;
                            region.dstOffset =
                                value.destination_offset;
                            region.size = value.size;
                            auto copy =
                                vk_structure<VkCopyBufferInfo2>(
                                    VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2);
                            copy.srcBuffer = source.buffer;
                            copy.dstBuffer = destination.buffer;
                            copy.regionCount = 1;
                            copy.pRegions = &region;
                            impl_->vk.vkCmdCopyBuffer2(
                                command_buffer, &copy);
                        } else if constexpr (
                            std::is_same_v<
                                Type,
                                runtime::
                                    BufferBarrierCommand>) {
                            auto& buffer = Impl::require(
                                impl_->buffers,
                                value.buffer.value,
                                "buffer");
                            auto barrier =
                                vk_structure<VkBufferMemoryBarrier2>(
                                    VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2);
                            barrier.srcStageMask =
                                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                            barrier.srcAccessMask =
                                VK_ACCESS_2_MEMORY_WRITE_BIT;
                            barrier.dstStageMask =
                                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                            barrier.dstAccessMask =
                                VK_ACCESS_2_MEMORY_READ_BIT |
                                VK_ACCESS_2_MEMORY_WRITE_BIT;
                            barrier.srcQueueFamilyIndex =
                                VK_QUEUE_FAMILY_IGNORED;
                            barrier.dstQueueFamilyIndex =
                                VK_QUEUE_FAMILY_IGNORED;
                            barrier.buffer = buffer.buffer;
                            barrier.offset = 0;
                            barrier.size = VK_WHOLE_SIZE;
                            auto dependency =
                                vk_structure<VkDependencyInfo>(
                                    VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
                            dependency.bufferMemoryBarrierCount = 1;
                            dependency.pBufferMemoryBarriers =
                                &barrier;
                            impl_->vk.vkCmdPipelineBarrier2(
                                command_buffer, &dependency);
                        } else if constexpr (
                            std::is_same_v<
                                Type,
                                runtime::SetEventCommand>) {
                            auto& event = Impl::require(
                                impl_->events,
                                value.event.value,
                                "event");
                            impl_->vk.vkCmdResetEvent2(
                                command_buffer,
                                event.event,
                                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
                            auto barrier =
                                vk_structure<VkMemoryBarrier2>(
                                    VK_STRUCTURE_TYPE_MEMORY_BARRIER_2);
                            barrier.srcStageMask =
                                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                            barrier.srcAccessMask =
                                VK_ACCESS_2_MEMORY_WRITE_BIT;
                            barrier.dstStageMask =
                                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                            barrier.dstAccessMask =
                                VK_ACCESS_2_MEMORY_READ_BIT |
                                VK_ACCESS_2_MEMORY_WRITE_BIT;
                            auto dependency =
                                vk_structure<VkDependencyInfo>(
                                    VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
                            dependency.memoryBarrierCount = 1;
                            dependency.pMemoryBarriers = &barrier;
                            impl_->vk.vkCmdSetEvent2(
                                command_buffer,
                                event.event,
                                &dependency);
                            set_events.push_back(
                                value.event.value);
                        } else {
                            auto& event = Impl::require(
                                impl_->events,
                                value.event.value,
                                "event");
                            auto barrier =
                                vk_structure<VkMemoryBarrier2>(
                                    VK_STRUCTURE_TYPE_MEMORY_BARRIER_2);
                            barrier.srcStageMask =
                                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                            barrier.srcAccessMask =
                                VK_ACCESS_2_MEMORY_WRITE_BIT;
                            barrier.dstStageMask =
                                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                            barrier.dstAccessMask =
                                VK_ACCESS_2_MEMORY_READ_BIT |
                                VK_ACCESS_2_MEMORY_WRITE_BIT;
                            auto dependency =
                                vk_structure<VkDependencyInfo>(
                                    VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
                            dependency.memoryBarrierCount = 1;
                            dependency.pMemoryBarriers = &barrier;
                            impl_->vk.vkCmdWaitEvents2(
                                command_buffer,
                                1,
                                &event.event,
                                &dependency);
                        }
                    },
                    command);
                visited[id] = 2;
            };
        for (const auto& node : graph.nodes) {
            execute(node.id);
        }
        impl_->check(
            impl_->vk.vkEndCommandBuffer(command_buffer),
            "vkEndCommandBuffer");

        std::vector<VkSemaphoreSubmitInfo> waits;
        waits.reserve(info.waits.size());
        for (const auto point : info.waits) {
            auto& fence = Impl::require(
                impl_->fences,
                point.fence.value,
                "fence");
            waits.push_back({
                VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                nullptr,
                fence.semaphore,
                point.value,
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                0});
        }
        std::vector<VkSemaphoreSubmitInfo> signals;
        signals.reserve(info.signals.size() + 1);
        for (const auto point : info.signals) {
            auto& fence = Impl::require(
                impl_->fences,
                point.fence.value,
                "fence");
            signals.push_back({
                VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                nullptr,
                fence.semaphore,
                point.value,
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                0});
        }
        const auto internal_value = queue.submitted + 1;
        signals.push_back({
            VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            nullptr,
            queue.completion,
            internal_value,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            0});
        auto command_info =
            vk_structure<VkCommandBufferSubmitInfo>(
                VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO);
        command_info.commandBuffer = command_buffer;
        auto submit = vk_structure<VkSubmitInfo2>(
            VK_STRUCTURE_TYPE_SUBMIT_INFO_2);
        submit.waitSemaphoreInfoCount =
            static_cast<std::uint32_t>(waits.size());
        submit.pWaitSemaphoreInfos = waits.data();
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &command_info;
        submit.signalSemaphoreInfoCount =
            static_cast<std::uint32_t>(signals.size());
        submit.pSignalSemaphoreInfos = signals.data();
        impl_->check(
            impl_->vk.vkQueueSubmit2(
                queue.queue, 1, &submit, VK_NULL_HANDLE),
            "vkQueueSubmit2");

        queue.submitted = internal_value;
        try {
            queue.pending.push_back({
                internal_value,
                command_pool,
                descriptor_pool});
        } catch (...) {
            static_cast<void>(
                impl_->vk.vkQueueWaitIdle(queue.queue));
            if (descriptor_pool) {
                impl_->vk.vkDestroyDescriptorPool(
                    impl_->device,
                    descriptor_pool,
                    nullptr);
            }
            impl_->vk.vkDestroyCommandPool(
                impl_->device, command_pool, nullptr);
            throw;
        }
        command_pool = VK_NULL_HANDLE;
        descriptor_pool = VK_NULL_HANDLE;
        for (const auto point : info.signals) {
            Impl::require(
                impl_->fences,
                point.fence.value,
                "fence").last_signaled = point.value;
        }
        for (const auto event : set_events) {
            Impl::require(
                impl_->events, event, "event").recorded = true;
        }
        for (const auto& [image_id, layout] :
             planned_layouts) {
            Impl::require(
                impl_->images,
                image_id,
                "image").layout = layout;
        }
        return impl_->submission();
    } catch (...) {
        if (descriptor_pool) {
            impl_->vk.vkDestroyDescriptorPool(
                impl_->device, descriptor_pool, nullptr);
        }
        if (command_pool) {
            impl_->vk.vkDestroyCommandPool(
                impl_->device, command_pool, nullptr);
        }
        throw;
    }
}

bool VulkanRuntimeDevice::wait(
    runtime::TimelinePoint point,
    std::chrono::nanoseconds timeout) {
    std::scoped_lock lock(impl_->mutex);
    impl_->ready();
    auto& fence = Impl::require(
        impl_->fences, point.fence.value, "fence");
    if (point.value > fence.last_signaled) return false;
    auto wait_info = vk_structure<VkSemaphoreWaitInfo>(
        VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO);
    wait_info.semaphoreCount = 1;
    wait_info.pSemaphores = &fence.semaphore;
    wait_info.pValues = &point.value;
    const auto timeout_ns =
        timeout == std::chrono::nanoseconds::max()
        ? std::numeric_limits<std::uint64_t>::max()
        : static_cast<std::uint64_t>(
              std::max(timeout.count(), std::int64_t{0}));
    const auto result = impl_->vk.vkWaitSemaphores(
        impl_->device, &wait_info, timeout_ns);
    if (result == VK_TIMEOUT) return false;
    impl_->check(result, "vkWaitSemaphores");
    for (auto& [id, queue] : impl_->queues) {
        static_cast<void>(id);
        impl_->collect(queue);
    }
    return true;
}

std::uint64_t VulkanRuntimeDevice::fence_value(
    runtime::FenceHandle fence_handle) const {
    std::scoped_lock lock(impl_->mutex);
    impl_->ready();
    auto& fence = Impl::require(
        impl_->fences, fence_handle.value, "fence");
    std::uint64_t value = 0;
    impl_->check(
        impl_->vk.vkGetSemaphoreCounterValue(
            impl_->device, fence.semaphore, &value),
        "vkGetSemaphoreCounterValue");
    return value;
}

void VulkanRuntimeDevice::wait_idle() {
    std::scoped_lock lock(impl_->mutex);
    impl_->wait_idle_locked();
}

runtime::AccelerationCapabilities
VulkanRuntimeDevice::acceleration_capabilities() const noexcept {
    runtime::AccelerationFeatureSet features =
        runtime::acceleration_feature_bit(
            runtime::AccelerationFeature::ComputeBvh);
    if (impl_->ray_query) {
        features |= runtime::acceleration_feature_bit(
            runtime::AccelerationFeature::RayQuery) |
            runtime::acceleration_feature_bit(
                runtime::AccelerationFeature::Compaction) |
            runtime::acceleration_feature_bit(
                runtime::AccelerationFeature::Refit);
    }
    return {
        features,
        std::max(1u, impl_->max_triangle_geometries),
        std::max(1u, impl_->max_instances),
        impl_->scratch_alignment};
}

runtime::AccelerationSceneHandle
VulkanRuntimeDevice::create_acceleration_scene(
    const runtime::AccelerationSceneDesc& desc) {
    runtime::validate(desc);
    std::scoped_lock lock(impl_->mutex);
    impl_->ready();
    if (!impl_->ray_query) {
        throw runtime::Error(
            runtime::ErrorCode::Unsupported,
            "Vulkan ray query acceleration is unavailable");
    }
    if (desc.instances.size() > impl_->max_instances) {
        throw runtime::Error(
            runtime::ErrorCode::Unsupported,
            "Vulkan acceleration instance count exceeds adapter limit");
    }
    if (desc.geometries.size() >
        impl_->max_triangle_geometries) {
        throw runtime::Error(
            runtime::ErrorCode::Unsupported,
            "Vulkan acceleration geometry count exceeds adapter limit");
    }
    Impl::Acceleration acceleration;
    Impl::NativeBuffer scratch;
    Impl::NativeBuffer instance_buffer;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkQueryPool query_pool = VK_NULL_HANDLE;
    const auto start = std::chrono::steady_clock::now();
    try {
        auto pool_create =
            vk_structure<VkCommandPoolCreateInfo>(
                VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
        pool_create.flags =
            VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pool_create.queueFamilyIndex =
            impl_->queue_family;
        impl_->check(
            impl_->vk.vkCreateCommandPool(
                impl_->device,
                &pool_create,
                nullptr,
                &command_pool),
            "vkCreateCommandPool acceleration");
        const auto begin_commands = [&] {
            auto allocate =
                vk_structure<VkCommandBufferAllocateInfo>(
                    VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
            allocate.commandPool = command_pool;
            allocate.level =
                VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocate.commandBufferCount = 1;
            VkCommandBuffer command = VK_NULL_HANDLE;
            impl_->check(
                impl_->vk.vkAllocateCommandBuffers(
                    impl_->device, &allocate, &command),
                "vkAllocateCommandBuffers acceleration");
            auto begin =
                vk_structure<VkCommandBufferBeginInfo>(
                    VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
            begin.flags =
                VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            impl_->check(
                impl_->vk.vkBeginCommandBuffer(
                    command, &begin),
                "vkBeginCommandBuffer acceleration");
            return command;
        };
        const auto submit_commands =
            [&](VkCommandBuffer command) {
                impl_->check(
                    impl_->vk.vkEndCommandBuffer(command),
                    "vkEndCommandBuffer acceleration");
                auto command_info =
                    vk_structure<VkCommandBufferSubmitInfo>(
                        VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO);
                command_info.commandBuffer = command;
                auto submit =
                    vk_structure<VkSubmitInfo2>(
                        VK_STRUCTURE_TYPE_SUBMIT_INFO_2);
                submit.commandBufferInfoCount = 1;
                submit.pCommandBufferInfos = &command_info;
                impl_->check(
                    impl_->vk.vkQueueSubmit2(
                        impl_->native_queues.front(),
                        1,
                        &submit,
                        VK_NULL_HANDLE),
                    "vkQueueSubmit2 acceleration");
                impl_->check(
                    impl_->vk.vkQueueWaitIdle(
                        impl_->native_queues.front()),
                    "vkQueueWaitIdle acceleration");
                impl_->check(
                    impl_->vk.vkResetCommandPool(
                        impl_->device,
                        command_pool,
                        0),
                    "vkResetCommandPool acceleration");
            };
        auto barrier =
            vk_structure<VkMemoryBarrier2>(
                VK_STRUCTURE_TYPE_MEMORY_BARRIER_2);
        barrier.srcStageMask =
            VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        barrier.srcAccessMask =
            VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        barrier.dstStageMask =
            VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        barrier.dstAccessMask =
            VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        auto dependency =
            vk_structure<VkDependencyInfo>(
                VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
        dependency.memoryBarrierCount = 1;
        dependency.pMemoryBarriers = &barrier;

        const auto geometry_count = desc.geometries.size();
        std::vector<
            VkAccelerationStructureGeometryTrianglesDataKHR>
            triangles(geometry_count);
        std::vector<VkAccelerationStructureGeometryKHR>
            native_geometries(geometry_count);
        std::vector<
            VkAccelerationStructureBuildGeometryInfoKHR>
            bottom_builds(geometry_count);
        std::vector<
            VkAccelerationStructureBuildSizesInfoKHR>
            bottom_sizes(geometry_count);
        std::vector<std::uint32_t>
            primitive_counts(geometry_count);
        acceleration.bottoms.resize(
            geometry_count, VK_NULL_HANDLE);
        acceleration.bottom_storage.resize(geometry_count);
        std::uint64_t scratch_peak = 0;
        std::uint64_t uncompacted_bytes = 0;
        for (std::size_t index = 0;
             index < geometry_count;
             ++index) {
            const auto& geometry = desc.geometries[index];
            auto& vertices = Impl::require(
                impl_->buffers,
                geometry.vertices.value,
                "vertex buffer");
            auto& indices = Impl::require(
                impl_->buffers,
                geometry.indices.value,
                "index buffer");
            if (!runtime::has_usage(
                    vertices.desc.usage,
                    runtime::BufferUsage::AccelerationInput) ||
                !runtime::has_usage(
                    indices.desc.usage,
                    runtime::BufferUsage::AccelerationInput)) {
                throw runtime::Error(
                    runtime::ErrorCode::InvalidArgument,
                    "Vulkan acceleration geometry requires acceleration-input buffers");
            }
            const auto vertex_bytes =
                static_cast<std::uint64_t>(
                    geometry.vertex_count - 1) *
                    geometry.vertex_stride +
                sizeof(float) * 3;
            const auto index_bytes =
                static_cast<std::uint64_t>(
                    geometry.index_count) *
                sizeof(std::uint32_t);
            if (geometry.vertex_offset >
                    vertices.desc.size_bytes ||
                vertex_bytes >
                    vertices.desc.size_bytes -
                        geometry.vertex_offset ||
                geometry.index_offset >
                    indices.desc.size_bytes ||
                index_bytes >
                    indices.desc.size_bytes -
                        geometry.index_offset) {
                throw runtime::Error(
                    runtime::ErrorCode::Overflow,
                    "Vulkan acceleration geometry exceeds input buffer");
            }
            triangles[index] =
                vk_structure<
                    VkAccelerationStructureGeometryTrianglesDataKHR>(
                    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR);
            triangles[index].vertexFormat =
                VK_FORMAT_R32G32B32_SFLOAT;
            triangles[index].vertexData.deviceAddress =
                impl_->buffer_address(vertices.buffer) +
                geometry.vertex_offset;
            triangles[index].vertexStride =
                geometry.vertex_stride;
            triangles[index].maxVertex =
                geometry.vertex_count - 1;
            triangles[index].indexType =
                VK_INDEX_TYPE_UINT32;
            triangles[index].indexData.deviceAddress =
                impl_->buffer_address(indices.buffer) +
                geometry.index_offset;
            native_geometries[index] =
                vk_structure<VkAccelerationStructureGeometryKHR>(
                    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR);
            native_geometries[index].geometryType =
                VK_GEOMETRY_TYPE_TRIANGLES_KHR;
            native_geometries[index].flags =
                VK_GEOMETRY_OPAQUE_BIT_KHR;
            native_geometries[index].geometry.triangles =
                triangles[index];
            bottom_builds[index] =
                vk_structure<
                    VkAccelerationStructureBuildGeometryInfoKHR>(
                    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR);
            bottom_builds[index].type =
                VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
            bottom_builds[index].flags =
                acceleration_build_flags(
                    desc.build, true);
            bottom_builds[index].mode =
                VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
            bottom_builds[index].geometryCount = 1;
            bottom_builds[index].pGeometries =
                &native_geometries[index];
            primitive_counts[index] =
                geometry.index_count / 3;
            bottom_sizes[index] =
                vk_structure<
                    VkAccelerationStructureBuildSizesInfoKHR>(
                    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR);
            impl_->vk
                .vkGetAccelerationStructureBuildSizesKHR(
                    impl_->device,
                    VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                    &bottom_builds[index],
                    &primitive_counts[index],
                    &bottom_sizes[index]);
            scratch_peak = std::max<std::uint64_t>(
                scratch_peak,
                bottom_sizes[index].buildScratchSize);
            uncompacted_bytes +=
                bottom_sizes[index].accelerationStructureSize;
            acceleration.bottom_storage[index] =
                impl_->create_native_buffer(
                    bottom_sizes[index]
                        .accelerationStructureSize,
                    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    true);
            auto create =
                vk_structure<
                    VkAccelerationStructureCreateInfoKHR>(
                    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR);
            create.buffer =
                acceleration.bottom_storage[index].buffer;
            create.size =
                bottom_sizes[index]
                    .accelerationStructureSize;
            create.type =
                VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
            impl_->check(
                impl_->vk.vkCreateAccelerationStructureKHR(
                    impl_->device,
                    &create,
                    nullptr,
                    &acceleration.bottoms[index]),
                "vkCreateAccelerationStructureKHR bottom");
            bottom_builds[index].dstAccelerationStructure =
                acceleration.bottoms[index];
        }
        if (desc.build.scratch_budget_bytes != 0 &&
            scratch_peak >
                desc.build.scratch_budget_bytes) {
            throw runtime::Error(
                runtime::ErrorCode::OutOfMemory,
                "Vulkan acceleration scratch budget exceeded");
        }
        scratch = impl_->create_native_buffer(
            scratch_peak,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            true);
        const auto scratch_address =
            impl_->buffer_address(scratch.buffer);
        for (auto& build : bottom_builds) {
            build.scratchData.deviceAddress =
                scratch_address;
        }
        if (desc.build.compact) {
            auto query =
                vk_structure<VkQueryPoolCreateInfo>(
                    VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO);
            query.queryType =
                VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
            query.queryCount =
                static_cast<std::uint32_t>(geometry_count);
            impl_->check(
                impl_->vk.vkCreateQueryPool(
                    impl_->device,
                    &query,
                    nullptr,
                    &query_pool),
                "vkCreateQueryPool acceleration");
        }
        auto command = begin_commands();
        if (query_pool) {
            impl_->vk.vkCmdResetQueryPool(
                command,
                query_pool,
                0,
                static_cast<std::uint32_t>(geometry_count));
        }
        for (std::size_t index = 0;
             index < geometry_count;
             ++index) {
            VkAccelerationStructureBuildRangeInfoKHR range{};
            range.primitiveCount = primitive_counts[index];
            const auto* range_pointer = &range;
            impl_->vk.vkCmdBuildAccelerationStructuresKHR(
                command,
                1,
                &bottom_builds[index],
                &range_pointer);
            impl_->vk.vkCmdPipelineBarrier2(
                command, &dependency);
            if (query_pool) {
                impl_->vk
                    .vkCmdWriteAccelerationStructuresPropertiesKHR(
                        command,
                        1,
                        &acceleration.bottoms[index],
                        VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR,
                        query_pool,
                        static_cast<std::uint32_t>(index));
            }
        }
        submit_commands(command);
        std::uint64_t compacted_bottom_bytes = 0;
        if (query_pool) {
            std::vector<std::uint64_t> compact_sizes(
                geometry_count);
            impl_->check(
                impl_->vk.vkGetQueryPoolResults(
                    impl_->device,
                    query_pool,
                    0,
                    static_cast<std::uint32_t>(geometry_count),
                    compact_sizes.size() *
                        sizeof(std::uint64_t),
                    compact_sizes.data(),
                    sizeof(std::uint64_t),
                    VK_QUERY_RESULT_64_BIT |
                        VK_QUERY_RESULT_WAIT_BIT),
                "vkGetQueryPoolResults acceleration");
            std::vector<VkAccelerationStructureKHR>
                compact_handles(
                    geometry_count, VK_NULL_HANDLE);
            std::vector<Impl::NativeBuffer>
                compact_storage(geometry_count);
            bool copy_required = false;
            for (std::size_t index = 0;
                 index < geometry_count;
                 ++index) {
                if (compact_sizes[index] == 0 ||
                    compact_sizes[index] >=
                        bottom_sizes[index]
                            .accelerationStructureSize) {
                    compacted_bottom_bytes +=
                        bottom_sizes[index]
                            .accelerationStructureSize;
                    continue;
                }
                copy_required = true;
                compacted_bottom_bytes +=
                    compact_sizes[index];
                compact_storage[index] =
                    impl_->create_native_buffer(
                        compact_sizes[index],
                        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        true);
                auto create =
                    vk_structure<
                        VkAccelerationStructureCreateInfoKHR>(
                        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR);
                create.buffer =
                    compact_storage[index].buffer;
                create.size = compact_sizes[index];
                create.type =
                    VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
                impl_->check(
                    impl_->vk.vkCreateAccelerationStructureKHR(
                        impl_->device,
                        &create,
                        nullptr,
                        &compact_handles[index]),
                    "vkCreateAccelerationStructureKHR compact");
            }
            if (copy_required) {
                command = begin_commands();
                auto copy_barrier =
                    vk_structure<VkMemoryBarrier2>(
                        VK_STRUCTURE_TYPE_MEMORY_BARRIER_2);
                copy_barrier.srcStageMask =
                    VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
                copy_barrier.srcAccessMask =
                    VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
                copy_barrier.dstStageMask =
                    VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR;
                copy_barrier.dstAccessMask =
                    VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;
                auto copy_dependency =
                    vk_structure<VkDependencyInfo>(
                        VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
                copy_dependency.memoryBarrierCount = 1;
                copy_dependency.pMemoryBarriers =
                    &copy_barrier;
                impl_->vk.vkCmdPipelineBarrier2(
                    command, &copy_dependency);
                for (std::size_t index = 0;
                     index < geometry_count;
                     ++index) {
                    if (!compact_handles[index]) continue;
                    auto copy =
                        vk_structure<
                            VkCopyAccelerationStructureInfoKHR>(
                            VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR);
                    copy.src =
                        acceleration.bottoms[index];
                    copy.dst = compact_handles[index];
                    copy.mode =
                        VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
                    impl_->vk
                        .vkCmdCopyAccelerationStructureKHR(
                            command, &copy);
                }
                submit_commands(command);
                for (std::size_t index = 0;
                     index < geometry_count;
                     ++index) {
                    if (!compact_handles[index]) continue;
                    impl_->vk
                        .vkDestroyAccelerationStructureKHR(
                            impl_->device,
                            acceleration.bottoms[index],
                            nullptr);
                    impl_->destroy_native_buffer(
                        acceleration.bottom_storage[index]);
                    acceleration.bottoms[index] =
                        compact_handles[index];
                    acceleration.bottom_storage[index] =
                        std::move(compact_storage[index]);
                }
            }
        } else {
            for (const auto& size : bottom_sizes) {
                compacted_bottom_bytes +=
                    size.accelerationStructureSize;
            }
        }

        std::vector<VkDeviceAddress> bottom_addresses(
            geometry_count);
        for (std::size_t index = 0;
             index < geometry_count;
             ++index) {
            auto address =
                vk_structure<
                    VkAccelerationStructureDeviceAddressInfoKHR>(
                    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR);
            address.accelerationStructure =
                acceleration.bottoms[index];
            bottom_addresses[index] =
                impl_->vk
                    .vkGetAccelerationStructureDeviceAddressKHR(
                        impl_->device, &address);
        }
        const auto instance_bytes =
            static_cast<VkDeviceSize>(
                desc.instances.size() *
                sizeof(VkAccelerationStructureInstanceKHR));
        instance_buffer = impl_->create_native_buffer(
            instance_bytes,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            true);
        void* mapped = nullptr;
        impl_->check(
            impl_->vk.vkMapMemory(
                impl_->device,
                instance_buffer.memory,
                0,
                instance_bytes,
                0,
                &mapped),
            "vkMapMemory acceleration instances");
        auto* native_instances =
            static_cast<VkAccelerationStructureInstanceKHR*>(
                mapped);
        for (std::size_t index = 0;
             index < desc.instances.size();
             ++index) {
            VkAccelerationStructureInstanceKHR value{};
            std::ranges::copy(
                desc.instances[index].object_to_world,
                &value.transform.matrix[0][0]);
            value.instanceCustomIndex =
                desc.instances[index].instance_index;
            value.mask =
                desc.instances[index].visibility_mask;
            value.flags =
                VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
            value.accelerationStructureReference =
                bottom_addresses[
                    desc.instances[index].geometry_index];
            native_instances[index] = value;
        }
        impl_->vk.vkUnmapMemory(
            impl_->device, instance_buffer.memory);
        auto instance_data =
            vk_structure<
                VkAccelerationStructureGeometryInstancesDataKHR>(
                VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR);
        instance_data.data.deviceAddress =
            impl_->buffer_address(instance_buffer.buffer);
        auto top_geometry =
            vk_structure<VkAccelerationStructureGeometryKHR>(
                VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR);
        top_geometry.geometryType =
            VK_GEOMETRY_TYPE_INSTANCES_KHR;
        top_geometry.geometry.instances = instance_data;
        auto top_build =
            vk_structure<
                VkAccelerationStructureBuildGeometryInfoKHR>(
                VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR);
        top_build.type =
            VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        top_build.flags =
            acceleration_build_flags(desc.build, false);
        top_build.mode =
            VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        top_build.geometryCount = 1;
        top_build.pGeometries = &top_geometry;
        const auto instance_count =
            static_cast<std::uint32_t>(
                desc.instances.size());
        auto top_sizes =
            vk_structure<
                VkAccelerationStructureBuildSizesInfoKHR>(
                VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR);
        impl_->vk
            .vkGetAccelerationStructureBuildSizesKHR(
                impl_->device,
                VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                &top_build,
                &instance_count,
                &top_sizes);
        scratch_peak = std::max<std::uint64_t>(
            scratch_peak,
            std::max(
                top_sizes.buildScratchSize,
                top_sizes.updateScratchSize));
        if (desc.build.scratch_budget_bytes != 0 &&
            scratch_peak >
                desc.build.scratch_budget_bytes) {
            throw runtime::Error(
                runtime::ErrorCode::OutOfMemory,
                "Vulkan acceleration scratch budget exceeded");
        }
        if (scratch_peak > scratch.allocation_size) {
            impl_->destroy_native_buffer(scratch);
            scratch = impl_->create_native_buffer(
                scratch_peak,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                true);
        }
        acceleration.top_storage =
            impl_->create_native_buffer(
                top_sizes.accelerationStructureSize,
                VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                true);
        auto top_create =
            vk_structure<VkAccelerationStructureCreateInfoKHR>(
                VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR);
        top_create.buffer =
            acceleration.top_storage.buffer;
        top_create.size =
            top_sizes.accelerationStructureSize;
        top_create.type =
            VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        impl_->check(
            impl_->vk.vkCreateAccelerationStructureKHR(
                impl_->device,
                &top_create,
                nullptr,
                &acceleration.top),
            "vkCreateAccelerationStructureKHR top");
        top_build.dstAccelerationStructure =
            acceleration.top;
        top_build.scratchData.deviceAddress =
            impl_->buffer_address(scratch.buffer);
        command = begin_commands();
        auto input_barrier =
            vk_structure<VkMemoryBarrier2>(
                VK_STRUCTURE_TYPE_MEMORY_BARRIER_2);
        input_barrier.srcStageMask =
            VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR |
            VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR;
        input_barrier.srcAccessMask =
            VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        input_barrier.dstStageMask =
            VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        input_barrier.dstAccessMask =
            VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        auto input_dependency =
            vk_structure<VkDependencyInfo>(
                VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
        input_dependency.memoryBarrierCount = 1;
        input_dependency.pMemoryBarriers =
            &input_barrier;
        impl_->vk.vkCmdPipelineBarrier2(
            command, &input_dependency);
        VkAccelerationStructureBuildRangeInfoKHR top_range{};
        top_range.primitiveCount = instance_count;
        const auto* top_range_pointer = &top_range;
        impl_->vk.vkCmdBuildAccelerationStructuresKHR(
            command,
            1,
            &top_build,
            &top_range_pointer);
        barrier.dstStageMask =
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barrier.dstAccessMask =
            VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        impl_->vk.vkCmdPipelineBarrier2(
            command, &dependency);
        submit_commands(command);
        impl_->vk.vkDestroyCommandPool(
            impl_->device,
            command_pool,
            nullptr);
        command_pool = VK_NULL_HANDLE;
        if (query_pool) {
            impl_->vk.vkDestroyQueryPool(
                impl_->device,
                query_pool,
                nullptr);
            query_pool = VK_NULL_HANDLE;
        }
        impl_->destroy_native_buffer(scratch);
        impl_->destroy_native_buffer(instance_buffer);

        const auto id = impl_->handle();
        acceleration.desc = desc;
        acceleration.geometries.assign(
            desc.geometries.begin(),
            desc.geometries.end());
        acceleration.desc.geometries =
            acceleration.geometries;
        acceleration.instances.assign(
            desc.instances.begin(),
            desc.instances.end());
        acceleration.desc.instances =
            acceleration.instances;
        acceleration.stats.geometry_count =
            static_cast<std::uint32_t>(
                acceleration.geometries.size());
        acceleration.stats.instance_count =
            static_cast<std::uint32_t>(
                acceleration.instances.size());
        acceleration.stats.build_nanoseconds =
            static_cast<std::uint64_t>(
                std::chrono::duration_cast<
                    std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() -
                    start)
                    .count());
        acceleration.stats.scratch_peak_bytes =
            scratch_peak;
        acceleration.stats.uncompacted_bytes =
            uncompacted_bytes +
            top_sizes.accelerationStructureSize;
        acceleration.stats.compacted_bytes =
            compacted_bottom_bytes +
            top_sizes.accelerationStructureSize;
        impl_->accelerations.emplace(
            id, std::move(acceleration));
        return runtime::AccelerationSceneHandle{id};
    } catch (...) {
        if (command_pool) {
            impl_->vk.vkDestroyCommandPool(
                impl_->device,
                command_pool,
                nullptr);
        }
        if (query_pool) {
            impl_->vk.vkDestroyQueryPool(
                impl_->device,
                query_pool,
                nullptr);
        }
        impl_->destroy_native_buffer(scratch);
        impl_->destroy_native_buffer(instance_buffer);
        impl_->destroy_acceleration(acceleration);
        throw;
    }
}

void VulkanRuntimeDevice::update_acceleration_scene(
    runtime::AccelerationSceneHandle scene,
    const runtime::AccelerationUpdateDesc& desc) {
    std::scoped_lock lock(impl_->mutex);
    impl_->ready();
    auto& acceleration = Impl::require(
        impl_->accelerations,
        scene.value,
        "acceleration scene");
    runtime::validate(acceleration.desc, desc);
    if (acceleration.desc.build.update_policy ==
        runtime::AccelerationUpdatePolicy::Static) {
        throw runtime::Error(
            runtime::ErrorCode::Unsupported,
            "static Vulkan acceleration rejects updates");
    }
    const auto start = std::chrono::steady_clock::now();
    Impl::NativeBuffer instance_buffer;
    Impl::NativeBuffer scratch;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    try {
        std::vector<VkDeviceAddress> bottom_addresses(
            acceleration.bottoms.size());
        for (std::size_t index = 0;
             index < acceleration.bottoms.size();
             ++index) {
            auto address =
                vk_structure<
                    VkAccelerationStructureDeviceAddressInfoKHR>(
                    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR);
            address.accelerationStructure =
                acceleration.bottoms[index];
            bottom_addresses[index] =
                impl_->vk
                    .vkGetAccelerationStructureDeviceAddressKHR(
                        impl_->device, &address);
        }
        const auto instance_bytes =
            static_cast<VkDeviceSize>(
                desc.instances.size() *
                sizeof(VkAccelerationStructureInstanceKHR));
        instance_buffer = impl_->create_native_buffer(
            instance_bytes,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            true);
        void* mapped = nullptr;
        impl_->check(
            impl_->vk.vkMapMemory(
                impl_->device,
                instance_buffer.memory,
                0,
                instance_bytes,
                0,
                &mapped),
            "vkMapMemory acceleration update");
        auto* native_instances =
            static_cast<VkAccelerationStructureInstanceKHR*>(
                mapped);
        for (std::size_t index = 0;
             index < desc.instances.size();
             ++index) {
            VkAccelerationStructureInstanceKHR value{};
            std::ranges::copy(
                desc.instances[index].object_to_world,
                &value.transform.matrix[0][0]);
            value.instanceCustomIndex =
                desc.instances[index].instance_index;
            value.mask =
                desc.instances[index].visibility_mask;
            value.flags =
                VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
            value.accelerationStructureReference =
                bottom_addresses[
                    desc.instances[index].geometry_index];
            native_instances[index] = value;
        }
        impl_->vk.vkUnmapMemory(
            impl_->device, instance_buffer.memory);
        auto instance_data =
            vk_structure<
                VkAccelerationStructureGeometryInstancesDataKHR>(
                VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR);
        instance_data.data.deviceAddress =
            impl_->buffer_address(instance_buffer.buffer);
        auto top_geometry =
            vk_structure<VkAccelerationStructureGeometryKHR>(
                VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR);
        top_geometry.geometryType =
            VK_GEOMETRY_TYPE_INSTANCES_KHR;
        top_geometry.geometry.instances = instance_data;
        auto build =
            vk_structure<
                VkAccelerationStructureBuildGeometryInfoKHR>(
                VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR);
        build.type =
            VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        build.flags = acceleration_build_flags(
            acceleration.desc.build, false);
        const bool refit =
            acceleration.desc.build.update_policy ==
            runtime::AccelerationUpdatePolicy::Refit;
        build.mode = refit
            ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR
            : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        build.srcAccelerationStructure =
            refit ? acceleration.top : VK_NULL_HANDLE;
        build.dstAccelerationStructure =
            acceleration.top;
        build.geometryCount = 1;
        build.pGeometries = &top_geometry;
        const auto instance_count =
            static_cast<std::uint32_t>(
                desc.instances.size());
        auto sizes =
            vk_structure<
                VkAccelerationStructureBuildSizesInfoKHR>(
                VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR);
        impl_->vk
            .vkGetAccelerationStructureBuildSizesKHR(
                impl_->device,
                VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                &build,
                &instance_count,
                &sizes);
        const auto scratch_bytes = refit
            ? sizes.updateScratchSize
            : sizes.buildScratchSize;
        if (acceleration.desc.build.scratch_budget_bytes != 0 &&
            scratch_bytes >
                acceleration.desc.build
                    .scratch_budget_bytes) {
            throw runtime::Error(
                runtime::ErrorCode::OutOfMemory,
                "Vulkan acceleration update scratch budget exceeded");
        }
        scratch = impl_->create_native_buffer(
            scratch_bytes,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            true);
        build.scratchData.deviceAddress =
            impl_->buffer_address(scratch.buffer);
        auto pool_create =
            vk_structure<VkCommandPoolCreateInfo>(
                VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
        pool_create.flags =
            VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pool_create.queueFamilyIndex =
            impl_->queue_family;
        impl_->check(
            impl_->vk.vkCreateCommandPool(
                impl_->device,
                &pool_create,
                nullptr,
                &command_pool),
            "vkCreateCommandPool acceleration update");
        auto allocate =
            vk_structure<VkCommandBufferAllocateInfo>(
                VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
        allocate.commandPool = command_pool;
        allocate.level =
            VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate.commandBufferCount = 1;
        VkCommandBuffer command = VK_NULL_HANDLE;
        impl_->check(
            impl_->vk.vkAllocateCommandBuffers(
                impl_->device,
                &allocate,
                &command),
            "vkAllocateCommandBuffers acceleration update");
        auto begin =
            vk_structure<VkCommandBufferBeginInfo>(
                VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
        begin.flags =
            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        impl_->check(
            impl_->vk.vkBeginCommandBuffer(
                command, &begin),
            "vkBeginCommandBuffer acceleration update");
        VkAccelerationStructureBuildRangeInfoKHR range{};
        range.primitiveCount = instance_count;
        const auto* range_pointer = &range;
        impl_->vk.vkCmdBuildAccelerationStructuresKHR(
            command,
            1,
            &build,
            &range_pointer);
        auto barrier =
            vk_structure<VkMemoryBarrier2>(
                VK_STRUCTURE_TYPE_MEMORY_BARRIER_2);
        barrier.srcStageMask =
            VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        barrier.srcAccessMask =
            VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        barrier.dstStageMask =
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barrier.dstAccessMask =
            VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        auto dependency =
            vk_structure<VkDependencyInfo>(
                VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
        dependency.memoryBarrierCount = 1;
        dependency.pMemoryBarriers = &barrier;
        impl_->vk.vkCmdPipelineBarrier2(
            command, &dependency);
        impl_->check(
            impl_->vk.vkEndCommandBuffer(command),
            "vkEndCommandBuffer acceleration update");
        auto command_info =
            vk_structure<VkCommandBufferSubmitInfo>(
                VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO);
        command_info.commandBuffer = command;
        auto submit =
            vk_structure<VkSubmitInfo2>(
                VK_STRUCTURE_TYPE_SUBMIT_INFO_2);
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &command_info;
        impl_->check(
            impl_->vk.vkQueueSubmit2(
                impl_->native_queues.front(),
                1,
                &submit,
                VK_NULL_HANDLE),
            "vkQueueSubmit2 acceleration update");
        impl_->check(
            impl_->vk.vkQueueWaitIdle(
                impl_->native_queues.front()),
            "vkQueueWaitIdle acceleration update");
        impl_->vk.vkDestroyCommandPool(
            impl_->device, command_pool, nullptr);
        command_pool = VK_NULL_HANDLE;
        impl_->destroy_native_buffer(scratch);
        impl_->destroy_native_buffer(instance_buffer);
        acceleration.instances.assign(
            desc.instances.begin(),
            desc.instances.end());
        acceleration.desc.instances =
            acceleration.instances;
        acceleration.stats.update_nanoseconds =
            static_cast<std::uint64_t>(
                std::chrono::duration_cast<
                    std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() -
                    start)
                    .count());
        acceleration.stats.scratch_peak_bytes =
            std::max(
                acceleration.stats.scratch_peak_bytes,
                static_cast<std::uint64_t>(
                    scratch_bytes));
        if (refit) {
            ++acceleration.stats.refit_count;
        } else {
            ++acceleration.stats.rebuild_count;
        }
    } catch (...) {
        if (command_pool) {
            impl_->vk.vkDestroyCommandPool(
                impl_->device,
                command_pool,
                nullptr);
        }
        impl_->destroy_native_buffer(scratch);
        impl_->destroy_native_buffer(instance_buffer);
        throw;
    }
}

runtime::AccelerationBuildStats
VulkanRuntimeDevice::acceleration_build_stats(
    runtime::AccelerationSceneHandle scene) const {
    std::scoped_lock lock(impl_->mutex);
    const auto& acceleration = Impl::require(
        impl_->accelerations,
        scene.value,
        "acceleration scene");
    return acceleration.stats;
}

void VulkanRuntimeDevice::destroy(
    runtime::AccelerationSceneHandle scene) {
    std::scoped_lock lock(impl_->mutex);
    impl_->ready();
    impl_->wait_idle_locked();
    auto found =
        impl_->accelerations.find(scene.value);
    if (!scene || found == impl_->accelerations.end()) {
        throw runtime::Error(
            runtime::ErrorCode::InvalidHandle,
            "invalid Vulkan acceleration scene handle");
    }
    impl_->destroy_acceleration(found->second);
    impl_->accelerations.erase(found);
}

void* VulkanRuntimeDevice::host_buffer(
    runtime::BufferHandle buffer_handle) const {
    std::scoped_lock lock(impl_->mutex);
    impl_->ready();
    auto& buffer = Impl::require(
        impl_->buffers, buffer_handle.value, "buffer");
    if (!buffer.mapped) {
        throw runtime::Error(
            runtime::ErrorCode::InvalidArgument,
            "Vulkan device-local buffer is not host visible");
    }
    return buffer.mapped;
}

std::uint64_t
VulkanRuntimeDevice::allocated_bytes() const {
    std::scoped_lock lock(impl_->mutex);
    return impl_->allocated;
}

std::vector<std::byte>
VulkanRuntimeDevice::pipeline_cache_data() const {
    std::scoped_lock lock(impl_->mutex);
    impl_->ready();
    for (;;) {
        std::size_t size = 0;
        impl_->check(
            impl_->vk.vkGetPipelineCacheData(
                impl_->device,
                impl_->pipeline_cache,
                &size,
                nullptr),
            "vkGetPipelineCacheData size");
        std::vector<std::byte> data(size);
        const auto result =
            impl_->vk.vkGetPipelineCacheData(
                impl_->device,
                impl_->pipeline_cache,
                &size,
                data.data());
        if (result == VK_INCOMPLETE) continue;
        impl_->check(result, "vkGetPipelineCacheData");
        data.resize(size);
        return data;
    }
}

bool VulkanRuntimeDevice::validation_layer_enabled()
    const noexcept {
    return impl_->env->validation_layer;
}

std::vector<ValidationMessage>
VulkanRuntimeDevice::validation_messages() const {
    std::scoped_lock lock(impl_->env->validation_mutex);
    if (impl_->validation_start >=
        impl_->env->messages.size()) {
        return {};
    }
    return {
        impl_->env->messages.begin() +
            static_cast<std::ptrdiff_t>(
                impl_->validation_start),
        impl_->env->messages.end()};
}

std::vector<BackendAdapterInfo>
enumerate_vulkan_adapters() {
    const auto env = environment();
    std::vector<BackendAdapterInfo> adapters;
    adapters.reserve(env->adapters.size());
    for (const auto& record : env->adapters) {
        adapters.push_back(record.info);
    }
    return adapters;
}

std::unique_ptr<VulkanRuntimeDevice>
make_vulkan_runtime_device(
    BackendAdapterInfo adapter,
    std::uint64_t memory_budget_bytes,
    std::span<const std::byte> pipeline_cache) {
    return std::make_unique<VulkanRuntimeDevice>(
        std::move(adapter),
        memory_budget_bytes,
        pipeline_cache);
}

}
