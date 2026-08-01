#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4d.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/usd/sdf/assetPath.h>

#include "material_network.hpp"

PXR_NAMESPACE_OPEN_SCOPE
namespace {

using Graph = ure::scene_ir::MaterialGraph;
using GraphNode = ure::scene_ir::MaterialGraphNode;
using GraphNodeId =
    ure::scene_ir::MaterialGraphNodeId;
using GraphNodeKind =
    ure::scene_ir::MaterialGraphNodeKind;

bool finite(float value) {
    return std::isfinite(value);
}

std::optional<float> float_value(
    const VtValue& value) {
    if (value.IsHolding<float>()) {
        return value.UncheckedGet<float>();
    }
    if (value.IsHolding<double>()) {
        return static_cast<float>(
            value.UncheckedGet<double>());
    }
    if (value.IsHolding<int>()) {
        return static_cast<float>(
            value.UncheckedGet<int>());
    }
    return std::nullopt;
}

std::optional<ure::core::Vec3f> color_value(
    const VtValue& value) {
    if (value.IsHolding<GfVec3f>()) {
        const auto& source =
            value.UncheckedGet<GfVec3f>();
        return ure::core::Vec3f{
            source[0],
            source[1],
            source[2]};
    }
    if (value.IsHolding<GfVec3d>()) {
        const auto& source =
            value.UncheckedGet<GfVec3d>();
        return ure::core::Vec3f{
            static_cast<float>(source[0]),
            static_cast<float>(source[1]),
            static_cast<float>(source[2])};
    }
    if (value.IsHolding<GfVec4f>()) {
        const auto& source =
            value.UncheckedGet<GfVec4f>();
        return ure::core::Vec3f{
            source[0],
            source[1],
            source[2]};
    }
    if (value.IsHolding<GfVec4d>()) {
        const auto& source =
            value.UncheckedGet<GfVec4d>();
        return ure::core::Vec3f{
            static_cast<float>(source[0]),
            static_cast<float>(source[1]),
            static_cast<float>(source[2])};
    }
    if (const auto scalar = float_value(value)) {
        return ure::core::Vec3f{
            *scalar,
            *scalar,
            *scalar};
    }
    return std::nullopt;
}

std::optional<std::string> string_value(
    const VtValue& value) {
    if (value.IsHolding<std::string>()) {
        return value.UncheckedGet<std::string>();
    }
    if (value.IsHolding<TfToken>()) {
        return value.UncheckedGet<TfToken>()
            .GetString();
    }
    if (value.IsHolding<SdfAssetPath>()) {
        const auto& asset =
            value.UncheckedGet<SdfAssetPath>();
        return asset.GetResolvedPath().empty()
            ? asset.GetAssetPath()
            : asset.GetResolvedPath();
    }
    return std::nullopt;
}

bool finite(const ure::core::Vec3f& value) {
    return finite(value.x) &&
           finite(value.y) &&
           finite(value.z);
}

std::optional<GraphNodeKind> native_kind(
    std::string_view identifier) {
    static const std::map<
        std::string_view,
        GraphNodeKind>
        kinds{
            {"URE_constant_color",
             GraphNodeKind::ConstantColor},
            {"ure:constant_color",
             GraphNodeKind::ConstantColor},
            {"URE_constant_float",
             GraphNodeKind::ConstantFloat},
            {"ure:constant_float",
             GraphNodeKind::ConstantFloat},
            {"URE_texture2d",
             GraphNodeKind::Texture2D},
            {"ure:texture2d",
             GraphNodeKind::Texture2D},
            {"URE_add", GraphNodeKind::Add},
            {"ure:add", GraphNodeKind::Add},
            {"URE_multiply",
             GraphNodeKind::Multiply},
            {"ure:multiply",
             GraphNodeKind::Multiply},
            {"URE_mix", GraphNodeKind::Mix},
            {"ure:mix", GraphNodeKind::Mix},
            {"URE_checker2d",
             GraphNodeKind::Checker2D},
            {"ure:checker2d",
             GraphNodeKind::Checker2D},
            {"URE_noise2d",
             GraphNodeKind::Noise2D},
            {"ure:noise2d",
             GraphNodeKind::Noise2D},
            {"URE_bsdf_lambert",
             GraphNodeKind::BsdfLambert},
            {"ure:bsdf_lambert",
             GraphNodeKind::BsdfLambert},
            {"URE_bsdf_metal",
             GraphNodeKind::BsdfMetal},
            {"ure:bsdf_metal",
             GraphNodeKind::BsdfMetal},
            {"URE_bsdf_dielectric",
             GraphNodeKind::BsdfDielectric},
            {"ure:bsdf_dielectric",
             GraphNodeKind::BsdfDielectric},
            {"URE_bsdf_light",
             GraphNodeKind::BsdfLight},
            {"ure:bsdf_light",
             GraphNodeKind::BsdfLight},
            {"URE_bsdf_mix",
             GraphNodeKind::BsdfMix},
            {"ure:bsdf_mix",
             GraphNodeKind::BsdfMix},
            {"URE_bsdf_layer",
             GraphNodeKind::BsdfLayer},
            {"ure:bsdf_layer",
             GraphNodeKind::BsdfLayer},
            {"URE_output_surface",
             GraphNodeKind::OutputSurface},
            {"ure:output_surface",
             GraphNodeKind::OutputSurface}};
    const auto found = kinds.find(identifier);
    return found == kinds.end()
        ? std::nullopt
        : std::optional<GraphNodeKind>(
              found->second);
}

class Builder {
public:
    Builder(
        const SdfPath& material_path,
        const HdMaterialNetwork2& network)
        : material_path_(material_path),
          network_(network) {
    }

