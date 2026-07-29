#include <cstdlib>
#include <iostream>
#include <string>

#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/rendererPlugin.h>
#include <pxr/imaging/hd/rendererPluginHandle.h>
#include <pxr/imaging/hd/rendererPluginRegistry.h>

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
    auto plugin =
        pxr::HdRendererPluginRegistry::GetInstance()
            .GetOrCreateRendererPlugin(
                pxr::TfToken(
                    "HdURERendererPlugin"));
    check(static_cast<bool>(plugin),
          "HdURE plugin was not discovered");
    if (!plugin) {
        return EXIT_FAILURE;
    }
    check(!plugin->IsSupported(true),
          "U.2 skeleton was advertised as render-ready");

    pxr::HdRenderSettingsMap settings;
    settings[pxr::TfToken("ure:backend")] =
        pxr::VtValue(pxr::TfToken("cuda"));
    pxr::HdRenderDelegate* delegate =
        plugin->CreateRenderDelegate(settings);
    check(delegate != nullptr,
          "HdURE plugin did not create its delegate");
    if (delegate) {
        const auto stats = delegate->GetRenderStats();
        const auto* identity =
            find_value(stats, "delegateIdentity");
        const auto* ready =
            find_value(stats, "renderReady");
        check(
            identity && ready &&
                identity->Get<std::string>() ==
                    "ure.hydra.render-delegate/1.0" &&
                !ready->Get<bool>(),
            "discovered delegate boundary is wrong");
        plugin->DeleteRenderDelegate(delegate);
    }

    std::cout << "Phase U.2 Hydra plugin checks: "
              << (failures ? "FAILED" : "PASSED")
              << '\n';
    return failures == 0
        ? EXIT_SUCCESS
        : EXIT_FAILURE;
}
