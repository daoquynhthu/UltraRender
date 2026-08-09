find_program(UR_PWSH_EXECUTABLE NAMES pwsh REQUIRED)

add_test(
    NAME test_public_boundary_audit
    COMMAND
        ${UR_PWSH_EXECUTABLE}
        -NoProfile
        -File ${CMAKE_CURRENT_SOURCE_DIR}/test_public_boundary_audit.ps1
        -RepoRoot ${CMAKE_SOURCE_DIR}
        -BuildDir ${CMAKE_BINARY_DIR})

set_tests_properties(
    test_public_boundary_audit
    PROPERTIES TIMEOUT 90)

set(UR_PB1_STAGE_DIR ${CMAKE_BINARY_DIR}/pb1_sdk)
set(UR_PB1_STAGE_HEADERS
    ${UR_PB1_STAGE_DIR}/include/ultrarender/ure_loader.h
    ${UR_PB1_STAGE_DIR}/include/ultrarender/ure_registry.h)
add_custom_command(
    OUTPUT ${UR_PB1_STAGE_HEADERS}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${UR_PB1_STAGE_DIR}/include/ultrarender
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/contracts/generated/include/ultrarender/ure_loader.h
        ${UR_PB1_STAGE_DIR}/include/ultrarender/ure_loader.h
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/contracts/generated/include/ultrarender/ure_registry.h
        ${UR_PB1_STAGE_DIR}/include/ultrarender/ure_registry.h
    DEPENDS
        ${CMAKE_SOURCE_DIR}/contracts/generated/include/ultrarender/ure_loader.h
        ${CMAKE_SOURCE_DIR}/contracts/generated/include/ultrarender/ure_registry.h
    VERBATIM)
add_custom_target(ure_public_stage_headers DEPENDS ${UR_PB1_STAGE_HEADERS})

add_executable(test_contract_registry test_registry.cpp)
target_link_libraries(test_contract_registry PRIVATE ure_contract_codegen_support)
target_include_directories(test_contract_registry PRIVATE ${UR_PB1_STAGE_DIR}/include)
target_compile_definitions(test_contract_registry PRIVATE
    URE_TEST_REGISTRY_PATH="${CMAKE_SOURCE_DIR}/contracts/registry/public_contract_registry.json"
    URE_TEST_COMPATIBILITY_PATH="${CMAKE_SOURCE_DIR}/contracts/registry/registry_compatibility.json"
    URE_TEST_SCHEMA_DIR="${CMAKE_SOURCE_DIR}/contracts/schemas")
add_dependencies(test_contract_registry ure_public_stage_headers)

add_executable(test_public_headers_cpp test_public_headers_cpp.cpp)
target_include_directories(test_public_headers_cpp PRIVATE ${UR_PB1_STAGE_DIR}/include)
add_dependencies(test_public_headers_cpp ure_public_stage_headers)

add_executable(ultrarender_mock_worker
    ${CMAKE_SOURCE_DIR}/tests/fixtures/contracts/mock_worker/mock_worker.cpp)
target_link_libraries(ultrarender_mock_worker PRIVATE ure_contract_codegen_support)

add_executable(external_client
    ${CMAKE_SOURCE_DIR}/tests/fixtures/contracts/mock_worker/external_client.c)
