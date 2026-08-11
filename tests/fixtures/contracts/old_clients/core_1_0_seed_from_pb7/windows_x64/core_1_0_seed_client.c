#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <ultrarender/ure_loader.h>

#define CHECK(expression)                                                      \
    do {                                                                       \
        if (!(expression)) {                                                   \
            fprintf(stderr, "Core 1.0 PB.7-layout seed failed at line %d\n", \
                    __LINE__);                                                 \
            return __LINE__;                                                   \
        }                                                                      \
    } while (0)

static int query_table(ure_query_interface_fn query_interface,
                       const uint8_t id[16], uint64_t minimum_size) {
    ure_interface_query_t query = {0};
    ure_interface_response_t response = {0};
    query.header.type = URE_STRUCTURE_INTERFACE_QUERY;
    query.header.size = (uint32_t)sizeof(query);
    memcpy(query.interface_id.bytes, id, sizeof(query.interface_id.bytes));
    query.minimum_major = 1;
    query.maximum_major = 1;
    response.header.type = URE_STRUCTURE_INTERFACE_RESPONSE;
    response.header.size = (uint32_t)sizeof(response);
    CHECK(query_interface(&query, &response, NULL) == URE_RESULT_SUCCESS);
    CHECK(response.version_major == 1 && response.table != NULL &&
          response.table_size >= minimum_size);
    CHECK(((const ure_interface_table_header_t *)response.table)->struct_size >=
          minimum_size);
    return 0;
}

static int run(ure_get_runtime_manifest_fn get_manifest,
               ure_query_interface_fn query_interface) {
    static const uint8_t ids[][16] = {
        URE_INTERFACE_RUNTIME_UUID_BYTES,   URE_INTERFACE_INSTANCE_UUID_BYTES,
        URE_INTERFACE_ERROR_UUID_BYTES,     URE_INTERFACE_OPERATION_UUID_BYTES,
        URE_INTERFACE_EVENT_UUID_BYTES,     URE_INTERFACE_FRAME_UUID_BYTES,
        URE_INTERFACE_SCENE_UUID_BYTES,     URE_INTERFACE_SESSION_UUID_BYTES};
    const uint64_t sizes[] = {
        sizeof(ure_runtime_interface_t),   sizeof(ure_instance_interface_t),
        sizeof(ure_error_interface_t),     sizeof(ure_operation_interface_t),
        sizeof(ure_event_interface_t),     sizeof(ure_frame_interface_t),
        offsetof(ure_scene_interface_t, apply_transaction),
        sizeof(ure_session_interface_t)};
    ure_runtime_manifest_request_t request = {0};
    ure_runtime_manifest_t manifest = {0};
    size_t index = 0;
    request.header.type = URE_STRUCTURE_RUNTIME_MANIFEST_REQUEST;
    request.header.size = (uint32_t)sizeof(request);
    request.minimum_major = 1;
    request.maximum_major = 1;
    manifest.header.type = URE_STRUCTURE_RUNTIME_MANIFEST;
    manifest.header.size = (uint32_t)sizeof(manifest);
    CHECK(get_manifest(&request, &manifest, NULL) == URE_RESULT_SUCCESS);
    CHECK(manifest.runtime_major == 1 && manifest.runtime_minor == 0);
    for (; index < sizeof(sizes) / sizeof(sizes[0]); ++index)
        CHECK(query_table(query_interface, ids[index], sizes[index]) == 0);
    return 0;
}

int main(int argc, char **argv) {
    wchar_t runtime_path[32768] = {0};
    HMODULE module = NULL;
    ure_get_runtime_manifest_fn get_manifest = NULL;
    ure_query_interface_fn query_interface = NULL;
    int result = 0;
    CHECK(argc == 2);
    CHECK(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, argv[1], -1,
                              runtime_path, 32768) != 0);
    module = LoadLibraryW(runtime_path);
    CHECK(module != NULL);
    get_manifest = (ure_get_runtime_manifest_fn)GetProcAddress(
        module, "ureGetRuntimeManifest");
    query_interface = (ure_query_interface_fn)GetProcAddress(
        module, "ureQueryInterface");
    CHECK(get_manifest != NULL && query_interface != NULL);
    result = run(get_manifest, query_interface);
    FreeLibrary(module);
    return result;
}
