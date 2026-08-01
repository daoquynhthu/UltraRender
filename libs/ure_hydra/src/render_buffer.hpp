#pragma once

#include <atomic>
#include <mutex>
#include <span>
#include <vector>

#include <pxr/base/gf/vec3i.h>
#include <pxr/imaging/hd/renderBuffer.h>

PXR_NAMESPACE_OPEN_SCOPE

class HdURERenderBuffer final
    : public HdRenderBuffer {
public:
    explicit HdURERenderBuffer(
        const SdfPath& id);
    ~HdURERenderBuffer() override;

    bool Allocate(
        const GfVec3i& dimensions,
        HdFormat format,
        bool multi_sampled) override;
    unsigned int GetWidth() const override;
    unsigned int GetHeight() const override;
    unsigned int GetDepth() const override;
    HdFormat GetFormat() const override;
    bool IsMultiSampled() const override;
    void* Map() override;
    void Unmap() override;
    bool IsMapped() const override;
    void Resolve() override;
    bool IsConverged() const override;

    void Write(
        std::span<const float> source,
        std::size_t source_channels,
        bool converged);
    void SetConverged(bool converged);

protected:
    void _Deallocate() override;

private:
    mutable std::mutex mutex_;
    std::vector<float> data_;
    GfVec3i dimensions_ = {0, 0, 0};
    HdFormat format_ = HdFormatInvalid;
    std::atomic<unsigned int> map_count_ = 0;
    std::atomic<bool> converged_ = false;
};

PXR_NAMESPACE_CLOSE_SCOPE
