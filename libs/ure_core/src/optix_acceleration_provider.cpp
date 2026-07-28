#define NOMINMAX

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <cuda.h>
#include <cuda_runtime.h>

#if UR_HAS_OPTIX
#include <optix.h>
#include <optix_function_table_definition.h>
#include <optix_stack_size.h>
#include <optix_stubs.h>
#endif

#include "ure/optix_acceleration.hpp"

#include "cuda_check.cuh"
#include "cuda_runtime_device.cuh"

namespace ure::gpu {

#if UR_HAS_OPTIX

namespace {

runtime::Error optix_error(
    OptixResult result,
    const char* operation) {
    return runtime::Error(
        result == OPTIX_ERROR_DEVICE_OUT_OF_MEMORY
            ? runtime::ErrorCode::OutOfMemory
            : runtime::ErrorCode::BackendFailure,
        std::string(operation) + ": " +
            optixGetErrorName(result));
}

void check_optix(
    OptixResult result,
    const char* operation) {
    if (result != OPTIX_SUCCESS) {
        throw optix_error(result, operation);
    }
}

struct OptixTraceGeometryData {
    CUdeviceptr vertices = 0;
    std::uint64_t vertex_stride = 0;
    CUdeviceptr normals = 0;
    CUdeviceptr texcoords = 0;
    CUdeviceptr tangents = 0;
    CUdeviceptr indices = 0;
};

struct OptixTraceInstanceData {
    std::uint32_t stable_index = 0;
    std::uint32_t material_index = 0;
};

struct OptixTraceLaunchParams {
    OptixTraversableHandle scene = 0;
    CUdeviceptr rays = 0;
    CUdeviceptr hits = 0;
    CUdeviceptr framebuffer = 0;
    CUdeviceptr instances = 0;
    std::uint32_t ray_count = 0;
};

template <typename Data>
struct alignas(OPTIX_SBT_RECORD_ALIGNMENT) OptixSbtRecord {
    std::array<char, OPTIX_SBT_RECORD_HEADER_SIZE> header{};
    Data data{};
};

struct alignas(OPTIX_SBT_RECORD_ALIGNMENT)
OptixSbtHeaderRecord {
    std::array<char, OPTIX_SBT_RECORD_HEADER_SIZE> header{};
};

class OptixAccelerationProvider final :
    public runtime::AccelerationProvider {
public:
    explicit OptixAccelerationProvider(
        CudaRuntimeDevice& device)
        : device_(device) {
        check_optix(optixInit(), "optixInit");
        CUcontext cuda_context = nullptr;
        const auto context_result =
            cuCtxGetCurrent(&cuda_context);
        if (context_result != CUDA_SUCCESS ||
            !cuda_context) {
            throw runtime::Error(
                runtime::ErrorCode::BackendFailure,
                "OptiX requires an active CUDA context");
        }
        OptixDeviceContextOptions options{};
        check_optix(
            optixDeviceContextCreate(
                cuda_context, &options, &context_),
            "optixDeviceContextCreate");
        queue_ = device_.create_queue({
            runtime::QueueClass::ComputeTransfer,
            0,
            "optix.acceleration"});
        stream_ = device_.native_stream(queue_);
    }

    ~OptixAccelerationProvider() override {
        std::scoped_lock lock(mutex_);
        static_cast<void>(cudaStreamSynchronize(stream_));
        for (auto& [id, scene] : scenes_) {
            static_cast<void>(id);
            destroy_scene(scene);
        }
        scenes_.clear();
        if (queue_) {
            try {
                device_.destroy(queue_);
            } catch (...) {
            }
        }
        if (context_) {
            static_cast<void>(
                optixDeviceContextDestroy(context_));
        }
    }

    runtime::AccelerationCapabilities
    acceleration_capabilities() const noexcept override {
        return {
            runtime::acceleration_feature_bit(
                runtime::AccelerationFeature::RayQuery) |
                runtime::acceleration_feature_bit(
                    runtime::AccelerationFeature::
                        RayTracingPipeline) |
                runtime::acceleration_feature_bit(
                    runtime::AccelerationFeature::Compaction) |
                runtime::acceleration_feature_bit(
                    runtime::AccelerationFeature::Refit),
            (std::numeric_limits<std::uint32_t>::max)(),
            0x00ffffffu,
            OPTIX_ACCEL_BUFFER_BYTE_ALIGNMENT};
    }

    runtime::AccelerationSceneHandle
    create_acceleration_scene(
        const runtime::AccelerationSceneDesc& desc) override {
        runtime::validate(desc);
        std::scoped_lock lock(mutex_);
        Scene scene;
        const auto start =
            std::chrono::steady_clock::now();
        try {
            scene.desc = desc;
            scene.geometries.assign(
                desc.geometries.begin(),
                desc.geometries.end());
            scene.instances.assign(
                desc.instances.begin(),
                desc.instances.end());
            scene.desc.geometries = scene.geometries;
            scene.desc.instances = scene.instances;
            scene.gas.reserve(scene.geometries.size());
            for (const auto& geometry :
                 scene.geometries) {
                scene.gas.push_back(
                    build_gas(geometry, desc.build, scene.stats));
            }
            scene.instance_buffer =
                upload_instances(
                    scene, scene.instances);
            scene.ias = build_ias(
                scene, false, scene.stats);
            scene.stats.geometry_count =
                static_cast<std::uint32_t>(
                    scene.geometries.size());
            scene.stats.instance_count =
                static_cast<std::uint32_t>(
                    scene.instances.size());
            scene.stats.build_nanoseconds =
                static_cast<std::uint64_t>(
                    std::chrono::duration_cast<
                        std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() -
                        start)
                        .count());
            const auto id = next_handle();
            scenes_.emplace(id, std::move(scene));
            return runtime::AccelerationSceneHandle{id};
        } catch (...) {
            destroy_scene(scene);
            throw;
        }
    }

