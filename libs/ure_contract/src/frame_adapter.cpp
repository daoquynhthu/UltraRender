#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

#include <windows.h>
#include <bcrypt.h>

#include "runtime_objects.hpp"

namespace ure::contract {
namespace {

using Digest = std::array<std::uint8_t, 32>;

class AlgorithmHandle {
  public:
    AlgorithmHandle() {
        if (BCryptOpenAlgorithmProvider(&value_, BCRYPT_SHA256_ALGORITHM, nullptr,
                                        0) < 0)
            throw std::runtime_error("SHA-256 provider unavailable");
    }

    ~AlgorithmHandle() {
        if (value_)
            BCryptCloseAlgorithmProvider(value_, 0);
    }

    AlgorithmHandle(const AlgorithmHandle &) = delete;
    AlgorithmHandle &operator=(const AlgorithmHandle &) = delete;

    BCRYPT_ALG_HANDLE get() const noexcept { return value_; }

  private:
    BCRYPT_ALG_HANDLE value_{};
};

class HashHandle {
  public:
    explicit HashHandle(BCRYPT_ALG_HANDLE algorithm) {
        if (BCryptCreateHash(algorithm, &value_, nullptr, 0, nullptr, 0, 0) < 0)
            throw std::runtime_error("SHA-256 hash allocation failed");
    }

    ~HashHandle() {
        if (value_)
            BCryptDestroyHash(value_);
    }

    HashHandle(const HashHandle &) = delete;
    HashHandle &operator=(const HashHandle &) = delete;

    BCRYPT_HASH_HANDLE get() const noexcept { return value_; }

  private:
    BCRYPT_HASH_HANDLE value_{};
};

void hash_bytes(BCRYPT_HASH_HANDLE hash, std::span<const std::uint8_t> bytes) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto count = static_cast<ULONG>(std::min<std::size_t>(
            bytes.size() - offset, std::numeric_limits<ULONG>::max()));
        if (BCryptHashData(hash, const_cast<PUCHAR>(bytes.data() + offset), count,
                           0) < 0)
            throw std::runtime_error("SHA-256 update failed");
        offset += count;
    }
}

Digest digest(std::string_view domain,
              std::span<const std::uint8_t> payload = {}) {
    AlgorithmHandle algorithm;
    HashHandle hash(algorithm.get());
    hash_bytes(hash.get(),
               std::span(reinterpret_cast<const std::uint8_t *>(domain.data()),
                         domain.size()));
    const std::uint8_t separator{};
    hash_bytes(hash.get(), std::span(&separator, 1));
    hash_bytes(hash.get(), payload);
    Digest output{};
    if (BCryptFinishHash(hash.get(), output.data(),
                         static_cast<ULONG>(output.size()), 0) < 0)
        throw std::runtime_error("SHA-256 finalization failed");
    return output;
}

void store_digest(ure_digest256_t &output, const Digest &value) noexcept {
    std::memcpy(output.bytes, value.data(), value.size());
}

bool checked_multiply(std::uint64_t left, std::uint64_t right,
                      std::uint64_t &output) noexcept {
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left)
        return false;
    output = left * right;
    return true;
}

struct PlaneData {
    std::uint32_t schema{URE_FRAME_PLANE_COLOR};
    std::uint32_t scalar_type{URE_SCALAR_TYPE_FLOAT32};
    std::uint32_t component_layout{URE_COMPONENT_LAYOUT_RGBA};
    std::uint32_t normalization{URE_NORMALIZATION_SAMPLE_MEAN};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t depth{1};
    std::uint32_t element_stride{16};
    std::uint64_t row_stride{};
    std::uint64_t slice_stride{};
    Digest observable{};
    Digest unit{};
    Digest measure{};
    Digest time{};
    Digest uncertainty{};
    Digest provenance{};
    std::vector<std::uint8_t> bytes;
};

