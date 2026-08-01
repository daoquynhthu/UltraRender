#include <exception>
#include <string>
#include <utility>
#include <vector>

#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/sceneDelegate.h>

#include "material_network.hpp"
#include "material_sprim.hpp"
#include "render_param.hpp"

PXR_NAMESPACE_OPEN_SCOPE

HdUREMaterial::HdUREMaterial(
    const SdfPath& id)
    : HdMaterial(id) {
}

HdUREMaterial::~HdUREMaterial() = default;

HdDirtyBits
HdUREMaterial::GetInitialDirtyBitsMask() const {
    return HdMaterial::AllDirty;
}

void HdUREMaterial::Sync(
    HdSceneDelegate* delegate,
    HdRenderParam* render_param,
    HdDirtyBits* dirty_bits) {
    auto* state =
        dynamic_cast<HdURERenderParam*>(
            render_param);
    if (!delegate || !state || !dirty_bits) {
        TF_CODING_ERROR(
            "HdURE material sync requires delegate, render param and dirty bits");
        return;
    }
    if ((*dirty_bits &
         HdMaterial::AllDirty) == 0) {
        return;
    }
    try {
        const VtValue resource =
            delegate->GetMaterialResource(
                GetId());
        HdMaterialNetwork2 network;
        if (resource.IsHolding<
                HdMaterialNetwork2>()) {
            network =
                resource.UncheckedGet<
                    HdMaterialNetwork2>();
        } else if (
            resource.IsHolding<
                HdMaterialNetworkMap>()) {
            bool is_volume = false;
            network =
                HdConvertToHdMaterialNetwork2(
                    resource.UncheckedGet<
                        HdMaterialNetworkMap>(),
                    &is_volume);
            if (is_volume) {
                std::vector<HdUREMaterialLoss>
                    loss_report{{
                        HdUREMaterialLossSeverity::
                            Error,
                        "URE-U4-ERROR-VOLUME",
                        GetId().GetString(),
                        "Hydra volume material requires a native volume graph contract"}};
                state->RejectMaterial(
                    GetId().GetString(),
                    std::move(loss_report),
                    "Hydra volume material requires a native volume graph contract");
                *dirty_bits =
                    HdChangeTracker::Clean;
                return;
            }
        } else {
            std::vector<HdUREMaterialLoss>
                loss_report{{
                    HdUREMaterialLossSeverity::
                        Error,
                    "URE-U4-ERROR-RESOURCE-TYPE",
                    GetId().GetString(),
                    "Hydra material resource is not a material network"}};
            state->RejectMaterial(
                GetId().GetString(),
                std::move(loss_report),
                "Hydra material resource is not a material network");
            *dirty_bits =
                HdChangeTracker::Clean;
            return;
        }

        auto conversion =
            ConvertMaterialNetwork(
                GetId(),
                network);
        if (!conversion.Accepted()) {
            state->RejectMaterial(
                GetId().GetString(),
                std::move(
                    conversion.loss_report),
                std::move(conversion.error));
        } else {
            HdUREMaterialRecord record;
            record.path =
                GetId().GetString();
            record.material =
                std::move(
                    conversion.material);
            record.loss_report =
                std::move(
                    conversion.loss_report);
            state->UpdateMaterial(
                std::move(record));
        }
    } catch (const std::exception& error) {
        std::vector<HdUREMaterialLoss>
            loss_report{{
                HdUREMaterialLossSeverity::Error,
                "URE-U4-ERROR-CONVERSION",
                GetId().GetString(),
                error.what()}};
        state->RejectMaterial(
            GetId().GetString(),
            std::move(loss_report),
            error.what());
    }
    *dirty_bits = HdChangeTracker::Clean;
}

void HdUREMaterial::Finalize(
    HdRenderParam* render_param) {
    if (auto* state =
            dynamic_cast<HdURERenderParam*>(
                render_param)) {
        state->RemoveMaterial(
            GetId().GetString());
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
