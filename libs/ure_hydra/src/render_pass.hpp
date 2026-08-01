#pragma once

#include <cstdint>
#include <memory>

#include <pxr/imaging/hd/renderPass.h>

#include <ure/render_config.hpp>

namespace ure {
class RenderSession;
struct Camera;
}

PXR_NAMESPACE_OPEN_SCOPE

class HdURERenderParam;

class HdURERenderPass final
    : public HdRenderPass {
public:
    HdURERenderPass(
        HdRenderIndex* index,
        const HdRprimCollection& collection,
        HdURERenderParam* render_param,
        ure::RenderConfig config,
        int max_spp);
    ~HdURERenderPass() override;

    bool IsConverged() const override;

protected:
    void _Execute(
        const HdRenderPassStateSharedPtr& state,
        const TfTokenVector& render_tags) override;
    void _MarkCollectionDirty() override;

private:
    std::unique_ptr<ure::RenderSession> session_;
    HdURERenderParam* render_param_ = nullptr;
    ure::RenderConfig config_;
    int max_spp_ = 1;
    std::uint64_t scene_revision_ = 0;
    std::uint64_t camera_hash_ = 0;
    int width_ = 0;
    int height_ = 0;
    std::uint64_t loss_count_ = 0;
    bool collection_dirty_ = true;
    bool converged_ = false;
    bool failed_ = false;
};

PXR_NAMESPACE_CLOSE_SCOPE