    HdUREMaterialConversion Convert() {
        const auto terminal =
            network_.terminals.find(
                HdMaterialTerminalTokens->surface);
        if (terminal ==
            network_.terminals.end()) {
            Error(
                "URE-U4-ERROR-SURFACE-TERMINAL",
                material_path_.GetString(),
                "USD material has no surface terminal");
            return Finish();
        }
        for (const auto& [name, connection] :
             network_.terminals) {
            static_cast<void>(connection);
            if (name !=
                HdMaterialTerminalTokens->surface) {
                Error(
                    "URE-U4-ERROR-UNSUPPORTED-TERMINAL",
                    material_path_.GetString(),
                    "USD material terminal is not representable: " +
                        name.GetString());
            }
        }

        const GraphNodeId surface =
            ConvertNode(
                terminal->second.upstreamNode);
        if (surface ==
            ure::scene_ir::
                kInvalidMaterialGraphNode) {
            return Finish();
        }
        const auto* surface_node =
            graph_.find_node(surface);
        if (!surface_node) {
            Error(
                "URE-U4-ERROR-SURFACE-NODE",
                material_path_.GetString(),
                "USD surface terminal did not produce a native node");
            return Finish();
        }
        if (surface_node->kind ==
            GraphNodeKind::OutputSurface) {
            graph_.output_node_id = surface;
        } else {
            GraphNode output;
            output.kind =
                GraphNodeKind::OutputSurface;
            output.name =
                material_path_.GetString() +
                "/ure_surface";
            output.inputs.push_back(
                ure::scene_ir::
                    material_graph_input(
                        "surface",
                        surface,
                        terminal->second.
                            upstreamOutputName.
                            GetString()));
            graph_.output_node_id =
                graph_.add_node(
                    std::move(output));
        }
        for (const auto& [path, node] :
             network_.nodes) {
            static_cast<void>(node);
            if (!used_nodes_.contains(path)) {
                Warning(
                    "URE-U4-LOSS-UNREACHABLE-NODE",
                    path.GetString(),
                    "USD material node is not reachable from the surface terminal");
            }
        }
        return Finish();
    }

private:
    using Connection =
        HdMaterialConnection2;

    const HdMaterialNode2* FindNode(
        const SdfPath& path) {
        const auto found =
            network_.nodes.find(path);
        if (found == network_.nodes.end()) {
            Error(
                "URE-U4-ERROR-MISSING-NODE",
                path.GetString(),
                "USD material connection references a missing node");
            return nullptr;
        }
        used_nodes_.insert(path);
        return &found->second;
    }

    std::optional<Connection> ConnectionFor(
        const HdMaterialNode2& node,
        const TfToken& name,
        const SdfPath& path) {
        const auto found =
            node.inputConnections.find(name);
        if (found ==
            node.inputConnections.end()) {
            return std::nullopt;
        }
        if (found->second.size() != 1) {
            Error(
                "URE-U4-ERROR-CONNECTION-ARITY",
                path.GetString(),
                "USD material input requires exactly one upstream connection: " +
                    name.GetString());
            return std::nullopt;
        }
        return found->second.front();
    }

    const VtValue* Parameter(
        const HdMaterialNode2& node,
        const TfToken& name) const {
        const auto found =
            node.parameters.find(name);
        return found == node.parameters.end()
            ? nullptr
            : &found->second;
    }

    void ReportUnknownInputs(
        const HdMaterialNode2& node,
        const SdfPath& path,
        const std::set<std::string>&
            allowed_parameters,
        const std::set<std::string>&
            allowed_connections) {
        for (const auto& [name, value] :
             node.parameters) {
            static_cast<void>(value);
            if (!allowed_parameters.contains(
                    name.GetString())) {
                Warning(
                    "URE-U4-LOSS-UNUSED-PARAMETER",
                    path.GetString(),
                    "USD material parameter is not represented: " +
                        name.GetString());
            }
        }
        for (const auto& [name, connections] :
             node.inputConnections) {
            static_cast<void>(connections);
            if (!allowed_connections.contains(
                    name.GetString())) {
                Error(
                    "URE-U4-ERROR-UNUSED-CONNECTION",
                    path.GetString(),
                    "USD material connection is not representable: " +
                        name.GetString());
            }
        }
    }

