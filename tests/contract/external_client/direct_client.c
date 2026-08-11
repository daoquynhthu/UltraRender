#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>

#include <ultrarender/ure_loader.h>

#include "image_artifact.h"

static int check(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "scene boundary: %s\n", message);
        return 0;
    }
    return 1;
}

static void *query_table(ure_query_interface_fn query, const uint8_t id[16],
                         size_t minimum_size) {
    ure_interface_query_t request = {0};
    ure_interface_response_t response = {0};
    request.header.type = URE_STRUCTURE_INTERFACE_QUERY;
    request.header.size = sizeof(request);
    memcpy(request.interface_id.bytes, id, 16);
    request.minimum_major = 1;
    request.maximum_major = 1;
    response.header.type = URE_STRUCTURE_INTERFACE_RESPONSE;
    response.header.size = sizeof(response);
    if (query(&request, &response, NULL) != URE_RESULT_SUCCESS ||
        response.table_size < minimum_size)
        return NULL;
    return (void *)response.table;
}

static uint8_t *read_bytes(const char *path, size_t *size) {
    FILE *file = NULL;
    long length = 0;
    uint8_t *bytes = NULL;
    if (fopen_s(&file, path, "rb") != 0 || !file)
        return NULL;
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) <= 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    bytes = (uint8_t *)malloc((size_t)length);
    if (!bytes || fread(bytes, 1, (size_t)length, file) != (size_t)length) {
        free(bytes);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *size = (size_t)length;
    return bytes;
}

static ure_scene_budget_t scene_budget(uint64_t content_bytes) {
    ure_scene_budget_t budget = {0};
    budget.header.type = URE_STRUCTURE_SCENE_BUDGET;
    budget.header.size = sizeof(budget);
    budget.max_content_bytes = content_bytes;
    budget.max_uncompressed_bytes = content_bytes * 4;
    budget.max_resident_bytes = UINT64_C(268435456);
    budget.max_resource_count = 4096;
    budget.max_object_count = 100000;
    budget.max_nesting_depth = 64;
    budget.max_decompression_ratio = 256;
    return budget;
}

static ure_native_scene_blob_t scene_blob(const uint8_t *bytes, size_t size) {
    ure_native_scene_blob_t blob = {0};
    blob.header.type = URE_STRUCTURE_NATIVE_SCENE_BLOB;
    blob.header.size = sizeof(blob);
    blob.source_kind = URE_SCENE_SOURCE_MEMORY;
    blob.format = URE_SCENE_FORMAT_URESCENE;
    blob.bytes.data = bytes;
    blob.bytes.size = size;
    blob.schema_max_major = 1;
    blob.budget = scene_budget(UINT64_C(16777216));
    return blob;
}

static ure_native_scene_blob_t memory_blob(const uint8_t *bytes, size_t size,
                                           uint32_t format) {
    ure_native_scene_blob_t blob = scene_blob(bytes, size);
    blob.format = format;
    return blob;
}

static ure_native_scene_blob_t file_blob(const char *path, uint32_t format) {
    ure_native_scene_blob_t blob = {0};
    blob.header.type = URE_STRUCTURE_NATIVE_SCENE_BLOB;
    blob.header.size = sizeof(blob);
    blob.source_kind = URE_SCENE_SOURCE_FILE;
    blob.format = format;
    blob.path_utf8.data = path;
    blob.path_utf8.size = strlen(path);
    blob.schema_max_major = 1;
    blob.budget = scene_budget(UINT64_C(16777216));
    return blob;
}

static ure_result_t validate_blob(const ure_scene_interface_t *scenes,
                                  ure_handle_t instance,
                                  const ure_native_scene_blob_t *blob,
                                  uint32_t *valid) {
    char diagnostics[4096] = {0};
    ure_scene_validation_result_t validation = {0};
    validation.header.type = URE_STRUCTURE_SCENE_VALIDATION_RESULT;
    validation.header.size = sizeof(validation);
    validation.diagnostics_capacity = sizeof(diagnostics);
    validation.diagnostics_data = diagnostics;
    ure_result_t result = scenes->validate(instance, blob, &validation, NULL);
    if (valid)
        *valid = validation.valid;
    return result;
}

