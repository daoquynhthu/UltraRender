#include "ure/backend.hpp"
#include "ure/runtime/multi_backend.hpp"
#include "ure/vulkan_runtime.hpp"

#if defined(UR_T10_HAS_D3D12)
#include "ure/d3d12_runtime.hpp"
#endif

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace rt = ure::runtime;

static std::vector<std::byte> read_file(
    const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error(
            "failed to open backend artifact");
    }
    const std::vector<char> bytes{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}};
    std::vector<std::byte> result(bytes.size());
    std::ranges::transform(
        bytes,
        result.begin(),
        [](char value) {
            return std::byte{
                static_cast<std::uint8_t>(value)};
        });
    return result;
}

static rt::IdentityDigest artifact_identity(
    const std::filesystem::path& path) {
    const auto bytes = read_file(path);
    return rt::identity_digest(bytes);
}

static std::string digest_hex(
    const rt::IdentityDigest& digest) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(digest.size() * 2);
    for (const auto byte : digest) {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 0x0f]);
    }
    return result;
}

static std::string json_escape(std::string_view value) {
    std::string result;
    for (const char character : value) {
        switch (character) {
        case '\\':
            result += "\\\\";
            break;
        case '"':
            result += "\\\"";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(character) < 0x20) {
                throw std::runtime_error(
                    "backend identity contains control data");
            }
            result.push_back(character);
            break;
        }
    }
    return result;
}

static const char* backend_name(
    ure::BackendKind backend) {
    switch (backend) {
    case ure::BackendKind::Cuda:
        return "cuda";
    case ure::BackendKind::Vulkan:
        return "vulkan";
    case ure::BackendKind::D3D12:
        return "d3d12";
    case ure::BackendKind::Auto:
        break;
    }
    return "invalid";
}

static std::optional<std::string> report_path() {
#if defined(_MSC_VER)
    std::size_t size = 0;
    if (getenv_s(
            &size,
            nullptr,
            0,
            "UR_PHASE_T10_REPORT") != 0 ||
        size == 0) {
        return std::nullopt;
    }
    std::vector<char> value(size);
    if (getenv_s(
            &size,
            value.data(),
            value.size(),
            "UR_PHASE_T10_REPORT") != 0) {
        throw std::runtime_error(
            "failed to read report path");
    }
    return std::string{value.data()};
#else
    const char* value =
        std::getenv("UR_PHASE_T10_REPORT");
    if (!value || *value == '\0') {
        return std::nullopt;
    }
    return std::string{value};
#endif
}

static void append_workers(
    std::vector<rt::WorkerCapability>& workers,
    std::span<const ure::BackendAdapterInfo> adapters,
    const rt::IdentityDigest& semantics,
    const rt::IdentityDigest& executable) {
    for (const auto& adapter : adapters) {
        rt::WorkerCapability worker;
        worker.adapter = adapter;
        worker.semantic_identity = semantics;
        worker.executable_identity = executable;
        workers.push_back(std::move(worker));
    }
}

