#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>

#include <ure/client/client.hpp>
#include <ure/config.hpp>
#include <ultrarender/ure_registry.h>

namespace {

std::atomic_bool cancel_requested{};

BOOL WINAPI console_control(DWORD event) {
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT ||
        event == CTRL_CLOSE_EVENT) {
        cancel_requested.store(true, std::memory_order_relaxed);
        return TRUE;
    }
    return FALSE;
}

std::filesystem::path executable_directory() {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length == path.size())
        throw std::runtime_error("executable path is unavailable");
    path.resize(length);
    return std::filesystem::path(path).parent_path();
}

std::filesystem::path resolve_component(const std::string &value,
                                        const std::filesystem::path &fallback,
                                        const std::filesystem::path &directory) {
    const std::filesystem::path path = value.empty() ? fallback : value;
    return std::filesystem::absolute(path.is_absolute() ? path
                                                        : directory / path);
}

std::wstring quote_argument(const std::wstring &value) {
    std::wstring output(1, L'"');
    std::size_t backslashes{};
    for (const wchar_t character : value) {
        if (character == L'\\') {
            ++backslashes;
        } else if (character == L'"') {
            output.append(backslashes * 2 + 1, L'\\');
            output.push_back(character);
            backslashes = 0;
        } else {
            output.append(backslashes, L'\\');
            backslashes = 0;
            output.push_back(character);
        }
    }
    output.append(backslashes * 2, L'\\');
    output.push_back(L'"');
    return output;
}

int cmd_native_tool(int argc, char **argv) {
    const auto tool = executable_directory() / "ultrarender_native_tool.exe";
    if (!std::filesystem::is_regular_file(tool)) {
        std::cerr << "ure_cli: native tooling component is unavailable\n";
        return 3;
    }
    std::wstring command = quote_argument(tool.wstring());
    for (int index = 1; index < argc; ++index) {
        command.push_back(L' ');
        command += quote_argument(std::filesystem::path(argv[index]).wstring());
    }
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(tool.c_str(), command.data(), nullptr, nullptr, TRUE, 0,
                        nullptr, tool.parent_path().c_str(), &startup,
                        &process)) {
        std::cerr << "ure_cli: native tooling component could not start\n";
        return 3;
    }
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code{};
    const bool inspected = GetExitCodeProcess(process.hProcess, &exit_code) != 0;
    CloseHandle(process.hProcess);
    return inspected ? static_cast<int>(exit_code) : 3;
}

bool allowed_render_arguments(int argc, char **argv, std::string &error) {
    int render_index = -1;
    for (int index = 1; index < argc; ++index) {
        if (std::string_view(argv[index]) == "render") {
            render_index = index;
            break;
        }
    }
    if (render_index < 0)
        return true;
    bool scene_seen = false;
    for (int index = render_index + 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "-q" || argument == "--quiet" || argument == "-v" ||
            argument == "--verbose")
            continue;
        const bool option = argument == "--transport" ||
                            argument == "--runtime" || argument == "--worker" ||
                            argument == "--spp" ||
                            argument == "--cancel-after-ms";
        if (option) {
            if (++index >= argc) {
                error = "missing value for " + std::string(argument);
                return false;
            }
            continue;
        }
        if (argument.starts_with("--transport=") ||
            argument.starts_with("--runtime=") ||
            argument.starts_with("--worker=") ||
            argument.starts_with("--spp=") ||
            argument.starts_with("--cancel-after-ms="))
            continue;
        if (argument.starts_with('-')) {
            error = "render option is not executable through ProductJob 0.1: " +
                    std::string(argument);
            return false;
        }
        if (scene_seen) {
            error = "render accepts exactly one native scene input";
            return false;
        }
        scene_seen = true;
    }
    return true;
}

