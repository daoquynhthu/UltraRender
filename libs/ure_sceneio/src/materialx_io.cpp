#include "ure/materialx_io.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <format>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ure::io {
namespace {

struct XmlElement {
    std::string name;
    std::map<std::string, std::string> attrs;
};

std::string trim(std::string_view text) {
    size_t first = 0;
    while (first < text.size() && std::isspace(static_cast<unsigned char>(text[first]))) ++first;
    size_t last = text.size();
    while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1]))) --last;
    return std::string(text.substr(first, last - first));
}

std::vector<XmlElement> parse_xml_elements(std::string_view xml) {
    std::vector<XmlElement> elements;
    size_t pos = 0;
    while (true) {
        size_t open = xml.find('<', pos);
        if (open == std::string_view::npos) break;
        size_t close = xml.find('>', open + 1);
        if (close == std::string_view::npos) throw std::runtime_error("MaterialX XML tag is not closed");
        std::string tag = trim(xml.substr(open + 1, close - open - 1));
        pos = close + 1;
        if (tag.empty() || tag[0] == '/' || tag[0] == '?' || tag.rfind("!--", 0) == 0) continue;
        if (!tag.empty() && tag.back() == '/') tag.pop_back();
        tag = trim(tag);
        size_t name_end = 0;
        while (name_end < tag.size() && !std::isspace(static_cast<unsigned char>(tag[name_end]))) ++name_end;
        XmlElement element;
        element.name = tag.substr(0, name_end);
        size_t attr_pos = name_end;
        while (attr_pos < tag.size()) {
            while (attr_pos < tag.size() && std::isspace(static_cast<unsigned char>(tag[attr_pos]))) ++attr_pos;
            if (attr_pos >= tag.size()) break;
            size_t eq = tag.find('=', attr_pos);
            if (eq == std::string::npos) break;
            std::string key = trim(std::string_view(tag).substr(attr_pos, eq - attr_pos));
            size_t quote = eq + 1;
            while (quote < tag.size() && std::isspace(static_cast<unsigned char>(tag[quote]))) ++quote;
            if (quote >= tag.size() || (tag[quote] != '"' && tag[quote] != '\'')) {
                throw std::runtime_error("MaterialX XML attribute must be quoted");
            }
            char quote_char = tag[quote++];
            size_t end_quote = tag.find(quote_char, quote);
            if (end_quote == std::string::npos) throw std::runtime_error("MaterialX XML attribute quote is not closed");
            element.attrs[key] = tag.substr(quote, end_quote - quote);
            attr_pos = end_quote + 1;
        }
        elements.push_back(std::move(element));
    }
    return elements;
}

std::string attr(const XmlElement& element, std::string_view name, std::string_view fallback = {}) {
    auto it = element.attrs.find(std::string(name));
    return it == element.attrs.end() ? std::string(fallback) : it->second;
}

float parse_float(std::string_view text, float fallback = 0.0f) {
    if (text.empty()) return fallback;
    float value = fallback;
    auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc()) throw std::runtime_error("MaterialX float value is invalid");
    return value;
}

core::Vec3f parse_color(std::string_view text, core::Vec3f fallback = {}) {
    if (text.empty()) return fallback;
    std::string normalized(text);
    std::replace(normalized.begin(), normalized.end(), ',', ' ');
    std::istringstream stream(normalized);
    core::Vec3f value = fallback;
    if (!(stream >> value.x >> value.y >> value.z)) {
        throw std::runtime_error("MaterialX color3 value is invalid");
    }
    return value;
}

std::string color_string(const core::Vec3f& c) {
    return std::format("{:.9g},{:.9g},{:.9g}", c.x, c.y, c.z);
}

const scene_ir::MaterialGraphNode& require_node(const scene_ir::MaterialGraph& graph, scene_ir::MaterialGraphNodeId id) {
    return graph.require_node(id, "MaterialX export");
}

void emit_input(std::ostringstream& out, const scene_ir::MaterialGraphInput& input) {
    out << "    <input name=\"" << input.name << "\" nodename=\"n" << input.node_id << "\" />\n";
}