    void ValidateRange(
        const HdMaterialNode2& node,
        const SdfPath& path,
        const char* name,
        float minimum,
        float maximum) {
        const VtValue* value =
            Parameter(node, TfToken(name));
        if (!value) {
            return;
        }
        const auto scalar = float_value(*value);
        if (!scalar ||
            !finite(*scalar) ||
            *scalar < minimum ||
            *scalar > maximum) {
            Error(
                "URE-U4-ERROR-PARAMETER-RANGE",
                path.GetString(),
                "USD material parameter is outside its physical range: " +
                    std::string(name));
        }
    }

    GraphNodeId AddFloat(
        float value,
        std::string name) {
        if (!finite(value)) {
            Error(
                "URE-U4-ERROR-NONFINITE",
                std::move(name),
                "USD material scalar is non-finite");
            return ure::scene_ir::
                kInvalidMaterialGraphNode;
        }
        GraphNode node;
        node.kind =
            GraphNodeKind::ConstantFloat;
        node.name = std::move(name);
        node.value = value;
        return graph_.add_node(std::move(node));
    }

    GraphNodeId AddColor(
        ure::core::Vec3f value,
        std::string name) {
        if (!finite(value)) {
            Error(
                "URE-U4-ERROR-NONFINITE",
                std::move(name),
                "USD material color is non-finite");
            return ure::scene_ir::
                kInvalidMaterialGraphNode;
        }
        GraphNode node;
        node.kind =
            GraphNodeKind::ConstantColor;
        node.name = std::move(name);
        node.color = value;
        return graph_.add_node(std::move(node));
    }

    GraphNodeId FloatInput(
        const HdMaterialNode2& node,
        const SdfPath& path,
        const char* name,
        float fallback) {
        const TfToken token(name);
        if (const auto connection =
                ConnectionFor(
                    node,
                    token,
                    path)) {
            NoteTextureChannel(
                *connection,
                path,
                token);
            return ConvertNode(
                connection->upstreamNode);
        }
        if (const VtValue* value =
                Parameter(node, token)) {
            if (const auto scalar =
                    float_value(*value)) {
                return AddFloat(
                    *scalar,
                    path.GetString() +
                        "/" + name);
            }
            Error(
                "URE-U4-ERROR-PARAMETER-TYPE",
                path.GetString(),
                "USD material scalar parameter has an unsupported type: " +
                    std::string(name));
            return ure::scene_ir::
                kInvalidMaterialGraphNode;
        }
        return AddFloat(
            fallback,
            path.GetString() + "/" + name);
    }

    GraphNodeId ColorInput(
        const HdMaterialNode2& node,
        const SdfPath& path,
        const char* name,
        ure::core::Vec3f fallback) {
        const TfToken token(name);
        if (const auto connection =
                ConnectionFor(
                    node,
                    token,
                    path)) {
            return ConvertNode(
                connection->upstreamNode);
        }
        if (const VtValue* value =
                Parameter(node, token)) {
            if (const auto color =
                    color_value(*value)) {
                return AddColor(
                    *color,
                    path.GetString() +
                        "/" + name);
            }
            Error(
                "URE-U4-ERROR-PARAMETER-TYPE",
                path.GetString(),
                "USD material color parameter has an unsupported type: " +
                    std::string(name));
            return ure::scene_ir::
                kInvalidMaterialGraphNode;
        }
        return AddColor(
            fallback,
            path.GetString() + "/" + name);
    }

    void NoteTextureChannel(
        const Connection& connection,
        const SdfPath& path,
        const TfToken& input) {
        const std::string output =
            connection.upstreamOutputName.
                GetString();
        if (!output.empty() &&
            output != "out" &&
            output != "result" &&
            output != "rgb") {
            Warning(
                "URE-U4-LOSS-TEXTURE-CHANNEL",
                path.GetString(),
                "Texture channel '" + output +
                    "' for scalar input '" +
                    input.GetString() +
                    "' is represented by the native scalar texture semantic");
        }
    }

