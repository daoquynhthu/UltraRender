#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include <ure/client/client.hpp>

namespace {

void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

ure::client::Client connect(ure::client::TransportMode mode,
                            const std::filesystem::path &runtime,
                            const std::filesystem::path &worker) {
    ure::client::ConnectionOptions options;
    options.transport = mode;
    options.runtime_path = runtime;
    options.worker_path = worker;
    return ure::client::Client::connect(options);
}

bool nonzero(const std::array<std::uint8_t, 32> &identity) {
    return std::ranges::any_of(identity,
                               [](std::uint8_t value) { return value != 0; });
}

bool has_report_identity(const std::string &report) {
    const std::string marker = "\"adapter_loss_report_identity\": \"";
    const auto begin = report.find(marker);
    if (begin == std::string::npos)
        return false;
    const auto identity_begin = begin + marker.size();
    const auto identity_end = report.find('"', identity_begin);
    return identity_end == identity_begin + 64 &&
           std::ranges::all_of(
               report.begin() + static_cast<std::ptrdiff_t>(identity_begin),
               report.begin() + static_cast<std::ptrdiff_t>(identity_end),
               [](const char value) {
                   return (value >= '0' && value <= '9') ||
                          (value >= 'a' && value <= 'f');
               });
}

void require_equivalent(const ure::client::SceneToolResult &left,
                        const ure::client::SceneToolResult &right,
                        const char *message) {
    require(left.snapshot_identity == right.snapshot_identity &&
                left.semantic_identity == right.semantic_identity &&
                left.material_program_set_identity ==
                    right.material_program_set_identity &&
                left.disposition_count == right.disposition_count &&
                left.diagnostic_count == right.diagnostic_count &&
                left.stored_bytes == right.stored_bytes &&
                left.decompressed_bytes == right.decompressed_bytes &&
                left.resident_bytes == right.resident_bytes &&
                left.streamed_bytes == right.streamed_bytes &&
                left.temporary_bytes == right.temporary_bytes &&
                left.output_bytes == right.output_bytes &&
                left.scene_count == right.scene_count &&
                left.resource_count == right.resource_count &&
                left.cache_count == right.cache_count &&
                left.dependency_count == right.dependency_count &&
                left.material_program_count == right.material_program_count &&
                left.adapter_loss_report_size ==
                    right.adapter_loss_report_size &&
                left.report == right.report,
            message);
}

}

