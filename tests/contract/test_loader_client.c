#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ultrarender/ure_loader.h>

#define CHECK(expression) do { if (!(expression)) return __LINE__; } while (0)

typedef struct oversized_request_t {
    ure_runtime_manifest_request_t value;
    uint64_t tail[4];
} oversized_request_t;

typedef struct oversized_manifest_t {
    ure_runtime_manifest_t value;
    uint64_t tail[4];
} oversized_manifest_t;

static ure_bootstrap_diagnostic_t make_diagnostic(char* message, uint32_t capacity) {
    ure_bootstrap_diagnostic_t diagnostic = {0};
    diagnostic.header.type = URE_STRUCTURE_BOOTSTRAP_DIAGNOSTIC;
    diagnostic.header.size = (uint32_t)sizeof(diagnostic);
    diagnostic.message_capacity = capacity;
    diagnostic.message_data = message;
    return diagnostic;
}

static ure_runtime_manifest_request_t make_manifest_request(void) {
    ure_runtime_manifest_request_t request = {0};
    request.header.type = URE_STRUCTURE_RUNTIME_MANIFEST_REQUEST;
    request.header.size = (uint32_t)sizeof(request);
    request.maximum_minor = 1;
    return request;
}

static ure_runtime_manifest_t make_manifest(void) {
    ure_runtime_manifest_t manifest = {0};
    manifest.header.type = URE_STRUCTURE_RUNTIME_MANIFEST;
    manifest.header.size = (uint32_t)sizeof(manifest);
    return manifest;
}

static ure_interface_query_t make_runtime_query(void) {
    ure_interface_query_t query = {0};
    static const uint8_t runtime_id[16] = URE_INTERFACE_RUNTIME_UUID_BYTES;
    query.header.type = URE_STRUCTURE_INTERFACE_QUERY;
    query.header.size = (uint32_t)sizeof(query);
    memcpy(query.interface_id.bytes, runtime_id, sizeof(runtime_id));
    query.maximum_minor = 1;
    return query;
}

static ure_interface_response_t make_response(void) {
    ure_interface_response_t response = {0};
    response.header.type = URE_STRUCTURE_INTERFACE_RESPONSE;
    response.header.size = (uint32_t)sizeof(response);
    return response;
}

static int contains_bytes(const uint8_t* data, uint64_t size, const char* expected) {
    const size_t expected_size = strlen(expected);
    uint64_t index = 0;
    if (expected_size > size) return 0;
    for (; index + expected_size <= size; ++index) {
        if (memcmp(data + index, expected, expected_size) == 0) return 1;
    }
    return 0;
}

