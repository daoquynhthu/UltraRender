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

static ure_bootstrap_diagnostic_t diagnostic(char* text, uint32_t capacity) {
    ure_bootstrap_diagnostic_t value = {0};
    value.header.type = URE_STRUCTURE_BOOTSTRAP_DIAGNOSTIC;
    value.header.size = (uint32_t)sizeof(value);
    value.message_data = text;
    value.message_capacity = capacity;
    return value;
}

static const void* query_table(ure_query_interface_fn query,
                               const uint8_t id[16], uint64_t minimum_size) {
    char text[128] = {0};
    ure_bootstrap_diagnostic_t diag = diagnostic(text, (uint32_t)sizeof(text));
    ure_interface_query_t request = {0};
    ure_interface_response_t response = {0};
    request.header.type = URE_STRUCTURE_INTERFACE_QUERY;
    request.header.size = (uint32_t)sizeof(request);
    memcpy(request.interface_id.bytes, id, 16);
    request.maximum_minor = 1;
    response.header.type = URE_STRUCTURE_INTERFACE_RESPONSE;
    response.header.size = (uint32_t)sizeof(response);
    if (query(&request, &response, &diag) != URE_RESULT_SUCCESS ||
        response.table_size < minimum_size)
        return NULL;
    return response.table;
}

typedef struct query_thread_context_t {
    ure_get_runtime_manifest_fn get_manifest;
    ure_query_interface_fn query;
    const uint8_t* id;
    volatile LONG failures;
} query_thread_context_t;

static DWORD WINAPI query_thread(void* parameter) {
    query_thread_context_t* context = (query_thread_context_t*)parameter;
    uint32_t index = 0;
    for (; index < 1000; ++index) {
        ure_runtime_manifest_request_t request = {0};
        ure_runtime_manifest_t manifest = {0};
        char text[64] = {0};
        ure_bootstrap_diagnostic_t diag = diagnostic(text, (uint32_t)sizeof(text));
        request.header.type = URE_STRUCTURE_RUNTIME_MANIFEST_REQUEST;
        request.header.size = (uint32_t)sizeof(request);
        request.maximum_minor = 1;
        manifest.header.type = URE_STRUCTURE_RUNTIME_MANIFEST;
        manifest.header.size = (uint32_t)sizeof(manifest);
        if (context->get_manifest(&request, &manifest, &diag) !=
                URE_RESULT_SUCCESS ||
            manifest.runtime_minor != 1 || manifest.abi_manifest_json.size == 0) {
            InterlockedIncrement(&context->failures);
        }
        if (!query_table(context->query, context->id,
                         sizeof(ure_runtime_interface_t))) {
            InterlockedIncrement(&context->failures);
        }
    }
    return 0;
}

typedef struct operation_thread_context_t {
    const ure_operation_interface_t* operations;
    ure_handle_t operation;
    HANDLE start;
    ure_bool32_t accepted;
    volatile LONG failures;
} operation_thread_context_t;

static DWORD WINAPI retain_operation_thread(void* parameter) {
    operation_thread_context_t* context = (operation_thread_context_t*)parameter;
    uint32_t index = 0;
    CHECK(WaitForSingleObject(context->start, INFINITE) == WAIT_OBJECT_0);
    for (; index < 1000; ++index) {
        ure_handle_t error = NULL;
        if (context->operations->retain(context->operation, &error) !=
                URE_RESULT_SUCCESS ||
            context->operations->release(context->operation, &error) !=
                URE_RESULT_SUCCESS ||
            error != NULL) {
            InterlockedIncrement(&context->failures);
        }
    }
    return 0;
}

static DWORD WINAPI cancel_operation_thread(void* parameter) {
    operation_thread_context_t* context = (operation_thread_context_t*)parameter;
    ure_handle_t error = NULL;
    CHECK(WaitForSingleObject(context->start, INFINITE) == WAIT_OBJECT_0);
    if (context->operations->request_cancel(context->operation,
                                            &context->accepted,
                                            &error) != URE_RESULT_SUCCESS ||
        !context->accepted || error != NULL) {
        InterlockedIncrement(&context->failures);
    }
    return 0;
}

typedef struct close_thread_context_t {
    const ure_instance_interface_t* instances;
    ure_handle_t instance;
    HANDLE start;
    volatile LONG failures;
} close_thread_context_t;