struct FrameObject final : Object {
    ~FrameObject() override {
        if (budget_accounted && instance) {
            std::scoped_lock lock(instance->mutex);
            if (instance->retained_frames != 0)
                --instance->retained_frames;
            if (instance->retained_bytes >= retained_bytes)
                instance->retained_bytes -= retained_bytes;
            else
                instance->retained_bytes = 0;
        }
        if (operation)
            operation_interface().release(operation, nullptr);
    }

    std::mutex mutex;
    std::shared_ptr<InstanceObject> instance;
    std::vector<PlaneData> planes;
    Digest frame_identity{};
    Digest scene_revision{};
    Digest camera_revision{};
    Digest objective{};
    Digest estimator{};
    Digest provenance{};
    ure_handle_t operation{};
    std::uint64_t sample_begin{};
    std::uint64_t sample_count{1};
    std::uint64_t created_ns{};
    std::uint64_t retained_bytes{};
    std::uint32_t width{};
    std::uint32_t height{};
    bool mapped{};
    std::uint32_t mapped_plane{};
    std::uint64_t map_token{};
    std::uint64_t next_map_token{1};
    bool budget_accounted{};
};

ure_result_t frame_retain_impl(ure_handle_t frame, ure_handle_t *error) {
    clear_error(error);
    return handles().retain(frame, ObjectType::Frame)
               ? URE_RESULT_SUCCESS
               : make_error(URE_RESULT_INVALID_HANDLE, 200,
                            "invalid frame handle", error);
}

ure_result_t frame_release_impl(ure_handle_t frame, ure_handle_t *error) {
    clear_error(error);
    const auto object =
        handles().get<FrameObject>(frame, ObjectType::Frame, true);
    if (!object)
        return make_error(URE_RESULT_INVALID_HANDLE, 201, "invalid frame handle",
                          error);
    if (handles().reference_count(frame, ObjectType::Frame) > 1) {
        return handles().release(frame, ObjectType::Frame)
                   ? URE_RESULT_SUCCESS
                   : make_error(URE_RESULT_INVALID_HANDLE, 201,
                                "invalid frame release", error);
    }
    std::scoped_lock lock(object->mutex);
    if (object->mapped)
        return make_error(URE_RESULT_BUSY, 202, "frame plane is mapped", error);
    std::shared_ptr<Object> final_object;
    return handles().release(frame, ObjectType::Frame, &final_object)
               ? URE_RESULT_SUCCESS
               : make_error(URE_RESULT_INVALID_HANDLE, 201,
                            "invalid frame release", error);
}

ure_result_t frame_get_info_impl(ure_handle_t handle, ure_frame_info_t *info,
                                 ure_handle_t *error) {
    clear_error(error);
    const auto frame = handles().get<FrameObject>(handle, ObjectType::Frame);
    if (!frame)
        return make_error(URE_RESULT_INVALID_HANDLE, 203, "invalid frame handle",
                          error);
    if (!valid_output(info, URE_STRUCTURE_FRAME_INFO) || info->reserved[0] != 0 ||
        info->reserved[1] != 0)
        return make_error(URE_RESULT_INVALID_ARGUMENT, 204,
                          "invalid frame info output", error);
    store_digest(info->frame_identity, frame->frame_identity);
    store_digest(info->scene_revision_identity, frame->scene_revision);
    store_digest(info->camera_revision_identity, frame->camera_revision);
    store_digest(info->objective_identity, frame->objective);
    store_digest(info->estimator_identity, frame->estimator);
    store_digest(info->provenance_identity, frame->provenance);
    info->operation = frame->operation;
    info->sample_begin = frame->sample_begin;
    info->sample_count = frame->sample_count;
    info->timestamp_ns = frame->created_ns;
    info->retained_bytes = frame->retained_bytes;
    info->width = frame->width;
    info->height = frame->height;
    info->completion = URE_FRAME_COMPLETION_COMPLETE;
    info->plane_count = static_cast<std::uint32_t>(frame->planes.size());
    info->dirty_x = 0;
    info->dirty_y = 0;
    info->dirty_width = frame->width;
    info->dirty_height = frame->height;
    return URE_RESULT_SUCCESS;
}