std::string export_kind(scene_ir::MaterialGraphNodeKind kind) {
    switch (kind) {
    case scene_ir::MaterialGraphNodeKind::ConstantColor: return "URE_constant_color";
    case scene_ir::MaterialGraphNodeKind::ConstantFloat: return "URE_constant_float";
    case scene_ir::MaterialGraphNodeKind::Texture2D: return "URE_texture2d";
    case scene_ir::MaterialGraphNodeKind::Add: return "URE_add";
    case scene_ir::MaterialGraphNodeKind::Multiply: return "URE_multiply";
    case scene_ir::MaterialGraphNodeKind::Mix: return "URE_mix";
    case scene_ir::MaterialGraphNodeKind::Checker2D: return "URE_checker2d";
    case scene_ir::MaterialGraphNodeKind::Noise2D: return "URE_noise2d";
    case scene_ir::MaterialGraphNodeKind::BsdfLambert: return "URE_bsdf_lambert";
    case scene_ir::MaterialGraphNodeKind::BsdfMetal: return "URE_bsdf_metal";
    case scene_ir::MaterialGraphNodeKind::BsdfDielectric: return "URE_bsdf_dielectric";
    case scene_ir::MaterialGraphNodeKind::BsdfLight: return "URE_bsdf_light";
    case scene_ir::MaterialGraphNodeKind::BsdfMix: return "URE_bsdf_mix";
    case scene_ir::MaterialGraphNodeKind::BsdfLayer: return "URE_bsdf_layer";
    case scene_ir::MaterialGraphNodeKind::OutputSurface: return "surfacematerial";
    }
    throw std::runtime_error("MaterialGraph node kind is not exportable to MaterialX");
}

scene_ir::MaterialGraphNodeKind import_kind(std::string_view name) {
    if (name == "URE_constant_color") return scene_ir::MaterialGraphNodeKind::ConstantColor;
    if (name == "URE_constant_float") return scene_ir::MaterialGraphNodeKind::ConstantFloat;
    if (name == "URE_texture2d") return scene_ir::MaterialGraphNodeKind::Texture2D;
    if (name == "URE_add") return scene_ir::MaterialGraphNodeKind::Add;
    if (name == "URE_multiply") return scene_ir::MaterialGraphNodeKind::Multiply;
    if (name == "URE_mix") return scene_ir::MaterialGraphNodeKind::Mix;
    if (name == "URE_checker2d") return scene_ir::MaterialGraphNodeKind::Checker2D;
    if (name == "URE_noise2d") return scene_ir::MaterialGraphNodeKind::Noise2D;
    if (name == "URE_bsdf_lambert" || name == "standard_surface") return scene_ir::MaterialGraphNodeKind::BsdfLambert;
    if (name == "URE_bsdf_metal") return scene_ir::MaterialGraphNodeKind::BsdfMetal;
    if (name == "URE_bsdf_dielectric" || name == "dielectric_bsdf") return scene_ir::MaterialGraphNodeKind::BsdfDielectric;
    if (name == "URE_bsdf_light") return scene_ir::MaterialGraphNodeKind::BsdfLight;
    if (name == "URE_bsdf_mix") return scene_ir::MaterialGraphNodeKind::BsdfMix;
    if (name == "URE_bsdf_layer") return scene_ir::MaterialGraphNodeKind::BsdfLayer;
    if (name == "surfacematerial") return scene_ir::MaterialGraphNodeKind::OutputSurface;
    throw std::runtime_error("MaterialX node is not supported by the URE MaterialGraph adapter: " + std::string(name));
}

scene_ir::MaterialGraphNodeId parse_node_id(std::string_view name) {
    if (name.empty() || name[0] != 'n') throw std::runtime_error("MaterialX URE node name must use n<id>");
    scene_ir::MaterialGraphNodeId id = scene_ir::kInvalidMaterialGraphNode;
    auto digits = name.substr(1);
    auto result = std::from_chars(digits.data(), digits.data() + digits.size(), id);
    if (result.ec != std::errc()) throw std::runtime_error("MaterialX URE node id is invalid");
    return id;
}

bool is_exported_node_id(std::string_view name) {
    if (name.size() < 2 || name[0] != 'n') return false;
    for (size_t i = 1; i < name.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(name[i]))) return false;
    }
    return true;
}

} // namespace

