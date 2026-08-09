#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

#include "generator.hpp"
#include "registry.hpp"

namespace {

std::unordered_map<std::string, std::filesystem::path> parse_arguments(int argc, char** argv) {
    std::unordered_map<std::string, std::filesystem::path> result;
    for (int index = 2; index < argc; index += 2) {
        if (index + 1 >= argc || std::string_view(argv[index]).substr(0, 2) != "--") {
            throw std::runtime_error("Expected --name value arguments");
        }
        const std::string name(std::string_view(argv[index]).substr(2));
        if (!result.emplace(name, argv[index + 1]).second) throw std::runtime_error("Duplicate argument --" + name);
    }
    return result;
}

const std::filesystem::path& required(
    const std::unordered_map<std::string, std::filesystem::path>& arguments,
    std::string_view name) {
    const auto found = arguments.find(std::string(name));
    if (found == arguments.end()) throw std::runtime_error("Missing --" + std::string(name));
    return found->second;
}

}

int main(int argc, char** argv) {
    try {
        if (argc < 2) throw std::runtime_error("Usage: ure_contract_codegen lint|generate|compare [arguments]");
        const std::string mode = argv[1];
        const auto arguments = parse_arguments(argc, argv);
        const auto registry = ure::contract_codegen::load_registry(required(arguments, "registry"));
        ure::contract_codegen::validate_compatibility(required(arguments, "compatibility"), registry);
        ure::contract_codegen::validate_schemas(required(arguments, "schemas"));
        if (mode == "lint") {
            std::cout << registry.digest_hex << '\n';
        } else if (mode == "generate") {
            ure::contract_codegen::generate_contract_package(registry, required(arguments, "schemas"), required(arguments, "output"));
            std::cout << registry.digest_hex << '\n';
        } else if (mode == "compare") {
            ure::contract_codegen::compare_contract_package(registry, required(arguments, "schemas"), required(arguments, "expected"));
            std::cout << registry.digest_hex << '\n';
        } else {
            throw std::runtime_error("Unknown mode " + mode);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ure_contract_codegen: " << error.what() << '\n';
        return 1;
    }
}
