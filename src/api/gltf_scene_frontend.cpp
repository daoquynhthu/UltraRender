#include "api/gltf_scene_frontend.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string_view>
#include <vector>

namespace ure {

namespace {

struct JsonValue {
    enum class Type {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    Type type = Type::Null;
    bool bool_value = false;
    double number_value = 0.0;
    std::string string_value;
    std::vector<JsonValue> array_value;
    std::map<std::string, JsonValue> object_value;
};

class JsonParser {
public:
    explicit JsonParser(std::string_view source) : source_(source) {}

    JsonValue parse() {
        skip_ws();
        JsonValue result = parse_value();
        skip_ws();
        return result;
    }

private:
    JsonValue parse_value() {
        skip_ws();
        if (pos_ >= source_.size()) return {};

        char c = source_[pos_];
        if (c == '{') return parse_object();
        if (c == '[') return parse_array();
        if (c == '"') return parse_string();
        if (c == 't' || c == 'f') return parse_bool();
        if (c == 'n') return parse_null();
        return parse_number();
    }

    JsonValue parse_object() {
        JsonValue value;
        value.type = JsonValue::Type::Object;
        ++pos_;
        skip_ws();
        if (match('}')) return value;

        while (pos_ < source_.size()) {
            JsonValue key = parse_string();
            skip_ws();
            match(':');
            skip_ws();
            value.object_value[key.string_value] = parse_value();
            skip_ws();
            if (match('}')) break;
            match(',');
            skip_ws();
        }
        return value;
    }

    JsonValue parse_array() {
        JsonValue value;
        value.type = JsonValue::Type::Array;
        ++pos_;
        skip_ws();
        if (match(']')) return value;

        while (pos_ < source_.size()) {
            value.array_value.push_back(parse_value());
            skip_ws();
            if (match(']')) break;
            match(',');
            skip_ws();
        }
        return value;
    }

    JsonValue parse_string() {
        JsonValue value;
        value.type = JsonValue::Type::String;
        if (!match('"')) return value;

        while (pos_ < source_.size()) {
            char c = source_[pos_++];
            if (c == '"') break;
            if (c == '\\' && pos_ < source_.size()) {
                char esc = source_[pos_++];
                switch (esc) {
                    case '"': value.string_value.push_back('"'); break;
                    case '\\': value.string_value.push_back('\\'); break;
                    case '/': value.string_value.push_back('/'); break;
                    case 'b': value.string_value.push_back('\b'); break;
                    case 'f': value.string_value.push_back('\f'); break;
                    case 'n': value.string_value.push_back('\n'); break;
                    case 'r': value.string_value.push_back('\r'); break;
                    case 't': value.string_value.push_back('\t'); break;
                    default: value.string_value.push_back(esc); break;
                }
            } else {
                value.string_value.push_back(c);
            }
        }

        return value;
    }

    JsonValue parse_bool() {
        JsonValue value;
        value.type = JsonValue::Type::Bool;
        if (source_.substr(pos_, 4) == "true") {
            value.bool_value = true;
            pos_ += 4;
        } else if (source_.substr(pos_, 5) == "false") {
            value.bool_value = false;
            pos_ += 5;
        }
        return value;
    }

    JsonValue parse_null() {
        JsonValue value;
        value.type = JsonValue::Type::Null;
        if (source_.substr(pos_, 4) == "null") {
            pos_ += 4;
        }
        return value;
    }