    void update_acceleration_scene(
        runtime::AccelerationSceneHandle handle,
        const runtime::AccelerationUpdateDesc& desc) override {
        std::scoped_lock lock(mutex_);
        auto& scene = require(handle);
        runtime::validate(scene.desc, desc);
        if (scene.desc.build.update_policy ==
            runtime::AccelerationUpdatePolicy::Static) {
            throw runtime::Error(
                runtime::ErrorCode::Unsupported,
                "static OptiX acceleration rejects updates");
        }
        const auto start =
            std::chrono::steady_clock::now();
        auto next_instances =
            std::vector<runtime::AccelerationInstanceDesc>(
                desc.instances.begin(),
                desc.instances.end());
        const auto next_buffer =
            upload_instances(
                scene, next_instances);
        auto previous_instances =
            std::move(scene.instances);
        const auto previous_buffer =
            scene.instance_buffer;
        scene.instances =
            std::move(next_instances);
        scene.instance_buffer = next_buffer;
        scene.desc.instances = scene.instances;
        const bool refit =
            scene.desc.build.update_policy ==
            runtime::AccelerationUpdatePolicy::Refit;
        try {
            build_ias(scene, refit, scene.stats);
        } catch (...) {
            scene.instances =
                std::move(previous_instances);
            scene.instance_buffer =
                previous_buffer;
            scene.desc.instances = scene.instances;
            destroy_noexcept(next_buffer);
            throw;
        }
        device_.destroy(previous_buffer);
        scene.stats.update_nanoseconds =
            static_cast<std::uint64_t>(
                std::chrono::duration_cast<
                    std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() -
                    start)
                    .count());
        if (refit) {
            ++scene.stats.refit_count;
        } else {
            ++scene.stats.rebuild_count;
        }
    }

    runtime::AccelerationBuildStats
    acceleration_build_stats(
        runtime::AccelerationSceneHandle handle) const override {
        std::scoped_lock lock(mutex_);
        return require(handle).stats;
    }

    void destroy(
        runtime::AccelerationSceneHandle handle) override {
        std::scoped_lock lock(mutex_);
        auto found = scenes_.find(handle.value);
        if (!handle || found == scenes_.end()) {
            throw runtime::Error(
                runtime::ErrorCode::InvalidHandle,
                "invalid OptiX acceleration scene handle");
        }
        static_cast<void>(cudaStreamSynchronize(stream_));
        destroy_scene(found->second);
        scenes_.erase(found);
    }

