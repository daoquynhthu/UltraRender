#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include <ultrarender/ure_loader.h>

namespace {

struct Generator {
    std::uint64_t state{UINT64_C(0x8d12e519a73bc641)};
    std::uint64_t next() {
        state ^= state << 13U;
        state ^= state >> 7U;
        state ^= state << 17U;
        return state;
    }
};

std::vector<std::uint8_t> read_file(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        return {};
    const auto size = input.tellg();
    if (size <= 0)
        return {};
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char *>(bytes.data()), size))
        return {};
    return bytes;
}

ure_digest256_t sha256(const std::vector<std::uint8_t> &bytes) {
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    ure_digest256_t output{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr,
                                    0) < 0 ||
        BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0)
        std::abort();
    if (!bytes.empty() &&
        BCryptHashData(hash, const_cast<PUCHAR>(bytes.data()),
                       static_cast<ULONG>(bytes.size()), 0) < 0)
        std::abort();
    if (BCryptFinishHash(hash, output.bytes, sizeof(output.bytes), 0) < 0)
        std::abort();
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return output;
}

template <typename Table>
const Table *query_table(ure_query_interface_fn query,
                         const std::uint8_t (&id)[16]) {
    ure_interface_query_t request{};
    ure_interface_response_t response{};
    request.header = {URE_STRUCTURE_INTERFACE_QUERY, sizeof(request), nullptr};
    std::memcpy(request.interface_id.bytes, id, sizeof(id));
    request.maximum_minor = 1;
    response.header = {URE_STRUCTURE_INTERFACE_RESPONSE, sizeof(response),
                       nullptr};
    if (query(&request, &response, nullptr) != URE_RESULT_SUCCESS ||
        response.table_size < sizeof(Table))
        return nullptr;
    return static_cast<const Table *>(response.table);
}

ure_scene_budget_t budget() {
    ure_scene_budget_t value{};
    value.header = {URE_STRUCTURE_SCENE_BUDGET, sizeof(value), nullptr};
    value.max_content_bytes = UINT64_C(16777216);
    value.max_uncompressed_bytes = UINT64_C(67108864);
    value.max_resident_bytes = UINT64_C(268435456);
    value.max_resource_count = 4096;
    value.max_object_count = 100000;
    value.max_nesting_depth = 64;
    value.max_decompression_ratio = 256;
    return value;
}

bool bounded_result(ure_result_t result) {
    return result >= URE_RESULT_REVISION_CONFLICT &&
           result <= URE_RESULT_INCOMPLETE;
}

int fail(int line) {
    std::cerr << "PB.7 deterministic fuzz corpus failed at line " << line
              << '\n';
    return line;
}

#define CHECK(expression)          \
    do {                           \
        if (!(expression))         \
            return fail(__LINE__); \
    } while (false)