int main(int argc, char **argv) {
    try {
        require(argc == 6,
                "expected runtime, worker, Q3, Q4, and glTF paths");
        const auto runtime = std::filesystem::absolute(argv[1]);
        const auto worker = std::filesystem::absolute(argv[2]);
        const auto q3 = std::filesystem::absolute(argv[3]);
        const auto q4 = std::filesystem::absolute(argv[4]);
        const auto gltf = std::filesystem::absolute(argv[5]);
        const auto unique = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        const auto root = std::filesystem::temp_directory_path() /
                          ("ultrarender-prv2-contract-" + unique);
        struct Cleanup {
            std::filesystem::path root;
            ~Cleanup() {
                std::error_code error;
                std::filesystem::remove_all(root, error);
            }
        } cleanup{root};
        std::filesystem::create_directories(root);

        auto direct = connect(ure::client::TransportMode::Direct, runtime,
                              worker);
        auto isolated = connect(ure::client::TransportMode::Worker, runtime,
                                worker);
        const auto expect_failure = [&direct, &isolated](
                                        const ure::client::SceneToolRequest& request,
                                        std::int32_t expected_result,
                                        std::uint32_t expected_detail,
                                        std::string_view label) {
            ure::client::ErrorInfo direct_error;
            ure::client::ErrorInfo worker_error;
            bool direct_failed{};
            bool worker_failed{};
            try {
                static_cast<void>(direct.scene_tool(request));
            } catch (const ure::client::Error& error) {
                direct_error = error.info();
                direct_failed = true;
            }
            try {
                static_cast<void>(isolated.scene_tool(request));
            } catch (const ure::client::Error& error) {
                worker_error = error.info();
                worker_failed = true;
            }
            if (!direct_failed || !worker_failed ||
                direct_error.result != expected_result ||
                direct_error.detail != expected_detail ||
                direct_error.result != worker_error.result ||
                direct_error.detail != worker_error.detail ||
                direct_error.diagnostic_report.empty() ||
                direct_error.diagnostic_report != worker_error.diagnostic_report) {
                throw std::runtime_error(std::string(label) +
                                         " diagnostic parity failed: direct=" +
                                         std::to_string(direct_error.result) +
                                         "/" +
                                         std::to_string(direct_error.detail) +
                                         ", worker=" +
                                         std::to_string(worker_error.result) +
                                         "/" +
                                         std::to_string(worker_error.detail) +
                                         "\n" +
                                         direct_error.diagnostic_report);
            }
            return direct_error;
        };
        ure::client::SceneToolRequest realize;
        realize.operation = ure::client::SceneToolOperation::Realize;
        realize.inputs = {q4 / "procedural_scene.urescene"};
        const auto direct_realized = direct.scene_tool(realize);
        const auto worker_realized = isolated.scene_tool(realize);
        require(nonzero(direct_realized.snapshot_identity),
                "direct realization identity is zero");
        require_equivalent(direct_realized, worker_realized,
                           "direct and Worker realization results differ");
        require(direct_realized.report.find("ure.scene.procedural") !=
                    std::string::npos &&
                    direct_realized.report.find("Executed") !=
                        std::string::npos,
                "procedural execution disposition is absent");

        ure::client::SceneToolRequest inspect;
        inspect.operation = ure::client::SceneToolOperation::Inspect;
        inspect.inputs = {q3 / "full_scene.urescene"};
        require_equivalent(direct.scene_tool(inspect),
                           isolated.scene_tool(inspect),
                           "direct and Worker inspection results differ");

        ure::client::SceneToolRequest build_direct;
        build_direct.operation = ure::client::SceneToolOperation::Build;
        build_direct.inputs = {q4 / "procedural_scene.urescene"};
        build_direct.output = root / "direct-build.urescene";
        auto build_worker = build_direct;
        build_worker.output = root / "worker-build.urescene";
        require_equivalent(direct.scene_tool(build_direct),
                           isolated.scene_tool(build_worker),
                           "direct and Worker build results differ");

        const auto direct_author = root / "migrate-direct";
        const auto worker_author = root / "migrate-worker";
        std::filesystem::copy(q3, direct_author,
                              std::filesystem::copy_options::recursive);
        std::filesystem::copy(q3, worker_author,
                              std::filesystem::copy_options::recursive);
        ure::client::SceneToolRequest migrate_direct;
        migrate_direct.operation = ure::client::SceneToolOperation::Migrate;
        migrate_direct.inputs = {direct_author / "full_scene.urescene"};
        migrate_direct.output = direct_author / "migrated.urescene";
        auto migrate_worker = migrate_direct;
        migrate_worker.inputs = {worker_author / "full_scene.urescene"};
        migrate_worker.output = worker_author / "migrated.urescene";
        require_equivalent(direct.scene_tool(migrate_direct),
                           isolated.scene_tool(migrate_worker),
                           "direct and Worker migrate results differ");

        ure::client::SceneToolRequest preset_direct;
        preset_direct.operation =
            ure::client::SceneToolOperation::ApplyMaterialPreset;
        preset_direct.inputs = {direct_author / "full_scene.urescene"};
        preset_direct.output = root / "preset-direct.urescene";
        preset_direct.material_selector = "material/00000000";
        preset_direct.preset_name = "clear_glass";
        auto preset_worker = preset_direct;
        preset_worker.inputs = {worker_author / "full_scene.urescene"};
        preset_worker.output = root / "preset-worker.urescene";
        const auto direct_preset = direct.scene_tool(preset_direct);
        const auto worker_preset = isolated.scene_tool(preset_worker);
        require_equivalent(direct_preset, worker_preset,
                           "material preset Direct/Worker results differ");
        require(nonzero(direct_preset.material_program_set_identity) &&
                    direct_preset.material_program_count != 0,
                "material preset omitted canonical program identity");

        ure::client::SceneToolRequest export_direct;
        export_direct.operation =
            ure::client::SceneToolOperation::ExportMaterialX;
        export_direct.inputs = {preset_direct.output};
        export_direct.output = root / "direct.mtlx";
        export_direct.material_selector = "material/00000000";
        auto export_worker = export_direct;
        export_worker.inputs = {preset_worker.output};
        export_worker.output = root / "worker.mtlx";
        const auto direct_export = direct.scene_tool(export_direct);
        const auto worker_export = isolated.scene_tool(export_worker);
        require_equivalent(direct_export, worker_export,
                           "MaterialX export Direct/Worker results differ");
        require(direct_export.adapter_loss_report_size != 0 &&
                    nonzero(direct_export.material_program_set_identity) &&
                    has_report_identity(direct_export.report),
                "MaterialX export omitted its loss report");

        ure::client::SceneToolRequest import_direct;
        import_direct.operation =
            ure::client::SceneToolOperation::ImportMaterialX;
        import_direct.inputs = {direct_author / "full_scene.urescene",
                                export_direct.output};
        import_direct.output = root / "import-direct.urescene";
        import_direct.material_selector = "material/00000000";
        auto import_worker = import_direct;
        import_worker.inputs = {worker_author / "full_scene.urescene",
                                export_worker.output};
        import_worker.output = root / "import-worker.urescene";
        require_equivalent(direct.scene_tool(import_direct),
                           isolated.scene_tool(import_worker),
                           "MaterialX import Direct/Worker results differ");

        const auto unsupported_materialx = root / "unsupported.mtlx";
        {
            std::ofstream output(unsupported_materialx);
            output << "<materialx><unsupported_shader name=\"bad\" /></materialx>";
        }
        auto unsupported_import = import_direct;
        unsupported_import.inputs[1] = unsupported_materialx;
        unsupported_import.output = root / "unsupported.urescene";
        const auto unsupported_error = expect_failure(
            unsupported_import, URE_RESULT_INVALID_ARGUMENT, 706,
            "unsupported MaterialX import");
        require(has_report_identity(unsupported_error.diagnostic_report),
                "MaterialX rejection omitted its shared loss identity");

        ure::client::SceneToolRequest gltf_direct;
        gltf_direct.operation = ure::client::SceneToolOperation::Build;
        gltf_direct.inputs = {gltf};
        gltf_direct.output = root / "gltf-direct.urescene";
        auto gltf_worker = gltf_direct;
        gltf_worker.output = root / "gltf-worker.urescene";
        const auto direct_gltf = direct.scene_tool(gltf_direct);
        const auto worker_gltf = isolated.scene_tool(gltf_worker);
        require_equivalent(direct_gltf, worker_gltf,
                           "glTF authoring Direct/Worker results differ");
        require(direct_gltf.material_program_count != 0 &&
                    direct_gltf.report.find("ure.adapter.gltf") !=
                        std::string::npos,
                "glTF authoring omitted canonical material programs");

        auto unknown_preset = preset_direct;
        unknown_preset.output = root / "unknown-preset.urescene";
        unknown_preset.preset_name = "missing-preset";
        static_cast<void>(expect_failure(
            unknown_preset, URE_RESULT_INVALID_ARGUMENT, 706,
            "unknown material preset"));

        const auto author = root / "author";
        std::filesystem::copy(q3, author,
                              std::filesystem::copy_options::recursive);
        const auto package = root / "self-contained.urepkg";
        ure::client::SceneToolRequest pack;
        pack.operation = ure::client::SceneToolOperation::Pack;
        pack.inputs = {author / "full_scene.urescene"};
        pack.output = package;
        const auto packed = direct.scene_tool(pack);
        auto worker_pack = pack;
        worker_pack.inputs = {worker_author / "full_scene.urescene"};
        worker_pack.output = root / "worker-contained.urepkg";
        const auto worker_packed = isolated.scene_tool(worker_pack);
        require_equivalent(packed, worker_packed,
                           "direct and Worker pack results differ");
        require(packed.resource_count >= 5,
                "self-contained package omitted resources");
        std::filesystem::remove_all(author);
        ure::client::SceneToolRequest validate;
        validate.operation = ure::client::SceneToolOperation::Validate;
        validate.inputs = {package};
        const auto direct_validated = direct.scene_tool(validate);
        const auto validated = isolated.scene_tool(validate);
        require_equivalent(direct_validated, validated,
                           "direct and Worker validation results differ");
        require(validated.resource_count >= 5 &&
                    nonzero(validated.snapshot_identity),
                "isolated package validation did not realize resources");
        require(validated.report.find(root.generic_string()) ==
                    std::string::npos,
                "scene-tool report leaked a private absolute path");

        ure::client::SceneToolRequest unpack_direct;
        unpack_direct.operation = ure::client::SceneToolOperation::Unpack;
        unpack_direct.inputs = {package};
        unpack_direct.output = root / "unpacked-direct";
        auto unpack_worker = unpack_direct;
        unpack_worker.output = root / "unpacked-worker";
        require_equivalent(direct.scene_tool(unpack_direct),
                           isolated.scene_tool(unpack_worker),
                           "direct and Worker unpack results differ");

        ure::client::SceneToolRequest missing;
        missing.operation = ure::client::SceneToolOperation::Validate;
        missing.inputs = {root / "private" / "missing.urescene"};
        ure::client::ErrorInfo direct_error;
        ure::client::ErrorInfo worker_error;
        try {
            static_cast<void>(direct.scene_tool(missing));
            require(false, "missing direct input unexpectedly succeeded");
        } catch (const ure::client::Error &error) {
            direct_error = error.info();
        }
        try {
            static_cast<void>(isolated.scene_tool(missing));
            require(false, "missing Worker input unexpectedly succeeded");
        } catch (const ure::client::Error &error) {
            worker_error = error.info();
        }
        require(direct_error.result == worker_error.result &&
                    direct_error.detail == worker_error.detail &&
                    direct_error.detail == 620,
                "negative Direct and Worker classifications differ");
        require(!direct_error.diagnostic_report.empty() &&
                    direct_error.diagnostic_report ==
                        worker_error.diagnostic_report,
                "negative Direct and Worker reports differ");
        require(direct_error.diagnostic_report.find(root.generic_string()) ==
                    std::string::npos,
                "negative report leaked a private absolute path");

        const auto corrupt_scene = root / "corrupt-resource.urepkg";
        {
            std::ifstream input(package,
                                std::ios::binary);
            std::string text{std::istreambuf_iterator<char>(input),
                             std::istreambuf_iterator<char>()};
            std::ifstream resource(q3 / "textures" / "albedo.ppm",
                                   std::ios::binary);
            const std::string payload{
                std::istreambuf_iterator<char>(resource),
                std::istreambuf_iterator<char>()};
            const auto offset = text.find(payload);
            require(offset != std::string::npos && !payload.empty(),
                    "packaged texture payload was not found");
            text[offset] = static_cast<char>(text[offset] ^ 1);
            std::ofstream output(corrupt_scene,
                                 std::ios::binary | std::ios::trunc);
            output << text;
        }
        ure::client::SceneToolRequest corrupt;
        corrupt.operation = ure::client::SceneToolOperation::Validate;
        corrupt.inputs = {corrupt_scene};
        ure::client::ErrorInfo direct_corrupt;
        ure::client::ErrorInfo worker_corrupt;
        try {
            static_cast<void>(direct.scene_tool(corrupt));
            require(false, "hash-mismatched direct input unexpectedly succeeded");
        } catch (const ure::client::Error &error) {
            direct_corrupt = error.info();
        }
        try {
            static_cast<void>(isolated.scene_tool(corrupt));
            require(false, "hash-mismatched Worker input unexpectedly succeeded");
        } catch (const ure::client::Error &error) {
            worker_corrupt = error.info();
        }
        if (direct_corrupt.result != worker_corrupt.result ||
            direct_corrupt.detail != worker_corrupt.detail ||
            direct_corrupt.detail != 609 ||
            direct_corrupt.diagnostic_report !=
                worker_corrupt.diagnostic_report) {
            std::cerr << "hash mismatch direct result/detail="
                      << direct_corrupt.result << '/' << direct_corrupt.detail
                      << " worker=" << worker_corrupt.result << '/'
                      << worker_corrupt.detail << '\n'
                      << direct_corrupt.diagnostic_report << '\n';
        }
        require(direct_corrupt.result == worker_corrupt.result &&
                    direct_corrupt.detail == worker_corrupt.detail &&
                    direct_corrupt.detail == 609 &&
                    direct_corrupt.diagnostic_report ==
                        worker_corrupt.diagnostic_report,
                "resource hash-mismatch diagnostic parity failed");

        ure::client::SceneToolRequest ambiguous;
        ambiguous.operation = ure::client::SceneToolOperation::Validate;
        ambiguous.inputs = {
            q3.parent_path() / "pb5_public_boundary" /
            "ambiguous_scenes.urepkg"};
        static_cast<void>(expect_failure(
            ambiguous, URE_RESULT_MALFORMED_DATA, 618,
            "ambiguous package selection"));
        for (const auto operation : {
                 ure::client::SceneToolOperation::Build,
                 ure::client::SceneToolOperation::Migrate,
                 ure::client::SceneToolOperation::Pack}) {
            auto package_republication = ambiguous;
            package_republication.operation = operation;
            package_republication.output =
                root / (std::to_string(static_cast<std::uint32_t>(operation)) +
                        ".urescene");
            static_cast<void>(expect_failure(
                package_republication, URE_RESULT_INVALID_ARGUMENT, 622,
                "ambiguous package republication"));
        }

        auto oversized = validate;
        oversized.budget.max_content_bytes = 128;
        oversized.budget.max_uncompressed_bytes = 128;
        static_cast<void>(expect_failure(
            oversized, URE_RESULT_BUDGET_EXHAUSTED, 614,
            "stored package budget"));

        auto oversized_text = validate;
        oversized_text.package_scene_id = std::string(1025, 'x');
        bool oversized_text_failed{};
        try {
            static_cast<void>(isolated.scene_tool(oversized_text));
        } catch (const ure::client::Error& error) {
            oversized_text_failed =
                error.info().result == URE_RESULT_INVALID_ARGUMENT &&
                error.info().detail == 706;
        }
        require(oversized_text_failed,
                "Worker scene-tool bounded strings were not rejected");

        const auto missing_author = root / "missing-author";
        std::filesystem::copy(q3, missing_author,
                              std::filesystem::copy_options::recursive);
        std::filesystem::remove(missing_author / "textures" / "albedo.ppm");
        auto missing_resource = validate;
        missing_resource.inputs = {missing_author / "full_scene.urescene"};
        const auto missing_resource_error = expect_failure(
            missing_resource, URE_RESULT_MALFORMED_DATA, 608,
            "missing declared resource");
        require(missing_resource_error.diagnostic_report.find(
                    root.generic_string()) == std::string::npos,
                "missing-resource report leaked a private absolute path");

        std::ifstream procedural_input(q4 / "procedural_scene.ure");
        std::string procedural_text{
            std::istreambuf_iterator<char>(procedural_input),
            std::istreambuf_iterator<char>()};
        const std::string procedural_source_text = procedural_text;
        const std::string empty_features = "\"features\": []";
        const auto mutated_author = root / "mutated-author";
        std::filesystem::copy(q4, mutated_author,
                              std::filesystem::copy_options::recursive);
        const auto features_offset = procedural_text.find(empty_features);
        require(features_offset != std::string::npos,
                "procedural fixture feature list was not found");
        procedural_text.replace(
            features_offset, empty_features.size(),
            "\"features\": [{\"name\":\"ure.future.unsupported\","
            "\"minimum_version\":{\"major\":1,\"minor\":0},"
            "\"requirement\":\"required\",\"provider\":\"ure\","
            "\"dependencies\":[],\"parameters\":{}}]");
        const auto unsupported_path = mutated_author / "unsupported.ure";
        {
            std::ofstream output(unsupported_path,
                                 std::ios::binary | std::ios::trunc);
            output << procedural_text;
        }
        auto unsupported = validate;
        unsupported.inputs = {unsupported_path};
        static_cast<void>(expect_failure(
            unsupported, URE_RESULT_CAPABILITY_UNAVAILABLE, 601,
            "unsupported required feature"));

        const std::string safe_uri =
            "resources/mesh/890ecd29105dbec78013bbdca3c7b38f404f6d3d1db94dd7058f85f5a1fe1e06.urmesh";
        procedural_text = procedural_source_text;
        const auto uri_offset = procedural_text.find(safe_uri);
        require(uri_offset != std::string::npos,
                "procedural fixture resource URI was not found");
        procedural_text.replace(uri_offset, safe_uri.size(),
                                "../private/escape.urmesh");
        const auto traversal_path = mutated_author / "traversal.ure";
        {
            std::ofstream output(traversal_path,
                                 std::ios::binary | std::ios::trunc);
            output << procedural_text;
        }
        auto traversal = validate;
        traversal.inputs = {traversal_path};
        static_cast<void>(expect_failure(
            traversal, URE_RESULT_MALFORMED_DATA, 612,
            "resource path traversal"));
        std::cout << "scene-tool extension tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
