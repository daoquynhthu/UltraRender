#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <ultrarender/ure_loader.h>

#include "private_conformance_fixture.h"

#define CHECK(expression)    \
    do {                     \
        if (!(expression))   \
            return __LINE__; \
    } while (0)

static const void *query_table(ure_query_interface_fn query,
                               const uint8_t id[16], uint64_t minimum_size) {
    ure_interface_query_t request = {0};
    ure_interface_response_t response = {0};
    request.header.type = URE_STRUCTURE_INTERFACE_QUERY;
    request.header.size = (uint32_t)sizeof(request);
    memcpy(request.interface_id.bytes, id, 16);
    request.minimum_major = 1;
    request.maximum_major = 1;
    response.header.type = URE_STRUCTURE_INTERFACE_RESPONSE;
    response.header.size = (uint32_t)sizeof(response);
    if (query(&request, &response, NULL) != URE_RESULT_SUCCESS ||
        response.table_size < minimum_size)
        return NULL;
    return response.table;
}

static int release_error(const ure_error_interface_t *errors,
                         ure_handle_t error, ure_result_t expected) {
    ure_error_info_t info = {0};
    info.header.type = URE_STRUCTURE_ERROR_INFO;
    info.header.size = (uint32_t)sizeof(info);
    CHECK(error != NULL);
    CHECK(errors->get_info(error, &info) == URE_RESULT_SUCCESS);
    CHECK(info.result == expected && info.message.size != 0);
    CHECK(errors->release(error) == URE_RESULT_SUCCESS);
    return 0;
}

static int nonzero_digest(const ure_digest256_t *digest) {
    uint32_t index = 0;
    for (; index < 32; ++index) {
        if (digest->bytes[index] != 0)
            return 1;
    }
    return 0;
}