ure_result_t frame_get_plane_info_impl(ure_handle_t handle,
                                       std::uint32_t plane_index,
                                       ure_frame_plane_info_t *info,
                                       ure_handle_t *error) {
    clear_error(error);
    const auto frame = handles().get<FrameObject>(handle, ObjectType::Frame);
    if (!frame)
        return make_error(URE_RESULT_INVALID_HANDLE, 205, "invalid frame handle",
                          error);
    if (!valid_output(info, URE_STRUCTURE_FRAME_PLANE_INFO) ||
        info->reserved[0] != 0 || info->reserved[1] != 0 ||
        plane_index >= frame->planes.size())
        return make_error(URE_RESULT_INVALID_ARGUMENT, 206,
                          "invalid frame plane query", error);
    const auto &plane = frame->planes[plane_index];
    info->plane_schema = plane.schema;
    info->scalar_type = plane.scalar_type;
    info->component_layout = plane.component_layout;
    info->normalization = plane.normalization;
    info->width = plane.width;
    info->height = plane.height;
    info->depth = plane.depth;
    info->element_stride = plane.element_stride;
    info->row_stride = plane.row_stride;
    info->slice_stride = plane.slice_stride;
    info->byte_extent = plane.bytes.size();
    store_digest(info->observable_identity, plane.observable);
    store_digest(info->unit_identity, plane.unit);
    store_digest(info->measure_identity, plane.measure);
    store_digest(info->time_identity, plane.time);
    store_digest(info->uncertainty_identity, plane.uncertainty);
    store_digest(info->provenance_identity, plane.provenance);
    return URE_RESULT_SUCCESS;
}

ure_result_t frame_map_impl(ure_handle_t handle, std::uint32_t plane_index,
                            ure_frame_map_t *output, ure_handle_t *error) {
    clear_error(error);
    const auto frame = handles().get<FrameObject>(handle, ObjectType::Frame);
    if (!frame)
        return make_error(URE_RESULT_INVALID_HANDLE, 207, "invalid frame handle",
                          error);
    if (!valid_output(output, URE_STRUCTURE_FRAME_MAP) || output->reserved != 0 ||
        plane_index >= frame->planes.size())
        return make_error(URE_RESULT_INVALID_ARGUMENT, 208,
                          "invalid frame map request", error);
    std::scoped_lock lock(frame->mutex);
    if (handles().get<FrameObject>(handle, ObjectType::Frame).get() != frame.get())
        return make_error(URE_RESULT_INVALID_HANDLE, 207,
                          "invalid frame handle", error);
    if (frame->mapped)
        return make_error(URE_RESULT_BUSY, 209, "frame already has a mapped plane",
                          error);
    if (frame->next_map_token == 0)
        return make_error(URE_RESULT_INTERNAL, 210, "frame map identity exhausted",
                          error);
    const auto &plane = frame->planes[plane_index];
    frame->mapped = true;
    frame->mapped_plane = plane_index;
    frame->map_token = frame->next_map_token++;
    output->frame = handle;
    output->plane_index = plane_index;
    output->data = plane.bytes.data();
    output->row_stride = plane.row_stride;
    output->slice_stride = plane.slice_stride;
    output->byte_extent = plane.bytes.size();
    output->map_token = frame->map_token;
    return URE_RESULT_SUCCESS;
}

ure_result_t frame_unmap_impl(ure_handle_t handle, std::uint64_t map_token,
                              ure_handle_t *error) {
    clear_error(error);
    const auto frame = handles().get<FrameObject>(handle, ObjectType::Frame);
    if (!frame)
        return make_error(URE_RESULT_INVALID_HANDLE, 211, "invalid frame handle",
                          error);
    std::scoped_lock lock(frame->mutex);
    if (!frame->mapped || map_token == 0 || map_token != frame->map_token)
        return make_error(URE_RESULT_INVALID_ARGUMENT, 212, "invalid frame unmap",
                          error);
    frame->mapped = false;
    frame->mapped_plane = 0;
    frame->map_token = 0;
    return URE_RESULT_SUCCESS;
}

