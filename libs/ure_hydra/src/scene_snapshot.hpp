#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <pxr/imaging/hd/rprimCollection.h>

#include <ure/scene_ir.hpp>

#include "render_param.hpp"

PXR_NAMESPACE_OPEN_SCOPE

struct HdURESceneSnapshot {
    ure::scene_ir::SceneIR scene;
    std::uint64_t revision = 0;
    std::vector<std::string> loss_report;
};

HdURESceneSnapshot BuildSceneSnapshot(
    const HdURERetainedScene& retained,
    const HdRprimCollection& collection,
    const ure::Camera& camera,
    int width,
    int height);

PXR_NAMESPACE_CLOSE_SCOPE
