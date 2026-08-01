#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/bprim.h>
#include <pxr/imaging/hd/camera.h>
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
#if defined(UR_HYDRA_RENDERING)
#include "render_buffer.hpp"
#include "render_pass.hpp"
#endif

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
#if defined(UR_HYDRA_RENDERING)
    static const TfTokenVector value{
        HdPrimTypeTokens->material,
        HdPrimTypeTokens->camera};
#else
    static const TfTokenVector value{
        HdPrimTypeTokens->material};
#endif
    return value;
}

const TfTokenVector& bprim_types() {
#if defined(UR_HYDRA_RENDERING)
    static const TfTokenVector value{
        HdPrimTypeTokens->renderBuffer};
    return value;
#else
    return empty_types();
#endif
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
    return bprim_types();
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
#if defined(UR_HYDRA_RENDERING)
    const VtValue backend_value = GetRenderSetting(
        TfToken("ure:backend"));
    const TfToken backend =
        backend_value.IsHolding<TfToken>()
        ? backend_value.UncheckedGet<TfToken>()
        : TfToken("auto");
    if (backend != TfToken("auto") &&
        backend != TfToken("cuda")) {
        TF_RUNTIME_ERROR(
            "HdURE U.5 supports only the CUDA backend");
        return {};
    }
    const VtValue samples_value = GetRenderSetting(
        TfToken("ure:samplesPerPass"));
    const VtValue max_spp_value = GetRenderSetting(
        TfToken("ure:maxSpp"));
    const int samples_per_pass =
        samples_value.IsHolding<int>()
        ? samples_value.UncheckedGet<int>()
        : 1;
    const int max_spp =
        max_spp_value.IsHolding<int>()
        ? max_spp_value.UncheckedGet<int>()
        : 64;
    if (samples_per_pass <= 0 || max_spp <= 0) {
        TF_RUNTIME_ERROR(
            "HdURE sample settings must be positive");
        return {};
    }
    ure::RenderConfig config;
    if (backend == TfToken("cuda")) {
        config.backend.kind =
            ure::BackendKind::Cuda;
    }
    config.samples_per_pass = samples_per_pass;
    config.environment_light.direct_sampling = true;
    return std::make_shared<HdURERenderPass>(
        index,
        collection,
        &impl_->render_param,
        std::move(config),
        max_spp);
#else
    static_cast<void>(index);
    static_cast<void>(collection);
    TF_CODING_ERROR(
        "HdURE render execution is unavailable until the U.5 session path is active");
    return {};
#endif
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
#if defined(UR_HYDRA_RENDERING)
    if (type_id == HdPrimTypeTokens->camera) {
        return new HdCamera(sprim_id);
    }
#endif
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
#if defined(UR_HYDRA_RENDERING)
    if (type_id == HdPrimTypeTokens->camera) {
        return new HdCamera(
            SdfPath("/__ureFallbackCamera"));
    }
#endif
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
#if defined(UR_HYDRA_RENDERING)
    if (type_id == HdPrimTypeTokens->renderBuffer) {
        return new HdURERenderBuffer(bprim_id);
    }
#endif
    static_cast<void>(type_id);
    static_cast<void>(bprim_id);
    TF_CODING_ERROR(
        "HdURE BPrims are unavailable before U.5");
    return nullptr;
}

HdBprim* HdURE::CreateFallbackBprim(
    const TfToken& type_id) {
#if defined(UR_HYDRA_RENDERING)
    if (type_id == HdPrimTypeTokens->renderBuffer) {
        return new HdURERenderBuffer(
            SdfPath("/__ureFallbackRenderBuffer"));
    }
#endif
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
        },
        {
            "Samples per Hydra execute",
            TfToken("ure:samplesPerPass"),
            VtValue(1)
        },
        {
            "Maximum progressive samples",
            TfToken("ure:maxSpp"),
            VtValue(64)
        }
    };
}

HdAovDescriptor HdURE::GetDefaultAovDescriptor(
    const TfToken& name) const {
    if (name == HdAovTokens->color) {
        return {
            HdFormatFloat32Vec4,
            false,
            VtValue(GfVec4f(0.0f))};
    }
    if (name == HdAovTokens->normal ||
        name == TfToken("albedo")) {
        return {
            HdFormatFloat32Vec3,
            false,
            VtValue(GfVec3f(0.0f))};
    }
    if (name == HdAovTokens->depth ||
        name == HdAovTokens->cameraDepth) {
        return {
            HdFormatFloat32,
            false,
            VtValue(0.0f)};
    }
    if (name == TfToken("uv") ||
        name == TfToken("motionVector")) {
        return {
            HdFormatFloat32Vec2,
            false,
            VtValue(GfVec2f(0.0f))};
    }
    return {};
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
#if defined(UR_HYDRA_RENDERING)
    stats["renderReady"] = VtValue(true);
#else
    stats["renderReady"] = VtValue(false);
#endif
    stats["renderSpp"] = VtValue(
        impl_->render_param.RenderSpp());
    stats["renderConverged"] = VtValue(
        impl_->render_param.RenderConverged());
    stats["renderLossCount"] = VtValue(
        impl_->render_param.RenderLossCount());
    stats["renderError"] = VtValue(
        impl_->render_param.RenderError());
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
