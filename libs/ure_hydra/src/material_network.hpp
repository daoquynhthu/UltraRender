#pragma once

#include <memory>
#include <string>
#include <vector>

#include <pxr/imaging/hd/material.h>

#include <ure/scene_ir.hpp>

#include "render_param.hpp"

PXR_NAMESPACE_OPEN_SCOPE

struct HdUREMaterialConversion {
    std::shared_ptr<const ure::scene_ir::MaterialNode>
        material;
    std::vector<HdUREMaterialLoss> loss_report;
    std::string error;

    bool Accepted() const {
        return material != nullptr &&
               error.empty();
    }
};

HdUREMaterialConversion
ConvertMaterialNetwork(
    const SdfPath& material_path,
    const HdMaterialNetwork2& network);

PXR_NAMESPACE_CLOSE_SCOPE