int run(const std::filesystem::path &runtime_path,
        const std::filesystem::path &scene_path) {
    const auto original = read_file(scene_path);
    CHECK(!original.empty());
    const HMODULE module = LoadLibraryExW(
        runtime_path.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    CHECK(module != nullptr);
    const auto manifest_fn = reinterpret_cast<ure_get_runtime_manifest_fn>(
        reinterpret_cast<void *>(GetProcAddress(module,
                                                 "ureGetRuntimeManifest")));
    const auto query = reinterpret_cast<ure_query_interface_fn>(
        reinterpret_cast<void *>(GetProcAddress(module, "ureQueryInterface")));
    static constexpr std::uint8_t runtime_id[16] =
        URE_INTERFACE_RUNTIME_UUID_BYTES;
    static constexpr std::uint8_t instance_id[16] =
        URE_INTERFACE_INSTANCE_UUID_BYTES;
    static constexpr std::uint8_t scene_id[16] = URE_INTERFACE_SCENE_UUID_BYTES;
    const auto *runtime = query_table<ure_runtime_interface_t>(query, runtime_id);
    const auto *instances =
        query_table<ure_instance_interface_t>(query, instance_id);
    const auto *scenes = query_table<ure_scene_interface_t>(query, scene_id);
    CHECK(manifest_fn && query && runtime && instances && scenes);

    Generator generator;
    for (std::uint32_t index = 0; index < 256; ++index) {
        std::array<ure_input_header_t, 4> chain{};
        ure_runtime_manifest_request_t request{};
        ure_runtime_manifest_t manifest{};
        request.header = {URE_STRUCTURE_RUNTIME_MANIFEST_REQUEST,
                          sizeof(request), nullptr};
        request.maximum_minor = 1;
        manifest.header = {URE_STRUCTURE_RUNTIME_MANIFEST, sizeof(manifest),
                           nullptr};
        const auto mode = generator.next() % 8;
        if (mode == 0)
            request.header.size = static_cast<std::uint32_t>(generator.next() %
                                                             sizeof(request));
        else if (mode == 1)
            request.header.type ^= 1U;
        else if (mode == 2)
            request.reserved[generator.next() % 2] = generator.next() | 1U;
        else if (mode >= 3) {
            const auto count = static_cast<std::size_t>(1 + generator.next() % 4);
            for (std::size_t item = 0; item < count; ++item) {
                chain[item] = {static_cast<std::uint32_t>(0x80001000U + item),
                               sizeof(ure_input_header_t),
                               item + 1 < count ? &chain[item + 1] : nullptr};
            }
            if (mode == 7)
                chain[count - 1].next = &chain[0];
            request.header.next = chain.data();
        }
        CHECK(bounded_result(manifest_fn(&request, &manifest, nullptr)));
    }

    const std::uint32_t required[]{URE_CAPABILITY_LIFECYCLE,
                                   URE_CAPABILITY_NATIVE_SCENE};
    ure_instance_create_info_t create{};
    create.header = {URE_STRUCTURE_INSTANCE_CREATE_INFO, sizeof(create),
                     nullptr};
    create.event_capacity = 32;
    create.required_capability_count = 2;
    create.required_capabilities = required;
    ure_handle_t instance{};
    CHECK(runtime->create_instance(&create, &instance, nullptr) ==
          URE_RESULT_SUCCESS);
    ure_native_scene_blob_t blob{};
    blob.header = {URE_STRUCTURE_NATIVE_SCENE_BLOB, sizeof(blob), nullptr};
    blob.source_kind = URE_SCENE_SOURCE_MEMORY;
    blob.format = URE_SCENE_FORMAT_URESCENE;
    blob.bytes = {original.data(), original.size()};
    blob.schema_max_major = 2;
    blob.budget = budget();
    ure_scene_revision_info_t revision{};
    revision.header = {URE_STRUCTURE_SCENE_REVISION_INFO, sizeof(revision),
                       nullptr};
    ure_handle_t scene{};
    CHECK(scenes->create(instance, &blob, &scene, &revision, nullptr) ==
              URE_RESULT_SUCCESS &&
          revision.revision == 1);

    for (std::uint32_t index = 0; index < 128; ++index) {
        auto mutated = original;
        const auto mutations = 1 + generator.next() % 8;
        for (std::uint64_t item = 0; item < mutations; ++item)
            mutated[generator.next() % mutated.size()] ^=
                static_cast<std::uint8_t>(1U << (generator.next() % 8));
        blob.bytes = {mutated.data(), mutated.size()};
        ure_scene_validation_result_t validation{};
        std::array<char, 512> diagnostics{};
        validation.header = {URE_STRUCTURE_SCENE_VALIDATION_RESULT,
                             sizeof(validation), nullptr};
        validation.diagnostics_capacity =
            static_cast<std::uint32_t>(diagnostics.size());
        validation.diagnostics_data = diagnostics.data();
        CHECK(bounded_result(
            scenes->validate(instance, &blob, &validation, nullptr)));
        CHECK(validation.diagnostics_written < diagnostics.size());
    }

    for (std::uint32_t index = 0; index < 128; ++index) {
        std::vector<std::uint8_t> payload(1 + generator.next() % 1024);
        std::ranges::generate(payload,
                              [&] { return static_cast<std::uint8_t>(generator.next()); });
        ure_scene_transaction_t transaction{};
        transaction.header = {URE_STRUCTURE_SCENE_TRANSACTION,
                              sizeof(transaction), nullptr};
        transaction.transaction_id.bytes[0] = 0x80;
        transaction.transaction_id.bytes[15] = static_cast<std::uint8_t>(index + 1);
        transaction.base_revision = 1;
        transaction.payload_schema = URE_PAYLOAD_SCENE_TRANSACTION;
        transaction.payload_version_major = 1;
        transaction.max_operation_count = 64;
        transaction.max_payload_bytes = 4096;
        transaction.payload = {payload.data(), payload.size()};
        transaction.payload_digest = sha256(payload);
        std::array<std::uint8_t, 1024> result_bytes{};
        ure_scene_transaction_result_t result{};
        result.header = {URE_STRUCTURE_SCENE_TRANSACTION_RESULT, sizeof(result),
                         nullptr};
        result.result_payload = {result_bytes.data(), result_bytes.size()};
        CHECK(bounded_result(
            scenes->apply_transaction(scene, &transaction, &result, nullptr)));
    }
    revision = {};
    revision.header = {URE_STRUCTURE_SCENE_REVISION_INFO, sizeof(revision),
                       nullptr};
    CHECK(scenes->get_revision(scene, &revision, nullptr) == URE_RESULT_SUCCESS &&
          revision.revision == 1);

    for (std::uintptr_t value = 1; value < 256; value += 7) {
        const auto invalid = reinterpret_cast<ure_handle_t>(
            UINT64_C(0x7fff00000000) + value);
        CHECK(instances->retain(invalid, nullptr) == URE_RESULT_INVALID_HANDLE);
        CHECK(scenes->retain(invalid, nullptr) == URE_RESULT_INVALID_HANDLE);
    }
    scenes->release(scene, nullptr);
    instances->close(instance, nullptr);
    instances->release(instance, nullptr);
    FreeLibrary(module);
    return 0;
}

}

int main(int argc, char **argv) {
    if (argc != 3)
        return 2;
    return run(argv[1], argv[2]);
}
