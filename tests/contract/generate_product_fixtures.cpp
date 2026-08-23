#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>

#include <ure/native_adapter.hpp>
#include <ure/native_scene_ir.hpp>

namespace {

struct FixtureSpec {
    std::string_view name;
    std::uint32_t width;
    std::uint32_t height;
};

constexpr std::array kFixtures{
    FixtureSpec{"cornell_854x480", 854, 480},
    FixtureSpec{"cornell_1280x720", 1280, 720},
    FixtureSpec{"cornell_1920x1080", 1920, 1080},
};

}

int main(int argc, char **argv) {
    try {
        if (argc != 3)
            throw std::invalid_argument(
                "usage: generate_product_fixtures <cornell.gltf> <output-directory>");
        const auto imported =
            ure::native_scene::import_gltf_native(std::filesystem::absolute(argv[1]));
        if (!imported.ok())
            throw std::runtime_error(imported.diagnostics.empty()
                                         ? "Cornell import failed"
                                         : imported.diagnostics.front().message);
        const auto output_directory = std::filesystem::absolute(argv[2]);
        std::filesystem::create_directories(output_directory);
        for (const auto &spec : kFixtures) {
            auto archive = imported.archive;
            archive.document.id = "scene/preview/" + std::string(spec.name);
            archive.scene.width = static_cast<int>(spec.width);
            archive.scene.height = static_cast<int>(spec.height);
            archive.scene.spp = 0;
            archive.scene.camera.aspect_ratio =
                static_cast<float>(spec.width) / static_cast<float>(spec.height);
            archive.canonical_camera =
                ure::native_scene::canonical_camera_from_scene(archive.scene.camera);
            ure::native_scene::assign_deterministic_object_uuids(archive);
            const auto path = output_directory /
                              (std::string(spec.name) + ".urescene");
            ure::native_scene::save_native_scene(path, archive);
            std::cout << path.string() << '\n';
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
