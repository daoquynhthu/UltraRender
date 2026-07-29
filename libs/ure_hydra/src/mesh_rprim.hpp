#pragma once

#include <pxr/imaging/hd/mesh.h>

PXR_NAMESPACE_OPEN_SCOPE

class HdUREMesh final : public HdMesh {
public:
    explicit HdUREMesh(const SdfPath& id);
    ~HdUREMesh() override;

    HdDirtyBits
    GetInitialDirtyBitsMask() const override;
    void Sync(
        HdSceneDelegate* delegate,
        HdRenderParam* render_param,
        HdDirtyBits* dirty_bits,
        const TfToken& repr_token) override;
    void Finalize(
        HdRenderParam* render_param) override;
    HdMeshTopologySharedPtr
    GetTopology() const override;

protected:
    HdDirtyBits _PropagateDirtyBits(
        HdDirtyBits bits) const override;
    void _InitRepr(
        const TfToken& repr_token,
        HdDirtyBits* dirty_bits) override;

private:
    HdMeshTopologySharedPtr topology_;
};

PXR_NAMESPACE_CLOSE_SCOPE