scene_ir::MaterialGraph import_materialx_graph(std::string_view xml) {
    scene_ir::MaterialGraph graph;
    std::vector<XmlElement> elements = parse_xml_elements(xml);
    std::map<std::string, scene_ir::MaterialGraphNodeId> name_to_id;
    std::map<scene_ir::MaterialGraphNodeId, std::vector<scene_ir::MaterialGraphInput>> pending_inputs;

    for (const XmlElement& element : elements) {
        if (element.name == "input") continue;
        if (element.name == "materialx") continue;
        scene_ir::MaterialGraphNode node;
        node.kind = import_kind(element.name);
        std::string node_name = attr(element, "name");
        node.id = is_exported_node_id(node_name) ? parse_node_id(node_name) : graph.next_node_id();
        node.name = node_name;
        if (node.kind == scene_ir::MaterialGraphNodeKind::ConstantColor) {
            node.color = parse_color(attr(element, "value"));
        } else if (node.kind == scene_ir::MaterialGraphNodeKind::ConstantFloat) {
            node.value = parse_float(attr(element, "value"));
        } else if (node.kind == scene_ir::MaterialGraphNodeKind::BsdfLambert ||
                   node.kind == scene_ir::MaterialGraphNodeKind::BsdfMetal ||
                   node.kind == scene_ir::MaterialGraphNodeKind::BsdfLight) {
            node.color = parse_color(attr(element, "color"), node.color);
        } else if (node.kind == scene_ir::MaterialGraphNodeKind::Texture2D) {
            auto image = std::make_shared<scene_ir::ImageResource>();
            image->uri = attr(element, "file");
            auto texture = std::make_shared<scene_ir::TextureResource>();
            texture->image = image;
            node.texture = texture;
        }
        name_to_id[node_name] = node.id;
        graph.nodes.push_back(std::move(node));
    }

    scene_ir::MaterialGraphNodeId current_parent = scene_ir::kInvalidMaterialGraphNode;
    for (const XmlElement& element : elements) {
        if (element.name != "input") {
            std::string node_name = attr(element, "name");
            auto it = name_to_id.find(node_name);
            current_parent = it == name_to_id.end() ? scene_ir::kInvalidMaterialGraphNode : it->second;
            continue;
        }
        if (current_parent == scene_ir::kInvalidMaterialGraphNode) {
            throw std::runtime_error("MaterialX input appears before a parent node");
        }
        std::string ref = attr(element, "nodename");
        scene_ir::MaterialGraphInput input;
        input.name = attr(element, "name");
        if (!ref.empty()) {
            auto it = name_to_id.find(ref);
            if (it == name_to_id.end()) throw std::runtime_error("MaterialX input references an unknown nodename");
            input.node_id = it->second;
        } else if (element.attrs.contains("value")) {
            scene_ir::MaterialGraphNode value_node;
            std::string type = attr(element, "type");
            if (type == "color3") {
                value_node.kind = scene_ir::MaterialGraphNodeKind::ConstantColor;
                value_node.color = parse_color(attr(element, "value"));
            } else {
                value_node.kind = scene_ir::MaterialGraphNodeKind::ConstantFloat;
                value_node.value = parse_float(attr(element, "value"));
            }
            input.node_id = graph.add_node(std::move(value_node));
        } else {
            throw std::runtime_error("MaterialX input must provide nodename or value");
        }
        pending_inputs[current_parent].push_back(std::move(input));
    }

    for (auto& node : graph.nodes) {
        auto it = pending_inputs.find(node.id);
        if (it != pending_inputs.end()) node.inputs = std::move(it->second);
        if (node.kind == scene_ir::MaterialGraphNodeKind::OutputSurface) {
            graph.output_node_id = node.id;
        }
    }
    graph.validate();
    return graph;
}

std::string export_materialx_graph(const scene_ir::MaterialGraph& graph, std::string_view material_name) {
    graph.validate();
    std::ostringstream out;
    out << "<materialx version=\"1.39\" xmlns:URE=\"https://ultrarender/MaterialGraph\">\n";
    for (const auto& node : graph.nodes) {
        const std::string kind = export_kind(node.kind);
        if (node.kind == scene_ir::MaterialGraphNodeKind::OutputSurface) {
            out << "  <surfacematerial name=\"n" << node.id << "\" material=\"" << material_name << "\">\n";
            for (const auto& input : node.inputs) emit_input(out, input);
            out << "  </surfacematerial>\n";
            continue;
        }
        out << "  <" << kind << " name=\"n" << node.id << "\"";
        if (node.kind == scene_ir::MaterialGraphNodeKind::ConstantColor) out << " value=\"" << color_string(node.color) << "\"";
        if (node.kind == scene_ir::MaterialGraphNodeKind::ConstantFloat) out << " value=\"" << std::format("{:.9g}", node.value) << "\"";
        if (node.kind == scene_ir::MaterialGraphNodeKind::Texture2D) {
            if (!node.texture || !node.texture->image || node.texture->image->uri.empty()) {
                throw std::runtime_error("MaterialX export requires Texture2D nodes to carry an image URI");
            }
            out << " file=\"" << node.texture->image->uri << "\"";
        }
        if (node.inputs.empty()) {
            out << " />\n";
        } else {
            out << ">\n";
            for (const auto& input : node.inputs) {
                (void)require_node(graph, input.node_id);
                emit_input(out, input);
            }
            out << "  </" << kind << ">\n";
        }
    }
    out << "</materialx>\n";
    return out.str();
}

} // namespace ure::io
