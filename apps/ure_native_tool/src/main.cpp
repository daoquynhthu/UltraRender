#include <exception>
#include <filesystem>
#include <iostream>

#include <ure/config.hpp>
#include <ure/log.hpp>
#include <ure/native_adapter.hpp>
#include <ure/native_scene_tooling.hpp>

namespace {

int cmd_native_tool(const ure::config::CliResult &cli) {
    try {
        switch (cli.command) {
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
    case ure::config::CliCommand::Export:
        return cmd_native_tool(cli);
    case ure::config::CliCommand::Info:
    case ure::config::CliCommand::Validate:
    case ure::config::CliCommand::Build:
    case ure::config::CliCommand::Pack:
    case ure::config::CliCommand::Unpack:
    case ure::config::CliCommand::Inspect:
    case ure::config::CliCommand::Migrate:
    case ure::config::CliCommand::Realize:
    case ure::config::CliCommand::Render:
    case ure::config::CliCommand::ListDevices:
        std::cerr << "Only adapter export is owned by ultrarender_native_tool\n";
        return 3;
    }
    return 3;
}
