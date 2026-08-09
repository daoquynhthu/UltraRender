#include <cstddef>
#include <type_traits>

#include <ultrarender/ure_loader.h>

static_assert(std::is_standard_layout_v<ure_input_header_t>);
static_assert(std::is_standard_layout_v<ure_output_header_t>);
static_assert(sizeof(ure_uuid_t) == 16);
static_assert(sizeof(ure_digest256_t) == 32);
static_assert(offsetof(ure_input_header_t, type) == 0);
static_assert(offsetof(ure_input_header_t, size) == 4);
static_assert(offsetof(ure_input_header_t, next) == 8);
static_assert(offsetof(ure_output_header_t, type) == 0);
static_assert(offsetof(ure_output_header_t, size) == 4);
static_assert(offsetof(ure_output_header_t, next) == 8);

int main() {
    ure_runtime_manifest_request_t request{};
    request.header.type = URE_STRUCTURE_RUNTIME_MANIFEST_REQUEST;
    request.header.size = sizeof(request);
    return request.header.next == nullptr ? 0 : 1;
}