    void trace(
        runtime::AccelerationSceneHandle handle,
        const OptixAccelerationTraceDesc& desc) {
        std::scoped_lock lock(mutex_);
        auto& scene = require(handle);
        if (desc.geometries.size() !=
                scene.geometries.size() ||
            desc.ray_count == 0 ||
            !desc.rays ||
            !desc.hits ||
            !desc.framebuffer ||
            desc.module_code.empty()) {
            throw runtime::Error(
                runtime::ErrorCode::InvalidArgument,
                "invalid OptiX acceleration trace description");
        }
        OptixModule module = nullptr;
        OptixProgramGroup raygen_group = nullptr;
        OptixProgramGroup miss_group = nullptr;
        OptixProgramGroup hit_group = nullptr;
        OptixPipeline pipeline = nullptr;
        runtime::BufferHandle raygen_record_buffer;
        runtime::BufferHandle miss_record_buffer;
        runtime::BufferHandle hit_record_buffer;
        runtime::BufferHandle instance_data_buffer;
        runtime::BufferHandle launch_params_buffer;
        auto cleanup = [&] {
            static_cast<void>(
                cudaStreamSynchronize(stream_));
            destroy_noexcept(launch_params_buffer);
            destroy_noexcept(instance_data_buffer);
            destroy_noexcept(hit_record_buffer);
            destroy_noexcept(miss_record_buffer);
            destroy_noexcept(raygen_record_buffer);
            if (pipeline) {
                static_cast<void>(
                    optixPipelineDestroy(pipeline));
            }
            if (hit_group) {
                static_cast<void>(
                    optixProgramGroupDestroy(hit_group));
            }
            if (miss_group) {
                static_cast<void>(
                    optixProgramGroupDestroy(miss_group));
            }
            if (raygen_group) {
                static_cast<void>(
                    optixProgramGroupDestroy(raygen_group));
            }
            if (module) {
                static_cast<void>(
                    optixModuleDestroy(module));
            }
        };
        try {
            OptixModuleCompileOptions module_options{};
            module_options.optLevel =
                OPTIX_COMPILE_OPTIMIZATION_LEVEL_3;
            module_options.debugLevel =
                OPTIX_COMPILE_DEBUG_LEVEL_NONE;
            OptixPipelineCompileOptions compile_options{};
            compile_options.traversableGraphFlags =
                OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_SINGLE_LEVEL_INSTANCING;
            compile_options.numPayloadValues = 1;
            compile_options.numAttributeValues = 2;
            compile_options.exceptionFlags =
                OPTIX_EXCEPTION_FLAG_STACK_OVERFLOW |
                OPTIX_EXCEPTION_FLAG_TRACE_DEPTH;
            compile_options.pipelineLaunchParamsVariableName =
                "params";
            std::array<char, 4096> log{};
            std::size_t log_size = log.size();
            check_optix(
                optixModuleCreate(
                    context_,
                    &module_options,
                    &compile_options,
                    reinterpret_cast<const char*>(
                        desc.module_code.data()),
                    desc.module_code.size(),
                    log.data(),
                    &log_size,
                    &module),
                "optixModuleCreate trace");
            OptixProgramGroupOptions group_options{};
            OptixProgramGroupDesc group_desc{};
            group_desc.kind =
                OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
            group_desc.raygen.module = module;
            group_desc.raygen.entryFunctionName =
                "__raygen__main";
            log_size = log.size();
            check_optix(
                optixProgramGroupCreate(
                    context_,
                    &group_desc,
                    1,
                    &group_options,
                    log.data(),
                    &log_size,
                    &raygen_group),
                "optixProgramGroupCreate raygen");
            group_desc = {};
            group_desc.kind =
                OPTIX_PROGRAM_GROUP_KIND_MISS;
            group_desc.miss.module = module;
            group_desc.miss.entryFunctionName =
                "__miss__main";
            log_size = log.size();
            check_optix(
                optixProgramGroupCreate(
                    context_,
                    &group_desc,
                    1,
                    &group_options,
                    log.data(),
                    &log_size,
                    &miss_group),
                "optixProgramGroupCreate miss");
            group_desc = {};
            group_desc.kind =
                OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
            group_desc.hitgroup.moduleCH = module;
            group_desc.hitgroup.entryFunctionNameCH =
                "__closesthit__main";
            log_size = log.size();
            check_optix(
                optixProgramGroupCreate(
                    context_,
                    &group_desc,
                    1,
                    &group_options,
                    log.data(),
                    &log_size,
                    &hit_group),
                "optixProgramGroupCreate hit");
            const std::array groups = {
                raygen_group,
                miss_group,
                hit_group};
            OptixPipelineLinkOptions link_options{};
            link_options.maxTraceDepth = 1;
            log_size = log.size();
            check_optix(
                optixPipelineCreate(
                    context_,
                    &compile_options,
                    &link_options,
                    groups.data(),
                    static_cast<unsigned int>(
                        groups.size()),
                    log.data(),
                    &log_size,
                    &pipeline),
                "optixPipelineCreate");
            OptixStackSizes stack_sizes{};
            for (const auto group : groups) {
                check_optix(
                    optixUtilAccumulateStackSizes(
                        group,
                        &stack_sizes,
                        pipeline),
                    "optixUtilAccumulateStackSizes");
            }
            unsigned int direct_traversal = 0;
            unsigned int direct_state = 0;
            unsigned int continuation = 0;
            check_optix(
                optixUtilComputeStackSizes(
                    &stack_sizes,
                    1,
                    0,
                    0,
                    &direct_traversal,
                    &direct_state,
                    &continuation),
                "optixUtilComputeStackSizes");
            check_optix(
                optixPipelineSetStackSize(
                    pipeline,
                    direct_traversal,
                    direct_state,
                    continuation,
                    2),
                "optixPipelineSetStackSize");
            using HeaderRecord =
                OptixSbtHeaderRecord;
            HeaderRecord raygen_record;
            HeaderRecord miss_record;
            check_optix(
                optixSbtRecordPackHeader(
                    raygen_group,
                    &raygen_record),
                "optixSbtRecordPackHeader raygen");
            check_optix(
                optixSbtRecordPackHeader(
                    miss_group,
                    &miss_record),
                "optixSbtRecordPackHeader miss");
            using HitRecord =
                OptixSbtRecord<OptixTraceGeometryData>;
            std::vector<HitRecord> hit_records(
                scene.geometries.size());
            for (std::size_t index = 0;
                 index < hit_records.size();
                 ++index) {
                check_optix(
                    optixSbtRecordPackHeader(
                        hit_group,
                        &hit_records[index]),
                    "optixSbtRecordPackHeader hit");
                const auto& geometry =
                    scene.geometries[index];
                const auto& attributes =
                    desc.geometries[index];
                if (!attributes.normals ||
                    !attributes.texcoords ||
                    !attributes.tangents) {
                    throw runtime::Error(
                        runtime::ErrorCode::InvalidArgument,
                        "OptiX trace geometry attributes are incomplete");
                }
                hit_records[index].data = {
                    address(geometry.vertices) +
                        geometry.vertex_offset,
                    geometry.vertex_stride,
                    address(attributes.normals),
                    address(attributes.texcoords),
                    address(attributes.tangents),
                    address(geometry.indices) +
                        geometry.index_offset};
            }
            std::vector<OptixTraceInstanceData>
                instance_data(scene.instances.size());
            for (std::size_t index = 0;
                 index < scene.instances.size();
                 ++index) {
                instance_data[index] = {
                    scene.instances[index].instance_index,
                    scene.instances[index].material_index};
            }
            raygen_record_buffer = upload_trace_data(
                std::span<const HeaderRecord>{
                    &raygen_record, 1},
                "optix.trace.raygen_record");
            miss_record_buffer = upload_trace_data(
                std::span<const HeaderRecord>{
                    &miss_record, 1},
                "optix.trace.miss_record");
            hit_record_buffer = upload_trace_data(
                std::span<const HitRecord>{
                    hit_records},
                "optix.trace.hit_records");
            instance_data_buffer = upload_trace_data(
                std::span<
                    const OptixTraceInstanceData>{
                    instance_data},
                "optix.trace.instances");
            const OptixTraceLaunchParams launch_params{
                scene.ias.handle,
                address(desc.rays),
                address(desc.hits),
                address(desc.framebuffer),
                address(instance_data_buffer),
                desc.ray_count};
            launch_params_buffer = upload_trace_data(
                std::span<
                    const OptixTraceLaunchParams>{
                    &launch_params, 1},
                "optix.trace.params");
            OptixShaderBindingTable sbt{};
            sbt.raygenRecord =
                address(raygen_record_buffer);
            sbt.missRecordBase =
                address(miss_record_buffer);
            sbt.missRecordStrideInBytes =
                sizeof(HeaderRecord);
            sbt.missRecordCount = 1;
            sbt.hitgroupRecordBase =
                address(hit_record_buffer);
            sbt.hitgroupRecordStrideInBytes =
                sizeof(HitRecord);
            sbt.hitgroupRecordCount =
                static_cast<unsigned int>(
                    hit_records.size());
            check_optix(
                optixLaunch(
                    pipeline,
                    stream_,
                    address(launch_params_buffer),
                    sizeof(launch_params),
                    &sbt,
                    desc.ray_count,
                    1,
                    1),
                "optixLaunch acceleration trace");
            detail::check_cuda(
                cudaStreamSynchronize(stream_),
                "cudaStreamSynchronize OptiX trace");
            cleanup();
        } catch (...) {
            cleanup();
            throw;
        }
    }

private:
    struct Built {
        runtime::BufferHandle storage;
        OptixTraversableHandle handle = 0;
        std::uint64_t bytes = 0;
    };