    JsonValue parse_number() {
        JsonValue value;
        value.type = JsonValue::Type::Number;
        std::size_t start = pos_;
        while (pos_ < source_.size()) {
            char c = source_[pos_];
            if (!(std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E')) break;
            ++pos_;
        }
        value.number_value = std::stod(std::string(source_.substr(start, pos_ - start)));
        return value;
    }

    void skip_ws() {
        while (pos_ < source_.size() && std::isspace(static_cast<unsigned char>(source_[pos_]))) {
            ++pos_;
        }
    }

    bool match(char expected) {
        if (pos_ < source_.size() && source_[pos_] == expected) {
            ++pos_;
            return true;
        }
        return false;
    }

    std::string_view source_;
    std::size_t pos_ = 0;
};

const JsonValue* get_object_value(const JsonValue& value, const std::string& key) {
    if (value.type != JsonValue::Type::Object) return nullptr;
    auto it = value.object_value.find(key);
    return it != value.object_value.end() ? &it->second : nullptr;
}

const JsonValue* get_array_value(const JsonValue& value, int index) {
    if (value.type != JsonValue::Type::Array || index < 0 || index >= static_cast<int>(value.array_value.size())) return nullptr;
    return &value.array_value[index];
}

int get_int(const JsonValue& value, const std::string& key, int fallback = 0) {
    const JsonValue* child = get_object_value(value, key);
    return (child && child->type == JsonValue::Type::Number) ? static_cast<int>(child->number_value) : fallback;
}

double get_number(const JsonValue& value, const std::string& key, double fallback = 0.0) {
    const JsonValue* child = get_object_value(value, key);
    return (child && child->type == JsonValue::Type::Number) ? child->number_value : fallback;
}

std::string get_string(const JsonValue& value, const std::string& key, const std::string& fallback = {}) {
    const JsonValue* child = get_object_value(value, key);
    return (child && child->type == JsonValue::Type::String) ? child->string_value : fallback;
}

std::vector<float> get_number_array(const JsonValue& value, const std::string& key) {
    std::vector<float> out;
    const JsonValue* child = get_object_value(value, key);
    if (!child || child->type != JsonValue::Type::Array) return out;
    out.reserve(child->array_value.size());
    for (const JsonValue& entry : child->array_value) {
        if (entry.type == JsonValue::Type::Number) {
            out.push_back(static_cast<float>(entry.number_value));
        }
    }
    return out;
}

bool starts_with(const std::string& text, const std::string& prefix) {
    return text.rfind(prefix, 0) == 0;
}

std::vector<std::uint8_t> decode_base64(std::string_view encoded) {
    static const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::array<int, 256> lut;
    lut.fill(-1);
    for (int i = 0; i < static_cast<int>(alphabet.size()); ++i) {
        lut[static_cast<unsigned char>(alphabet[i])] = i;
    }

    std::vector<std::uint8_t> out;
    int val = 0;
    int valb = -8;
    for (char c : encoded) {
        if (std::isspace(static_cast<unsigned char>(c))) continue;
        if (c == '=') break;
        int decoded = lut[static_cast<unsigned char>(c)];
        if (decoded < 0) continue;
        val = (val << 6) + decoded;
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<std::uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

struct Mat4 {
    std::array<float, 16> m{};

    static Mat4 identity() {
        Mat4 out;
        out.m = {1, 0, 0, 0,
                 0, 1, 0, 0,
                 0, 0, 1, 0,
                 0, 0, 0, 1};
        return out;
    }

    static Mat4 translation(const Vec3& t) {
        Mat4 out = identity();
        out.m[12] = t.x;
        out.m[13] = t.y;
        out.m[14] = t.z;
        return out;
    }

    static Mat4 scale(const Vec3& s) {
        Mat4 out = identity();
        out.m[0] = s.x;
        out.m[5] = s.y;
        out.m[10] = s.z;
        return out;
    }

    static Mat4 rotation_quaternion(float x, float y, float z, float w) {
        Mat4 out = identity();
        float xx = x * x, yy = y * y, zz = z * z;
        float xy = x * y, xz = x * z, yz = y * z;
        float wx = w * x, wy = w * y, wz = w * z;
        out.m[0] = 1.0f - 2.0f * (yy + zz);
        out.m[1] = 2.0f * (xy + wz);
        out.m[2] = 2.0f * (xz - wy);
        out.m[4] = 2.0f * (xy - wz);
        out.m[5] = 1.0f - 2.0f * (xx + zz);
        out.m[6] = 2.0f * (yz + wx);
        out.m[8] = 2.0f * (xz + wy);
        out.m[9] = 2.0f * (yz - wx);
        out.m[10] = 1.0f - 2.0f * (xx + yy);
        return out;
    }

    static Mat4 from_array(const std::vector<float>& values) {
        Mat4 out = identity();
        if (values.size() == 16) {
            for (int i = 0; i < 16; ++i) out.m[i] = values[i];
        }
        return out;
    }
};

Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 out = Mat4::identity();
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            out.m[c * 4 + r] =
                a.m[0 * 4 + r] * b.m[c * 4 + 0] +
                a.m[1 * 4 + r] * b.m[c * 4 + 1] +
                a.m[2 * 4 + r] * b.m[c * 4 + 2] +
                a.m[3 * 4 + r] * b.m[c * 4 + 3];
        }
    }
    return out;
}

Vec3 normalize_vec3(const Vec3& v) {
    float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len <= 1e-8f) return {0, 0, 0};
    return {v.x / len, v.y / len, v.z / len};
}

void decompose_trs(const Mat4& matrix, Vec3& position, Vec3& scale, Vec3& rotation_deg) {
    position = {matrix.m[12], matrix.m[13], matrix.m[14]};

    Vec3 col0 = {matrix.m[0], matrix.m[1], matrix.m[2]};
    Vec3 col1 = {matrix.m[4], matrix.m[5], matrix.m[6]};
    Vec3 col2 = {matrix.m[8], matrix.m[9], matrix.m[10]};

    auto length = [](const Vec3& v) {
        return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    };

    scale = {length(col0), length(col1), length(col2)};
    if (scale.x <= 1e-8f) scale.x = 1.0f;
    if (scale.y <= 1e-8f) scale.y = 1.0f;
    if (scale.z <= 1e-8f) scale.z = 1.0f;

    col0 = normalize_vec3(col0);
    col1 = normalize_vec3(col1);
    col2 = normalize_vec3(col2);

    float sy = std::clamp(col2.x, -1.0f, 1.0f);
    float ry = std::asin(sy);
    float cy = std::cos(ry);

    float rx = 0.0f;
    float rz = 0.0f;
    if (std::fabs(cy) > 1e-5f) {
        rx = std::atan2(-col2.y, col2.z);
        rz = std::atan2(-col1.x, col0.x);
    } else {
        rx = std::atan2(col1.z, col1.y);
        rz = 0.0f;
    }

    constexpr float radians_to_degrees = 57.2957795f;
    rotation_deg = {rx * radians_to_degrees, ry * radians_to_degrees, rz * radians_to_degrees};
}

class MinimalGltfFrontend {
public:
    explicit MinimalGltfFrontend(const std::string& filepath) : filepath_(filepath), base_dir_(std::filesystem::path(filepath).parent_path()) {
        scene_.camera.position = {0.0f, 0.0f, 4.0f};
        scene_.camera.look_at = {0.0f, 0.0f, 0.0f};
        scene_.camera.fov = 40.0f;
    }

