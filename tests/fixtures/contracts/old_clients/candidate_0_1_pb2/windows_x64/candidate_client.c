#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <ultrarender/ure_loader.h>

#ifndef URE_CLIENT_PHASE
#error URE_CLIENT_PHASE must identify the frozen candidate baseline
#endif

#define CHECK(expression)                                                      \
    do {                                                                       \
        if (!(expression)) {                                                   \
            fprintf(stderr, "candidate client phase %d failed at line %d\n",  \
                    URE_CLIENT_PHASE, __LINE__);                               \
            return __LINE__;                                                   \
        }                                                                      \
    } while (0)

static ure_result_t query_table(ure_query_interface_fn query_interface,
                                const uint8_t id[16], uint64_t minimum_size) {
    ure_interface_query_t query = {0};
    ure_interface_response_t response = {0};
    query.header.type = URE_STRUCTURE_INTERFACE_QUERY;
    query.header.size = (uint32_t)sizeof(query);
    memcpy(query.interface_id.bytes, id, sizeof(query.interface_id.bytes));
    query.maximum_minor = 1;
    response.header.type = URE_STRUCTURE_INTERFACE_RESPONSE;
    response.header.size = (uint32_t)sizeof(response);
    if (query_interface(&query, &response, NULL) != URE_RESULT_SUCCESS)
        return URE_RESULT_CAPABILITY_UNAVAILABLE;
    if (response.table == NULL || response.table_size < minimum_size)
        return URE_RESULT_INCOMPATIBLE_VERSION;
    if (((const ure_interface_table_header_t *)response.table)->struct_size <
        minimum_size)
        return URE_RESULT_INCOMPATIBLE_VERSION;
    return URE_RESULT_SUCCESS;
}

static int run(ure_get_runtime_manifest_fn get_manifest,
               ure_query_interface_fn query_interface) {
    static const uint8_t runtime_id[16] = URE_INTERFACE_RUNTIME_UUID_BYTES;
    ure_runtime_manifest_request_t request = {0};
    ure_runtime_manifest_t manifest = {0};
    uint8_t missing_optional[16] = {0xff, 0x50, 0x42, URE_CLIENT_PHASE};
    request.header.type = URE_STRUCTURE_RUNTIME_MANIFEST_REQUEST;
    request.header.size = (uint32_t)sizeof(request);
    request.maximum_minor = 1;
    manifest.header.type = URE_STRUCTURE_RUNTIME_MANIFEST;
    manifest.header.size = (uint32_t)sizeof(manifest);
    CHECK(get_manifest(&request, &manifest, NULL) == URE_RESULT_SUCCESS);
    CHECK(manifest.runtime_major == 0 && manifest.runtime_minor == 1);
    CHECK(query_table(query_interface, runtime_id,
                      sizeof(ure_runtime_interface_t)) == URE_RESULT_SUCCESS);
    CHECK(query_table(query_interface, missing_optional,
                      sizeof(ure_interface_table_header_t)) ==
          URE_RESULT_CAPABILITY_UNAVAILABLE);

#if URE_CLIENT_PHASE >= 3
    {
        static const uint8_t ids[][16] = {
            URE_INTERFACE_INSTANCE_UUID_BYTES,
            URE_INTERFACE_ERROR_UUID_BYTES,
            URE_INTERFACE_OPERATION_UUID_BYTES,
            URE_INTERFACE_EVENT_UUID_BYTES};
        const uint64_t sizes[] = {sizeof(ure_instance_interface_t),
                                  sizeof(ure_error_interface_t),
                                  sizeof(ure_operation_interface_t),
                                  sizeof(ure_event_interface_t)};
        size_t index = 0;
        for (; index < sizeof(sizes) / sizeof(sizes[0]); ++index)
            CHECK(query_table(query_interface, ids[index], sizes[index]) ==
                  URE_RESULT_SUCCESS);
    }
#endif
#if URE_CLIENT_PHASE >= 4
    {
        static const uint8_t frame_id[16] = URE_INTERFACE_FRAME_UUID_BYTES;
        CHECK(query_table(query_interface, frame_id,
                          sizeof(ure_frame_interface_t)) == URE_RESULT_SUCCESS);
    }
#endif
#if URE_CLIENT_PHASE >= 5
    {
        static const uint8_t scene_id[16] = URE_INTERFACE_SCENE_UUID_BYTES;
        static const uint8_t session_id[16] = URE_INTERFACE_SESSION_UUID_BYTES;
        CHECK(query_table(query_interface, scene_id,
                          sizeof(ure_scene_interface_t)) == URE_RESULT_SUCCESS);
        CHECK(query_table(query_interface, session_id,
                          sizeof(ure_session_interface_t)) == URE_RESULT_SUCCESS);
    }
#endif
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