    GraphNodeId ConvertUvTexture(
        const SdfPath& path,
        const HdMaterialNode2& node) {
        const VtValue* file =
            Parameter(node, TfToken("file"));
        const auto uri = file
            ? string_value(*file)
            : std::nullopt;
        if (!uri || uri->empty()) {
            Error(
                "URE-U4-ERROR-TEXTURE-FILE",
                path.GetString(),
                "UsdUVTexture requires a non-empty file asset");
            return ure::scene_ir::
                kInvalidMaterialGraphNode;
        }
        int uv_set = 0;
        if (const auto st =
                ConnectionFor(
                    node,
                    TfToken("st"),
                    path)) {
            const std::string reader_output =
                st->upstreamOutputName.
                    GetString();
            if (!reader_output.empty() &&
                reader_output != "out" &&
                reader_output != "result") {
                Error(
                    "URE-U4-ERROR-TEXTURE-COORD",
                    path.GetString(),
                    "UsdUVTexture st uses an unsupported primvar-reader output");
            }
            const HdMaterialNode2* reader =
                FindNode(st->upstreamNode);
            if (!reader ||
                !reader->nodeTypeId.
                     GetString().
                     starts_with(
                         "UsdPrimvarReader_")) {
                Error(
                    "URE-U4-ERROR-TEXTURE-COORD",
                    path.GetString(),
                    "UsdUVTexture st must come from a UsdPrimvarReader");
            } else {
                const VtValue* varname =
                    Parameter(
                        *reader,
                        TfToken("varname"));
                const auto name = varname
                    ? string_value(*varname)
                    : std::nullopt;
                if (!name ||
                    (name->compare(0, 2, "st") !=
                         0 &&
                     name->compare(0, 2, "uv") !=
                         0)) {
                    Error(
                        "URE-U4-ERROR-TEXTURE-COORD",
                        st->upstreamNode.GetString(),
                        "UsdPrimvarReader must select an st or uv set");
                } else if (name->size() > 2) {
                    try {
                        std::size_t consumed = 0;
                        uv_set = std::stoi(
                            name->substr(2),
                            &consumed);
                        if (consumed !=
                            name->size() - 2) {
                            throw std::invalid_argument(
                                "suffix");
                        }
                    } catch (
                        const std::exception&) {
                        Error(
                            "URE-U4-ERROR-TEXTURE-COORD",
                            st->upstreamNode.
                                GetString(),
                            "USD texture coordinate suffix must be an integer");
                    }
                }
                ReportUnknownInputs(
                    *reader,
                    st->upstreamNode,
                    {"varname"},
                    {});
            }
        }
        if (uv_set < 0) {
            Error(
                "URE-U4-ERROR-TEXTURE-COORD",
                path.GetString(),
                "USD texture coordinate set is negative");
        }

        auto image =
            std::make_shared<
                ure::scene_ir::ImageResource>();
        image->name = path.GetString();
        image->uri = *uri;
        if (const VtValue* color_space =
                Parameter(
                    node,
                    TfToken("sourceColorSpace"))) {
            const auto value =
                string_value(*color_space);
            if (value &&
                (*value == "raw" ||
                 *value == "linear")) {
                image->color_space =
                    ure::scene_ir::
                        ImageColorSpace::Linear;
            } else if (
                value &&
                (*value == "sRGB" ||
                 *value == "srgb")) {
                image->color_space =
                    ure::scene_ir::
                        ImageColorSpace::SRGB;
            } else if (
                value && *value == "auto") {
                image->color_space =
                    ure::scene_ir::
                        ImageColorSpace::SRGB;
                Warning(
                    "URE-U4-LOSS-COLOR-SPACE-AUTO",
                    path.GetString(),
                    "UsdUVTexture auto color space is resolved to sRGB at the U.4 material boundary");
            } else {
                Error(
                    "URE-U4-ERROR-COLOR-SPACE",
                    path.GetString(),
                    "UsdUVTexture sourceColorSpace is unsupported");
            }
        } else {
            Warning(
                "URE-U4-LOSS-COLOR-SPACE-AUTO",
                path.GetString(),
                "UsdUVTexture default auto color space is resolved to sRGB at the U.4 material boundary");
        }
        for (const char* parameter :
             {"scale", "bias"}) {
            if (Parameter(
                    node,
                    TfToken(parameter))) {
                Warning(
                    "URE-U4-LOSS-TEXTURE-TRANSFORM",
                    path.GetString(),
                    "UsdUVTexture " +
                        std::string(parameter) +
                        " is not represented by the native Texture2D node");
            }
        }
        for (const char* parameter :
             {"wrapS", "wrapT", "fallback"}) {
            if (Parameter(
                    node,
                    TfToken(parameter))) {
                Warning(
                    "URE-U4-LOSS-TEXTURE-SAMPLER",
                    path.GetString(),
                    "UsdUVTexture " +
                        std::string(parameter) +
                        " is not represented by the native Texture2D node");
            }
        }
        ReportUnknownInputs(
            node,
            path,
            {
                "file",
                "sourceColorSpace",
                "scale",
                "bias",
                "wrapS",
                "wrapT",
                "fallback"
            },
            {"st"});

        auto texture =
            std::make_shared<
                ure::scene_ir::TextureResource>();
        texture->name = path.GetString();
        texture->image = std::move(image);
        texture->uv_set = uv_set;
        GraphNode graph_node;
        graph_node.kind =
            GraphNodeKind::Texture2D;
        graph_node.name = path.GetString();
        graph_node.texture =
            std::move(texture);
        return graph_.add_node(
            std::move(graph_node));
    }

