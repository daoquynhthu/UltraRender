find_program(UR_FLATC_EXECUTABLE NAMES flatc REQUIRED)
execute_process(
    COMMAND ${UR_FLATC_EXECUTABLE} --version
    OUTPUT_VARIABLE UR_FLATC_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)
if(NOT UR_FLATC_VERSION STREQUAL "flatc version 25.12.19")
    message(FATAL_ERROR "Phase PB.1 requires flatc version 25.12.19, found: ${UR_FLATC_VERSION}")
endif()

set(UR_CONTRACT_SCHEMA_DIR ${CMAKE_SOURCE_DIR}/contracts/schemas)
set(UR_CONTRACT_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(UR_CONTRACT_SCHEMAS
    ${UR_CONTRACT_SCHEMA_DIR}/ure_payload_candidate.fbs
    ${UR_CONTRACT_SCHEMA_DIR}/ure_frame_candidate.fbs
    ${UR_CONTRACT_SCHEMA_DIR}/ure_worker_candidate.fbs)
set(UR_CONTRACT_GENERATED_HEADERS
    ${UR_CONTRACT_GENERATED_DIR}/ure_payload_candidate_generated.h
    ${UR_CONTRACT_GENERATED_DIR}/ure_frame_candidate_generated.h
    ${UR_CONTRACT_GENERATED_DIR}/ure_worker_candidate_generated.h
    ${UR_CONTRACT_GENERATED_DIR}/ure_worker_future_fixture_generated.h)

add_custom_command(
    OUTPUT ${UR_CONTRACT_GENERATED_HEADERS}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${UR_CONTRACT_GENERATED_DIR}
    COMMAND
        ${UR_FLATC_EXECUTABLE}
        --cpp
        --scoped-enums
        --gen-object-api
        -I ${UR_CONTRACT_SCHEMA_DIR}
        -I ${CMAKE_SOURCE_DIR}/tests/fixtures/contracts/mock_worker
        -o ${UR_CONTRACT_GENERATED_DIR}
        ${UR_CONTRACT_SCHEMAS}
        ${CMAKE_SOURCE_DIR}/tests/fixtures/contracts/mock_worker/ure_worker_future_fixture.fbs
    DEPENDS ${UR_CONTRACT_SCHEMAS}
        ${CMAKE_SOURCE_DIR}/tests/fixtures/contracts/mock_worker/ure_worker_future_fixture.fbs
    VERBATIM)

add_library(ure_contract_schema INTERFACE)
target_include_directories(ure_contract_schema INTERFACE
    ${UR_CONTRACT_GENERATED_DIR}
    ${CMAKE_SOURCE_DIR}/third_party/flatbuffers/include)
target_sources(ure_contract_schema INTERFACE ${UR_CONTRACT_GENERATED_HEADERS})

add_library(ure_contract_codegen_support STATIC
    src/registry.cpp
    src/generator.cpp
    src/mock_protocol.cpp
    src/sha256.cpp)
target_include_directories(ure_contract_codegen_support PUBLIC src ${CMAKE_SOURCE_DIR}/third_party)
target_link_libraries(ure_contract_codegen_support PUBLIC ure_contract_schema)

add_executable(ure_contract_codegen src/main.cpp)
target_link_libraries(ure_contract_codegen PRIVATE ure_contract_codegen_support)

if(MSVC)
    target_compile_options(ure_contract_codegen_support PRIVATE /W4 /WX /utf-8 /EHsc /wd4819)
    target_compile_options(ure_contract_codegen PRIVATE /W4 /WX /utf-8 /EHsc /wd4819)
else()
    target_compile_options(ure_contract_codegen_support PRIVATE -Wall -Wextra -pedantic)
    target_compile_options(ure_contract_codegen PRIVATE -Wall -Wextra -pedantic)
endif()

set(UR_FLATC_EXECUTABLE ${UR_FLATC_EXECUTABLE} CACHE INTERNAL "Resolved Phase PB flatc executable")
set(UR_CONTRACT_GENERATED_DIR ${UR_CONTRACT_GENERATED_DIR} CACHE INTERNAL "Generated contract schema headers")