static int run_test(ure_query_interface_fn query) {
    static const uint8_t runtime_id[16] = URE_INTERFACE_RUNTIME_UUID_BYTES;
    static const uint8_t instance_id[16] = URE_INTERFACE_INSTANCE_UUID_BYTES;
    static const uint8_t error_id[16] = URE_INTERFACE_ERROR_UUID_BYTES;
    static const uint8_t event_id[16] = URE_INTERFACE_EVENT_UUID_BYTES;
    static const uint8_t frame_id[16] = URE_INTERFACE_FRAME_UUID_BYTES;
    static const uint8_t conformance_id[16] =
        URE_PRIVATE_INTERFACE_CONFORMANCE_UUID_BYTES;
    const ure_runtime_interface_t *runtime =
        query_table(query, runtime_id, sizeof(ure_runtime_interface_t));
    const ure_instance_interface_t *instances =
        query_table(query, instance_id, sizeof(ure_instance_interface_t));
    const ure_error_interface_t *errors =
        query_table(query, error_id, sizeof(ure_error_interface_t));
    const ure_event_interface_t *events =
        query_table(query, event_id, sizeof(ure_event_interface_t));
    const ure_frame_interface_t *frames =
        query_table(query, frame_id, sizeof(ure_frame_interface_t));
    const ure_private_conformance_interface_t *conformance = query_table(
        query, conformance_id, sizeof(ure_private_conformance_interface_t));
    ure_instance_frame_budget_t budget = {0};
    ure_instance_create_info_t create = {0};
    ure_private_conformance_frame_request_t request = {0};
    ure_handle_t instance = NULL;
    ure_handle_t frame_a = NULL;
    ure_handle_t frame_b = NULL;
    ure_handle_t frame_c = NULL;
    ure_handle_t error = NULL;
    uint32_t required = URE_CAPABILITY_FRAME_LEASE;
    uint8_t snapshot[64] = {0};
    CHECK(runtime && instances && errors && events && frames && conformance);

    budget.header.type = URE_STRUCTURE_INSTANCE_FRAME_BUDGET;
    budget.header.size = (uint32_t)sizeof(budget);
    budget.max_retained_frames = 2;
    budget.max_retained_bytes = 128;
    create.header.type = URE_STRUCTURE_INSTANCE_CREATE_INFO;
    create.header.size = (uint32_t)sizeof(create);
    create.header.next = &budget;
    create.event_capacity = 8;
    create.required_capability_count = 1;
    create.required_capabilities = &required;
    CHECK(runtime->create_instance(&create, &instance, &error) ==
          URE_RESULT_SUCCESS);
    CHECK(instance != NULL && error == NULL);

    request.header.type = URE_PRIVATE_STRUCTURE_CONFORMANCE_FRAME_REQUEST;
    request.header.size = (uint32_t)sizeof(request);
    request.width = 2;
    request.height = 2;
    request.seed = 7;
    CHECK(conformance->produce_frame(instance, &request, &frame_a, &error) ==
          URE_RESULT_SUCCESS);
    CHECK(frame_a != NULL && error == NULL);
    {
        ure_frame_info_t info = {0};
        ure_frame_plane_info_t plane = {0};
        ure_frame_map_t map = {0};
        ure_frame_copy_info_t copy = {0};
        uint8_t copied[80] = {0};
        info.header.type = URE_STRUCTURE_FRAME_INFO;
        info.header.size = (uint32_t)sizeof(info);
        CHECK(frames->get_info(frame_a, &info, &error) == URE_RESULT_SUCCESS);
        CHECK(info.width == 2 && info.height == 2 && info.plane_count == 1 &&
              info.retained_bytes == 64 &&
              info.completion == URE_FRAME_COMPLETION_COMPLETE);
        CHECK(nonzero_digest(&info.frame_identity) &&
              nonzero_digest(&info.provenance_identity));
        plane.header.type = URE_STRUCTURE_FRAME_PLANE_INFO;
        plane.header.size = (uint32_t)sizeof(plane);
        CHECK(frames->get_plane_info(frame_a, 0, &plane, &error) ==
              URE_RESULT_SUCCESS);
        CHECK(plane.plane_schema == URE_FRAME_PLANE_COLOR &&
              plane.scalar_type == URE_SCALAR_TYPE_FLOAT32 &&
              plane.component_layout == URE_COMPONENT_LAYOUT_RGBA &&
              plane.normalization == URE_NORMALIZATION_SAMPLE_MEAN &&
              plane.row_stride == 32 && plane.byte_extent == 64);
        CHECK(nonzero_digest(&plane.observable_identity) &&
              nonzero_digest(&plane.unit_identity) &&
              nonzero_digest(&plane.measure_identity) &&
              nonzero_digest(&plane.time_identity) &&
              nonzero_digest(&plane.provenance_identity));
        map.header.type = URE_STRUCTURE_FRAME_MAP;
        map.header.size = (uint32_t)sizeof(map);
        CHECK(frames->map_plane_read(frame_a, 0, &map, &error) ==
              URE_RESULT_SUCCESS);
        CHECK(map.frame == frame_a && map.byte_extent == 64 && map.data != NULL &&
              map.map_token != 0);
        memcpy(snapshot, map.data, sizeof(snapshot));
        CHECK(frames->map_plane_read(frame_a, 0, &map, &error) == URE_RESULT_BUSY);
        CHECK(release_error(errors, error, URE_RESULT_BUSY) == 0);
        CHECK(frames->release(frame_a, &error) == URE_RESULT_BUSY);
        CHECK(release_error(errors, error, URE_RESULT_BUSY) == 0);
        CHECK(frames->unmap_plane(frame_a, map.map_token + 1, &error) ==
              URE_RESULT_INVALID_ARGUMENT);
        CHECK(release_error(errors, error, URE_RESULT_INVALID_ARGUMENT) == 0);
        CHECK(frames->unmap_plane(frame_a, map.map_token, &error) ==
              URE_RESULT_SUCCESS);
        CHECK(frames->unmap_plane(frame_a, map.map_token, &error) ==
              URE_RESULT_INVALID_ARGUMENT);
        CHECK(release_error(errors, error, URE_RESULT_INVALID_ARGUMENT) == 0);
        copy.header.type = URE_STRUCTURE_FRAME_COPY_INFO;
        copy.header.size = (uint32_t)sizeof(copy);
        copy.frame = frame_a;
        copy.destination = copied;
        copy.destination_size = sizeof(copied);
        copy.destination_row_stride = 40;
        copy.destination_slice_stride = sizeof(copied);
        CHECK(frames->copy_plane(&copy, &error) == URE_RESULT_SUCCESS);
        CHECK(memcmp(copied, snapshot, 32) == 0 &&
              memcmp(copied + 40, snapshot + 32, 32) == 0);
        copy.destination_size = 63;
        CHECK(frames->copy_plane(&copy, &error) == URE_RESULT_BUFFER_TOO_SMALL);
        CHECK(release_error(errors, error, URE_RESULT_BUFFER_TOO_SMALL) == 0);
    }

    request.seed = 9;
    CHECK(conformance->produce_frame(instance, &request, &frame_b, &error) ==
          URE_RESULT_SUCCESS);
    request.seed = 11;
    CHECK(conformance->produce_frame(instance, &request, &frame_c, &error) ==
          URE_RESULT_BACKPRESSURE);
    CHECK(frame_c == NULL);
    CHECK(release_error(errors, error, URE_RESULT_BACKPRESSURE) == 0);
    {
        ure_frame_map_t map = {0};
        map.header.type = URE_STRUCTURE_FRAME_MAP;
        map.header.size = (uint32_t)sizeof(map);
        CHECK(frames->map_plane_read(frame_a, 0, &map, &error) ==
              URE_RESULT_SUCCESS);
        CHECK(memcmp(map.data, snapshot, sizeof(snapshot)) == 0);
        CHECK(frames->unmap_plane(frame_a, map.map_token, &error) ==
              URE_RESULT_SUCCESS);
    }
    CHECK(instances->release(instance, &error) == URE_RESULT_BUSY);
    CHECK(release_error(errors, error, URE_RESULT_BUSY) == 0);
    CHECK(frames->retain(frame_a, &error) == URE_RESULT_SUCCESS);
    CHECK(frames->release(frame_a, &error) == URE_RESULT_SUCCESS);
    CHECK(frames->release(frame_a, &error) == URE_RESULT_SUCCESS);
    CHECK(frames->get_info(frame_a, NULL, &error) == URE_RESULT_INVALID_HANDLE);
    CHECK(release_error(errors, error, URE_RESULT_INVALID_HANDLE) == 0);
    CHECK(conformance->produce_frame(instance, &request, &frame_c, &error) ==
          URE_RESULT_SUCCESS);
    {
        ure_event_record_t event = {0};
        uint32_t count = 0;
        event.header.type = URE_STRUCTURE_EVENT_RECORD;
        event.header.size = (uint32_t)sizeof(event);
        while (events->poll(instance, &event, &error) == URE_RESULT_SUCCESS) {
            CHECK(event.event_type == URE_EVENT_FRAME_READY);
            CHECK(event.frame != NULL);
            ++count;
        }
        CHECK(count == 3 && error == NULL);
    }
    CHECK(instances->close(instance, &error) == URE_RESULT_SUCCESS);
    CHECK(frames->release(frame_b, &error) == URE_RESULT_SUCCESS);
    CHECK(frames->release(frame_c, &error) == URE_RESULT_SUCCESS);
    CHECK(instances->release(instance, &error) == URE_RESULT_SUCCESS);
    {
        uint64_t live = UINT64_MAX;
        CHECK(conformance->live_handle_count(&live) == URE_RESULT_SUCCESS);
        CHECK(live == 0);
    }
    return 0;
}

int main(int argc, char **argv) {
    HMODULE module = NULL;
    ure_query_interface_fn query = NULL;
    int result = 0;
    if (argc != 2)
        return 1;
    module = LoadLibraryA(argv[1]);
    if (!module)
        return 2;
    query = (ure_query_interface_fn)(uintptr_t)GetProcAddress(
        module, "ureQueryInterface");
    if (!query) {
        FreeLibrary(module);
        return 3;
    }
    result = run_test(query);
    FreeLibrary(module);
    if (result != 0)
        fprintf(stderr, "frame lease check failed at line %d\n", result);
    return result;
}
