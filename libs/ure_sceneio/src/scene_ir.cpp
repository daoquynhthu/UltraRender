#include "ure/scene_ir.hpp"

namespace ure::scene_ir {

std::shared_ptr<MaterialNode> SceneIR::find_material(const std::string& name) const {
    for (const auto& material : materials) {
        if (material && material->name == name) {
            return material;
        }
    }
    return nullptr;
}

void SceneIR::add_material(const std::shared_ptr<MaterialNode>& material) {
    if (!material) return;

    for (auto& existing : materials) {
        if (existing && existing->name == material->name) {
            existing = material;
            return;
        }
    }

    materials.push_back(material);
}

std::shared_ptr<MeshResource> SceneIR::register_mesh(const std::string& name, const std::shared_ptr<Mesh>& mesh) {
    if (!mesh) return nullptr;

    for (const auto& existing : meshes) {
        if (existing && existing->mesh.get() == mesh.get()) {
            return existing;
        }
    }

    auto resource = std::make_shared<MeshResource>();
    resource->name = name;
    resource->mesh = mesh;
    meshes.push_back(resource);
    return resource;
}

std::shared_ptr<ImageResource> SceneIR::find_image(const std::string& name) const {
    for (const auto& image : images) {
        if (image && image->name == name) {
            return image;
        }
    }
    return nullptr;
}

std::shared_ptr<ImageResource> SceneIR::register_image(const std::string& name,
                                                       const std::string& uri,
                                                       ImageColorSpace color_space) {
    for (auto& existing : images) {
        if (existing && existing->name == name) {
            existing->uri = uri;
            existing->color_space = color_space;
            return existing;
        }
    }

    auto image = std::make_shared<ImageResource>();
    image->name = name;
    image->uri = uri;
    image->color_space = color_space;
    images.push_back(image);
    return image;
}

std::shared_ptr<TextureResource> SceneIR::register_texture(const std::string& name,
                                                           const std::shared_ptr<ImageResource>& image,
                                                           int uv_set) {
    if (!image) return nullptr;

    for (auto& existing : textures) {
        if (existing && existing->name == name) {
            existing->image = image;
            existing->uv_set = uv_set;
            return existing;
        }
    }

    auto texture = std::make_shared<TextureResource>();
    texture->name = name;
    texture->image = image;
    texture->uv_set = uv_set;
    textures.push_back(texture);
    return texture;
}

} // namespace ure::scene_ir