    GraphNodeId ConvertPreviewSurface(
        const SdfPath& path,
        const HdMaterialNode2& node) {
        const GraphNodeId base_color =
            ColorInput(
                node,
                path,
                "diffuseColor",
                {0.18f, 0.18f, 0.18f});
        const GraphNodeId roughness =
            FloatInput(
                node,
                path,
                "roughness",
                0.5f);
        const GraphNodeId metallic =
            FloatInput(
                node,
                path,
                "metallic",
                0.0f);
        const GraphNodeId ior =
            FloatInput(
                node,
                path,
                "ior",
                1.5f);
        ValidateRange(
            node,
            path,
            "roughness",
            0.0f,
            1.0f);
        ValidateRange(
            node,
            path,
            "metallic",
            0.0f,
            1.0f);
        ValidateRange(
            node,
            path,
            "ior",
            1.0f,
            10.0f);

        const auto opacity =
            Parameter(
                node,
                TfToken("opacity"));
        const auto opacity_value =
            opacity
            ? float_value(*opacity)
            : std::optional<float>(1.0f);
        if (ConnectionFor(
                node,
                TfToken("opacity"),
                path) ||
            !opacity_value ||
            std::abs(*opacity_value - 1.0f) >
                1.0e-6f) {
            Error(
                "URE-U4-ERROR-OPACITY",
                path.GetString(),
                "UsdPreviewSurface opacity requires a native opacity transport contract");
        }
        const auto clearcoat =
            Parameter(
                node,
                TfToken("clearcoat"));
        if (ConnectionFor(
                node,
                TfToken("clearcoat"),
                path) ||
            (clearcoat &&
             (!float_value(*clearcoat) ||
              std::abs(
                  *float_value(*clearcoat)) >
                  1.0e-6f))) {
            Error(
                "URE-U4-ERROR-CLEARCOAT",
                path.GetString(),
                "UsdPreviewSurface clearcoat is not representable by the current native layer contract");
        }
        if (ConnectionFor(
                node,
                TfToken("normal"),
                path)) {
            Error(
                "URE-U4-ERROR-NORMAL",
                path.GetString(),
                "UsdPreviewSurface normal input requires the native normal-resource binding path");
        }
        const VtValue* emissive =
            Parameter(
                node,
                TfToken("emissiveColor"));
        const auto emissive_value = emissive
            ? color_value(*emissive)
            : std::optional<
                  ure::core::Vec3f>(
                  ure::core::Vec3f{});
        if (ConnectionFor(
                node,
                TfToken("emissiveColor"),
                path) ||
            !emissive_value ||
            std::abs(emissive_value->x) >
                1.0e-6f ||
            std::abs(emissive_value->y) >
                1.0e-6f ||
            std::abs(emissive_value->z) >
                1.0e-6f) {
            Error(
                "URE-U4-ERROR-EMISSION",
                path.GetString(),
                "UsdPreviewSurface combined emission requires a native emission-plus-surface graph contract");
        }
        const VtValue* occlusion =
            Parameter(
                node,
                TfToken("occlusion"));
        const auto occlusion_value = occlusion
            ? float_value(*occlusion)
            : std::optional<float>(1.0f);
        if (ConnectionFor(
                node,
                TfToken("occlusion"),
                path) ||
            !occlusion_value ||
            std::abs(*occlusion_value - 1.0f) >
                1.0e-6f) {
            Error(
                "URE-U4-ERROR-OCCLUSION",
                path.GetString(),
                "UsdPreviewSurface occlusion requires a native occlusion input contract");
        }
        const auto workflow =
            Parameter(
                node,
                TfToken("useSpecularWorkflow"));
        if (workflow &&
            (!float_value(*workflow) ||
             std::abs(*float_value(*workflow)) >
                 1.0e-6f)) {
            Error(
                "URE-U4-ERROR-SPECULAR-WORKFLOW",
                path.GetString(),
                "UsdPreviewSurface specular workflow is unsupported");
        }
        ReportUnknownInputs(
            node,
            path,
            {
                "diffuseColor",
                "emissiveColor",
                "useSpecularWorkflow",
                "specularColor",
                "metallic",
                "roughness",
                "clearcoat",
                "clearcoatRoughness",
                "opacity",
                "opacityThreshold",
                "ior",
                "normal",
                "displacement",
                "occlusion"
            },
            {
                "diffuseColor",
                "emissiveColor",
                "metallic",
                "roughness",
                "clearcoat",
                "opacity",
                "ior",
                "normal",
                "occlusion"
            });

        GraphNode lambert;
        lambert.kind =
            GraphNodeKind::BsdfLambert;
        lambert.name =
            path.GetString() + "/lambert";
        lambert.inputs.push_back(
            ure::scene_ir::material_graph_input(
                "base_color",
                base_color));
        lambert.inputs.push_back(
            ure::scene_ir::material_graph_input(
                "roughness",
                roughness));
        const GraphNodeId lambert_id =
            graph_.add_node(
                std::move(lambert));

        GraphNode metal;
        metal.kind =
            GraphNodeKind::BsdfMetal;
        metal.name =
            path.GetString() + "/metal";
        metal.inputs.push_back(
            ure::scene_ir::material_graph_input(
                "base_color",
                base_color));
        metal.inputs.push_back(
            ure::scene_ir::material_graph_input(
                "roughness",
                roughness));
        const GraphNodeId metal_id =
            graph_.add_node(std::move(metal));

        const bool metallic_connected =
            node.inputConnections.contains(
                TfToken("metallic"));
        const VtValue* metallic_parameter =
            Parameter(
                node,
                TfToken("metallic"));
        const auto literal_metallic =
            metallic_parameter
            ? float_value(*metallic_parameter)
            : std::optional<float>(0.0f);
        if (!metallic_connected &&
            literal_metallic &&
            *literal_metallic <= 1.0e-6f) {
            GraphNode coating;
            coating.kind =
                GraphNodeKind::BsdfDielectric;
            coating.name =
                path.GetString() +
                "/specular";
            coating.inputs.push_back(
                ure::scene_ir::
                    material_graph_input(
                        "roughness",
                        roughness));
            coating.inputs.push_back(
                ure::scene_ir::
                    material_graph_input(
                        "ior",
                        ior));
            const GraphNodeId coating_id =
                graph_.add_node(
                    std::move(coating));
            GraphNode layer;
            layer.kind =
                GraphNodeKind::BsdfLayer;
            layer.name =
                path.GetString() + "/surface";
            layer.inputs.push_back(
                ure::scene_ir::
                    material_graph_input(
                        "coating",
                        coating_id));
            layer.inputs.push_back(
                ure::scene_ir::
                    material_graph_input(
                        "substrate",
                        lambert_id));
            Warning(
                "URE-U4-LOSS-PREVIEW-BSDF",
                path.GetString(),
                "UsdPreviewSurface opaque specular response is represented by the native dielectric-layer model");
            return graph_.add_node(
                std::move(layer));
        }
        if (!metallic_connected &&
            literal_metallic &&
            *literal_metallic >=
                1.0f - 1.0e-6f) {
            Warning(
                "URE-U4-LOSS-PREVIEW-BSDF",
                path.GetString(),
                "UsdPreviewSurface metallic response is represented by the native spectral metal model");
            return metal_id;
        }

        GraphNode mix;
        mix.kind = GraphNodeKind::BsdfMix;
        mix.name =
            path.GetString() + "/surface";
        mix.inputs.push_back(
            ure::scene_ir::material_graph_input(
                "a",
                lambert_id));
        mix.inputs.push_back(
            ure::scene_ir::material_graph_input(
                "b",
                metal_id));
        mix.inputs.push_back(
            ure::scene_ir::material_graph_input(
                "factor",
                metallic));
        Warning(
            "URE-U4-LOSS-PREVIEW-BSDF",
            path.GetString(),
            "UsdPreviewSurface mixed metallic response omits its separate opaque dielectric-specular lobe");
        return graph_.add_node(std::move(mix));
    }