ure_result_t frame_copy_impl(const ure_frame_copy_info_t *info,
                             ure_handle_t *error) {
    clear_error(error);
    if (!valid_input(info, URE_STRUCTURE_FRAME_COPY_INFO) ||
        info->reserved != 0 || !info->destination)
        return make_error(URE_RESULT_INVALID_ARGUMENT, 213,
                          "invalid frame copy request", error);
    const auto frame = handles().get<FrameObject>(info->frame, ObjectType::Frame);
    if (!frame)
        return make_error(URE_RESULT_INVALID_HANDLE, 214, "invalid frame handle",
                          error);
    if (info->plane_index >= frame->planes.size())
        return make_error(URE_RESULT_INVALID_ARGUMENT, 215,
                          "invalid frame plane index", error);
    const auto &plane = frame->planes[info->plane_index];
    const std::uint64_t row_bytes = plane.row_stride;
    if (info->destination_row_stride < row_bytes)
        return make_error(URE_RESULT_INVALID_ARGUMENT, 216,
                          "destination row stride is too small", error);
    std::uint64_t last_row_offset{};
    if (!checked_multiply(info->destination_row_stride, plane.height - 1,
                          last_row_offset) ||
        last_row_offset > std::numeric_limits<std::uint64_t>::max() - row_bytes ||
        last_row_offset + row_bytes > info->destination_size)
        return make_error(URE_RESULT_BUFFER_TOO_SMALL, 217,
                          "destination frame buffer is too small", error);
    for (std::uint32_t row = 0; row < plane.height; ++row) {
        std::memcpy(info->destination + info->destination_row_stride * row,
                    plane.bytes.data() + plane.row_stride * row,
                    static_cast<std::size_t>(row_bytes));
    }
    return URE_RESULT_SUCCESS;
}