static int run_tests(ure_get_runtime_manifest_fn get_manifest, ure_query_interface_fn query_interface) {
    static const uint8_t registry_digest[32] = URE_REGISTRY_DIGEST_BYTES;
    char message[128] = {0};
    ure_bootstrap_diagnostic_t diagnostic = make_diagnostic(message, (uint32_t)sizeof(message));
    ure_runtime_manifest_request_t request = make_manifest_request();
    ure_runtime_manifest_t manifest = make_manifest();
    CHECK(get_manifest(&request, &manifest, &diagnostic) == URE_RESULT_SUCCESS);
    CHECK(manifest.runtime_major == 0 && manifest.runtime_minor == 1 && manifest.runtime_patch == 0);
    CHECK(memcmp(manifest.registry_digest.bytes, registry_digest, sizeof(registry_digest)) == 0);
    CHECK(contains_bytes(manifest.abi_manifest_json.data, manifest.abi_manifest_json.size, "windows-x64-msvc-c11"));
    CHECK(contains_bytes(manifest.abi_manifest_json.data, manifest.abi_manifest_json.size, "runtime_build_digest"));
    CHECK(contains_bytes(manifest.abi_manifest_json.data, manifest.abi_manifest_json.size, "ure_interface_response_t"));
    CHECK(contains_bytes(manifest.abi_manifest_json.data, manifest.abi_manifest_json.size, "flatc version 25.12.19"));
    CHECK(contains_bytes(manifest.abi_manifest_json.data, manifest.abi_manifest_json.size, "\"renderer\":true"));
    CHECK(contains_bytes(manifest.abi_manifest_json.data, manifest.abi_manifest_json.size, "ure_native_scene_blob_t"));
    CHECK(contains_bytes(manifest.abi_manifest_json.data, manifest.abi_manifest_json.size, "ure_session_interface_t"));

    {
        oversized_request_t large_request = {0};
        oversized_manifest_t large_manifest = {0};
        large_request.value = make_manifest_request();
        large_request.value.header.size = (uint32_t)sizeof(large_request);
        large_manifest.value = make_manifest();
        large_manifest.value.header.size = (uint32_t)sizeof(large_manifest);
        CHECK(get_manifest(&large_request.value, &large_manifest.value, &diagnostic) == URE_RESULT_SUCCESS);
    }
    {
        ure_input_header_t optional = {0x80000001u, (uint32_t)sizeof(optional), NULL};
        ure_output_header_t optional_output = {0x80000002u, (uint32_t)sizeof(optional_output), NULL};
        request = make_manifest_request();
        manifest = make_manifest();
        request.header.next = &optional;
        manifest.header.next = &optional_output;
        CHECK(get_manifest(&request, &manifest, &diagnostic) == URE_RESULT_SUCCESS);
    }
    {
        ure_input_header_t duplicate_a = {0x80000001u, (uint32_t)sizeof(duplicate_a), NULL};
        ure_input_header_t duplicate_b = {0x80000001u, (uint32_t)sizeof(duplicate_b), NULL};
        duplicate_a.next = &duplicate_b;
        request = make_manifest_request();
        manifest = make_manifest();
        request.header.next = &duplicate_a;
        CHECK(get_manifest(&request, &manifest, &diagnostic) == URE_RESULT_INVALID_ARGUMENT);
    }
    {
        ure_input_header_t cycle = {0x80000001u, (uint32_t)sizeof(cycle), NULL};
        cycle.next = &cycle;
        request = make_manifest_request();
        manifest = make_manifest();
        request.header.next = &cycle;
        CHECK(get_manifest(&request, &manifest, &diagnostic) == URE_RESULT_INVALID_ARGUMENT);
    }
    {
        ure_input_header_t chain[33] = {0};
        size_t index = 0;
        for (; index < 33; ++index) {
            chain[index].type = 0x80000100u + (uint32_t)index;
            chain[index].size = (uint32_t)sizeof(chain[index]);
            chain[index].next = index + 1 < 33 ? &chain[index + 1] : NULL;
        }
        request = make_manifest_request();
        manifest = make_manifest();
        request.header.next = chain;
        CHECK(get_manifest(&request, &manifest, &diagnostic) == URE_RESULT_INVALID_ARGUMENT);
    }
    request = make_manifest_request();
    request.header.size = (uint32_t)sizeof(ure_input_header_t);
    manifest = make_manifest();
    CHECK(get_manifest(&request, &manifest, &diagnostic) == URE_RESULT_INVALID_ARGUMENT);
    request = make_manifest_request();
    CHECK(get_manifest(&request, NULL, &diagnostic) == URE_RESULT_INVALID_ARGUMENT);
    request = make_manifest_request();
    request.reserved[1] = 1;
    manifest = make_manifest();
    CHECK(get_manifest(&request, &manifest, &diagnostic) == URE_RESULT_INVALID_ARGUMENT);
    request = make_manifest_request();
    request.minimum_minor = 2;
    request.maximum_minor = 3;
    manifest = make_manifest();
    CHECK(get_manifest(&request, &manifest, &diagnostic) == URE_RESULT_INCOMPATIBLE_VERSION);
    request = make_manifest_request();
    request.expected_registry_digest.bytes[0] = 1;
    manifest = make_manifest();
    CHECK(get_manifest(&request, &manifest, &diagnostic) == URE_RESULT_INCOMPATIBLE_VERSION);

    {
        ure_bootstrap_diagnostic_t invalid_diagnostic = make_diagnostic(message, (uint32_t)sizeof(message));
        invalid_diagnostic.reserved = 1;
        request = make_manifest_request();
        manifest = make_manifest();
        CHECK(get_manifest(&request, &manifest, &invalid_diagnostic) == URE_RESULT_INVALID_ARGUMENT);
    }

    {
        char tiny[4] = {0};
        ure_bootstrap_diagnostic_t truncated = make_diagnostic(tiny, (uint32_t)sizeof(tiny));
        request = make_manifest_request();
        request.header.type = 0;
        manifest = make_manifest();
        CHECK(get_manifest(&request, &manifest, &truncated) == URE_RESULT_INVALID_ARGUMENT);
        CHECK(truncated.message_required > truncated.message_written);
        CHECK(truncated.message_written == 3 && tiny[3] == '\0');
    }

    {
        ure_interface_query_t query = make_runtime_query();
        ure_interface_response_t response = make_response();
        const void* first_table = NULL;
        CHECK(query_interface(&query, &response, &diagnostic) == URE_RESULT_SUCCESS);
        CHECK(response.version_major == 0 && response.version_minor == 1);
        CHECK(response.table_size == sizeof(ure_runtime_interface_t));
        CHECK(response.table != NULL);
        CHECK(((const ure_runtime_interface_t*)response.table)->header.struct_size == sizeof(ure_runtime_interface_t));
        first_table = response.table;
        response = make_response();
        CHECK(query_interface(&query, &response, &diagnostic) == URE_RESULT_SUCCESS);
        CHECK(response.table == first_table);
        query.minimum_minor = 2;
        query.maximum_minor = 3;
        response = make_response();
        CHECK(query_interface(&query, &response, &diagnostic) == URE_RESULT_INCOMPATIBLE_VERSION);
        query = make_runtime_query();
        CHECK(query_interface(&query, NULL, &diagnostic) == URE_RESULT_INVALID_ARGUMENT);
    }
    {
        static const uint8_t instance_id[16] = URE_INTERFACE_INSTANCE_UUID_BYTES;
        ure_interface_query_t query = make_runtime_query();
        ure_interface_response_t response = make_response();
        memcpy(query.interface_id.bytes, instance_id, sizeof(instance_id));
        CHECK(query_interface(&query, &response, &diagnostic) == URE_RESULT_SUCCESS);
        CHECK(response.table != NULL && response.table_size == sizeof(ure_instance_interface_t));
    }
    return 0;
}

int main(int argc, char** argv) {
    wchar_t runtime_path[32768] = {0};
    HMODULE runtime = NULL;
    ure_get_runtime_manifest_fn get_manifest = NULL;
    ure_query_interface_fn query_interface = NULL;
    int result = 0;
    CHECK(argc == 2);
    CHECK(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, argv[1], -1, runtime_path, 32768) != 0);
    runtime = LoadLibraryW(runtime_path);
    CHECK(runtime != NULL);
    get_manifest = (ure_get_runtime_manifest_fn)GetProcAddress(runtime, "ureGetRuntimeManifest");
    query_interface = (ure_query_interface_fn)GetProcAddress(runtime, "ureQueryInterface");
    CHECK(get_manifest != NULL && query_interface != NULL);
    result = run_tests(get_manifest, query_interface);
    FreeLibrary(runtime);
    if (result != 0) fprintf(stderr, "loader client check failed at line %d\n", result);
    return result;
}
