#pragma once

#include <memory>

#include <pxr/imaging/hd/renderDelegate.h>

PXR_NAMESPACE_OPEN_SCOPE

class HdURE final : public HdRenderDelegate {
public:
    HdURE();
    explicit HdURE(
        const HdRenderSettingsMap& settings);
    ~HdURE() override;

    const TfTokenVector&
    GetSupportedRprimTypes() const override;
    const TfTokenVector&
    GetSupportedSprimTypes() const override;
    const TfTokenVector&
    GetSupportedBprimTypes() const override;
    HdRenderParam* GetRenderParam() const override;
    HdResourceRegistrySharedPtr
    GetResourceRegistry() const override;
    HdRenderPassSharedPtr CreateRenderPass(
        HdRenderIndex* index,
        const HdRprimCollection& collection) override;
    HdInstancer* CreateInstancer(
        HdSceneDelegate* delegate,
        const SdfPath& id) override;
    void DestroyInstancer(
        HdInstancer* instancer) override;
    HdRprim* CreateRprim(
        const TfToken& type_id,
        const SdfPath& rprim_id) override;
    void DestroyRprim(HdRprim* rprim) override;
    HdSprim* CreateSprim(
        const TfToken& type_id,
        const SdfPath& sprim_id) override;
    HdSprim* CreateFallbackSprim(
        const TfToken& type_id) override;
    void DestroySprim(HdSprim* sprim) override;
    HdBprim* CreateBprim(
        const TfToken& type_id,
        const SdfPath& bprim_id) override;
    HdBprim* CreateFallbackBprim(
        const TfToken& type_id) override;
    void DestroyBprim(HdBprim* bprim) override;
    void CommitResources(
        HdChangeTracker* tracker) override;
    HdRenderSettingDescriptorList
    GetRenderSettingDescriptors() const override;
    HdAovDescriptor GetDefaultAovDescriptor(
        const TfToken& name) const override;
    TfTokenVector
    GetRenderSettingsNamespaces() const override;
    VtDictionary GetRenderStats() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

PXR_NAMESPACE_CLOSE_SCOPE