    struct Scene {
        runtime::AccelerationSceneDesc desc;
        std::vector<runtime::TriangleGeometryDesc>
            geometries;
        std::vector<runtime::AccelerationInstanceDesc>
            instances;
        std::vector<Built> gas;
        Built ias;
        runtime::BufferHandle instance_buffer;
        runtime::AccelerationBuildStats stats;
    };

    runtime::BufferHandle allocate(
        std::uint64_t bytes,
        const char* label) {
        return device_.create_buffer({
            bytes,
            OPTIX_ACCEL_BUFFER_BYTE_ALIGNMENT,
            runtime::BufferUsage::Storage |
                runtime::BufferUsage::AccelerationInput,
            runtime::MemoryClass::DeviceLocal,
            label});
    }

    CUdeviceptr address(
        runtime::BufferHandle buffer) const {
        return reinterpret_cast<CUdeviceptr>(
            device_.native_buffer(buffer));
    }

    template <typename T>
    runtime::BufferHandle upload_trace_data(
        std::span<const T> data,
        const char* label) {
        const auto bytes =
            data.size_bytes();
        const auto buffer = allocate(bytes, label);
        try {
            detail::check_cuda(
                cudaMemcpyAsync(
                    reinterpret_cast<void*>(address(buffer)),
                    data.data(),
                    bytes,
                    cudaMemcpyHostToDevice,
                    stream_),
                "cudaMemcpyAsync OptiX trace data");
            return buffer;
        } catch (...) {
            destroy_noexcept(buffer);
            throw;
        }
    }