#if defined(URE_CONTRACT_CONFORMANCE)
ure_result_t
produce_frame_impl(ure_handle_t instance_handle,
                   const ure_private_conformance_frame_request_t *request,
                   ure_handle_t *output, ure_handle_t *error) {
    clear_error(error);
    if (output)
        *output = nullptr;
    const auto instance =
        handles().get<InstanceObject>(instance_handle, ObjectType::Instance);
    if (!instance)
        return make_error(URE_RESULT_INVALID_HANDLE, 218, "invalid instance handle",
                          error);
    if (!output ||
        !valid_input(request, URE_PRIVATE_STRUCTURE_CONFORMANCE_FRAME_REQUEST) ||
        request->width == 0 || request->height == 0 || request->width > 8192 ||
        request->height > 8192 || request->reserved != 0)
        return make_error(URE_RESULT_INVALID_ARGUMENT, 219,
                          "invalid conformance frame request", error);
    std::uint64_t pixels{};
    std::uint64_t bytes{};
    if (!checked_multiply(request->width, request->height, pixels) ||
        !checked_multiply(pixels, 16, bytes) || bytes > UINT64_C(1073741824))
        return make_error(URE_RESULT_BUDGET_EXHAUSTED, 220,
                          "frame extent exceeds the hard byte limit", error);
    {
        std::scoped_lock lock(instance->mutex);
        if (!instance->frame_enabled)
            return make_error(URE_RESULT_CAPABILITY_UNAVAILABLE, 221,
                              "frame capability is not enabled", error);
        if (instance->retained_frames >= instance->max_retained_frames ||
            bytes > instance->max_retained_bytes - instance->retained_bytes)
            return make_error(URE_RESULT_BACKPRESSURE, 222,
                              "frame lease budget is exhausted", error);
    }
    try {
        auto frame = std::make_shared<FrameObject>();
        frame->type = ObjectType::Frame;
        frame->thread_policy = URE_THREAD_POLICY_CONCURRENT_READ;
        frame->owner = instance_handle;
        frame->parent = instance_handle;
        frame->instance = instance;
        frame->width = request->width;
        frame->height = request->height;
        frame->created_ns = timestamp_ns();
        frame->retained_bytes = bytes;
        PlaneData plane;
        plane.width = request->width;
        plane.height = request->height;
        plane.row_stride = static_cast<std::uint64_t>(request->width) * 16;
        plane.slice_stride = bytes;
        plane.bytes.resize(static_cast<std::size_t>(bytes));
        for (std::uint32_t y = 0; y < request->height; ++y) {
            for (std::uint32_t x = 0; x < request->width; ++x) {
                const std::array<float, 4> values{
                    static_cast<float>((x + request->seed) % 257U) / 256.0F,
                    static_cast<float>((y + request->seed * 3U) % 257U) / 256.0F,
                    static_cast<float>((x + y + request->seed * 7U) % 257U) / 256.0F,
                    1.0F};
                const std::size_t offset =
                    (static_cast<std::size_t>(y) * request->width + x) * 16;
                std::memcpy(plane.bytes.data() + offset, values.data(), 16);
            }
        }
        plane.observable = digest("UltraRender.Observable.LinearRgbRadiance.v1");
        plane.unit = digest("UltraRender.Unit.RadianceSI.v1");
        plane.measure = digest("UltraRender.Measure.PixelArea.v1");
        plane.time = digest("UltraRender.Time.StaticObservation.v1");
        plane.uncertainty = digest("UltraRender.Uncertainty.None.v1");
        plane.provenance =
            digest("UltraRender.FramePlane.Conformance.v1", plane.bytes);
        frame->frame_identity =
            digest("UltraRender.Frame.Conformance.v1", plane.bytes);
        frame->scene_revision = digest("UltraRender.SceneRevision.Conformance.v1");
        frame->camera_revision =
            digest("UltraRender.CameraRevision.Conformance.v1");
        frame->objective = digest("UltraRender.Objective.Conformance.v1");
        frame->estimator = digest("UltraRender.Estimator.Conformance.v1");
        frame->provenance = plane.provenance;
        frame->planes.push_back(std::move(plane));
        {
            std::scoped_lock lock(instance->mutex);
            if (instance->retained_frames >= instance->max_retained_frames ||
                bytes > instance->max_retained_bytes - instance->retained_bytes)
                return make_error(URE_RESULT_BACKPRESSURE, 222,
                                  "frame lease budget is exhausted", error);
            ++instance->retained_frames;
            instance->retained_bytes += bytes;
            frame->budget_accounted = true;
        }
        *output = handles().insert(frame);
        emit_event(instance, URE_EVENT_FRAME_READY, nullptr, *output);
        return URE_RESULT_SUCCESS;
    } catch (...) {
        return make_error(URE_RESULT_INTERNAL, 223,
                          "frame snapshot allocation failed", error);
    }
}
#endif