static DWORD WINAPI close_instance_thread(void* parameter) {
    close_thread_context_t* context = (close_thread_context_t*)parameter;
    ure_handle_t error = NULL;
    CHECK(WaitForSingleObject(context->start, INFINITE) == WAIT_OBJECT_0);
    if (context->instances->close(context->instance, &error) !=
            URE_RESULT_SUCCESS ||
        error != NULL) {
        InterlockedIncrement(&context->failures);
    }
    return 0;
}

static int inspect_error(const ure_error_interface_t* errors,
                         ure_handle_t error, ure_result_t expected) {
    ure_error_info_t info = {0};
    info.header.type = URE_STRUCTURE_ERROR_INFO;
    info.header.size = (uint32_t)sizeof(info);
    CHECK(error != NULL);
    CHECK(errors->get_info(error, &info) == URE_RESULT_SUCCESS);
    CHECK(info.result == expected && info.domain == URE_ERROR_DOMAIN_CORE &&
          info.message.size != 0);
    CHECK(info.structured_detail_schema == URE_PAYLOAD_ERROR &&
          info.structured_detail.size == 8);
    CHECK(info.structured_detail.data[0] == (uint8_t)(URE_PAYLOAD_ERROR & 0xffU));
    CHECK(errors->release(error) == URE_RESULT_SUCCESS);
    return 0;
}