    static unsigned int build_flags(
        const runtime::AccelerationBuildConfig& config,
        bool allow_update,
        bool allow_compaction) {
        unsigned int flags =
            config.quality ==
                    runtime::AccelerationBuildQuality::FastBuild
                ? OPTIX_BUILD_FLAG_PREFER_FAST_BUILD
                : OPTIX_BUILD_FLAG_PREFER_FAST_TRACE;
        if (allow_update) {
            flags |= OPTIX_BUILD_FLAG_ALLOW_UPDATE;
        }
        if (config.compact && allow_compaction) {
            flags |= OPTIX_BUILD_FLAG_ALLOW_COMPACTION;
        }
        return flags;
    }

    Built build(
        const OptixBuildInput& input,
        const runtime::AccelerationBuildConfig& config,
        bool allow_update,
        bool allow_compaction,
        runtime::AccelerationBuildStats& stats) {
        OptixAccelBuildOptions options{};
        options.buildFlags = build_flags(
            config, allow_update, allow_compaction);
        options.operation = OPTIX_BUILD_OPERATION_BUILD;
        OptixAccelBufferSizes sizes{};
        check_optix(
            optixAccelComputeMemoryUsage(
                context_,
                &options,
                &input,
                1,
                &sizes),
            "optixAccelComputeMemoryUsage");
        if (config.scratch_budget_bytes != 0 &&
            sizes.tempSizeInBytes >
                config.scratch_budget_bytes) {
            throw runtime::Error(
                runtime::ErrorCode::OutOfMemory,
                "OptiX acceleration scratch budget exceeded");
        }
        stats.scratch_peak_bytes = (std::max)(
            stats.scratch_peak_bytes,
            static_cast<std::uint64_t>(
                sizes.tempSizeInBytes));
        stats.uncompacted_bytes +=
            sizes.outputSizeInBytes;
        const auto scratch = allocate(
            sizes.tempSizeInBytes,
            "optix.acceleration.scratch");
        Built result;
        result.storage = allocate(
            sizes.outputSizeInBytes,
            "optix.acceleration.output");
        result.bytes = sizes.outputSizeInBytes;
        runtime::BufferHandle compact_size;
        try {
            OptixAccelEmitDesc emit{};
            if (config.compact && allow_compaction) {
                compact_size = allocate(
                    sizeof(std::uint64_t),
                    "optix.acceleration.compact_size");
                emit.type =
                    OPTIX_PROPERTY_TYPE_COMPACTED_SIZE;
                emit.result = address(compact_size);
            }
            check_optix(
                optixAccelBuild(
                    context_,
                    stream_,
                    &options,
                    &input,
                    1,
                    address(scratch),
                    sizes.tempSizeInBytes,
                    address(result.storage),
                    sizes.outputSizeInBytes,
                    &result.handle,
                    compact_size ? &emit : nullptr,
                    compact_size ? 1 : 0),
                "optixAccelBuild");
            auto compact_bytes =
                sizes.outputSizeInBytes;
            if (compact_size) {
                detail::check_cuda(
                    cudaMemcpyAsync(
                        &compact_bytes,
                        reinterpret_cast<const void*>(
                            address(compact_size)),
                        sizeof(compact_bytes),
                        cudaMemcpyDeviceToHost,
                        stream_),
                    "cudaMemcpyAsync OptiX compact size");
            }
            detail::check_cuda(
                cudaStreamSynchronize(stream_),
                "cudaStreamSynchronize OptiX build");
            if (compact_size &&
                compact_bytes > 0 &&
                compact_bytes < result.bytes) {
                Built compacted;
                compacted.storage = allocate(
                    compact_bytes,
                    "optix.acceleration.compacted");
                compacted.bytes = compact_bytes;
                check_optix(
                    optixAccelCompact(
                        context_,
                        stream_,
                        result.handle,
                        address(compacted.storage),
                        compacted.bytes,
                        &compacted.handle),
                    "optixAccelCompact");
                try {
                    detail::check_cuda(
                        cudaStreamSynchronize(stream_),
                        "cudaStreamSynchronize OptiX compaction");
                } catch (...) {
                    destroy_noexcept(
                        compacted.storage);
                    throw;
                }
                device_.destroy(result.storage);
                result = compacted;
            }
            stats.compacted_bytes += result.bytes;
            if (compact_size) {
                device_.destroy(compact_size);
            }
            device_.destroy(scratch);
            return result;
        } catch (...) {
            if (compact_size) {
                destroy_noexcept(compact_size);
            }
            if (result.storage) {
                destroy_noexcept(result.storage);
            }
            destroy_noexcept(scratch);
            throw;
        }
    }

