#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <string>
#include <type_traits>

#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>

#include "render_delegate.hpp"

namespace {

int failures = 0;

void check(
    bool condition,
    const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

const pxr::VtValue* find_value(
    const pxr::VtDictionary& values,
    const char* key) {
    const auto found = values.find(key);
    return found == values.end()
        ? nullptr
        : &found->second;
}

}

int main() {
    static_assert(
        std::is_base_of_v<
            pxr::HdRenderDelegate,
            pxr::HdURE>);
    static_assert(std::is_final_v<pxr::HdURE>);

    pxr::HdRenderSettingsMap settings;
    settings[pxr::TfToken("ure:backend")] =
        pxr::VtValue(pxr::TfToken("cuda"));
    pxr::HdURE delegate(settings);

    check(
        delegate.GetSupportedRprimTypes().size() == 1 &&
            delegate.GetSupportedRprimTypes().front() ==
                pxr::HdPrimTypeTokens->mesh,
        "Hydra prim capability boundary is wrong");
#if defined(UR_HYDRA_RENDERING)
    check(
        delegate.GetSupportedSprimTypes().size() == 2 &&
            delegate.GetSupportedBprimTypes().size() == 1 &&
            delegate.GetSupportedBprimTypes().front() ==
                pxr::HdPrimTypeTokens->renderBuffer,
        "Hydra rendering prim capabilities are wrong");
#else
    check(
        delegate.GetSupportedSprimTypes().size() == 1 &&
            delegate.GetSupportedSprimTypes().front() ==
                pxr::HdPrimTypeTokens->material &&
            delegate.GetSupportedBprimTypes().empty(),
        "Hydra adapter-only prim capabilities are wrong");
#endif
    check(delegate.GetRenderParam() != nullptr,
          "Hydra render param is missing");
    check(delegate.GetResourceRegistry() != nullptr,
          "Hydra resource registry is missing");
    check(
        delegate.GetRenderSetting(
            pxr::TfToken("ure:backend"))
            .Get<pxr::TfToken>() ==
            pxr::TfToken("cuda"),
        "initial Hydra render setting was lost");

    const auto descriptors =
        delegate.GetRenderSettingDescriptors();
    check(descriptors.size() == 4,
          "Hydra setting descriptors are incomplete");
    const auto color_aov =
        delegate.GetDefaultAovDescriptor(
            pxr::HdAovTokens->color);
    const auto depth_aov =
        delegate.GetDefaultAovDescriptor(
            pxr::HdAovTokens->depth);
    check(
        color_aov.format ==
                pxr::HdFormatFloat32Vec4 &&
            !color_aov.multiSampled &&
            depth_aov.format ==
                pxr::HdFormatFloat32,
        "Hydra AOV descriptors are incomplete");
    const auto namespaces =
        delegate.GetRenderSettingsNamespaces();
    check(namespaces.size() == 1 &&
              namespaces.front() ==
                  pxr::TfToken("ure"),
          "Hydra setting namespace is not isolated");

    const auto before = delegate.GetRenderStats();
    const auto* schema =
        find_value(before, "nativeSchemaIdentity");
    const auto* ready =
        find_value(before, "renderReady");
    const auto* epoch =
        find_value(before, "resourceEpoch");
    check(
        schema && ready && epoch &&
            schema->Get<std::string>() ==
                "ure.adapter.usd-schema/1.0" &&
#if defined(UR_HYDRA_RENDERING)
            ready->Get<bool>() &&
#else
            !ready->Get<bool>() &&
#endif
            epoch->Get<std::uint64_t>() == 0,
        "Hydra delegate boundary metadata is wrong");
    delegate.CommitResources(nullptr);
    const auto after = delegate.GetRenderStats();
    const auto* committed_epoch =
        find_value(after, "resourceEpoch");
    check(
        committed_epoch &&
            committed_epoch->Get<std::uint64_t>() == 1,
        "Hydra resource commit was not recorded");

    std::cout << "Phase U.2 Hydra delegate checks: "
              << (failures ? "FAILED" : "PASSED")
              << '\n';
    return failures == 0
        ? EXIT_SUCCESS
        : EXIT_FAILURE;
}