static ure_scene_revision_info_t revision_output(void) {
    ure_scene_revision_info_t revision = {0};
    revision.header.type = URE_STRUCTURE_SCENE_REVISION_INFO;
    revision.header.size = sizeof(revision);
    return revision;
}

int main(int argc, char **argv) {
    HMODULE module = NULL;
    uint8_t *bytes = NULL;
    uint8_t *text_bytes = NULL;
    uint8_t *package_bytes = NULL;
    uint8_t *ambiguous_bytes = NULL;
    size_t byte_count = 0;
    size_t text_byte_count = 0;
    size_t package_byte_count = 0;
    size_t ambiguous_byte_count = 0;
    ure_handle_t instance = NULL;
    ure_handle_t scene = NULL;
    ure_handle_t session = NULL;
    ure_handle_t operation = NULL;
    ure_handle_t frame = NULL;
    ure_handle_t cancel_session = NULL;
    ure_handle_t cancel_operation = NULL;
    ure_handle_t package_scene = NULL;
    ure_scene_revision_info_t replacement = {0};
    int ok = 1;
    if (argc != 8)
        return 2;
    bytes = read_bytes(argv[2], &byte_count);
    text_bytes = read_bytes(argv[3], &text_byte_count);
    package_bytes = read_bytes(argv[4], &package_byte_count);
    ambiguous_bytes = read_bytes(argv[5], &ambiguous_byte_count);
    if (!check(bytes && text_bytes && package_bytes && ambiguous_bytes,
               "fixture could not be read"))
        return 3;
    module = LoadLibraryExA(argv[1], NULL,
                            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!check(module != NULL, "runtime could not be loaded")) {
        free(bytes);
        free(text_bytes);
        free(package_bytes);
        free(ambiguous_bytes);
        return 4;
    }
    ure_query_interface_fn query = (ure_query_interface_fn)(void *)
        GetProcAddress(module, "ureQueryInterface");
    ure_get_runtime_manifest_fn get_manifest = (ure_get_runtime_manifest_fn)(void *)
        GetProcAddress(module, "ureGetRuntimeManifest");
    const uint8_t runtime_id[16] = URE_INTERFACE_RUNTIME_UUID_BYTES;
    const uint8_t instance_id[16] = URE_INTERFACE_INSTANCE_UUID_BYTES;
    const uint8_t scene_id[16] = URE_INTERFACE_SCENE_UUID_BYTES;
    const uint8_t session_id[16] = URE_INTERFACE_SESSION_UUID_BYTES;
    const uint8_t operation_id[16] = URE_INTERFACE_OPERATION_UUID_BYTES;
    const uint8_t frame_id[16] = URE_INTERFACE_FRAME_UUID_BYTES;
    const uint8_t event_id[16] = URE_INTERFACE_EVENT_UUID_BYTES;
    const uint8_t error_id[16] = URE_INTERFACE_ERROR_UUID_BYTES;
    const ure_runtime_interface_t *runtimes = (const ure_runtime_interface_t *)
        query_table(query, runtime_id, sizeof(*runtimes));
    const ure_instance_interface_t *instances = (const ure_instance_interface_t *)
        query_table(query, instance_id, sizeof(*instances));
    const ure_scene_interface_t *scenes = (const ure_scene_interface_t *)
        query_table(query, scene_id, sizeof(*scenes));
    const ure_session_interface_t *sessions = (const ure_session_interface_t *)
        query_table(query, session_id, sizeof(*sessions));
    const ure_operation_interface_t *operations = (const ure_operation_interface_t *)
        query_table(query, operation_id, sizeof(*operations));
    const ure_frame_interface_t *frames = (const ure_frame_interface_t *)
        query_table(query, frame_id, sizeof(*frames));
    const ure_event_interface_t *events = (const ure_event_interface_t *)
        query_table(query, event_id, sizeof(*events));
    const ure_error_interface_t *errors = (const ure_error_interface_t *)
        query_table(query, error_id, sizeof(*errors));
    ok &= check(query && get_manifest && runtimes && instances && scenes && sessions && operations && frames && events && errors,
                "required public interface is missing");
    if (!ok)
        goto cleanup;

    {
        ure_runtime_manifest_request_t request = {0};
        ure_runtime_manifest_t manifest = {0};
        request.header.type = URE_STRUCTURE_RUNTIME_MANIFEST_REQUEST;
        request.header.size = sizeof(request);
        request.minimum_major = 1;
        request.maximum_major = 1;
        manifest.header.type = URE_STRUCTURE_RUNTIME_MANIFEST;
        manifest.header.size = sizeof(manifest);
        ok &= check(get_manifest(&request, &manifest, NULL) == URE_RESULT_SUCCESS &&
                        manifest.runtime_major == 1 && manifest.runtime_minor == 0 &&
                        manifest.abi_manifest_json.size != 0,
                    "runtime manifest negotiation failed");
    }

    {
        const uint32_t required[] = {URE_CAPABILITY_LIFECYCLE,
                                     URE_CAPABILITY_FRAME_LEASE,
                                     URE_CAPABILITY_NATIVE_SCENE,
                                     URE_CAPABILITY_RENDER_SESSION};
        ure_instance_frame_budget_t frame_budget = {0};
        ure_instance_create_info_t create = {0};
        frame_budget.header.type = URE_STRUCTURE_INSTANCE_FRAME_BUDGET;
        frame_budget.header.size = sizeof(frame_budget);
        frame_budget.max_retained_frames = 8;
        frame_budget.max_retained_bytes = UINT64_C(268435456);
        create.header.type = URE_STRUCTURE_INSTANCE_CREATE_INFO;
        create.header.size = sizeof(create);
        create.header.next = &frame_budget;
        create.event_capacity = 256;
        create.required_capability_count = 4;
        create.required_capabilities = required;
        ok &= check(runtimes->create_instance(&create, &instance, NULL) ==
                        URE_RESULT_SUCCESS,
                    "instance creation failed");
    }
    if (!ok)
        goto cleanup;

    ok &= check(instances->retain(instance, NULL) == URE_RESULT_SUCCESS &&
                    instances->release(instance, NULL) == URE_RESULT_SUCCESS,
                "instance retain/release failed");
    {
        ure_capability_query_t query_capability = {0};
        ure_capability_descriptor_t descriptor = {0};
        query_capability.header.type = URE_STRUCTURE_CAPABILITY_QUERY;
        query_capability.header.size = sizeof(query_capability);
        query_capability.capability_id = URE_CAPABILITY_LIFECYCLE;
        query_capability.required = 1;
        descriptor.header.type = URE_STRUCTURE_CAPABILITY_DESCRIPTOR;
        descriptor.header.size = sizeof(descriptor);
        ok &= check(instances->query_capability(instance, &query_capability,
                                                &descriptor, NULL) ==
                            URE_RESULT_SUCCESS &&
                        descriptor.capability_id == URE_CAPABILITY_LIFECYCLE &&
                        descriptor.version_major == 1,
                    "capability query failed");
    }

    ure_native_scene_blob_t blob = scene_blob(bytes, byte_count);
    {
        uint32_t valid = 0;
        ok &= check(validate_blob(scenes, instance, &blob, &valid) ==
                            URE_RESULT_SUCCESS && valid,
                    "strict scene validation failed");
    }

    {
        uint32_t valid = 0;
        ure_native_scene_blob_t text_file = file_blob(argv[3], URE_SCENE_FORMAT_URE);
        ok &= check(validate_blob(scenes, instance, &text_file, &valid) ==
                            URE_RESULT_SUCCESS && valid,
                    "file-backed .ure validation failed");
        ure_native_scene_blob_t package_file =
            file_blob(argv[4], URE_SCENE_FORMAT_UREPKG);
        valid = 0;
        ok &= check(validate_blob(scenes, instance, &package_file, &valid) ==
                            URE_RESULT_SUCCESS && valid,
                    "file-backed .urepkg validation failed");
    }

    {
        ure_native_scene_blob_t package_blob = memory_blob(
            package_bytes, package_byte_count, URE_SCENE_FORMAT_UREPKG);
        ure_scene_revision_info_t package_revision = revision_output();
        ok &= check(scenes->create(instance, &package_blob, &package_scene,
                                   &package_revision, NULL) == URE_RESULT_SUCCESS &&
                        package_revision.selected_package_scene.size != 0,
                    "in-memory package scene creation failed");
        if (package_scene) {
            scenes->release(package_scene, NULL);
            package_scene = NULL;
        }
        ure_native_scene_blob_t ambiguous = memory_blob(
            ambiguous_bytes, ambiguous_byte_count, URE_SCENE_FORMAT_UREPKG);
        ok &= check(validate_blob(scenes, instance, &ambiguous, NULL) ==
                        URE_RESULT_MALFORMED_DATA,
                    "ambiguous package selection was accepted");
        package_bytes[0] ^= UINT8_C(0xff);
        ok &= check(validate_blob(scenes, instance, &package_blob, NULL) ==
                        URE_RESULT_MALFORMED_DATA,
                    "corrupt package was accepted");
        package_bytes[0] ^= UINT8_C(0xff);
    }

    {
        ure_native_scene_blob_t unsupported = blob;
        uint8_t *unsupported_bytes = (uint8_t *)malloc(byte_count);
        ok &= check(unsupported_bytes != NULL, "schema fixture allocation failed");
        if (unsupported_bytes) {
            memcpy(unsupported_bytes, bytes, byte_count);
            if (byte_count >= 12) {
                unsupported_bytes[8] = 2;
                unsupported_bytes[9] = 0;
                unsupported_bytes[10] = 0;
                unsupported_bytes[11] = 0;
            }
            unsupported.bytes.data = unsupported_bytes;
            ok &= check(validate_blob(scenes, instance, &unsupported, NULL) ==
                            URE_RESULT_INCOMPATIBLE_VERSION,
                        "unsupported native schema was not rejected");
            free(unsupported_bytes);
        }
        ure_native_scene_blob_t missing = memory_blob(
            text_bytes, text_byte_count, URE_SCENE_FORMAT_URE);
        ok &= check(validate_blob(scenes, instance, &missing, NULL) ==
                        URE_RESULT_MALFORMED_DATA,
                    "missing external resources were accepted");
        {
            static const uint8_t nested[] = "[[[[[[[[[]]]]]]]]]";
            ure_native_scene_blob_t nested_blob =
                memory_blob(nested, sizeof(nested) - 1, URE_SCENE_FORMAT_URE);
            nested_blob.budget.max_nesting_depth = 8;
            ok &= check(validate_blob(scenes, instance, &nested_blob, NULL) ==
                            URE_RESULT_BUDGET_EXHAUSTED,
                        "nesting-depth budget was not enforced");
        }
    }
    ure_scene_revision_info_t revision = revision_output();
    ok &= check(scenes->create(instance, &blob, &scene, &revision, NULL) ==
                    URE_RESULT_SUCCESS && revision.revision == 1,
                "scene creation failed");
    ok &= check(scenes->retain(scene, NULL) == URE_RESULT_SUCCESS &&
                    scenes->release(scene, NULL) == URE_RESULT_SUCCESS,
                "scene retain/release failed");
    if (!ok)
        goto cleanup;

    ure_objective_envelope_t objective = {0};
    objective.header.type = URE_STRUCTURE_OBJECTIVE_ENVELOPE;
    objective.header.size = sizeof(objective);
    objective.sample_budget = 1;
    {
        ure_handle_t error = NULL;
        const ure_result_t result = sessions->create(instance, scene, &objective,
                                                     &session, &error);
        if (result != URE_RESULT_SUCCESS && error) {
            ure_error_info_t info = {0};
            info.header.type = URE_STRUCTURE_ERROR_INFO;
            info.header.size = sizeof(info);
            if (errors->get_info(error, &info) == URE_RESULT_SUCCESS)
                fprintf(stderr, "session error: %.*s\n", (int)info.message.size,
                        info.message.data);
            errors->release(error);
        }
        ok &= check(result == URE_RESULT_SUCCESS, "session creation failed");
    }
    ok &= check(sessions->retain(session, NULL) == URE_RESULT_SUCCESS &&
                    sessions->release(session, NULL) == URE_RESULT_SUCCESS,
                "session retain/release failed");
    ok &= check(sessions->start(session, &operation, NULL) == URE_RESULT_SUCCESS,
                "render start failed");
    ok &= check(operations->retain(operation, NULL) == URE_RESULT_SUCCESS &&
                    operations->release(operation, NULL) == URE_RESULT_SUCCESS,
                "operation retain/release failed");
    replacement = revision_output();
    ok &= check(scenes->replace(scene, &blob, &replacement, NULL) ==
                    URE_RESULT_SUCCESS && replacement.revision == 2,
                "replacement during active work failed");
    {
        ure_handle_t error = NULL;
        const ure_result_t result = operations->wait(
            operation, UINT64_C(30000000000), &error);
        if (result != URE_RESULT_SUCCESS && error) {
            ure_error_info_t info = {0};
            info.header.type = URE_STRUCTURE_ERROR_INFO;
            info.header.size = sizeof(info);
            if (errors->get_info(error, &info) == URE_RESULT_SUCCESS)
                fprintf(stderr, "render error: %.*s\n", (int)info.message.size,
                        info.message.data);
            errors->release(error);
        }
        ok &= check(result == URE_RESULT_SUCCESS, "render operation failed");
    }
    {
        ure_operation_info_t info = {0};
        info.header.type = URE_STRUCTURE_OPERATION_INFO;
        info.header.size = sizeof(info);
        ok &= check(operations->get_info(operation, &info, NULL) ==
                            URE_RESULT_SUCCESS &&
                        info.state == URE_OPERATION_STATE_SUCCEEDED &&
                        info.completed_work == info.total_work,
                    "terminal operation information is invalid");
    }
    ok &= check(sessions->acquire_frame(session, &frame, NULL) == URE_RESULT_SUCCESS,
                "frame acquisition failed");
    if (frame) {
        ure_frame_info_t info = {0};
        ure_frame_plane_info_t plane = {0};
        ure_frame_map_t map = {0};
        ure_frame_copy_info_t copy = {0};
        uint8_t *snapshot = NULL;
        ure_image_evidence_t mapped_evidence = {0};
        ure_image_evidence_t copied_evidence = {0};
        ok &= check(frames->retain(frame, NULL) == URE_RESULT_SUCCESS &&
                        frames->release(frame, NULL) == URE_RESULT_SUCCESS,
                    "frame retain/release failed");
        info.header.type = URE_STRUCTURE_FRAME_INFO;
        info.header.size = sizeof(info);
        ok &= check(frames->get_info(frame, &info, NULL) == URE_RESULT_SUCCESS &&
                        info.width != 0 && info.height != 0 &&
                        info.sample_count == 1 &&
                        memcmp(info.scene_revision_identity.bytes,
                               revision.revision_identity.bytes, 32) == 0,
                    "frame metadata differs from the bound revision");
        plane.header.type = URE_STRUCTURE_FRAME_PLANE_INFO;
        plane.header.size = sizeof(plane);
        ok &= check(frames->get_plane_info(frame, 0, &plane, NULL) ==
                        URE_RESULT_SUCCESS && plane.byte_extent != 0,
                    "frame plane metadata is unavailable");
        map.header.type = URE_STRUCTURE_FRAME_MAP;
        map.header.size = sizeof(map);
        ok &= check(frames->map_plane_read(frame, 0, &map, NULL) ==
                        URE_RESULT_SUCCESS && map.data != NULL &&
                        map.byte_extent == plane.byte_extent,
                    "immutable frame mapping failed");
        snapshot = (uint8_t *)malloc((size_t)plane.byte_extent);
        ok &= check(snapshot != NULL, "frame snapshot allocation failed");
        if (snapshot && map.data)
            memcpy(snapshot, map.data, (size_t)plane.byte_extent);
        if (map.data)
            ok &= check(ure_write_pfm_rgba(argv[6], map.data, info.width,
                                           info.height, map.row_stride,
                                           &mapped_evidence),
                        "mapped render image is invalid");
        if (map.map_token)
            ok &= check(frames->unmap_plane(frame, map.map_token, NULL) ==
                            URE_RESULT_SUCCESS,
                        "frame unmap failed");
        if (snapshot) {
            uint8_t *copied = (uint8_t *)malloc((size_t)plane.byte_extent);
            ok &= check(copied != NULL, "frame copy allocation failed");
            if (copied) {
                copy.header.type = URE_STRUCTURE_FRAME_COPY_INFO;
                copy.header.size = sizeof(copy);
                copy.frame = frame;
                copy.destination = copied;
                copy.destination_size = plane.byte_extent;
                copy.destination_row_stride = plane.row_stride;
                copy.destination_slice_stride = plane.slice_stride;
                ok &= check(frames->copy_plane(&copy, NULL) == URE_RESULT_SUCCESS &&
                                memcmp(snapshot, copied, (size_t)plane.byte_extent) == 0,
                            "immutable frame copy differs from mapped bytes");
                ok &= check(ure_write_pfm_rgba(argv[7], copied, info.width,
                                               info.height, plane.row_stride,
                                               &copied_evidence),
                            "copied render image is invalid");
                ok &= check(mapped_evidence.pixel_count ==
                                    copied_evidence.pixel_count &&
                                mapped_evidence.minimum_rgb ==
                                    copied_evidence.minimum_rgb &&
                                mapped_evidence.maximum_rgb ==
                                    copied_evidence.maximum_rgb &&
                                mapped_evidence.mean_rgb ==
                                    copied_evidence.mean_rgb,
                            "map and copy image evidence differs");
                free(copied);
            }
            free(snapshot);
        }
    }

    {
        uint32_t event_count = 0;
        ure_event_record_t first_event = {0};
        first_event.header.type = URE_STRUCTURE_EVENT_RECORD;
        first_event.header.size = sizeof(first_event);
        ok &= check(events->wait(instance, UINT64_C(1000000000), &first_event,
                                 NULL) == URE_RESULT_SUCCESS,
                    "event wait failed");
        ++event_count;
        for (;;) {
            ure_event_record_t event = {0};
            ure_result_t result = URE_RESULT_SUCCESS;
            event.header.type = URE_STRUCTURE_EVENT_RECORD;
            event.header.size = sizeof(event);
            result = events->poll(instance, &event, NULL);
            if (result == URE_RESULT_INCOMPLETE)
                break;
            ok &= check(result == URE_RESULT_SUCCESS,
                        "progressive event polling failed");
            if (result != URE_RESULT_SUCCESS)
                break;
            ++event_count;
        }
        ok &= check(event_count != 0, "render emitted no public events");
    }

    {
        ure_objective_envelope_t cancel_objective = objective;
        ure_bool32_t accepted = 0;
        ure_result_t waited = URE_RESULT_SUCCESS;
        cancel_objective.sample_budget = 100000;
        ok &= check(sessions->create(instance, scene, &cancel_objective,
                                     &cancel_session, NULL) == URE_RESULT_SUCCESS &&
                        sessions->start(cancel_session, &cancel_operation, NULL) ==
                            URE_RESULT_SUCCESS,
                    "cancelable objective could not start");
        if (cancel_operation) {
            ure_session_info_t session_info = {0};
            ure_operation_info_t operation_info = {0};
            uint32_t attempt = 0;
            session_info.header.type = URE_STRUCTURE_SESSION_INFO;
            session_info.header.size = sizeof(session_info);
            operation_info.header.type = URE_STRUCTURE_OPERATION_INFO;
            operation_info.header.size = sizeof(operation_info);
            for (attempt = 0; attempt != 1000; ++attempt) {
                if (operations->get_info(cancel_operation, &operation_info,
                                         NULL) == URE_RESULT_SUCCESS &&
                    operation_info.state == URE_OPERATION_STATE_RUNNING)
                    break;
                Sleep(1);
            }
            ok &= check(sessions->get_info(cancel_session, &session_info, NULL) ==
                                URE_RESULT_SUCCESS &&
                            session_info.state == URE_SESSION_STATE_RUNNING &&
                            operation_info.state == URE_OPERATION_STATE_RUNNING,
                        "running session information is invalid");
            ok &= check(sessions->pause(cancel_session, NULL) ==
                                URE_RESULT_SUCCESS &&
                            sessions->resume(cancel_session, NULL) ==
                                URE_RESULT_SUCCESS,
                        "session pause/resume failed");
            ok &= check(operations->request_cancel(cancel_operation, &accepted,
                                                   NULL) == URE_RESULT_SUCCESS &&
                            accepted,
                        "operation cancellation was not accepted");
            waited = operations->wait(cancel_operation, UINT64_C(30000000000),
                                      NULL);
            ok &= check(waited == URE_RESULT_CANCELED ||
                            waited == URE_RESULT_SUCCESS,
                        "canceled operation did not reach a terminal state");
        }
    }

    {
        bytes[0] ^= UINT8_C(0xff);
        ure_scene_revision_info_t rejected = revision_output();
        ure_handle_t replacement_error = NULL;
        ok &= check(scenes->replace(scene, &blob, &rejected,
                                    &replacement_error) ==
                            URE_RESULT_MALFORMED_DATA &&
                        replacement_error != NULL,
                    "corrupt replacement was not rejected");
        if (replacement_error) {
            ure_error_info_t error_info = {0};
            error_info.header.type = URE_STRUCTURE_ERROR_INFO;
            error_info.header.size = sizeof(error_info);
            ok &= check(errors->retain(replacement_error) == URE_RESULT_SUCCESS &&
                            errors->get_info(replacement_error, &error_info) ==
                                URE_RESULT_SUCCESS &&
                            error_info.result == URE_RESULT_MALFORMED_DATA &&
                            errors->release(replacement_error) ==
                                URE_RESULT_SUCCESS &&
                            errors->release(replacement_error) ==
                                URE_RESULT_SUCCESS,
                        "retained structured error lifecycle failed");
        }
        bytes[0] ^= UINT8_C(0xff);
        ure_scene_revision_info_t retained = revision_output();
        ok &= check(scenes->get_revision(scene, &retained, NULL) ==
                        URE_RESULT_SUCCESS && retained.revision == 2 &&
                        memcmp(retained.revision_identity.bytes,
                               replacement.revision_identity.bytes, 32) == 0,
                    "failed replacement changed the accepted revision");
        ok &= check(sessions->bind_scene(session, scene, &retained, NULL) ==
                        URE_RESULT_SUCCESS && retained.revision == 2,
                    "session rebind failed");
    }

    {
        ure_native_scene_blob_t bounded = blob;
        char diagnostics[512] = {0};
        ure_scene_validation_result_t validation = {0};
        bounded.budget = scene_budget(128);
        validation.header.type = URE_STRUCTURE_SCENE_VALIDATION_RESULT;
        validation.header.size = sizeof(validation);
        validation.diagnostics_capacity = sizeof(diagnostics);
        validation.diagnostics_data = diagnostics;
        ok &= check(scenes->validate(instance, &bounded, &validation, NULL) ==
                        URE_RESULT_BUDGET_EXHAUSTED,
                    "content budget exhaustion was not fail-loud");
        bounded = blob;
        bounded.budget.max_resident_bytes = 16;
        ok &= check(scenes->validate(instance, &bounded, &validation, NULL) ==
                        URE_RESULT_BUDGET_EXHAUSTED,
                    "resident-memory budget exhaustion was not fail-loud");
    }

    {
        ure_session_info_t session_info = {0};
        session_info.header.type = URE_STRUCTURE_SESSION_INFO;
        session_info.header.size = sizeof(session_info);
        ok &= check(sessions->get_info(session, &session_info, NULL) ==
                            URE_RESULT_SUCCESS &&
                        session_info.bound_scene_revision == 2,
                    "rebound session information is invalid");
        ok &= check(sessions->reset(session, URE_SCENE_RESET_EXPLICIT, NULL) ==
                            URE_RESULT_SUCCESS,
                    "explicit session reset failed");
    }

cleanup:
    if (cancel_operation && operations)
        operations->release(cancel_operation, NULL);
    if (cancel_session && sessions) {
        sessions->close(cancel_session, NULL);
        sessions->release(cancel_session, NULL);
    }
    if (package_scene && scenes)
        scenes->release(package_scene, NULL);
    if (frame && frames)
        frames->release(frame, NULL);
    if (operation && operations)
        operations->release(operation, NULL);
    if (session && sessions) {
        sessions->close(session, NULL);
        sessions->release(session, NULL);
    }
    if (scene && scenes)
        scenes->release(scene, NULL);
    if (instance && instances) {
        instances->close(instance, NULL);
        instances->release(instance, NULL);
    }
    FreeLibrary(module);
    free(bytes);
    free(text_bytes);
    free(package_bytes);
    free(ambiguous_bytes);
    return ok ? 0 : 1;
}