    Built build_gas(
        const runtime::TriangleGeometryDesc& geometry,
        const runtime::AccelerationBuildConfig& config,
        runtime::AccelerationBuildStats& stats) {
        CUdeviceptr vertex = address(geometry.vertices) +
            geometry.vertex_offset;
        unsigned int geometry_flags =
            OPTIX_GEOMETRY_FLAG_DISABLE_ANYHIT;
        OptixBuildInput input{};
        input.type = OPTIX_BUILD_INPUT_TYPE_TRIANGLES;
        auto& triangles =
            input.triangleArray;
        triangles.vertexBuffers = &vertex;
        triangles.numVertices = geometry.vertex_count;
        triangles.vertexFormat =
            OPTIX_VERTEX_FORMAT_FLOAT3;
        triangles.vertexStrideInBytes =
            geometry.vertex_stride;
        triangles.indexBuffer =
            address(geometry.indices) +
            geometry.index_offset;
        triangles.numIndexTriplets =
            geometry.index_count / 3;
        triangles.indexFormat =
            OPTIX_INDICES_FORMAT_UNSIGNED_INT3;
        triangles.indexStrideInBytes =
            sizeof(std::uint32_t) * 3;
        triangles.flags = &geometry_flags;
        triangles.numSbtRecords = 1;
        return build(
            input, config, false, true, stats);
    }

