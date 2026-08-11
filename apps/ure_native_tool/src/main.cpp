#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <ure/config.hpp>
#include <ure/log.hpp>
#include <ure/native_adapter.hpp>
#include <ure/native_scene_tooling.hpp>

namespace {

std::string lowercase(std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool check_scene_path(const std::string &scene_path) {
    if (scene_path.empty() || !std::filesystem::exists(scene_path)) {
        std::cerr << "Native tooling input is unavailable: " << scene_path
                  << '\n';
        return false;
    }
    return true;
}

int cmd_info(const std::string &scene_path) {
    if (!check_scene_path(scene_path))
        return 1;
    try {
        const auto imported = ure::native_scene::import_gltf_native(scene_path);
        if (!imported.ok())
            throw std::runtime_error(imported.diagnostics.empty()
                                         ? "adapter import failed"
                                         : imported.diagnostics.front().message);
        const auto &scene = imported.archive.scene;
        std::cout << "Scene: " << scene_path << '\n'
                  << "  Meshes:     " << scene.meshes.size() << '\n'
                  << "  Materials:  " << scene.materials.size() << '\n'
                  << "  Instances:  " << scene.instances.size() << '\n'
                  << "  Spheres:    " << scene.spheres.size() << '\n'
                  << "  Textures:   " << scene.textures.size() << '\n'
                  << "  Images:     " << scene.images.size() << '\n'
                  << "  Width:      " << scene.width << '\n'
                  << "  Height:     " << scene.height << '\n'
                  << "  SPP:        " << scene.spp << '\n'
                  << "  Physics:    "
                  << (scene.physics.enabled ? "enabled" : "disabled") << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Scene inspection failed: " << error.what() << '\n';
        return 1;
    }
}

int cmd_validate(const std::string &scene_path) {
    if (!check_scene_path(scene_path))
        return 1;
    try {
        const std::string extension =
            lowercase(std::filesystem::path(scene_path).extension().string());
        if (extension == ".ure" || extension == ".urescene" ||
            extension == ".urepkg") {
            const auto inspection =
                ure::native_scene::inspect_native_asset(scene_path);
            for (const auto &diagnostic : inspection.diagnostics) {
                std::ostream &stream =
                    diagnostic.severity ==
                            ure::native_scene::DiagnosticSeverity::Error
                        ? std::cerr
                        : std::cout;
                stream << diagnostic.code << " [" << diagnostic.path
                       << "]: " << diagnostic.message << '\n';
            }
            if (inspection.ok())
                std::cout << "Valid: " << scene_path << '\n';
            return inspection.ok() ? 0 : 1;
        }
        const auto imported = ure::native_scene::import_gltf_native(scene_path);
        if (!imported.ok())
            throw std::runtime_error(imported.diagnostics.empty()
                                         ? "adapter import failed"
                                         : imported.diagnostics.front().message);
        std::cout << "Valid: " << scene_path << '\n'
                  << ure::native_scene::write_adapter_loss_report(
                         imported.loss_report);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Validation failed: " << error.what() << '\n';
        return 1;
    }
}

int cmd_native_tool(const ure::config::CliResult &cli) {
    try {
        switch (cli.command) {
        case ure::config::CliCommand::Build:
            ure::native_scene::build_native_scene(cli.scene_path,
                                                  cli.output_path);
            break;
        case ure::config::CliCommand::Pack: {
            std::vector<std::filesystem::path> inputs;
            inputs.reserve(cli.input_paths.size());
            for (const auto &input : cli.input_paths)
                inputs.emplace_back(input);
            ure::native_scene::pack_native_scenes(cli.output_path, inputs);
            break;
        }
        case ure::config::CliCommand::Unpack:
            ure::native_scene::unpack_native_package(cli.scene_path,
                                                     cli.output_path);
            break;
        case ure::config::CliCommand::Migrate:
            ure::native_scene::migrate_native_scene(cli.scene_path,
                                                    cli.output_path);
            break;
        case ure::config::CliCommand::Inspect: {
            const auto inspection =
                ure::native_scene::inspect_native_asset(cli.scene_path);
            std::cout << "ID: " << inspection.id << '\n'
                      << "Kind: "
                      << (inspection.kind ==
                                  ure::native_scene::ContainerKind::Package
                              ? "package"
                              : "scene")
                      << '\n'
                      << "Version: " << inspection.version.major << '.'
                      << inspection.version.minor << '\n'
                      << "Semantic hash: " << inspection.semantic_hash << '\n'
                      << "Scenes: " << inspection.scene_count << '\n'
                      << "Resources: " << inspection.resource_count << '\n'
                      << "Stored bytes: " << inspection.stored_bytes << '\n'
                      << "Resident bytes: " << inspection.resident_bytes << '\n';
            return inspection.ok() ? 0 : 1;
        }
        case ure::config::CliCommand::Export:
            ure::native_scene::export_native_scene_usda(
                cli.scene_path, cli.output_path,
                cli.allow_lossy
                    ? ure::native_scene::UsdExportPolicy::AllowDocumentedLoss
                    : ure::native_scene::UsdExportPolicy::Strict,
                cli.loss_report_path, cli.scene_id);
            break;
        default:
            return 1;
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Native tooling failed: " << error.what() << '\n';
        return 1;
    }
}

}

int main(int argc, char **argv) {
    const auto cli = ure::config::parse_cli(argc, argv);
    ure::log::set_min_level(cli.verbose   ? ure::log::Level::Debug
                            : cli.quiet ? ure::log::Level::Error
                                        : ure::log::Level::Info);
    switch (cli.command) {
    case ure::config::CliCommand::Info:
        return cmd_info(cli.scene_path);
    case ure::config::CliCommand::Validate:
        return cmd_validate(cli.scene_path);
    case ure::config::CliCommand::Build:
    case ure::config::CliCommand::Pack:
    case ure::config::CliCommand::Unpack:
    case ure::config::CliCommand::Inspect:
    case ure::config::CliCommand::Migrate:
    case ure::config::CliCommand::Export:
        return cmd_native_tool(cli);
    case ure::config::CliCommand::Render:
    case ure::config::CliCommand::ListDevices:
        std::cerr << "This command is not a native format tooling operation\n";
        return 3;
    }
    return 3;
}
