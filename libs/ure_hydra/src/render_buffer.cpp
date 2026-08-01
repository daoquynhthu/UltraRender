#include <algorithm>
#include <limits>
#include <stdexcept>

#include <pxr/imaging/hd/types.h>

#include "render_buffer.hpp"

PXR_NAMESPACE_OPEN_SCOPE

HdURERenderBuffer::HdURERenderBuffer(
    const SdfPath& id)
    : HdRenderBuffer(id) {
}

HdURERenderBuffer::~HdURERenderBuffer() = default;

bool HdURERenderBuffer::Allocate(
    const GfVec3i& dimensions,
    HdFormat format,
    bool multi_sampled) {
    if (dimensions[0] <= 0 ||
        dimensions[1] <= 0 ||
        dimensions[2] != 1 ||
        multi_sampled ||
        HdGetComponentFormat(format) !=
            HdFormatFloat32) {
        return false;
    }
    const std::size_t component_count =
        HdGetComponentCount(format);
    const std::size_t pixel_count =
        static_cast<std::size_t>(dimensions[0]) *
        static_cast<std::size_t>(dimensions[1]);
    if (component_count == 0 ||
        pixel_count >
            std::numeric_limits<std::size_t>::max() /
                component_count) {
        return false;
    }
    std::scoped_lock lock(mutex_);
    if (IsMapped()) {
        return false;
    }
    data_.assign(
        pixel_count * component_count,
        0.0f);
    dimensions_ = dimensions;
    format_ = format;
    converged_.store(
        false,
        std::memory_order_release);
    return true;
}

unsigned int HdURERenderBuffer::GetWidth() const {
    std::scoped_lock lock(mutex_);
    return static_cast<unsigned int>(
        dimensions_[0]);
}

unsigned int HdURERenderBuffer::GetHeight() const {
    std::scoped_lock lock(mutex_);
    return static_cast<unsigned int>(
        dimensions_[1]);
}

unsigned int HdURERenderBuffer::GetDepth() const {
    std::scoped_lock lock(mutex_);
    return static_cast<unsigned int>(
        dimensions_[2]);
}

HdFormat HdURERenderBuffer::GetFormat() const {
    std::scoped_lock lock(mutex_);
    return format_;
}

bool HdURERenderBuffer::IsMultiSampled() const {
    return false;
}

void* HdURERenderBuffer::Map() {
    std::scoped_lock lock(mutex_);
    if (data_.empty()) {
        return nullptr;
    }
    map_count_.fetch_add(
        1,
        std::memory_order_acq_rel);
    return data_.data();
}

void HdURERenderBuffer::Unmap() {
    unsigned int count = map_count_.load(
        std::memory_order_acquire);
    while (count != 0 &&
           !map_count_.compare_exchange_weak(
               count,
               count - 1,
               std::memory_order_acq_rel)) {
    }
}

bool HdURERenderBuffer::IsMapped() const {
    return map_count_.load(
               std::memory_order_acquire) != 0;
}

void HdURERenderBuffer::Resolve() {
}

bool HdURERenderBuffer::IsConverged() const {
    return converged_.load(
        std::memory_order_acquire);
}

void HdURERenderBuffer::Write(
    std::span<const float> source,
    std::size_t source_channels,
    bool converged) {
    if (source_channels == 0) {
        throw std::runtime_error(
            "Hydra render buffer source has no channels");
    }
    std::scoped_lock lock(mutex_);
    if (IsMapped()) {
        throw std::runtime_error(
            "Hydra render buffer cannot be written while mapped");
    }
    const std::size_t target_channels =
        HdGetComponentCount(format_);
    const std::size_t pixel_count =
        static_cast<std::size_t>(dimensions_[0]) *
        static_cast<std::size_t>(dimensions_[1]);
    if (target_channels == 0 ||
        data_.size() !=
            pixel_count * target_channels ||
        source.size() !=
            pixel_count * source_channels) {
        throw std::runtime_error(
            "Hydra render buffer layout does not match the URE AOV");
    }
    auto* target = data_.data();
    for (std::size_t pixel = 0;
         pixel < pixel_count;
         ++pixel) {
        for (std::size_t channel = 0;
             channel < target_channels;
             ++channel) {
            target[pixel * target_channels +
                   channel] =
                channel < source_channels
                ? source[pixel * source_channels +
                         channel]
                : (channel == 3 ? 1.0f : 0.0f);
        }
    }
    converged_.store(
        converged,
        std::memory_order_release);
}

void HdURERenderBuffer::SetConverged(
    bool converged) {
    converged_.store(
        converged,
        std::memory_order_release);
}

void HdURERenderBuffer::_Deallocate() {
    std::scoped_lock lock(mutex_);
    if (IsMapped()) {
        return;
    }
    data_.clear();
    dimensions_ = {0, 0, 0};
    format_ = HdFormatInvalid;
    converged_.store(
        false,
        std::memory_order_release);
}

PXR_NAMESPACE_CLOSE_SCOPE
