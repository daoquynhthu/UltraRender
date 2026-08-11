#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <ultrarender/ure_loader.h>

_Static_assert(sizeof(ure_uuid_t) == 16, "ure_uuid_t size");
_Static_assert(sizeof(ure_digest256_t) == 32, "ure_digest256_t size");
_Static_assert(offsetof(ure_input_header_t, type) == 0, "ure_input_header_t type offset");
_Static_assert(offsetof(ure_input_header_t, size) == 4, "ure_input_header_t size offset");
_Static_assert(offsetof(ure_input_header_t, next) == 8, "ure_input_header_t next offset");

static int append_text(wchar_t *destination, size_t capacity, const wchar_t *text) {
    return wcscat_s(destination, capacity, text) == 0;
}

static int run_worker(
    const wchar_t *worker,
    const wchar_t *request,
    const wchar_t *response,
    DWORD *exit_code) {
    wchar_t command[32768] = L"\"";
    if (!append_text(command, 32768, worker) ||
        !append_text(command, 32768, L"\" --request \"") ||
        !append_text(command, 32768, request) ||
        !append_text(command, 32768, L"\" --response \"") ||
        !append_text(command, 32768, response) ||
        !append_text(command, 32768, L"\"")) return 0;
    STARTUPINFOW startup = {0};
    PROCESS_INFORMATION process = {0};
    startup.cb = sizeof(startup);
    if (!CreateProcessW(NULL, command, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &startup, &process)) return 0;
    const DWORD wait_result = WaitForSingleObject(process.hProcess, 30000);
    const int result = wait_result == WAIT_OBJECT_0 && GetExitCodeProcess(process.hProcess, exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return result;
}

static unsigned char *read_file(const wchar_t *path, size_t *size) {
    FILE *stream = NULL;
    if (_wfopen_s(&stream, path, L"rb") != 0 || stream == NULL) return NULL;
    if (_fseeki64(stream, 0, SEEK_END) != 0) {
        fclose(stream);
        return NULL;
    }
    const __int64 length = _ftelli64(stream);
    if (length < 0 || _fseeki64(stream, 0, SEEK_SET) != 0) {
        fclose(stream);
        return NULL;
    }
    unsigned char *bytes = (unsigned char *)malloc((size_t)length + 1);
    if (bytes == NULL || fread(bytes, 1, (size_t)length, stream) != (size_t)length) {
        free(bytes);
        fclose(stream);
        return NULL;
    }
    fclose(stream);
    *size = (size_t)length;
    return bytes;
}

static int equal_files(const wchar_t *actual, const wchar_t *expected) {
    size_t actual_size = 0;
    size_t expected_size = 0;
    unsigned char *actual_bytes = read_file(actual, &actual_size);
    unsigned char *expected_bytes = read_file(expected, &expected_size);
    const int equal = actual_bytes != NULL && expected_bytes != NULL &&
        actual_size == expected_size && memcmp(actual_bytes, expected_bytes, actual_size) == 0;
    free(actual_bytes);
    free(expected_bytes);
    return equal;
}

static int make_path(wchar_t *path, size_t capacity, const wchar_t *root, const wchar_t *name, const wchar_t *suffix) {
    path[0] = L'\0';
    return append_text(path, capacity, root) && append_text(path, capacity, L"\\") &&
        append_text(path, capacity, name) && append_text(path, capacity, suffix);
}

int wmain(int argc, wchar_t **argv) {
    static const wchar_t *scenarios[] = {
        L"normal_lifecycle", L"missing_optional_capability", L"missing_required_capability", L"registry_mismatch", L"incompatible_protocol_version",
        L"unknown_optional_field", L"event_gap", L"backpressure", L"device_loss", L"worker_crash",
        L"malformed_message", L"truncated_message", L"oversized_message"
    };
    if (argc != 4) return 2;
    if (sizeof(ure_uuid_t) != 16 || sizeof(ure_digest256_t) != 32 || URE_REGISTRY_VERSION_MAJOR != 1) return 3;
    CreateDirectoryW(argv[3], NULL);
    for (size_t index = 0; index < sizeof(scenarios) / sizeof(scenarios[0]); ++index) {
        wchar_t request[32768];
        wchar_t expected[32768];
        wchar_t actual[32768];
        if (!make_path(request, 32768, argv[2], scenarios[index], L".request.bin") ||
            !make_path(expected, 32768, argv[2], scenarios[index], L".response.bin") ||
            !make_path(actual, 32768, argv[3], scenarios[index], L".response.bin")) return 4;
        DeleteFileW(actual);
        DWORD exit_code = 0;
        if (!run_worker(argv[1], request, actual, &exit_code)) return 5;
        if (wcscmp(scenarios[index], L"worker_crash") == 0) {
            if (exit_code != 86 || GetFileAttributesW(actual) != INVALID_FILE_ATTRIBUTES) return 6;
        } else if (exit_code != 0 || !equal_files(actual, expected)) {
            return 7;
        }
    }
    return 0;
}