    scene_ir::SceneIR parse() {
        if (!load_json()) return scene_;
        if (!load_buffers()) return scene_;
        parse_scene_nodes();
        return scene_;
    }

private:
    bool load_json() {
        if (!std::filesystem::exists(filepath_)) {
            std::cerr << "[GltfSceneFrontend] Error: File not found: " << filepath_ << std::endl;
            return false;
        }

        std::ifstream file(filepath_, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "[GltfSceneFrontend] Error: Could not open file " << filepath_ << std::endl;
            return false;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        root_ = JsonParser(buffer.str()).parse();
        return root_.type == JsonValue::Type::Object;
    }

    bool load_buffers() {
        const JsonValue* buffers = get_object_value(root_, "buffers");
        if (!buffers || buffers->type != JsonValue::Type::Array) return true;

        buffers_.resize(buffers->array_value.size());
        for (std::size_t i = 0; i < buffers->array_value.size(); ++i) {
            std::string uri = get_string(buffers->array_value[i], "uri");
            if (uri.empty()) continue;

            if (starts_with(uri, "data:")) {
                std::size_t comma = uri.find(',');
                if (comma == std::string::npos) return false;
                buffers_[i] = decode_base64(std::string_view(uri).substr(comma + 1));
                continue;
            }

            std::ifstream file(base_dir_ / uri, std::ios::binary);
            if (!file.is_open()) {
                std::cerr << "[GltfSceneFrontend] Error: Could not open buffer " << uri << std::endl;
                return false;
            }
            buffers_[i] = std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        }
        return true;
    }

    void parse_scene_nodes() {
        const JsonValue* scenes = get_object_value(root_, "scenes");
        if (!scenes || scenes->type != JsonValue::Type::Array || scenes->array_value.empty()) {
            const JsonValue* nodes = get_object_value(root_, "nodes");
            if (nodes && nodes->type == JsonValue::Type::Array) {
                for (int i = 0; i < static_cast<int>(nodes->array_value.size()); ++i) {
                    parse_node(i, Mat4::identity());
                }
            }
            return;
        }

        int default_scene = get_int(root_, "scene", 0);
        const JsonValue* scene_value = get_array_value(*scenes, default_scene);
        if (!scene_value) scene_value = &scenes->array_value.front();
        const JsonValue* node_indices = get_object_value(*scene_value, "nodes");
        if (!node_indices || node_indices->type != JsonValue::Type::Array) return;

        for (const JsonValue& node_index : node_indices->array_value) {
            if (node_index.type == JsonValue::Type::Number) {
                parse_node(static_cast<int>(node_index.number_value), Mat4::identity());
            }
        }
    }

    void parse_node(int node_index, const Mat4& parent_transform) {
        const JsonValue* nodes = get_object_value(root_, "nodes");
        const JsonValue* node = nodes ? get_array_value(*nodes, node_index) : nullptr;
        if (!node || node->type != JsonValue::Type::Object) return;

        Mat4 local = parse_node_transform(*node);
        Mat4 world = parent_transform * local;

        int mesh_index = get_int(*node, "mesh", -1);
        if (mesh_index >= 0) {
            instantiate_mesh(mesh_index, world, get_string(*node, "name", "gltf_node_" + std::to_string(node_index)));
        }

        const JsonValue* children = get_object_value(*node, "children");
        if (!children || children->type != JsonValue::Type::Array) return;
        for (const JsonValue& child : children->array_value) {
            if (child.type == JsonValue::Type::Number) {
                parse_node(static_cast<int>(child.number_value), world);
            }
        }
    }

    Mat4 parse_node_transform(const JsonValue& node) {
        std::vector<float> matrix_values = get_number_array(node, "matrix");
        if (matrix_values.size() == 16) {
            return Mat4::from_array(matrix_values);
        }

        Vec3 translation = {0, 0, 0};
        Vec3 scale = {1, 1, 1};
        auto translation_values = get_number_array(node, "translation");
        if (translation_values.size() >= 3) translation = {translation_values[0], translation_values[1], translation_values[2]};
        auto scale_values = get_number_array(node, "scale");
        if (scale_values.size() >= 3) scale = {scale_values[0], scale_values[1], scale_values[2]};

        Mat4 rotation = Mat4::identity();
        auto rotation_values = get_number_array(node, "rotation");
        if (rotation_values.size() >= 4) {
            rotation = Mat4::rotation_quaternion(rotation_values[0], rotation_values[1], rotation_values[2], rotation_values[3]);
        }

        return Mat4::translation(translation) * rotation * Mat4::scale(scale);
    }

    void instantiate_mesh(int mesh_index, const Mat4& world_transform, const std::string& node_name) {
        const JsonValue* meshes = get_object_value(root_, "meshes");
        const JsonValue* mesh = meshes ? get_array_value(*meshes, mesh_index) : nullptr;
        if (!mesh || mesh->type != JsonValue::Type::Object) return;

        const JsonValue* primitives = get_object_value(*mesh, "primitives");
        if (!primitives || primitives->type != JsonValue::Type::Array) return;

        for (int primitive_index = 0; primitive_index < static_cast<int>(primitives->array_value.size()); ++primitive_index) {
            const JsonValue& primitive = primitives->array_value[primitive_index];
            int mode = get_int(primitive, "mode", 4);
            if (mode != 4) continue;

            auto mesh_data = build_mesh_from_primitive(primitive);
            if (!mesh_data) continue;

            std::string mesh_name = get_string(*mesh, "name", node_name);
            if (primitives->array_value.size() > 1) {
                mesh_name += "_prim_" + std::to_string(primitive_index);
            }

            auto mesh_resource = scene_.register_mesh(mesh_name, mesh_data);
            scene_ir::InstanceNode instance;
            instance.name = mesh_name;
            instance.mesh = mesh_resource;
            instance.material = get_material(get_int(primitive, "material", -1));
            decompose_trs(world_transform, instance.position, instance.scale, instance.rotation);
            scene_.instances.push_back(instance);
        }
    }

    std::shared_ptr<Mesh> build_mesh_from_primitive(const JsonValue& primitive) {
        const JsonValue* attributes = get_object_value(primitive, "attributes");
        if (!attributes || attributes->type != JsonValue::Type::Object) return nullptr;

        const JsonValue* position_accessor = get_object_value(*attributes, "POSITION");
        if (!position_accessor || position_accessor->type != JsonValue::Type::Number) return nullptr;

        std::vector<float> positions = read_accessor_floats(static_cast<int>(position_accessor->number_value), 3);
        if (positions.empty()) return nullptr;

        std::vector<float> normals;
        if (const JsonValue* normal_accessor = get_object_value(*attributes, "NORMAL")) {
            if (normal_accessor->type == JsonValue::Type::Number) {
                normals = read_accessor_floats(static_cast<int>(normal_accessor->number_value), 3);
            }
        }

        std::vector<float> uvs;
        if (const JsonValue* uv_accessor = get_object_value(*attributes, "TEXCOORD_0")) {
            if (uv_accessor->type == JsonValue::Type::Number) {
                uvs = read_accessor_floats(static_cast<int>(uv_accessor->number_value), 2);
            }
        }

        std::vector<int> indices = read_indices(get_int(primitive, "indices", -1));
        if (indices.empty()) {
            indices.resize(positions.size() / 3);
            for (int i = 0; i < static_cast<int>(indices.size()); ++i) indices[i] = i;
        }

        auto mesh = std::make_shared<Mesh>();
        int vertex_count = static_cast<int>(positions.size() / 3);
        mesh->vertices.resize(vertex_count);
        for (int i = 0; i < vertex_count; ++i) {
            mesh->vertices[i].position = {positions[i * 3 + 0], positions[i * 3 + 1], positions[i * 3 + 2]};
            mesh->vertices[i].normal = normals.size() >= static_cast<std::size_t>((i + 1) * 3)
                ? Vec3{normals[i * 3 + 0], normals[i * 3 + 1], normals[i * 3 + 2]}
                : Vec3{0.0f, 0.0f, 1.0f};
            mesh->vertices[i].uv = uvs.size() >= static_cast<std::size_t>((i + 1) * 2)
                ? Vec2{uvs[i * 2 + 0], uvs[i * 2 + 1]}
                : Vec2{0.0f, 0.0f};
        }
        mesh->indices = std::move(indices);
        return mesh;
    }

    std::vector<float> read_accessor_floats(int accessor_index, int expected_components) {
        std::vector<float> out;
        const JsonValue* accessors = get_object_value(root_, "accessors");
        const JsonValue* buffer_views = get_object_value(root_, "bufferViews");
        const JsonValue* accessor = accessors ? get_array_value(*accessors, accessor_index) : nullptr;
        if (!accessors || !buffer_views || !accessor || accessor->type != JsonValue::Type::Object) return out;

        int buffer_view_index = get_int(*accessor, "bufferView", -1);
        const JsonValue* buffer_view = get_array_value(*buffer_views, buffer_view_index);
        if (!buffer_view || buffer_view->type != JsonValue::Type::Object) return out;

        int buffer_index = get_int(*buffer_view, "buffer", -1);
        if (buffer_index < 0 || buffer_index >= static_cast<int>(buffers_.size())) return out;
        const std::vector<std::uint8_t>& buffer = buffers_[buffer_index];

        int component_type = get_int(*accessor, "componentType", 5126);
        int count = get_int(*accessor, "count", 0);
        std::string type = get_string(*accessor, "type", "SCALAR");
        int component_count = (type == "VEC2") ? 2 : (type == "VEC3") ? 3 : (type == "VEC4") ? 4 : 1;
        if (component_count != expected_components || count <= 0) return out;

        std::size_t component_size = (component_type == 5126 || component_type == 5125) ? 4 : (component_type == 5123 || component_type == 5122) ? 2 : 1;
        std::size_t byte_offset = static_cast<std::size_t>(get_int(*buffer_view, "byteOffset", 0) + get_int(*accessor, "byteOffset", 0));
        std::size_t stride = static_cast<std::size_t>(get_int(*buffer_view, "byteStride", static_cast<int>(component_size * component_count)));
        out.resize(static_cast<std::size_t>(count) * component_count, 0.0f);

        for (int i = 0; i < count; ++i) {
            const std::uint8_t* ptr = buffer.data() + byte_offset + stride * i;
            for (int c = 0; c < component_count; ++c) {
                const std::uint8_t* component_ptr = ptr + c * component_size;
                float value = 0.0f;
                if (component_type == 5126) {
                    std::memcpy(&value, component_ptr, sizeof(float));
                } else if (component_type == 5125) {
                    std::uint32_t temp = 0;
                    std::memcpy(&temp, component_ptr, sizeof(temp));
                    value = static_cast<float>(temp);
                } else if (component_type == 5123) {
                    std::uint16_t temp = 0;
                    std::memcpy(&temp, component_ptr, sizeof(temp));
                    value = static_cast<float>(temp);
                } else if (component_type == 5121) {
                    value = static_cast<float>(*component_ptr);
                }
                out[i * component_count + c] = value;
            }
        }

        return out;
    }

    std::vector<int> read_indices(int accessor_index) {
        std::vector<int> out;
        if (accessor_index < 0) return out;

        const JsonValue* accessors = get_object_value(root_, "accessors");
        const JsonValue* buffer_views = get_object_value(root_, "bufferViews");
        const JsonValue* accessor = accessors ? get_array_value(*accessors, accessor_index) : nullptr;
        if (!accessors || !buffer_views || !accessor || accessor->type != JsonValue::Type::Object) return out;

        int buffer_view_index = get_int(*accessor, "bufferView", -1);
        const JsonValue* buffer_view = get_array_value(*buffer_views, buffer_view_index);
        if (!buffer_view || buffer_view->type != JsonValue::Type::Object) return out;

        int buffer_index = get_int(*buffer_view, "buffer", -1);
        if (buffer_index < 0 || buffer_index >= static_cast<int>(buffers_.size())) return out;
        const std::vector<std::uint8_t>& buffer = buffers_[buffer_index];

        int component_type = get_int(*accessor, "componentType", 5123);
        int count = get_int(*accessor, "count", 0);
        std::size_t component_size = (component_type == 5125) ? 4 : (component_type == 5123) ? 2 : 1;
        std::size_t byte_offset = static_cast<std::size_t>(get_int(*buffer_view, "byteOffset", 0) + get_int(*accessor, "byteOffset", 0));
        out.resize(count, 0);

        for (int i = 0; i < count; ++i) {
            const std::uint8_t* ptr = buffer.data() + byte_offset + static_cast<std::size_t>(i) * component_size;
            if (component_type == 5125) {
                std::uint32_t temp = 0;
                std::memcpy(&temp, ptr, sizeof(temp));
                out[i] = static_cast<int>(temp);
            } else if (component_type == 5123) {
                std::uint16_t temp = 0;
                std::memcpy(&temp, ptr, sizeof(temp));
                out[i] = static_cast<int>(temp);
            } else {
                out[i] = static_cast<int>(*ptr);
            }
        }
        return out;
    }

    std::shared_ptr<scene_ir::TextureResource> get_texture(int texture_index, scene_ir::ImageColorSpace color_space, const std::string& usage_name) {
        auto key = std::make_pair(texture_index, static_cast<int>(color_space));
        auto existing = texture_cache_.find(key);
        if (existing != texture_cache_.end()) return existing->second;

        const JsonValue* textures = get_object_value(root_, "textures");
        const JsonValue* texture = textures ? get_array_value(*textures, texture_index) : nullptr;
        if (!texture || texture->type != JsonValue::Type::Object) return nullptr;

        int image_index = get_int(*texture, "source", -1);
        if (image_index < 0) return nullptr;

        std::string texture_name = usage_name + "_" + std::to_string(texture_index);
        auto image = get_image(image_index, color_space, texture_name);
        if (!image) return nullptr;

        auto texture_resource = scene_.register_texture(texture_name, image, get_int(*texture, "texCoord", 0));
        texture_cache_[key] = texture_resource;
        return texture_resource;
    }

    std::shared_ptr<scene_ir::ImageResource> get_image(int image_index, scene_ir::ImageColorSpace color_space, const std::string& usage_name) {
        auto key = std::make_pair(image_index, static_cast<int>(color_space));
        auto existing = image_cache_.find(key);
        if (existing != image_cache_.end()) return existing->second;

        const JsonValue* images = get_object_value(root_, "images");
        const JsonValue* image = images ? get_array_value(*images, image_index) : nullptr;
        if (!image || image->type != JsonValue::Type::Object) return nullptr;

        std::string uri = get_string(*image, "uri");
        if (uri.empty() || starts_with(uri, "data:")) {
            std::cerr << "[GltfSceneFrontend] Warning: Only external image URIs are supported right now." << std::endl;
            return nullptr;
        }

        std::filesystem::path image_path = uri;
        if (image_path.is_relative()) {
            image_path = base_dir_ / image_path;
        }

        auto resource = scene_.register_image(usage_name + "_image", image_path.lexically_normal().string(), color_space);
        image_cache_[key] = resource;
        return resource;
    }

    std::shared_ptr<scene_ir::MaterialNode> get_material(int material_index) {
        if (material_index < 0) return nullptr;

        auto existing = material_cache_.find(material_index);
        if (existing != material_cache_.end()) return existing->second;

        const JsonValue* materials = get_object_value(root_, "materials");
        const JsonValue* material = materials ? get_array_value(*materials, material_index) : nullptr;
        if (!material || material->type != JsonValue::Type::Object) return nullptr;

        auto node = std::make_shared<scene_ir::MaterialNode>();
        node->name = get_string(*material, "name", "gltf_material_" + std::to_string(material_index));
        node->model = scene_ir::MaterialModel::Lambertian;

        if (const JsonValue* pbr = get_object_value(*material, "pbrMetallicRoughness")) {
            std::vector<float> base_color_factor = get_number_array(*pbr, "baseColorFactor");
            if (base_color_factor.size() >= 3) {
                node->base_color = {base_color_factor[0], base_color_factor[1], base_color_factor[2]};
            }

            float roughness_factor = static_cast<float>(get_number(*pbr, "roughnessFactor", 1.0));
            float metallic_factor = static_cast<float>(get_number(*pbr, "metallicFactor", 0.0));
            node->roughness = roughness_factor;
            if (metallic_factor > 0.5f) {
                node->model = scene_ir::MaterialModel::Metal;
            }

            if (const JsonValue* base_color_texture = get_object_value(*pbr, "baseColorTexture")) {
                int texture_index = get_int(*base_color_texture, "index", -1);
                node->base_color_texture = get_texture(texture_index, scene_ir::ImageColorSpace::SRGB, node->name + "_base");
            }

            if (const JsonValue* metallic_roughness_texture = get_object_value(*pbr, "metallicRoughnessTexture")) {
                int texture_index = get_int(*metallic_roughness_texture, "index", -1);
                node->roughness_texture = get_texture(texture_index, scene_ir::ImageColorSpace::Linear, node->name + "_roughness");
            }
        }

        std::vector<float> emissive_factor = get_number_array(*material, "emissiveFactor");
        if (emissive_factor.size() >= 3) {
            node->emission = {emissive_factor[0], emissive_factor[1], emissive_factor[2]};
            if ((node->emission.x + node->emission.y + node->emission.z) > 0.0f) {
                node->model = scene_ir::MaterialModel::Light;
            }
        }

        if (const JsonValue* emissive_texture = get_object_value(*material, "emissiveTexture")) {
            int texture_index = get_int(*emissive_texture, "index", -1);
            node->emission_texture = get_texture(texture_index, scene_ir::ImageColorSpace::SRGB, node->name + "_emissive");
            node->model = scene_ir::MaterialModel::Light;
            if (node->emission.x == 0.0f && node->emission.y == 0.0f && node->emission.z == 0.0f) {
                node->emission = {1.0f, 1.0f, 1.0f};
            }
        }

        scene_.add_material(node);
        material_cache_[material_index] = node;
        return node;
    }

    std::string filepath_;
    std::filesystem::path base_dir_;
    JsonValue root_;
    scene_ir::SceneIR scene_;
    std::vector<std::vector<std::uint8_t>> buffers_;
    std::map<int, std::shared_ptr<scene_ir::MaterialNode>> material_cache_;
    std::map<std::pair<int, int>, std::shared_ptr<scene_ir::ImageResource>> image_cache_;
    std::map<std::pair<int, int>, std::shared_ptr<scene_ir::TextureResource>> texture_cache_;
};

}

scene_ir::SceneIR GltfSceneFrontend::parse_file_to_ir(const std::string& filepath) {
    return MinimalGltfFrontend(filepath).parse();
}

} // namespace ure