ure_result_t create_snapshot_impl(
    ure_handle_t instance_handle, ure_handle_t operation_handle,
    const ure_digest256_t &scene_revision,
    const ure_digest256_t &objective, std::uint64_t sample_count,
    std::uint32_t width, std::uint32_t height, const float *rgb,
    std::uint64_t rgb_count, ure_handle_t *output, ure_handle_t *error) {
    clear_error(error);
    if (output)
        *output = nullptr;
    const auto instance =
        handles().get<InstanceObject>(instance_handle, ObjectType::Instance);
    if (!instance)
        return make_error(URE_RESULT_INVALID_HANDLE, 224,
                          "invalid frame owner instance", error);
    std::uint64_t pixels{};
    std::uint64_t expected_rgb{};
    std::uint64_t bytes{};
    if (!output || width == 0 || height == 0 || width > 8192 || height > 8192 ||
        !checked_multiply(width, height, pixels) ||
        !checked_multiply(pixels, 16, bytes) || bytes > UINT64_C(1073741824))
        return make_error(URE_RESULT_INVALID_ARGUMENT, 225,
                          "invalid renderer frame extent: " +
                              std::to_string(width) + "x" +
                              std::to_string(height) + ", output=" +
                              (output ? std::string("set") : std::string("null")),
                          error);
    if (!rgb || !checked_multiply(pixels, 3, expected_rgb) ||
        rgb_count != expected_rgb)
        return make_error(URE_RESULT_INVALID_ARGUMENT, 230,
                          "renderer RGB plane extent differs from the scene: expected " +
                              std::to_string(expected_rgb) + ", received " +
                              std::to_string(rgb_count),
                          error);
    if (operation_handle &&
        !handles().get<OperationObject>(operation_handle, ObjectType::Operation))
        return make_error(URE_RESULT_INVALID_HANDLE, 226,
                          "invalid frame operation", error);
    {
        std::scoped_lock lock(instance->mutex);
        if (!instance->frame_enabled)
            return make_error(URE_RESULT_CAPABILITY_UNAVAILABLE, 227,
                              "frame capability is not enabled", error);
        if (instance->retained_frames >= instance->max_retained_frames ||
            bytes > instance->max_retained_bytes - instance->retained_bytes)
            return make_error(URE_RESULT_BACKPRESSURE, 228,
                              "frame lease budget is exhausted", error);
    }
    try {
        auto frame = std::make_shared<FrameObject>();
        frame->type = ObjectType::Frame;
        frame->thread_policy = URE_THREAD_POLICY_CONCURRENT_READ;
        frame->owner = instance_handle;
        frame->parent = instance_handle;
        frame->instance = instance;
        frame->width = width;
        frame->height = height;
        frame->sample_count = sample_count;
        frame->created_ns = timestamp_ns();
        frame->retained_bytes = bytes;
        PlaneData plane;
        plane.width = width;
        plane.height = height;
        plane.row_stride = static_cast<std::uint64_t>(width) * 16;
        plane.slice_stride = bytes;
        plane.bytes.resize(static_cast<std::size_t>(bytes));
        for (std::uint64_t pixel = 0; pixel < pixels; ++pixel) {
            const std::array<float, 4> rgba{
                rgb[pixel * 3], rgb[pixel * 3 + 1], rgb[pixel * 3 + 2], 1.0F};
            std::memcpy(plane.bytes.data() + pixel * 16, rgba.data(), 16);
        }
        plane.observable = digest("UltraRender.Observable.LinearRgbRadiance.v1");
        plane.unit = digest("UltraRender.Unit.RadianceSI.v1");
        plane.measure = digest("UltraRender.Measure.PixelArea.v1");
        plane.time = digest("UltraRender.Time.StaticObservation.v1");
        plane.uncertainty = digest("UltraRender.Uncertainty.SampleEstimate.v1");
        plane.provenance =
            digest("UltraRender.FramePlane.RenderSession.v1", plane.bytes);
        frame->frame_identity =
            digest("UltraRender.Frame.RenderSession.v1", plane.bytes);
        std::memcpy(frame->scene_revision.data(), scene_revision.bytes,
                    frame->scene_revision.size());
        frame->camera_revision = digest(
            "UltraRender.CameraRevision.NativeScene.v1",
            std::span(scene_revision.bytes, sizeof(scene_revision.bytes)));
        std::memcpy(frame->objective.data(), objective.bytes,
                    frame->objective.size());
        frame->estimator = digest("UltraRender.Estimator.Automatic.v1");
        frame->provenance = plane.provenance;
        frame->planes.push_back(std::move(plane));
        if (operation_handle) {
            if (!handles().retain(operation_handle, ObjectType::Operation))
                return make_error(URE_RESULT_INVALID_HANDLE, 226,
                                  "invalid frame operation", error);
            frame->operation = operation_handle;
        }
        {
            std::scoped_lock lock(instance->mutex);
            if (instance->retained_frames >= instance->max_retained_frames ||
                bytes > instance->max_retained_bytes - instance->retained_bytes)
                return make_error(URE_RESULT_BACKPRESSURE, 228,
                                  "frame lease budget is exhausted", error);
            ++instance->retained_frames;
            instance->retained_bytes += bytes;
            frame->budget_accounted = true;
        }
        *output = handles().insert(frame);
        emit_event(instance, URE_EVENT_FRAME_READY, operation_handle, *output);
        return URE_RESULT_SUCCESS;
    } catch (...) {
        return make_error(URE_RESULT_INTERNAL, 229,
                          "renderer frame snapshot allocation failed", error);
    }
}