set_target_properties(external_client PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_include_directories(external_client PRIVATE ${UR_PB1_STAGE_DIR}/include)
add_dependencies(external_client ure_public_stage_headers)

if(TARGET ure_contract)
    add_executable(test_abi_layout_c test_abi_layout_c.c)
    set_target_properties(test_abi_layout_c PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_include_directories(test_abi_layout_c PRIVATE ${UR_PB1_STAGE_DIR}/include)
    add_dependencies(test_abi_layout_c ure_public_stage_headers)

    add_executable(test_abi_layout_cpp test_abi_layout.cpp)
    target_include_directories(test_abi_layout_cpp PRIVATE ${UR_PB1_STAGE_DIR}/include)
    add_dependencies(test_abi_layout_cpp ure_public_stage_headers)

    add_executable(test_loader_client test_loader_client.c)
    set_target_properties(test_loader_client PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_include_directories(test_loader_client PRIVATE ${UR_PB1_STAGE_DIR}/include)
    add_dependencies(test_loader_client ure_public_stage_headers ure_contract)

    add_executable(test_runtime_lifecycle test_runtime_lifecycle.c)
    set_target_properties(test_runtime_lifecycle PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_include_directories(test_runtime_lifecycle PRIVATE ${UR_PB1_STAGE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR})
    add_dependencies(test_runtime_lifecycle ure_public_stage_headers ure_contract)
endif()

if(MSVC)
    foreach(target test_contract_registry test_public_headers_cpp ultrarender_mock_worker external_client)
        target_compile_options(${target} PRIVATE /W4 /WX /utf-8 /wd4819)
    endforeach()
    if(TARGET ure_contract)
        foreach(target test_abi_layout_c test_abi_layout_cpp test_loader_client test_runtime_lifecycle)
            target_compile_options(${target} PRIVATE /W4 /WX /utf-8 /wd4819)
        endforeach()
        target_compile_options(test_loader_client PRIVATE /wd4191)
        target_compile_options(test_runtime_lifecycle PRIVATE /wd4191)
    endif()
endif()

add_custom_target(ure_public_sdk_stage ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/contracts/generated ${UR_PB1_STAGE_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${UR_PB1_STAGE_DIR}/bin
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:ultrarender_mock_worker> ${UR_PB1_STAGE_DIR}/bin/
    DEPENDS ultrarender_mock_worker ure_public_stage_headers
    VERBATIM)

if(TARGET ure_contract)
    set(UR_PB2_STAGE_DIR ${CMAKE_BINARY_DIR}/pb2_runtime)
    add_custom_target(ure_contract_runtime_stage ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/contracts/generated ${UR_PB2_STAGE_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/contracts/abi ${UR_PB2_STAGE_DIR}/abi
        COMMAND ${CMAKE_COMMAND} -E make_directory ${UR_PB2_STAGE_DIR}/bin
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:ure_contract> ${UR_PB2_STAGE_DIR}/bin/
        DEPENDS ure_contract ure_public_stage_headers
        VERBATIM)
endif()

add_test(NAME test_contract_registry COMMAND test_contract_registry)
add_test(NAME test_public_headers_cpp COMMAND test_public_headers_cpp)
add_test(
    NAME test_contract_codegen_compare
    COMMAND ure_contract_codegen compare
        --registry ${CMAKE_SOURCE_DIR}/contracts/registry/public_contract_registry.json
        --compatibility ${CMAKE_SOURCE_DIR}/contracts/registry/registry_compatibility.json
        --schemas ${CMAKE_SOURCE_DIR}/contracts/schemas
        --expected ${CMAKE_SOURCE_DIR}/contracts/generated)
add_test(
    NAME test_contract_schema_conform
    COMMAND ${UR_FLATC_EXECUTABLE} --conform
        ${CMAKE_SOURCE_DIR}/contracts/schemas/baseline/0.1/ure_worker_candidate.fbs
        ${CMAKE_SOURCE_DIR}/contracts/schemas/ure_worker_candidate.fbs)
add_test(
    NAME test_public_loader_header_mirror
    COMMAND ${CMAKE_COMMAND} -E compare_files
        ${CMAKE_SOURCE_DIR}/contracts/generated/include/ultrarender/ure_loader.h
        ${CMAKE_SOURCE_DIR}/libs/ure_public/include/ultrarender/ure_loader.h)
add_test(
    NAME test_public_registry_header_mirror
    COMMAND ${CMAKE_COMMAND} -E compare_files
        ${CMAKE_SOURCE_DIR}/contracts/generated/include/ultrarender/ure_registry.h
        ${CMAKE_SOURCE_DIR}/libs/ure_public/include/ultrarender/ure_registry.h)
add_test(
    NAME test_contract_codegen_negative
    COMMAND
        ${UR_PWSH_EXECUTABLE}
        -NoProfile
        -File ${CMAKE_CURRENT_SOURCE_DIR}/test_contract_codegen_negative.ps1
        -Codegen $<TARGET_FILE:ure_contract_codegen>
        -Flatc ${UR_FLATC_EXECUTABLE}
        -RepoRoot ${CMAKE_SOURCE_DIR})
add_test(
    NAME test_golden_message_mirror
    COMMAND
        ${UR_PWSH_EXECUTABLE}
        -NoProfile
        -File ${CMAKE_CURRENT_SOURCE_DIR}/test_golden_message_mirror.ps1
        -Generated ${CMAKE_SOURCE_DIR}/contracts/generated/golden_messages
        -Fixture ${CMAKE_SOURCE_DIR}/tests/fixtures/contracts/golden_messages)
add_test(
    NAME test_mock_worker_external_client
    COMMAND external_client
        ${UR_PB1_STAGE_DIR}/bin/ultrarender_mock_worker.exe
        ${UR_PB1_STAGE_DIR}/golden_messages
        ${CMAKE_CURRENT_BINARY_DIR}/mock_client_output)
if(TARGET ure_contract)
    add_test(
        NAME test_candidate_abi_layout
        COMMAND
            ${UR_PWSH_EXECUTABLE}
            -NoProfile
            -File ${CMAKE_CURRENT_SOURCE_DIR}/test_abi_layout.ps1
            -CExecutable $<TARGET_FILE:test_abi_layout_c>
            -CppExecutable $<TARGET_FILE:test_abi_layout_cpp>
            -Expected ${CMAKE_SOURCE_DIR}/contracts/abi/windows_x64_candidate.json)
    add_test(
        NAME test_candidate_loader_exports
        COMMAND
            ${UR_PWSH_EXECUTABLE}
            -NoProfile
            -File ${CMAKE_CURRENT_SOURCE_DIR}/test_loader_exports.ps1
            -RuntimeDll $<TARGET_FILE:ure_contract>
            -ClientExecutable $<TARGET_FILE:test_loader_client>)
    add_test(
        NAME test_candidate_loader_client
        COMMAND test_loader_client $<TARGET_FILE:ure_contract>)
    add_test(
        NAME test_runtime_lifecycle
        COMMAND test_runtime_lifecycle $<TARGET_FILE:ure_contract>)
endif()

set_tests_properties(
    test_contract_registry
    test_public_headers_cpp
    test_contract_codegen_compare
    test_contract_schema_conform
    test_public_loader_header_mirror
    test_public_registry_header_mirror
    test_contract_codegen_negative
    test_golden_message_mirror
    test_mock_worker_external_client
    PROPERTIES TIMEOUT 60)

if(TARGET ure_contract)
    set_tests_properties(
        test_candidate_abi_layout
        test_candidate_loader_exports
        test_candidate_loader_client
        test_runtime_lifecycle
        PROPERTIES TIMEOUT 60)
endif()
