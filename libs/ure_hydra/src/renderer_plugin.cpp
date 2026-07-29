#include <pxr/base/tf/registryManager.h>
#include <pxr/base/tf/type.h>
#include <pxr/imaging/hd/rendererPlugin.h>
#include <pxr/imaging/hd/rendererPluginRegistry.h>

#include "render_delegate.hpp"

PXR_NAMESPACE_OPEN_SCOPE

class HdURERendererPlugin final
    : public HdRendererPlugin {
public:
    HdRenderDelegate*
    CreateRenderDelegate() override {
        return new HdURE();
    }

    HdRenderDelegate* CreateRenderDelegate(
        const HdRenderSettingsMap& settings) override {
        return new HdURE(settings);
    }

    void DeleteRenderDelegate(
        HdRenderDelegate* delegate) override {
        delete delegate;
    }

    bool IsSupported(
        bool gpu_enabled = true) const override {
        static_cast<void>(gpu_enabled);
        return false;
    }
};

TF_REGISTRY_FUNCTION(TfType) {
    HdRendererPluginRegistry::Define<
        HdURERendererPlugin>();
}

PXR_NAMESPACE_CLOSE_SCOPE