ure_result_t URE_CALL frame_retain(ure_handle_t frame,
                                   ure_handle_t *error) noexcept {
    return guard_entry(error, [&] { return frame_retain_impl(frame, error); });
}

ure_result_t URE_CALL frame_release(ure_handle_t frame,
                                    ure_handle_t *error) noexcept {
    return guard_entry(error, [&] { return frame_release_impl(frame, error); });
}

ure_result_t URE_CALL frame_get_info(ure_handle_t frame, ure_frame_info_t *info,
                                     ure_handle_t *error) noexcept {
    return guard_entry(error,
                       [&] { return frame_get_info_impl(frame, info, error); });
}

ure_result_t URE_CALL frame_get_plane_info(ure_handle_t frame,
                                           std::uint32_t plane_index,
                                           ure_frame_plane_info_t *info,
                                           ure_handle_t *error) noexcept {
    return guard_entry(error, [&] {
        return frame_get_plane_info_impl(frame, plane_index, info, error);
    });
}

ure_result_t URE_CALL frame_map(ure_handle_t frame, std::uint32_t plane_index,
                                ure_frame_map_t *map,
                                ure_handle_t *error) noexcept {
    return guard_entry(
        error, [&] { return frame_map_impl(frame, plane_index, map, error); });
}

ure_result_t URE_CALL frame_unmap(ure_handle_t frame, std::uint64_t map_token,
                                  ure_handle_t *error) noexcept {
    return guard_entry(error,
                       [&] { return frame_unmap_impl(frame, map_token, error); });
}

ure_result_t URE_CALL frame_copy(const ure_frame_copy_info_t *info,
                                 ure_handle_t *error) noexcept {
    return guard_entry(error, [&] { return frame_copy_impl(info, error); });
}

}

const ure_frame_interface_t &frame_interface() noexcept {
    static const ure_frame_interface_t table{
        {sizeof(table), 0, 1}, frame_retain, frame_release, frame_get_info, frame_get_plane_info, frame_map, frame_unmap, frame_copy};
    return table;
}

ure_result_t create_frame_snapshot(
    ure_handle_t instance, ure_handle_t operation,
    const ure_digest256_t &scene_revision,
    const ure_digest256_t &objective, std::uint64_t sample_count,
    std::uint32_t width, std::uint32_t height, const float *rgb,
    std::uint64_t rgb_count, ure_handle_t *frame,
    ure_handle_t *error) noexcept {
    return guard_entry(error, [&] {
        return create_snapshot_impl(instance, operation, scene_revision, objective,
                                    sample_count, width, height, rgb, rgb_count,
                                    frame, error);
    });
}

#if defined(URE_CONTRACT_CONFORMANCE)
ure_result_t produce_conformance_frame(
    ure_handle_t instance,
    const ure_private_conformance_frame_request_t *request, ure_handle_t *frame,
    ure_handle_t *error) noexcept {
    return guard_entry(error, [&] {
        return produce_frame_impl(instance, request, frame, error);
    });
}
#endif

}
