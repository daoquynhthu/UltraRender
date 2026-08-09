#ifndef ULTRARENDER_PRIVATE_CONFORMANCE_HPP
#define ULTRARENDER_PRIVATE_CONFORMANCE_HPP

#include <cstdint>

#include <ultrarender/ure_loader.h>

inline constexpr std::uint32_t
    URE_PRIVATE_STRUCTURE_CONFORMANCE_OPERATION_REQUEST = 4026531846U;
inline constexpr std::uint32_t URE_PRIVATE_OBJECTIVE_DEVICE_LOSS = 4026531840U;
inline constexpr std::uint32_t URE_PRIVATE_STRUCTURE_CONFORMANCE_FRAME_REQUEST =
    4026531847U;
inline constexpr std::uint8_t URE_PRIVATE_INTERFACE_CONFORMANCE_UUID_BYTES[16]{
    0xe1, 0xf2, 0x20, 0x01, 0x41, 0x20, 0x5a, 0xd1,
    0x9e, 0xe0, 0x2f, 0xa0, 0xc7, 0xb3, 0x00, 0x01};

typedef struct ure_private_conformance_operation_request_t {
    ure_input_header_t header;
    std::uint32_t work_steps;
    std::uint32_t step_delay_milliseconds;
    ure_bool32_t fail_at_end;
    ure_bool32_t device_lost_at_end;
    std::uint64_t reserved[2];
} ure_private_conformance_operation_request_t;

typedef struct ure_private_conformance_frame_request_t {
    ure_input_header_t header;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t seed;
    std::uint32_t reserved;
} ure_private_conformance_frame_request_t;

typedef struct ure_private_conformance_interface_t {
    ure_interface_table_header_t header;
    ure_result_t(URE_CALL* submit_operation)(
        ure_handle_t instance,
        const ure_private_conformance_operation_request_t* request,
        ure_handle_t* operation, ure_handle_t* error);
    ure_result_t(URE_CALL* emit_events)(ure_handle_t instance,
                                        std::uint32_t event_count,
                                        std::uint32_t event_type,
                                        ure_handle_t* error);
    ure_result_t(URE_CALL* validate_operation_owner)(ure_handle_t instance,
                                                     ure_handle_t operation,
                                                     ure_handle_t* error);
    ure_result_t(URE_CALL* live_handle_count)(std::uint64_t* count);
    ure_result_t(URE_CALL* fail_next_error_allocation)(void);
    ure_result_t(URE_CALL* produce_frame)(
        ure_handle_t instance,
        const ure_private_conformance_frame_request_t* request,
        ure_handle_t* frame, ure_handle_t* error);
} ure_private_conformance_interface_t;

#endif