ure::client::SceneFormat scene_format(const std::filesystem::path &path) {
    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(),
                           [](unsigned char value) {
                               return static_cast<char>(std::tolower(value));
                           });
    if (extension == ".ure")
        return ure::client::SceneFormat::Ure;
    if (extension == ".urescene")
        return ure::client::SceneFormat::UreScene;
    if (extension == ".urepkg")
        return ure::client::SceneFormat::UrePackage;
    throw std::runtime_error(
        "ProductJob 0.1 accepts only .ure, .urescene, or .urepkg inputs");
}

std::string digest_hex(std::span<const std::uint8_t, 32> digest) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const std::uint8_t value : digest)
        output << std::setw(2) << static_cast<unsigned>(value);
    return output.str();
}

int render(const ure::config::CliResult &cli) {
    if (cli.transport != "worker" && cli.transport != "direct")
        throw std::runtime_error("--transport must be worker or direct");
    if (cli.config.renderer.spp <= 0)
        throw std::runtime_error("--spp must be positive");
    const auto directory = executable_directory();
    ure::client::ConnectionOptions options;
    options.transport = cli.transport == "direct"
                            ? ure::client::TransportMode::Direct
                            : ure::client::TransportMode::Worker;
    options.runtime_path = resolve_component(
        cli.runtime_path, "ultrarender_runtime_1.dll", directory);
    options.worker_path = resolve_component(
        cli.worker_path, "ultrarender_worker_1.exe", directory);
    ure::client::SceneInput scene;
    scene.path = std::filesystem::absolute(cli.scene_path);
    scene.format = scene_format(scene.path);
    ure::client::Objective objective;
    objective.output_semantics = {URE_FRAME_PLANE_COLOR};
    objective.sample_budget =
        static_cast<std::uint64_t>(cli.config.renderer.spp);
    auto client = ure::client::Client::connect(options);
    auto job = client.create_job(scene, objective);
    job.start();
    if (cli.cancel_after_ms != 0) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(cli.cancel_after_ms));
        job.request_cancel();
    }
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::minutes(10);
    while (!job.wait(std::chrono::milliseconds(100))) {
        if (cancel_requested.exchange(false, std::memory_order_relaxed))
            job.request_cancel();
        if (std::chrono::steady_clock::now() >= deadline)
            throw std::runtime_error("product job exceeded the CLI wait bound");
    }
    const auto result = job.result();
    if (!cli.quiet) {
        std::cout << "transport=" << cli.transport << '\n'
                  << "state=succeeded\n"
                  << "accepted_samples=" << result.info.accepted_samples << '\n'
                  << "frame=" << result.frame.width << 'x'
                  << result.frame.height << '\n'
                  << "frame_bytes="
                  << result.frame.planes.front().bytes.size() << '\n'
                  << "build_identity="
                  << digest_hex(result.info.identities.build) << '\n'
                  << "snapshot_identity="
                  << digest_hex(result.info.identities.snapshot) << '\n'
                  << "objective_identity="
                  << digest_hex(result.info.identities.objective) << '\n'
                  << "plan_identity="
                  << digest_hex(result.info.identities.plan) << '\n'
                  << "frame_content_identity="
                  << digest_hex(result.artifact.frame_content_identity) << '\n';
    }
    return 0;
}

}

int main(int argc, char **argv) {
    try {
        std::string argument_error;
        if (!allowed_render_arguments(argc, argv, argument_error)) {
            std::cerr << "ure_cli: " << argument_error << '\n';
            return 2;
        }
        SetConsoleCtrlHandler(console_control, TRUE);
        const auto cli = ure::config::parse_cli(argc, argv);
        if (cli.command != ure::config::CliCommand::Render)
            return cmd_native_tool(argc, argv);
        return render(cli);
    } catch (const ure::client::Error &error) {
        std::cerr << "ure_cli: " << error.what() << " (result="
                  << error.info().result << ", domain=" << error.info().domain
                  << ", detail=" << error.info().detail << ")\n";
        return error.info().result == URE_RESULT_CANCELED ? 130 : 1;
    } catch (const std::exception &error) {
        std::cerr << "ure_cli: " << error.what() << '\n';
        return 1;
    }
}