    runtime::BufferHandle upload_instances(
        const Scene& scene,
        std::span<const runtime::AccelerationInstanceDesc>
            instances) {
        std::vector<OptixInstance> native(
            instances.size());
        for (std::size_t index = 0;
             index < instances.size();
             ++index) {
            const auto& source = instances[index];
            auto& destination = native[index];
            std::memcpy(
                destination.transform,
                source.object_to_world.data(),
                sizeof(destination.transform));
            destination.instanceId =
                source.instance_index;
            destination.sbtOffset =
                source.geometry_index;
            destination.visibilityMask =
                source.visibility_mask;
            destination.flags =
                OPTIX_INSTANCE_FLAG_DISABLE_TRIANGLE_FACE_CULLING;
            destination.traversableHandle =
                scene.gas[source.geometry_index].handle;
        }
        const auto bytes =
            native.size() * sizeof(OptixInstance);
        const auto buffer = allocate(
            bytes, "optix.acceleration.instances");
        try {
            detail::check_cuda(
                cudaMemcpyAsync(
                    reinterpret_cast<void*>(
                        address(buffer)),
                    native.data(),
                    bytes,
                    cudaMemcpyHostToDevice,
                    stream_),
                "cudaMemcpyAsync OptiX instances");
            detail::check_cuda(
                cudaStreamSynchronize(stream_),
                "cudaStreamSynchronize OptiX instances");
            return buffer;
        } catch (...) {
            destroy_noexcept(buffer);
            throw;
        }
    }

    Built build_ias(
        Scene& scene,
        bool refit,
        runtime::AccelerationBuildStats& stats) {
        OptixBuildInput input{};
        input.type = OPTIX_BUILD_INPUT_TYPE_INSTANCES;
        input.instanceArray.instances =
            address(scene.instance_buffer);
        input.instanceArray.numInstances =
            static_cast<unsigned int>(
                scene.instances.size());
        if (!scene.ias.storage) {
            return build(
                input,
                scene.desc.build,
                scene.desc.build.update_policy !=
                    runtime::AccelerationUpdatePolicy::Static,
                false,
                stats);
        }
        OptixAccelBuildOptions options{};
        options.buildFlags = build_flags(
            scene.desc.build, true, false);
        options.operation = refit
            ? OPTIX_BUILD_OPERATION_UPDATE
            : OPTIX_BUILD_OPERATION_BUILD;
        OptixAccelBufferSizes sizes{};
        check_optix(
            optixAccelComputeMemoryUsage(
                context_,
                &options,
                &input,
                1,
                &sizes),
            "optixAccelComputeMemoryUsage update");
        const auto scratch_bytes = refit
            ? sizes.tempUpdateSizeInBytes
            : sizes.tempSizeInBytes;
        if (scene.desc.build.scratch_budget_bytes != 0 &&
            scratch_bytes >
                scene.desc.build.scratch_budget_bytes) {
            throw runtime::Error(
                runtime::ErrorCode::OutOfMemory,
                "OptiX acceleration update scratch budget exceeded");
        }
        const auto scratch = allocate(
            scratch_bytes,
            "optix.acceleration.update_scratch");
        try {
            OptixTraversableHandle updated = 0;
            check_optix(
                optixAccelBuild(
                    context_,
                    stream_,
                    &options,
                    &input,
                    1,
                    address(scratch),
                    scratch_bytes,
                    address(scene.ias.storage),
                    scene.ias.bytes,
                    &updated,
                    nullptr,
                    0),
                "optixAccelBuild update");
            detail::check_cuda(
                cudaStreamSynchronize(stream_),
                "cudaStreamSynchronize OptiX update");
            scene.ias.handle = updated;
            stats.scratch_peak_bytes = (std::max)(
                stats.scratch_peak_bytes,
                static_cast<std::uint64_t>(
                    scratch_bytes));
            device_.destroy(scratch);
            return scene.ias;
        } catch (...) {
            destroy_noexcept(scratch);
            throw;
        }
    }