    GraphNodeId AddNativeInput(
        GraphNode& output,
        const HdMaterialNode2& source,
        const SdfPath& path,
        const char* name,
        bool color,
        bool required,
        float fallback_float,
        ure::core::Vec3f fallback_color) {
        const TfToken token(name);
        GraphNodeId value =
            ure::scene_ir::
                kInvalidMaterialGraphNode;
        std::string upstream_output = "out";
        if (const auto connection =
                ConnectionFor(
                    source,
                    token,
                    path)) {
            value = ConvertNode(
                connection->upstreamNode);
            upstream_output =
                connection->
                    upstreamOutputName.
                    GetString();
        } else if (
            Parameter(source, token) ||
            !required) {
            value = color
                ? ColorInput(
                      source,
                      path,
                      name,
                      fallback_color)
                : FloatInput(
                      source,
                      path,
                      name,
                      fallback_float);
        } else {
            Error(
                "URE-U4-ERROR-NATIVE-INPUT",
                path.GetString(),
                "URE material node requires input: " +
                    std::string(name));
        }
        if (value !=
            ure::scene_ir::
                kInvalidMaterialGraphNode) {
            output.inputs.push_back(
                ure::scene_ir::
                    material_graph_input(
                        name,
                        value,
                        upstream_output));
        }
        return value;
    }

