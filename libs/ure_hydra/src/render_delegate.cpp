#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/bprim.h>
#include <pxr/imaging/hd/instancer.h>
#include <pxr/imaging/hd/mesh.h>
#include <pxr/imaging/hd/resourceRegistry.h>
#include <pxr/imaging/hd/rprim.h>
#include <pxr/imaging/hd/sprim.h>
#include <pxr/imaging/hd/tokens.h>

#include <ure/usd_schema_adapter.hpp>

#include "material_sprim.hpp"
#include "mesh_rprim.hpp"
#include "render_delegate.hpp"
#include "render_param.hpp"

PXR_NAMESPACE_OPEN_SCOPE
namespace {

const TfTokenVector& empty_types() {
    static const TfTokenVector value;
    return value;
}

const TfTokenVector& rprim_types() {
    static const TfTokenVector value{
        HdPrimTypeTokens->mesh};
    return value;
}

const TfTokenVector& sprim_types() {
    static const TfTokenVector value{
        HdPrimTypeTokens->material};
    return value;
}

}

struct HdURE::Impl {
    HdResourceRegistrySharedPtr resource_registry =
        std::make_shared<HdResourceRegistry>();
    HdURERenderParam render_param;
    std::atomic<std::uint64_t> resource_epoch = 0;
};

HdURE::HdURE()
    : HdRenderDelegate(),
      impl_(std::make_unique<Impl>()) {
    _PopulateDefaultSettings(
        GetRenderSettingDescriptors());
}

HdURE::HdURE(
    const HdRenderSettingsMap& settings)
    : HdRenderDelegate(settings),
      impl_(std::make_unique<Impl>()) {
    _PopulateDefaultSettings(
        GetRenderSettingDescriptors());
}

HdURE::~HdURE() = default;

const TfTokenVector&
HdURE::GetSupportedRprimTypes() const {
    return rprim_types();
}

const TfTokenVector&
HdURE::GetSupportedSprimTypes() const {
    return sprim_types();
}

const TfTokenVector&
HdURE::GetSupportedBprimTypes() const {
    return empty_types();
}

HdRenderParam* HdURE::GetRenderParam() const {
    return &impl_->render_param;
}

HdResourceRegistrySharedPtr
HdURE::GetResourceRegistry() const {
    return impl_->resource_registry;
}

HdRenderPassSharedPtr HdURE::CreateRenderPass(
    HdRenderIndex* index,
    const HdRprimCollection& collection) {
    static_cast<void>(index);
    static_cast<void>(collection);
    TF_CODING_ERROR(
        "HdURE render execution is unavailable until the U.5 session path is active");
    return {};
}

HdInstancer* HdURE::CreateInstancer(
    HdSceneDelegate* delegate,
    const SdfPath& id) {
    static_cast<void>(delegate);
    static_cast<void>(id);
    TF_CODING_ERROR(
        "HdURE instancing requires a dedicated native instance mapping");
    return nullptr;
}

void HdURE::DestroyInstancer(
    HdInstancer* instancer) {
    delete instancer;
}

HdRprim* HdURE::CreateRprim(
    const TfToken& type_id,
    const SdfPath& rprim_id) {
    if (type_id == HdPrimTypeTokens->mesh) {
        return new HdUREMesh(rprim_id);
    }
    TF_CODING_ERROR(
        "HdURE received an unsupported RPrim type");
    return nullptr;
}

void HdURE::DestroyRprim(HdRprim* rprim) {
    delete rprim;
}

HdSprim* HdURE::CreateSprim(
    const TfToken& type_id,
    const SdfPath& sprim_id) {
    if (type_id ==
        HdPrimTypeTokens->material) {
        return new HdUREMaterial(sprim_id);
    }
    TF_CODING_ERROR(
        "HdURE received an unsupported SPrim type");
    return nullptr;
}

HdSprim* HdURE::CreateFallbackSprim(
    const TfToken& type_id) {
    if (type_id ==
        HdPrimTypeTokens->material) {
        return new HdUREMaterial(
            SdfPath(
                "/__ureFallbackMaterial"));
    }
    TF_CODING_ERROR(
        "HdURE received an unsupported fallback SPrim type");
    return nullptr;
}

void HdURE::DestroySprim(HdSprim* sprim) {
    delete sprim;
}

HdBprim* HdURE::CreateBprim(
    const TfToken& type_id,
    const SdfPath& bprim_id) {
    static_cast<void>(type_id);
    static_cast<void>(bprim_id);
    TF_CODING_ERROR(
        "HdURE BPrims are unavailable before U.5");
    return nullptr;
}

HdBprim* HdURE::CreateFallbackBprim(
    const TfToken& type_id) {
    static_cast<void>(type_id);
    TF_CODING_ERROR(
        "HdURE fallback BPrims are unavailable before U.5");
    return nullptr;
}

void HdURE::DestroyBprim(HdBprim* bprim) {
    delete bprim;
}

void HdURE::CommitResources(
    HdChangeTracker* tracker) {
    static_cast<void>(tracker);
    impl_->resource_registry->Commit();
    impl_->resource_epoch.fetch_add(
        1,
        std::memory_order_release);
}

HdRenderSettingDescriptorList
HdURE::GetRenderSettingDescriptors() const {
    return {
        {
            "UltraRender backend",
            TfToken("ure:backend"),
            VtValue(TfToken("auto"))
        },
        {
            "URE native schema",
            TfToken("ure:nativeSchema"),
            VtValue(std::string(
                ure::usd::kUsdSchemaAdapterIdentity))
        }
    };
}

TfTokenVector
HdURE::GetRenderSettingsNamespaces() const {
    return {TfToken("ure")};
}

VtDictionary HdURE::GetRenderStats() const {
    VtDictionary stats;
    stats["delegateIdentity"] =
        VtValue(std::string(
            "ure.hydra.render-delegate/1.0"));
    stats["nativeSchemaIdentity"] =
        VtValue(std::string(
            ure::usd::kUsdSchemaAdapterIdentity));
    stats["resourceEpoch"] =
        VtValue(impl_->resource_epoch.load(
            std::memory_order_acquire));
    stats["renderReady"] = VtValue(false);
    stats["meshCount"] =
        VtValue(static_cast<std::uint64_t>(
            impl_->render_param.MeshCount()));
    stats["rejectedMeshCount"] =
        VtValue(
            impl_->render_param.
                RejectedMeshCount());
    stats["lastError"] =
        VtValue(
            impl_->render_param.LastError());
    stats["materialCount"] =
        VtValue(static_cast<std::uint64_t>(
            impl_->render_param.
                MaterialCount()));
    stats["rejectedMaterialCount"] =
        VtValue(
            impl_->render_param.
                RejectedMaterialCount());
    stats["materialLossCount"] =
        VtValue(
            impl_->render_param.
                MaterialLossCount());
    return stats;
}

PXR_NAMESPACE_CLOSE_SCOPE