    Scene& require(
        runtime::AccelerationSceneHandle handle) {
        const auto found = scenes_.find(handle.value);
        if (!handle || found == scenes_.end()) {
            throw runtime::Error(
                runtime::ErrorCode::InvalidHandle,
                "invalid OptiX acceleration scene handle");
        }
        return found->second;
    }

    const Scene& require(
        runtime::AccelerationSceneHandle handle) const {
        const auto found = scenes_.find(handle.value);
        if (!handle || found == scenes_.end()) {
            throw runtime::Error(
                runtime::ErrorCode::InvalidHandle,
                "invalid OptiX acceleration scene handle");
        }
        return found->second;
    }

    std::uint64_t next_handle() {
        if (next_handle_ ==
            (std::numeric_limits<std::uint64_t>::max)()) {
            throw runtime::Error(
                runtime::ErrorCode::Overflow,
                "OptiX acceleration handle space exhausted");
        }
        return next_handle_++;
    }

    void destroy_scene(Scene& scene) noexcept {
        if (scene.instance_buffer) {
            destroy_noexcept(scene.instance_buffer);
        }
        if (scene.ias.storage) {
            destroy_noexcept(scene.ias.storage);
        }
        for (auto& gas : scene.gas) {
            if (!gas.storage) continue;
            destroy_noexcept(gas.storage);
        }
        scene = {};
    }

    void destroy_noexcept(
        runtime::BufferHandle buffer) noexcept {
        if (!buffer) return;
        try {
            device_.destroy(buffer);
        } catch (...) {
        }
    }

    CudaRuntimeDevice& device_;
    OptixDeviceContext context_ = nullptr;
    runtime::QueueHandle queue_;
    cudaStream_t stream_ = nullptr;
    mutable std::mutex mutex_;
    std::uint64_t next_handle_ = 1;
    std::unordered_map<std::uint64_t, Scene> scenes_;
};

}

bool optix_acceleration_available() noexcept {
    return true;
}

std::unique_ptr<runtime::AccelerationProvider>
make_optix_acceleration_provider(runtime::Device& device) {
    auto* cuda =
        dynamic_cast<CudaRuntimeDevice*>(&device);
    if (!cuda) {
        throw runtime::Error(
            runtime::ErrorCode::Unsupported,
            "OptiX acceleration requires the CUDA runtime device");
    }
    return std::make_unique<
        OptixAccelerationProvider>(*cuda);
}

void trace_optix_acceleration_scene(
    runtime::AccelerationProvider& provider,
    runtime::AccelerationSceneHandle scene,
    const OptixAccelerationTraceDesc& desc) {
    auto* optix =
        dynamic_cast<OptixAccelerationProvider*>(
            &provider);
    if (!optix) {
        throw runtime::Error(
            runtime::ErrorCode::Unsupported,
            "OptiX trace requires the OptiX acceleration provider");
    }
    optix->trace(scene, desc);
}

#else

bool optix_acceleration_available() noexcept {
    return false;
}

std::unique_ptr<runtime::AccelerationProvider>
make_optix_acceleration_provider(runtime::Device&) {
    throw runtime::Error(
        runtime::ErrorCode::Unsupported,
        "OptiX acceleration provider is unavailable because the SDK was not found");
}

void trace_optix_acceleration_scene(
    runtime::AccelerationProvider&,
    runtime::AccelerationSceneHandle,
    const OptixAccelerationTraceDesc&) {
    throw runtime::Error(
        runtime::ErrorCode::Unsupported,
        "OptiX acceleration trace is unavailable because the SDK was not found");
}

#endif

}