    GraphNodeId ConvertNativeNode(
        const SdfPath& path,
        const HdMaterialNode2& source,
        GraphNodeKind kind) {
        GraphNode output;
        output.kind = kind;
        output.name = path.GetString();
        std::set<std::string>
            allowed_parameters;
        std::set<std::string>
            allowed_connections;
        switch (kind) {
        case GraphNodeKind::ConstantColor: {
            allowed_parameters = {"value"};
            const VtValue* value =
                Parameter(
                    source,
                    TfToken("value"));
            const auto color = value
                ? color_value(*value)
                : std::nullopt;
            if (!color || !finite(*color)) {
                Error(
                    "URE-U4-ERROR-NATIVE-VALUE",
                    path.GetString(),
                    "URE constant color requires a finite value");
            } else {
                output.color = *color;
            }
            break;
        }
        case GraphNodeKind::ConstantFloat: {
            allowed_parameters = {"value"};
            const VtValue* value =
                Parameter(
                    source,
                    TfToken("value"));
            const auto scalar = value
                ? float_value(*value)
                : std::nullopt;
            if (!scalar || !finite(*scalar)) {
                Error(
                    "URE-U4-ERROR-NATIVE-VALUE",
                    path.GetString(),
                    "URE constant float requires a finite value");
            } else {
                output.value = *scalar;
            }
            break;
        }
        case GraphNodeKind::Texture2D:
            return ConvertUvTexture(path, source);
        case GraphNodeKind::Add:
        case GraphNodeKind::Multiply:
            allowed_parameters = {"a", "b"};
            allowed_connections = {"a", "b"};
            AddNativeInput(
                output,
                source,
                path,
                "a",
                true,
                true,
                0.0f,
                {});
            AddNativeInput(
                output,
                source,
                path,
                "b",
                true,
                true,
                0.0f,
                {});
            break;
        case GraphNodeKind::Mix:
            allowed_parameters = {
                "a", "b", "factor"};
            allowed_connections = {
                "a", "b", "factor"};
            AddNativeInput(
                output,
                source,
                path,
                "a",
                true,
                true,
                0.0f,
                {});
            AddNativeInput(
                output,
                source,
                path,
                "b",
                true,
                true,
                0.0f,
                {});
            AddNativeInput(
                output,
                source,
                path,
                "factor",
                false,
                true,
                0.5f,
                {});
            break;
        case GraphNodeKind::Checker2D:
        case GraphNodeKind::Noise2D:
            allowed_parameters = {
                "a", "b", "scale"};
            allowed_connections = {
                "a", "b", "scale"};
            AddNativeInput(
                output,
                source,
                path,
                "a",
                true,
                false,
                0.0f,
                {});
            AddNativeInput(
                output,
                source,
                path,
                "b",
                true,
                false,
                0.0f,
                {1.0f, 1.0f, 1.0f});
            AddNativeInput(
                output,
                source,
                path,
                "scale",
                false,
                false,
                1.0f,
                {});
            break;
        case GraphNodeKind::BsdfLambert:
        case GraphNodeKind::BsdfMetal:
            allowed_parameters = {
                "base_color", "roughness"};
            allowed_connections = {
                "base_color", "roughness"};
            if (kind ==
                GraphNodeKind::BsdfMetal) {
                allowed_parameters.insert("eta");
                allowed_parameters.insert("k");
                allowed_connections.insert("eta");
                allowed_connections.insert("k");
            }
            AddNativeInput(
                output,
                source,
                path,
                "base_color",
                true,
                false,
                0.0f,
                {0.8f, 0.8f, 0.8f});
            AddNativeInput(
                output,
                source,
                path,
                "roughness",
                false,
                false,
                0.5f,
                {});
            if (kind ==
                GraphNodeKind::BsdfMetal) {
                if (Parameter(
                        source,
                        TfToken("eta")) ||
                    source.inputConnections.
                        contains(
                            TfToken("eta"))) {
                    AddNativeInput(
                        output,
                        source,
                        path,
                        "eta",
                        true,
                        false,
                        0.0f,
                        {});
                }
                if (Parameter(
                        source,
                        TfToken("k")) ||
                    source.inputConnections.
                        contains(
                            TfToken("k"))) {
                    AddNativeInput(
                        output,
                        source,
                        path,
                        "k",
                        true,
                        false,
                        0.0f,
                        {});
                }
            }
            break;
        case GraphNodeKind::BsdfDielectric:
            allowed_parameters = {
                "base_color", "roughness", "ior"};
            allowed_connections = {
                "base_color", "roughness", "ior"};
            AddNativeInput(
                output,
                source,
                path,
                "base_color",
                true,
                false,
                0.0f,
                {1.0f, 1.0f, 1.0f});
            AddNativeInput(
                output,
                source,
                path,
                "roughness",
                false,
                false,
                0.0f,
                {});
            AddNativeInput(
                output,
                source,
                path,
                "ior",
                false,
                false,
                1.5f,
                {});
            break;
        case GraphNodeKind::BsdfLight:
            allowed_parameters = {"emission"};
            allowed_connections = {"emission"};
            AddNativeInput(
                output,
                source,
                path,
                "emission",
                true,
                false,
                0.0f,
                {1.0f, 1.0f, 1.0f});
            break;
        case GraphNodeKind::BsdfMix:
            allowed_parameters = {
                "a", "b", "factor"};
            allowed_connections = {
                "a", "b", "factor"};
            AddNativeInput(
                output,
                source,
                path,
                "a",
                true,
                true,
                0.0f,
                {});
            AddNativeInput(
                output,
                source,
                path,
                "b",
                true,
                true,
                0.0f,
                {});
            AddNativeInput(
                output,
                source,
                path,
                "factor",
                false,
                true,
                0.5f,
                {});
            break;
        case GraphNodeKind::BsdfLayer:
            allowed_parameters = {
                "coating",
                "substrate",
                "thickness",
                "absorption"};
            allowed_connections =
                allowed_parameters;
            AddNativeInput(
                output,
                source,
                path,
                "coating",
                true,
                true,
                0.0f,
                {});
            AddNativeInput(
                output,
                source,
                path,
                "substrate",
                true,
                true,
                0.0f,
                {});
            if (Parameter(
                    source,
                    TfToken("thickness")) ||
                source.inputConnections.
                    contains(
                        TfToken("thickness"))) {
                AddNativeInput(
                    output,
                    source,
                    path,
                    "thickness",
                    false,
                    false,
                    0.0f,
                    {});
            }
            if (Parameter(
                    source,
                    TfToken("absorption")) ||
                source.inputConnections.
                    contains(
                        TfToken("absorption"))) {
                AddNativeInput(
                    output,
                    source,
                    path,
                    "absorption",
                    true,
                    false,
                    0.0f,
                    {});
            }
            break;
        case GraphNodeKind::OutputSurface:
            allowed_parameters = {"surface"};
            allowed_connections = {"surface"};
            AddNativeInput(
                output,
                source,
                path,
                "surface",
                true,
                true,
                0.0f,
                {});
            break;
        default:
            Error(
                "URE-U4-ERROR-NATIVE-NODE",
                path.GetString(),
                "URE material node kind is outside the U.4 adapter boundary");
            break;
        }
        ReportUnknownInputs(
            source,
            path,
            allowed_parameters,
            allowed_connections);
        return graph_.add_node(std::move(output));
    }