static int run_lifecycle(ure_get_runtime_manifest_fn get_manifest,
                         ure_query_interface_fn query) {
    static const uint8_t runtime_id[16] = URE_INTERFACE_RUNTIME_UUID_BYTES;
    static const uint8_t instance_id[16] = URE_INTERFACE_INSTANCE_UUID_BYTES;
    static const uint8_t error_id[16] = URE_INTERFACE_ERROR_UUID_BYTES;
    static const uint8_t operation_id[16] = URE_INTERFACE_OPERATION_UUID_BYTES;
    static const uint8_t event_id[16] = URE_INTERFACE_EVENT_UUID_BYTES;
    static const uint8_t conformance_id[16] =
        URE_PRIVATE_INTERFACE_CONFORMANCE_UUID_BYTES;
    const ure_runtime_interface_t* runtime =
        query_table(query, runtime_id, sizeof(ure_runtime_interface_t));
    const ure_instance_interface_t* instances =
        query_table(query, instance_id, sizeof(ure_instance_interface_t));
    const ure_error_interface_t* errors =
        query_table(query, error_id, sizeof(ure_error_interface_t));
    const ure_operation_interface_t* operations =
        query_table(query, operation_id, sizeof(ure_operation_interface_t));
    const ure_event_interface_t* events =
        query_table(query, event_id, sizeof(ure_event_interface_t));
    const ure_private_conformance_interface_t* conformance = query_table(
        query, conformance_id, sizeof(ure_private_conformance_interface_t));
    ure_instance_create_info_t create_info = {0};
    ure_handle_t instance = NULL;
    ure_handle_t error = NULL;
    uint32_t required = URE_CAPABILITY_FRAME_LEASE;
    CHECK(runtime && instances && errors && operations && events && conformance);
    {
        query_thread_context_t context = {get_manifest, query, runtime_id, 0};
        HANDLE threads[8] = {0};
        uint32_t index = 0;
        for (; index < 8; ++index) {
            threads[index] = CreateThread(NULL, 0, query_thread, &context, 0, NULL);
            CHECK(threads[index] != NULL);
        }
        CHECK(WaitForMultipleObjects(8, threads, TRUE, INFINITE) == WAIT_OBJECT_0);
        for (index = 0; index < 8; ++index)
            CloseHandle(threads[index]);
        CHECK(context.failures == 0);
    }

    create_info.header.type = URE_STRUCTURE_INSTANCE_CREATE_INFO;
    create_info.header.size = (uint32_t)sizeof(create_info);
    create_info.event_capacity = 4;
    create_info.required_capability_count = 1;
    create_info.required_capabilities = &required;
    CHECK(runtime->create_instance(&create_info, &instance, &error) ==
          URE_RESULT_CAPABILITY_UNAVAILABLE);
    CHECK(inspect_error(errors, error, URE_RESULT_CAPABILITY_UNAVAILABLE) == 0);

    required = URE_CAPABILITY_LIFECYCLE;
    CHECK(runtime->create_instance(&create_info, &instance, &error) ==
          URE_RESULT_SUCCESS);
    CHECK(instance != NULL && error == NULL);
    {
        ure_capability_query_t capability = {0};
        ure_capability_descriptor_t descriptor = {0};
        capability.header.type = URE_STRUCTURE_CAPABILITY_QUERY;
        capability.header.size = (uint32_t)sizeof(capability);
        capability.capability_id = URE_CAPABILITY_LIFECYCLE;
        capability.required = 1;
        capability.request_enable = 1;
        descriptor.header.type = URE_STRUCTURE_CAPABILITY_DESCRIPTOR;
        descriptor.header.size = (uint32_t)sizeof(descriptor);
        CHECK(instances->query_capability(instance, &capability, &descriptor,
                                          &error) == URE_RESULT_SUCCESS);
        CHECK(descriptor.enabled && descriptor.applicable &&
              descriptor.runtime_state == URE_RUNTIME_STATE_APPLICABLE);
        CHECK(descriptor.version_major == 0 && descriptor.version_minor == 1 &&
              descriptor.version_patch == 0);
        CHECK(descriptor.stability == URE_STABILITY_CORE &&
              descriptor.maturity == URE_MATURITY_NOT_APPLICABLE);
        CHECK(descriptor.dependency_count == 1 &&
              descriptor.dependencies[0] == URE_CAPABILITY_BOOTSTRAP);
        capability.capability_id = URE_CAPABILITY_FRAME_LEASE;
        CHECK(instances->query_capability(instance, &capability, &descriptor,
                                          &error) ==
              URE_RESULT_CAPABILITY_UNAVAILABLE);
        CHECK(inspect_error(errors, error, URE_RESULT_CAPABILITY_UNAVAILABLE) == 0);
        CHECK(conformance->fail_next_error_allocation() == URE_RESULT_SUCCESS);
        CHECK(instances->query_capability(instance, &capability, &descriptor,
                                          &error) ==
              URE_RESULT_CAPABILITY_UNAVAILABLE);
        CHECK(error == NULL);
        capability.required = 0;
        capability.request_enable = 0;
        CHECK(instances->query_capability(instance, &capability, &descriptor,
                                          &error) == URE_RESULT_SUCCESS);
        CHECK(!descriptor.enabled && !descriptor.applicable &&
              descriptor.maturity == URE_MATURITY_EXPERIMENTAL);
        CHECK(descriptor.runtime_state == URE_RUNTIME_STATE_COMPILED &&
              descriptor.reason.size != 0);
        capability.capability_id = URE_CAPABILITY_TELEMETRY;
        CHECK(instances->query_capability(instance, &capability, &descriptor,
                                          &error) == URE_RESULT_SUCCESS);
        CHECK(!descriptor.enabled &&
              descriptor.maturity == URE_MATURITY_EXPERIMENTAL &&
              descriptor.reason.size != 0);
        capability.capability_id = UINT32_MAX;
        CHECK(instances->query_capability(instance, &capability, &descriptor,
                                          &error) ==
              URE_RESULT_CAPABILITY_UNAVAILABLE);
        CHECK(inspect_error(errors, error, URE_RESULT_CAPABILITY_UNAVAILABLE) == 0);
    }
    {
        ure_private_conformance_operation_request_t request = {0};
        ure_operation_info_t info = {0};
        ure_bool32_t accepted = 0;
        ure_handle_t operation = NULL;
        ure_handle_t other_instance = NULL;
        request.header.type = URE_PRIVATE_STRUCTURE_CONFORMANCE_OPERATION_REQUEST;
        request.header.size = (uint32_t)sizeof(request);
        request.work_steps = 50;
        request.step_delay_milliseconds = 2;
        CHECK(conformance->submit_operation(instance, &request, &operation,
                                            &error) == URE_RESULT_SUCCESS);
        CHECK(runtime->create_instance(&create_info, &other_instance, &error) ==
              URE_RESULT_SUCCESS);
        CHECK(conformance->validate_operation_owner(instance, operation, &error) ==
              URE_RESULT_SUCCESS);
        CHECK(conformance->validate_operation_owner(
                  other_instance, operation, &error) == URE_RESULT_INVALID_HANDLE);
        CHECK(inspect_error(errors, error, URE_RESULT_INVALID_HANDLE) == 0);
        CHECK(operations->wait(operation, 0, &error) == URE_RESULT_TIMEOUT);
        CHECK(instances->retain(instance, &error) == URE_RESULT_SUCCESS);
        CHECK(instances->release(instance, &error) == URE_RESULT_SUCCESS);
        CHECK(instances->release(instance, &error) == URE_RESULT_BUSY);
        CHECK(inspect_error(errors, error, URE_RESULT_BUSY) == 0);
        CHECK(operations->request_cancel(operation, &accepted, &error) ==
                  URE_RESULT_SUCCESS &&
              accepted);
        CHECK(operations->wait(operation, 1000000000ULL, &error) ==
              URE_RESULT_CANCELED);
        info.header.type = URE_STRUCTURE_OPERATION_INFO;
        info.header.size = (uint32_t)sizeof(info);
        CHECK(operations->get_info(operation, &info, &error) == URE_RESULT_SUCCESS);
        CHECK(info.state == URE_OPERATION_STATE_CANCELED &&
              info.progress_sequence != 0);
        CHECK(operations->retain(instance, &error) == URE_RESULT_INVALID_HANDLE);
        CHECK(inspect_error(errors, error, URE_RESULT_INVALID_HANDLE) == 0);
        CHECK(operations->release(operation, &error) == URE_RESULT_SUCCESS);
        CHECK(operations->retain(operation, &error) == URE_RESULT_INVALID_HANDLE);
        CHECK(inspect_error(errors, error, URE_RESULT_INVALID_HANDLE) == 0);
        CHECK(instances->close(other_instance, &error) == URE_RESULT_SUCCESS);
        CHECK(instances->release(other_instance, &error) == URE_RESULT_SUCCESS);
    }
    {
        ure_private_conformance_operation_request_t request = {0};
        operation_thread_context_t context = {0};
        HANDLE threads[8] = {0};
        HANDLE cancel_thread = NULL;
        uint32_t index = 0;
        request.header.type = URE_PRIVATE_STRUCTURE_CONFORMANCE_OPERATION_REQUEST;
        request.header.size = (uint32_t)sizeof(request);
        request.work_steps = 1000;
        request.step_delay_milliseconds = 1;
        CHECK(conformance->submit_operation(instance, &request, &context.operation,
                                            &error) == URE_RESULT_SUCCESS);
        context.operations = operations;
        context.start = CreateEventW(NULL, TRUE, FALSE, NULL);
        CHECK(context.start != NULL);
        for (; index < 8; ++index) {
            threads[index] =
                CreateThread(NULL, 0, retain_operation_thread, &context, 0, NULL);
            CHECK(threads[index] != NULL);
        }
        cancel_thread =
            CreateThread(NULL, 0, cancel_operation_thread, &context, 0, NULL);
        CHECK(cancel_thread != NULL);
        CHECK(SetEvent(context.start));
        CHECK(WaitForMultipleObjects(8, threads, TRUE, INFINITE) == WAIT_OBJECT_0);
        CHECK(WaitForSingleObject(cancel_thread, INFINITE) == WAIT_OBJECT_0);
        for (index = 0; index < 8; ++index)
            CloseHandle(threads[index]);
        CloseHandle(cancel_thread);
        CloseHandle(context.start);
        CHECK(context.failures == 0 && context.accepted);
        CHECK(operations->wait(context.operation, 1000000000ULL, &error) ==
              URE_RESULT_CANCELED);
        CHECK(operations->release(context.operation, &error) == URE_RESULT_SUCCESS);
    }
    {
        uint32_t mode = 0;
        for (; mode < 3; ++mode) {
            ure_private_conformance_operation_request_t request = {0};
            ure_operation_info_t info = {0};
            ure_error_info_t wait_error_info = {0};
            ure_handle_t operation = NULL;
            ure_handle_t wait_error = NULL;
            ure_result_t expected = mode == 0   ? URE_RESULT_SUCCESS
                                    : mode == 1 ? URE_RESULT_INTERNAL
                                                : URE_RESULT_DEVICE_LOST;
            request.header.type = URE_PRIVATE_STRUCTURE_CONFORMANCE_OPERATION_REQUEST;
            request.header.size = (uint32_t)sizeof(request);
            request.work_steps = 1;
            request.fail_at_end = mode == 1;
            request.device_lost_at_end = mode == 2;
            CHECK(conformance->submit_operation(instance, &request, &operation,
                                                &error) == URE_RESULT_SUCCESS);
            CHECK(operations->wait(operation, 1000000000ULL, &error) == expected);
            if (mode == 0) {
                CHECK(error == NULL);
            } else {
                wait_error_info.header.type = URE_STRUCTURE_ERROR_INFO;
                wait_error_info.header.size = (uint32_t)sizeof(wait_error_info);
                CHECK(error != NULL);
                wait_error = error;
                CHECK(errors->get_info(wait_error, &wait_error_info) ==
                      URE_RESULT_SUCCESS);
                CHECK(wait_error_info.result == expected &&
                      wait_error_info.operation == operation);
                CHECK(wait_error_info.cause != NULL &&
                      wait_error_info.structured_detail.size == 8);
            }
            info.header.type = URE_STRUCTURE_OPERATION_INFO;
            info.header.size = (uint32_t)sizeof(info);
            CHECK(operations->get_info(operation, &info, &error) ==
                  URE_RESULT_SUCCESS);
            CHECK(info.state == (mode == 0   ? URE_OPERATION_STATE_SUCCEEDED
                                 : mode == 1 ? URE_OPERATION_STATE_FAILED
                                             : URE_OPERATION_STATE_DEVICE_LOST));
            if (mode != 0) {
                CHECK(info.terminal_error != NULL);
                CHECK(info.terminal_error == wait_error_info.cause);
            }
            CHECK(operations->release(operation, &error) == URE_RESULT_SUCCESS);
            if (mode != 0) {
                ure_error_info_t cause_info = {0};
                cause_info.header.type = URE_STRUCTURE_ERROR_INFO;
                cause_info.header.size = (uint32_t)sizeof(cause_info);
                CHECK(errors->get_info(wait_error_info.cause, &cause_info) ==
                      URE_RESULT_SUCCESS);
                CHECK(cause_info.result == expected &&
                      cause_info.operation == operation);
                CHECK(errors->release(wait_error) == URE_RESULT_SUCCESS);
                CHECK(errors->get_info(wait_error_info.cause, &cause_info) ==
                      URE_RESULT_INVALID_HANDLE);
                error = NULL;
            }
        }
    }
    {
        ure_private_conformance_operation_request_t request = {0};
        ure_handle_t operation = NULL;
        request.header.type = URE_PRIVATE_STRUCTURE_CONFORMANCE_OPERATION_REQUEST;
        request.header.size = (uint32_t)sizeof(request);
        request.work_steps = 50;
        request.step_delay_milliseconds = 1;
        CHECK(conformance->submit_operation(instance, &request, &operation, &error) == URE_RESULT_SUCCESS);
        CHECK(operations->release(operation, &error) == URE_RESULT_SUCCESS);
        CHECK(operations->retain(operation, &error) == URE_RESULT_INVALID_HANDLE);
        CHECK(inspect_error(errors, error, URE_RESULT_INVALID_HANDLE) == 0);
    }
    {
        ure_private_conformance_operation_request_t request = {0};
        ure_handle_t closing_instance = NULL;
        ure_handle_t operation = NULL;
        close_thread_context_t context = {0};
        HANDLE thread = NULL;
        request.header.type = URE_PRIVATE_STRUCTURE_CONFORMANCE_OPERATION_REQUEST;
        request.header.size = (uint32_t)sizeof(request);
        request.work_steps = 50;
        request.step_delay_milliseconds = 2;
        CHECK(runtime->create_instance(&create_info, &closing_instance, &error) ==
              URE_RESULT_SUCCESS);
        CHECK(conformance->submit_operation(closing_instance, &request, &operation,
                                            &error) == URE_RESULT_SUCCESS);
        context.instances = instances;
        context.instance = closing_instance;
        context.start = CreateEventW(NULL, TRUE, FALSE, NULL);
        CHECK(context.start != NULL);
        thread = CreateThread(NULL, 0, close_instance_thread, &context, 0, NULL);
        CHECK(thread != NULL);
        CHECK(SetEvent(context.start));
        CHECK(operations->wait(operation, 1000000000ULL, &error) ==
              URE_RESULT_CANCELED);
        CHECK(WaitForSingleObject(thread, INFINITE) == WAIT_OBJECT_0);
        CHECK(context.failures == 0);
        CloseHandle(thread);
        CloseHandle(context.start);
        CHECK(conformance->submit_operation(closing_instance, &request,
                                            &context.instance,
                                            &error) == URE_RESULT_INVALID_HANDLE);
        CHECK(inspect_error(errors, error, URE_RESULT_INVALID_HANDLE) == 0);
        CHECK(instances->retain(closing_instance, &error) ==
              URE_RESULT_INVALID_HANDLE);
        CHECK(inspect_error(errors, error, URE_RESULT_INVALID_HANDLE) == 0);
        CHECK(operations->release(operation, &error) == URE_RESULT_SUCCESS);
        CHECK(instances->release(closing_instance, &error) == URE_RESULT_SUCCESS);
    }
    for (;;) {
        ure_event_record_t event = {0};
        ure_result_t result;
        event.header.type = URE_STRUCTURE_EVENT_RECORD;
        event.header.size = (uint32_t)sizeof(event);
        result = events->poll(instance, &event, &error);
        if (result == URE_RESULT_INCOMPLETE)
            break;
        CHECK(result == URE_RESULT_SUCCESS);
    }
    CHECK(conformance->emit_events(instance, 20, URE_EVENT_DIAGNOSTIC, &error) ==
          URE_RESULT_SUCCESS);
    {
        ure_event_record_t event = {0};
        event.header.type = URE_STRUCTURE_EVENT_RECORD;
        event.header.size = (uint32_t)sizeof(event);
        CHECK(events->poll(instance, &event, &error) == URE_RESULT_SUCCESS);
        CHECK(event.event_type == URE_EVENT_DIAGNOSTIC &&
              event.coalesced_count == 20);
        CHECK(events->poll(instance, &event, &error) == URE_RESULT_INCOMPLETE);
    }
    CHECK(conformance->emit_events(instance, 20, URE_EVENT_OPERATION_STATE,
                                   &error) == URE_RESULT_SUCCESS);
    {
        uint64_t previous = 0;
        int saw_gap = 0;
        for (;;) {
            ure_event_record_t event = {0};
            ure_result_t result;
            event.header.type = URE_STRUCTURE_EVENT_RECORD;
            event.header.size = (uint32_t)sizeof(event);
            result = events->poll(instance, &event, &error);
            if (result == URE_RESULT_INCOMPLETE)
                break;
            CHECK(result == URE_RESULT_SUCCESS && event.sequence > previous);
            previous = event.sequence;
            if (event.event_type == URE_EVENT_GAP) {
                CHECK(event.first_lost_sequence <= event.last_lost_sequence);
                saw_gap = 1;
            }
        }
        CHECK(saw_gap);
    }
    CHECK(instances->close(instance, &error) == URE_RESULT_SUCCESS);
    CHECK(instances->release(instance, &error) == URE_RESULT_SUCCESS);
    CHECK(instances->retain(instance, &error) == URE_RESULT_INVALID_HANDLE);
    CHECK(inspect_error(errors, error, URE_RESULT_INVALID_HANDLE) == 0);
    {
        uint64_t count = UINT64_MAX;
        CHECK(conformance->live_handle_count(&count) == URE_RESULT_SUCCESS);
        CHECK(count == 0);
    }
    return 0;
}

int main(int argc, char** argv) {
    wchar_t path[32768] = {0};
    HMODULE runtime;
    ure_get_runtime_manifest_fn get_manifest;
    ure_query_interface_fn query;
    int result;
    CHECK(argc == 2);
    CHECK(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, argv[1], -1, path,
                              32768) != 0);
    runtime = LoadLibraryW(path);
    CHECK(runtime != NULL);
    get_manifest = (ure_get_runtime_manifest_fn)GetProcAddress(
        runtime, "ureGetRuntimeManifest");
    query = (ure_query_interface_fn)GetProcAddress(runtime, "ureQueryInterface");
    CHECK(get_manifest != NULL && query != NULL);
    result = run_lifecycle(get_manifest, query);
    FreeLibrary(runtime);
    if (result)
        fprintf(stderr, "runtime lifecycle check failed at line %d\n", result);
    return result;
}
