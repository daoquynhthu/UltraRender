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

double parse_double(std::string_view text, double fallback = 0.0) {
    if (text.empty()) return fallback;
    double value = fallback;
    auto result =
        std::from_chars(
            text.data(),
            text.data() + text.size(),
            value);
    if (result.ec != std::errc() ||
        result.ptr != text.data() + text.size()) {
        throw std::runtime_error(
            "MaterialX double value is invalid");
    }
    return value;
}

int parse_int(std::string_view text, int fallback = 0) {
    if (text.empty()) return fallback;
    int value = fallback;
    auto result =
        std::from_chars(
            text.data(),
            text.data() + text.size(),
            value);
    if (result.ec != std::errc() ||
        result.ptr != text.data() + text.size()) {
        throw std::runtime_error(
            "MaterialX integer value is invalid");
    }
    return value;
}

std::string hex_encode(std::string_view value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(value.size() * 2);
    for (unsigned char byte : value) {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 0x0f]);
    }
    return result;
}

std::string hex_decode(std::string_view value) {
    if (value.size() % 2 != 0) {
        throw std::runtime_error(
            "MaterialX hexadecimal attribute is invalid");
    }
    auto nibble = [](char digit) -> int {
        if (digit >= '0' && digit <= '9') {
            return digit - '0';
        }
        if (digit >= 'a' && digit <= 'f') {
            return digit - 'a' + 10;
        }
        if (digit >= 'A' && digit <= 'F') {
            return digit - 'A' + 10;
        }
        return -1;
    };
    std::string result;
    result.reserve(value.size() / 2);
    for (std::size_t index = 0;
         index < value.size();
         index += 2) {
        const int high = nibble(value[index]);
        const int low = nibble(value[index + 1]);
        if (high < 0 || low < 0) {
            throw std::runtime_error(
                "MaterialX hexadecimal attribute is invalid");
        }
        result.push_back(
            static_cast<char>((high << 4) | low));
    }
    return result;
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
    case scene_ir::MaterialGraphNodeKind::BsdfGrating: return "URE_bsdf_grating";
    case scene_ir::MaterialGraphNodeKind::BsdfPhaseMask: return "URE_bsdf_phase_mask";
    case scene_ir::MaterialGraphNodeKind::BsdfZonePlate: return "URE_bsdf_zone_plate";
    case scene_ir::MaterialGraphNodeKind::BsdfDoe: return "URE_bsdf_doe";
    case scene_ir::MaterialGraphNodeKind::BsdfScatteringTable: return "URE_bsdf_scattering_table";
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
    if (name == "URE_bsdf_grating") return scene_ir::MaterialGraphNodeKind::BsdfGrating;
    if (name == "URE_bsdf_phase_mask") return scene_ir::MaterialGraphNodeKind::BsdfPhaseMask;
    if (name == "URE_bsdf_zone_plate") return scene_ir::MaterialGraphNodeKind::BsdfZonePlate;
    if (name == "URE_bsdf_doe") return scene_ir::MaterialGraphNodeKind::BsdfDoe;
    if (name == "URE_bsdf_scattering_table") return scene_ir::MaterialGraphNodeKind::BsdfScatteringTable;
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

bool is_diffractive_kind(
    scene_ir::MaterialGraphNodeKind kind) {
    return kind >=
               scene_ir::MaterialGraphNodeKind::
                   BsdfGrating &&
           kind <=
               scene_ir::MaterialGraphNodeKind::
                   BsdfScatteringTable;
}

scene_ir::DiffractiveOperatorKind
operator_kind_for_node(
    scene_ir::MaterialGraphNodeKind kind) {
    switch (kind) {
    case scene_ir::MaterialGraphNodeKind::BsdfGrating:
        return scene_ir::DiffractiveOperatorKind::Grating;
    case scene_ir::MaterialGraphNodeKind::BsdfPhaseMask:
        return scene_ir::DiffractiveOperatorKind::PhaseMask;
    case scene_ir::MaterialGraphNodeKind::BsdfZonePlate:
        return scene_ir::DiffractiveOperatorKind::ZonePlate;
    case scene_ir::MaterialGraphNodeKind::BsdfDoe:
        return scene_ir::DiffractiveOperatorKind::Doe;
    case scene_ir::MaterialGraphNodeKind::BsdfScatteringTable:
        return scene_ir::DiffractiveOperatorKind::ScatteringTable;
    default:
        throw std::runtime_error(
            "MaterialX node is not a diffractive operator");
    }
}

std::string scattering_table_string(
    const scene_ir::DiffractiveOperator& diffraction) {
    if (diffraction.table.size() >
        scene_ir::kMaxDiffractiveScatteringEntries) {
        throw std::runtime_error(
            "MaterialX diffractive scattering table exceeds the adapter budget");
    }
    std::ostringstream out;
    for (std::size_t index = 0;
         index < diffraction.table.size();
         ++index) {
        if (index != 0) out << ';';
        const auto& entry = diffraction.table[index];
        out << std::format(
            "{:.9g},{:.9g},{},{},{:.9g},{:.9g},{:.9g},{:.9g},{:.9g},{:.9g},{:.9g},{:.9g}",
            entry.wavelength_nm,
            entry.incident_cosine,
            entry.order,
            static_cast<int>(entry.side),
            entry.jones_ss.real,
            entry.jones_ss.imag,
            entry.jones_sp.real,
            entry.jones_sp.imag,
            entry.jones_ps.real,
            entry.jones_ps.imag,
            entry.jones_pp.real,
            entry.jones_pp.imag);
    }
    return out.str();
}

std::vector<scene_ir::DiffractiveScatteringEntry>
parse_scattering_table(std::string_view text) {
    std::vector<
        scene_ir::DiffractiveScatteringEntry> result;
    std::size_t row_start = 0;
    while (row_start < text.size()) {
        const std::size_t row_end =
            text.find(';', row_start);
        const std::string_view row =
            text.substr(
                row_start,
                row_end == std::string_view::npos
                    ? text.size() - row_start
                    : row_end - row_start);
        std::vector<std::string_view> fields;
        std::size_t field_start = 0;
        while (field_start <= row.size()) {
            const std::size_t field_end =
                row.find(',', field_start);
            fields.push_back(
                row.substr(
                    field_start,
                    field_end == std::string_view::npos
                        ? row.size() - field_start
                        : field_end - field_start));
            if (field_end == std::string_view::npos) break;
            field_start = field_end + 1;
        }
        if (fields.size() != 12) {
            throw std::runtime_error(
                "MaterialX diffractive table row must contain 12 fields");
        }
        scene_ir::DiffractiveScatteringEntry entry;
        entry.wavelength_nm =
            static_cast<float>(parse_double(fields[0]));
        entry.incident_cosine =
            static_cast<float>(parse_double(fields[1]));
        entry.order = parse_int(fields[2]);
        const int side = parse_int(fields[3]);
        if (side < 0 || side > 1) {
            throw std::runtime_error(
                "MaterialX diffractive table side is invalid");
        }
        entry.side =
            static_cast<
                scene_ir::DiffractiveScatterSide>(side);
        entry.jones_ss = {
            static_cast<float>(parse_double(fields[4])),
            static_cast<float>(parse_double(fields[5]))};
        entry.jones_sp = {
            static_cast<float>(parse_double(fields[6])),
            static_cast<float>(parse_double(fields[7]))};
        entry.jones_ps = {
            static_cast<float>(parse_double(fields[8])),
            static_cast<float>(parse_double(fields[9]))};
        entry.jones_pp = {
            static_cast<float>(parse_double(fields[10])),
            static_cast<float>(parse_double(fields[11]))};
        result.push_back(entry);
        if (result.size() >
            scene_ir::kMaxDiffractiveScatteringEntries) {
            throw std::runtime_error(
                "MaterialX diffractive scattering table exceeds the adapter budget");
        }
        if (row_end == std::string_view::npos) break;
        row_start = row_end + 1;
    }
    return result;
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
        } else if (is_diffractive_kind(node.kind)) {
            const auto expected =
                operator_kind_for_node(node.kind);
            const int operator_kind =
                parse_int(
                    attr(element, "URE:operator_kind"),
                    static_cast<int>(expected));
            const int side =
                parse_int(
                    attr(element, "URE:side"),
                    0);
            if (operator_kind < 0 ||
                operator_kind > 4 ||
                static_cast<
                    scene_ir::DiffractiveOperatorKind>(
                    operator_kind) != expected ||
                side < 0 ||
                side > 1) {
                throw std::runtime_error(
                    "MaterialX diffractive operator enum is invalid");
            }
            node.diffraction.kind = expected;
            node.diffraction.side =
                static_cast<
                    scene_ir::DiffractiveScatterSide>(
                    side);
            node.diffraction.period_m =
                parse_double(
                    attr(element, "URE:period_m"),
                    node.diffraction.period_m);
            node.diffraction.orientation_rad =
                parse_double(
                    attr(element, "URE:orientation_rad"),
                    node.diffraction.orientation_rad);
            node.diffraction.duty_cycle =
                parse_double(
                    attr(element, "URE:duty_cycle"),
                    node.diffraction.duty_cycle);
            node.diffraction.phase_depth_rad =
                parse_double(
                    attr(element, "URE:phase_depth_rad"),
                    node.diffraction.phase_depth_rad);
            node.diffraction.design_wavelength_nm =
                parse_double(
                    attr(
                        element,
                        "URE:design_wavelength_nm"),
                    node.diffraction.design_wavelength_nm);
            node.diffraction.focal_length_m =
                parse_double(
                    attr(element, "URE:focal_length_m"),
                    node.diffraction.focal_length_m);
            node.diffraction.aperture_radius_m =
                parse_double(
                    attr(
                        element,
                        "URE:aperture_radius_m"),
                    node.diffraction.aperture_radius_m);
            node.diffraction.max_order =
                parse_int(
                    attr(element, "URE:max_order"),
                    node.diffraction.max_order);
            node.diffraction.table_id =
                hex_decode(
                    attr(element, "URE:table_id_hex"));
            node.diffraction.table =
                parse_scattering_table(
                    attr(element, "URE:table"));
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
        if (is_diffractive_kind(node.kind)) {
            const auto expected =
                operator_kind_for_node(node.kind);
            if (node.diffraction.kind != expected) {
                throw std::runtime_error(
                    "MaterialX diffractive node kind does not match its operator");
            }
            out << " URE:operator_kind=\""
                << static_cast<int>(node.diffraction.kind)
                << "\" URE:side=\""
                << static_cast<int>(node.diffraction.side)
                << "\" URE:period_m=\""
                << std::format(
                       "{:.17g}",
                       node.diffraction.period_m)
                << "\" URE:orientation_rad=\""
                << std::format(
                       "{:.17g}",
                       node.diffraction.orientation_rad)
                << "\" URE:duty_cycle=\""
                << std::format(
                       "{:.17g}",
                       node.diffraction.duty_cycle)
                << "\" URE:phase_depth_rad=\""
                << std::format(
                       "{:.17g}",
                       node.diffraction.phase_depth_rad)
                << "\" URE:design_wavelength_nm=\""
                << std::format(
                       "{:.17g}",
                       node.diffraction.design_wavelength_nm)
                << "\" URE:focal_length_m=\""
                << std::format(
                       "{:.17g}",
                       node.diffraction.focal_length_m)
                << "\" URE:aperture_radius_m=\""
                << std::format(
                       "{:.17g}",
                       node.diffraction.aperture_radius_m)
                << "\" URE:max_order=\""
                << node.diffraction.max_order
                << "\" URE:table_id_hex=\""
                << hex_encode(node.diffraction.table_id)
                << "\" URE:table=\""
                << scattering_table_string(
                       node.diffraction)
                << "\"";
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