    GraphNodeId ConvertNode(
        const SdfPath& path) {
        const auto cached = cache_.find(path);
        if (cached != cache_.end()) {
            return cached->second;
        }
        if (!active_.insert(path).second) {
            Error(
                "URE-U4-ERROR-CYCLE",
                path.GetString(),
                "USD material network contains a cycle");
            return ure::scene_ir::
                kInvalidMaterialGraphNode;
        }
        const HdMaterialNode2* node =
            FindNode(path);
        if (!node) {
            active_.erase(path);
            return ure::scene_ir::
                kInvalidMaterialGraphNode;
        }
        const std::string identifier =
            node->nodeTypeId.GetString();
        GraphNodeId result =
            ure::scene_ir::
                kInvalidMaterialGraphNode;
        if (identifier ==
            "UsdPreviewSurface") {
            result = ConvertPreviewSurface(
                path,
                *node);
        } else if (
            identifier ==
            "UsdUVTexture") {
            result = ConvertUvTexture(
                path,
                *node);
        } else if (
            identifier.starts_with(
                "UsdPrimvarReader_")) {
            Error(
                "URE-U4-ERROR-PRIMVAR-READER",
                path.GetString(),
                "UsdPrimvarReader is supported only as a texture-coordinate source");
        } else if (
            const auto kind =
                native_kind(identifier)) {
            result = ConvertNativeNode(
                path,
                *node,
                *kind);
        } else {
            Error(
                "URE-U4-ERROR-UNSUPPORTED-NODE",
                path.GetString(),
                "USD material node is unsupported: " +
                    identifier);
        }
        active_.erase(path);
        cache_.insert_or_assign(path, result);
        return result;
    }

    void Warning(
        std::string code,
        std::string path,
        std::string message) {
        loss_report_.push_back({
            HdUREMaterialLossSeverity::Warning,
            std::move(code),
            std::move(path),
            std::move(message)});
    }

    void Error(
        std::string code,
        std::string path,
        std::string message) {
        if (error_.empty()) {
            error_ = message;
        }
        loss_report_.push_back({
            HdUREMaterialLossSeverity::Error,
            std::move(code),
            std::move(path),
            std::move(message)});
    }

    HdUREMaterialConversion Finish() {
        if (error_.empty()) {
            try {
                graph_.validate();
            } catch (
                const std::exception& error) {
                Error(
                    "URE-U4-ERROR-GRAPH",
                    material_path_.GetString(),
                    error.what());
            }
        }
        HdUREMaterialConversion result;
        result.loss_report =
            std::move(loss_report_);
        result.error = std::move(error_);
        if (result.error.empty()) {
            auto material =
                std::make_shared<
                    ure::scene_ir::MaterialNode>();
            material->name =
                material_path_.GetString();
            material->graph =
                std::make_shared<Graph>(
                    std::move(graph_));
            result.material =
                std::move(material);
        }
        return result;
    }

    SdfPath material_path_;
    const HdMaterialNetwork2& network_;
    Graph graph_;
    std::vector<HdUREMaterialLoss>
        loss_report_;
    std::string error_;
    std::map<SdfPath, GraphNodeId> cache_;
    std::set<SdfPath> active_;
    std::set<SdfPath> used_nodes_;
};

}

HdUREMaterialConversion
ConvertMaterialNetwork(
    const SdfPath& material_path,
    const HdMaterialNetwork2& network) {
    return Builder(
        material_path,
        network)
        .Convert();
}

PXR_NAMESPACE_CLOSE_SCOPE