int main() {
    try {
        const auto semantics = rt::identity_digest(
            "ure.phase_t10.radiometric-spectral-polarimetric.v1");
        const auto cuda_artifact =
            artifact_identity(UR_CUDA_ARTIFACT);
        const auto vulkan_artifact =
            artifact_identity(UR_VULKAN_ARTIFACT);
        const auto cuda_adapters =
            ure::enumerate_backend_adapters(
                ure::BackendKind::Cuda);
        const auto vulkan_adapters =
            ure::vulkan::enumerate_vulkan_adapters();
        if (cuda_adapters.empty() ||
            vulkan_adapters.empty()) {
            throw std::runtime_error(
                "CUDA and Vulkan adapters are required");
        }
        std::vector<rt::WorkerCapability> workers;
        append_workers(
            workers,
            cuda_adapters,
            semantics,
            cuda_artifact);
        append_workers(
            workers,
            vulkan_adapters,
            semantics,
            vulkan_artifact);
#if defined(UR_T10_HAS_D3D12)
        const auto d3d12_adapters =
            ure::d3d12::enumerate_d3d12_adapters();
        if (d3d12_adapters.empty()) {
            throw std::runtime_error(
                "enabled D3D12 backend has no adapter");
        }
        append_workers(
            workers,
            d3d12_adapters,
            semantics,
            artifact_identity(UR_D3D12_ARTIFACT));
#endif
        ure::resource::ResourceSetMetadata resources;
        resources.content_hash =
            rt::identity_digest(
                "ure.phase_t10.inventory.resources.v1");
        resources.descriptor_count = 1;
        resources.logical_bytes = 1024;
        resources.minimum_resident_bytes = 1;
        rt::ExecutionRequirements requirements;
        requirements.required_features =
            ure::backend_feature_bit(
                ure::BackendFeature::Compute) |
            ure::backend_feature_bit(
                ure::BackendFeature::SpectralTransport) |
            ure::backend_feature_bit(
                ure::BackendFeature::Polarization);
        requirements.minimum_resident_bytes = 1;
        requirements.semantic_identity = semantics;
        const auto schedule =
            rt::negotiate_sample_shards(
                requirements,
                resources,
                workers,
                workers.size() * 4);
        if (!schedule.heterogeneous ||
            schedule.shards.size() != workers.size()) {
            throw std::runtime_error(
                "actual backend inventory did not form a heterogeneous schedule");
        }
        std::uint64_t expected_start = 0;
        for (const auto& shard : schedule.shards) {
            if (shard.sample_start != expected_start ||
                shard.sample_count != 4) {
                throw std::runtime_error(
                    "actual backend schedule is not deterministic");
            }
            expected_start += shard.sample_count;
        }
        if (const auto configured_report_path =
                report_path()) {
            const std::filesystem::path path{
                *configured_report_path};
            std::filesystem::create_directories(
                path.parent_path());
            std::ofstream report(
                path,
                std::ios::binary |
                    std::ios::trunc);
            if (!report) {
                throw std::runtime_error(
                    "failed to open inventory report");
            }
            report
                << "{\n"
                << "  \"schema\": \"ure.phase_t10.inventory.v1\",\n"
                << "  \"heterogeneous\": true,\n"
                << "  \"semantic_identity\": \""
                << digest_hex(
                       schedule.compatibility
                           .semantic_identity)
                << "\",\n"
                << "  \"worker_count\": "
                << schedule.shards.size()
                << ",\n"
                << "  \"resource_set\": {\"content_hash\": \""
                << digest_hex(resources.content_hash)
                << "\", \"descriptor_count\": "
                << resources.descriptor_count
                << ", \"logical_bytes\": "
                << resources.logical_bytes
                << ", \"minimum_resident_bytes\": "
                << resources.minimum_resident_bytes
                << "},\n"
                << "  \"workers\": [\n";
            for (std::size_t index = 0;
                 index < schedule.shards.size();
                 ++index) {
                const auto& shard =
                    schedule.shards[index];
                const auto capability = std::ranges::find_if(
                    workers,
                    [&](const rt::WorkerCapability& worker) {
                        return worker.adapter.adapter_id ==
                            shard.worker.adapter_id &&
                            worker.adapter.kind ==
                            shard.worker.backend;
                    });
                if (capability == workers.end()) {
                    throw std::runtime_error(
                        "scheduled worker capability is missing");
                }
                report
                    << "    {\"backend\": \""
                    << backend_name(
                           shard.worker.backend)
                    << "\", \"adapter_id\": \""
                    << json_escape(
                           shard.worker.adapter_id)
                    << "\", \"vendor_id\": "
                    << shard.worker.vendor_id
                    << ", \"device_id\": "
                    << shard.worker.device_id
                    << ", \"features\": "
                    << capability->adapter.features
                    << ", \"total_memory_bytes\": "
                    << capability->adapter.memory.total_bytes
                    << ", \"available_memory_bytes\": "
                    << capability->adapter.memory.available_bytes
                    << ", \"budget_bytes\": "
                    << capability->adapter.memory.budget_bytes
                    << ", \"driver\": \""
                    << json_escape(
                           shard.worker.driver_identity)
                    << "\", \"compiler\": \""
                    << json_escape(
                           shard.worker.compiler_identity)
                    << "\", \"executable\": \""
                    << digest_hex(
                           shard.worker
                               .executable_identity)
                    << "\", \"sample_start\": "
                    << shard.sample_start
                    << ", \"sample_count\": "
                    << shard.sample_count
                    << ", \"resource_cache\": \""
                    << digest_hex(
                           shard.resource_cache.digest)
                    << "\"}"
                    << (index + 1 <
                                schedule.shards.size()
                            ? ","
                            : "")
                    << "\n";
            }
            report << "  ]\n}\n";
            if (!report) {
                throw std::runtime_error(
                    "failed to write inventory report");
            }
        }
        std::printf(
            "Actual CUDA/Vulkan"
#if defined(UR_T10_HAS_D3D12)
            "/D3D12"
#endif
            " inventory formed %zu compatible sample shards.\n",
            schedule.shards.size());
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
