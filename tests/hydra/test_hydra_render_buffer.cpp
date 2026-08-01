#include <cmath>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <vector>

#include <pxr/base/gf/vec3i.h>
#include <pxr/imaging/hd/types.h>

#include "render_buffer.hpp"

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

}

int main() {
    pxr::HdURERenderBuffer buffer(
        pxr::SdfPath("/unit/color"));
    check(
        !buffer.Allocate(
            pxr::GfVec3i(2, 2, 2),
            pxr::HdFormatFloat32Vec4,
            false) &&
            !buffer.Allocate(
                pxr::GfVec3i(2, 2, 1),
                pxr::HdFormatUNorm8Vec4,
                false) &&
            !buffer.Allocate(
                pxr::GfVec3i(2, 2, 1),
                pxr::HdFormatFloat32Vec4,
                true),
        "unsupported render-buffer layouts were accepted");
    check(
        buffer.Allocate(
            pxr::GfVec3i(2, 2, 1),
            pxr::HdFormatFloat32Vec4,
            false),
        "linear float render-buffer allocation failed");
    const std::vector<float> rgb{
        0.1f, 0.2f, 0.3f,
        0.4f, 0.5f, 0.6f,
        0.7f, 0.8f, 0.9f,
        1.0f, 0.5f, 0.25f};
    buffer.Write(rgb, 3, true);
    check(buffer.IsConverged(),
          "render-buffer convergence was not published");
    auto* mapped = static_cast<float*>(buffer.Map());
    check(mapped != nullptr && buffer.IsMapped(),
          "render-buffer mapping failed");
    if (mapped) {
        check(
            std::abs(mapped[0] - 0.1f) < 1.0e-6f &&
                std::abs(mapped[3] - 1.0f) < 1.0e-6f &&
                std::abs(mapped[15] - 1.0f) < 1.0e-6f,
            "render-buffer channel expansion is wrong");
    }
    bool rejected_write = false;
    try {
        buffer.Write(rgb, 3, false);
    } catch (const std::exception&) {
        rejected_write = true;
    }
    check(rejected_write,
          "mapped render buffer accepted a concurrent write");
    buffer.Unmap();
    check(!buffer.IsMapped(),
          "render-buffer unmap did not release the mapping");

    std::cout << "Phase U.5 Hydra render-buffer checks: "
              << (failures ? "FAILED" : "PASSED")
              << '\n';
    return failures == 0
        ? EXIT_SUCCESS
        : EXIT_FAILURE;
}
