#pragma once

#include "ure/render.hpp"

#include <memory>

namespace ure {

std::unique_ptr<IRenderEngine> create_automatic_render_engine(
    const RenderConfig& config);

}
