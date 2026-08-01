#pragma once

#include <pxr/imaging/hd/material.h>

PXR_NAMESPACE_OPEN_SCOPE

class HdUREMaterial final : public HdMaterial {
public:
    explicit HdUREMaterial(const SdfPath& id);
    ~HdUREMaterial() override;

    HdDirtyBits
    GetInitialDirtyBitsMask() const override;
    void Sync(
        HdSceneDelegate* delegate,
        HdRenderParam* render_param,
        HdDirtyBits* dirty_bits) override;
    void Finalize(
        HdRenderParam* render_param) override;
};

PXR_NAMESPACE_CLOSE_SCOPE
