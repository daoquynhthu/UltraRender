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
target_include_directories(ultrarender_mock_worker PRIVATE ${UR_PB1_STAGE_DIR}/include)
add_dependencies(ultrarender_mock_worker ure_public_stage_headers)

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
    add_dependencies(test_runtime_lifecycle ure_public_stage_headers ure_contract_conformance)

    add_executable(test_frame_leases test_frame_leases.c)
    set_target_properties(test_frame_leases PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_include_directories(test_frame_leases PRIVATE ${UR_PB1_STAGE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR})
    add_dependencies(test_frame_leases ure_public_stage_headers ure_contract_conformance)

    add_executable(test_scene_boundary
        ${CMAKE_CURRENT_SOURCE_DIR}/external_client/direct_client.c)
    set_target_properties(test_scene_boundary PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_include_directories(test_scene_boundary PRIVATE ${UR_PB1_STAGE_DIR}/include)
    add_dependencies(test_scene_boundary ure_public_stage_headers ure_contract)

    add_executable(test_scene_transaction external_client/transaction_client.cpp)
    target_link_libraries(test_scene_transaction PRIVATE
        ure_public ure_contract_schema bcrypt)
    add_dependencies(test_scene_transaction ure_public_stage_headers ure_contract)

    add_library(worker_test_support STATIC external_client/worker_client.cpp)
    target_link_libraries(worker_test_support PUBLIC
        ure_public
        ure_contract_schema
        ure_worker_conformance_schema
        PRIVATE
        bcrypt)
    target_include_directories(worker_test_support PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/external_client)

    add_executable(test_worker_protocol test_worker_protocol.cpp)
    target_link_libraries(test_worker_protocol PRIVATE worker_test_support ure_public)
    target_include_directories(test_worker_protocol PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_dependencies(test_worker_protocol ure_worker ure_worker_conformance ure_contract ure_contract_conformance)

    add_executable(test_worker_crash test_worker_crash.cpp)
    target_link_libraries(test_worker_crash PRIVATE worker_test_support)
    add_dependencies(test_worker_crash ure_worker_conformance ure_contract_conformance)

    add_executable(test_scene_worker test_scene_worker.cpp)
    target_link_libraries(test_scene_worker PRIVATE worker_test_support ure_public)
    add_dependencies(test_scene_worker ure_worker ure_contract ure_contract_conformance)

    add_executable(test_pb7_fuzz test_pb7_fuzz.cpp)
    target_link_libraries(test_pb7_fuzz PRIVATE ure_public bcrypt)
    add_dependencies(test_pb7_fuzz ure_contract)

    add_executable(test_worker_runtime_security test_worker_runtime_security.cpp)
    target_link_libraries(test_worker_runtime_security PRIVATE
        worker_test_support iphlpapi)
    add_dependencies(test_worker_runtime_security ure_worker ure_contract)
endif()

if(MSVC)
    foreach(target test_contract_registry test_public_headers_cpp ultrarender_mock_worker external_client)
        target_compile_options(${target} PRIVATE /W4 /WX /utf-8 /wd4819)
    endforeach()
    if(TARGET ure_contract)
        foreach(target test_abi_layout_c test_abi_layout_cpp test_loader_client test_runtime_lifecycle test_frame_leases test_scene_boundary test_scene_transaction worker_test_support test_worker_protocol test_worker_crash test_scene_worker test_pb7_fuzz test_worker_runtime_security)
            target_compile_options(${target} PRIVATE /W4 /WX /utf-8 /wd4819)
        endforeach()
        target_compile_options(test_loader_client PRIVATE /wd4191)
        target_compile_options(test_runtime_lifecycle PRIVATE /wd4191)
        target_compile_options(test_frame_leases PRIVATE /wd4191)
        target_compile_options(test_scene_boundary PRIVATE /wd4191)
        target_compile_options(test_worker_protocol PRIVATE /wd4191)
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
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:ure_worker> ${UR_PB2_STAGE_DIR}/bin/
        COMMAND ${CMAKE_COMMAND} -E rm -f ${UR_PB2_STAGE_DIR}/bin/$<TARGET_FILE_NAME:ure_worker_conformance>
        DEPENDS ure_contract ure_worker ure_public_stage_headers
        VERBATIM)

    set(UR_PB7_SDK_STAGE_DIR ${CMAKE_BINARY_DIR}/pb7_packages/sdk)
    set(UR_PB7_RUNTIME_STAGE_DIR ${CMAKE_BINARY_DIR}/pb7_packages/runtime)
    add_custom_target(ure_candidate_packages ALL
        COMMAND
            ${UR_PWSH_EXECUTABLE}
            -NoProfile
            -File ${CMAKE_CURRENT_SOURCE_DIR}/stage_candidate_packages.ps1
            -RepoRoot ${CMAKE_SOURCE_DIR}
            -SdkStage ${UR_PB7_SDK_STAGE_DIR}
            -RuntimeStage ${UR_PB7_RUNTIME_STAGE_DIR}
            -RuntimeDll $<TARGET_FILE:ure_contract>
            -WorkerExecutable $<TARGET_FILE:ure_worker>
            -MockWorkerExecutable $<TARGET_FILE:ultrarender_mock_worker>
        DEPENDS ure_contract ure_worker ultrarender_mock_worker ure_public_stage_headers
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
    NAME test_scene_contract_schema_conform
    COMMAND ${UR_FLATC_EXECUTABLE} --conform
        ${CMAKE_SOURCE_DIR}/contracts/schemas/baseline/0.1/ure_scene_candidate.fbs
        ${CMAKE_SOURCE_DIR}/contracts/schemas/ure_scene_candidate.fbs)
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
    NAME test_scene_uuid_golden
    COMMAND
        ${UR_PWSH_EXECUTABLE}
        -NoProfile
        -File ${CMAKE_CURRENT_SOURCE_DIR}/test_scene_uuid_golden.ps1
        -Golden ${CMAKE_SOURCE_DIR}/contracts/generated/scene_uuid_v2_golden.json
        -Fixture ${CMAKE_SOURCE_DIR}/tests/assets/native_scene/pb6_scene_transaction_full/base_scene.ure
        -AdditionalFixture ${CMAKE_SOURCE_DIR}/tests/assets/native_scene/pb6_scene_transaction/base_scene.ure)
add_test(
    NAME test_phase_pb6_static
    COMMAND
        ${UR_PWSH_EXECUTABLE}
        -NoProfile
        -File ${CMAKE_CURRENT_SOURCE_DIR}/test_phase_pb6_static.ps1
        -RepoRoot ${CMAKE_SOURCE_DIR})
add_test(
    NAME test_phase_pb7_static
    COMMAND
        ${UR_PWSH_EXECUTABLE}
        -NoProfile
        -File ${CMAKE_CURRENT_SOURCE_DIR}/test_phase_pb7_static.ps1
        -RepoRoot ${CMAKE_SOURCE_DIR})
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
        COMMAND test_runtime_lifecycle $<TARGET_FILE:ure_contract_conformance>)
    add_test(
        NAME test_frame_leases
        COMMAND test_frame_leases $<TARGET_FILE:ure_contract_conformance>)
    add_test(
        NAME test_scene_boundary
        COMMAND test_scene_boundary
            $<TARGET_FILE:ure_contract>
            ${CMAKE_SOURCE_DIR}/tests/assets/native_scene/q4_procedural_scene/procedural_scene.urescene
            ${CMAKE_SOURCE_DIR}/tests/assets/native_scene/q4_procedural_scene/procedural_scene.ure
            ${CMAKE_SOURCE_DIR}/tests/assets/native_scene/pb5_public_boundary/single_scene.urepkg
            ${CMAKE_SOURCE_DIR}/tests/assets/native_scene/pb5_public_boundary/ambiguous_scenes.urepkg)
    add_test(
        NAME test_scene_transaction
        COMMAND test_scene_transaction
            $<TARGET_FILE:ure_contract>
            ${CMAKE_SOURCE_DIR}/tests/assets/native_scene/pb6_scene_transaction_full/base_scene.urescene)
    add_test(
        NAME test_worker_protocol
        COMMAND test_worker_protocol
            $<TARGET_FILE:ure_worker>
            $<TARGET_FILE:ure_worker_conformance>
            $<TARGET_FILE:ure_contract>
            $<TARGET_FILE:ure_contract_conformance>)
    add_test(
        NAME test_worker_crash
        COMMAND test_worker_crash
            $<TARGET_FILE:ure_worker_conformance>
            $<TARGET_FILE:ure_contract_conformance>)
    add_test(
        NAME test_scene_worker
        COMMAND test_scene_worker
            $<TARGET_FILE:ure_worker>
            $<TARGET_FILE:ure_contract>
            $<TARGET_FILE:ure_contract_conformance>
            ${CMAKE_SOURCE_DIR}/tests/assets/native_scene/q4_procedural_scene/procedural_scene.urescene
            ${CMAKE_SOURCE_DIR}/tests/assets/native_scene/pb6_scene_transaction_full/base_scene.urescene)
    add_test(
        NAME test_worker_security
        COMMAND
            ${UR_PWSH_EXECUTABLE}
            -NoProfile
            -File ${CMAKE_CURRENT_SOURCE_DIR}/test_worker_security.ps1
            -WorkerExecutable $<TARGET_FILE:ure_worker>
            -ConformanceExecutable $<TARGET_FILE:ure_worker_conformance>
            -ConformanceRuntime $<TARGET_FILE:ure_contract_conformance>
            -RuntimeStage ${UR_PB2_STAGE_DIR}
            -RepoRoot ${CMAKE_SOURCE_DIR})
    add_test(
        NAME test_worker_runtime_security
        COMMAND test_worker_runtime_security
            $<TARGET_FILE:ure_worker>
            $<TARGET_FILE:ure_contract>)
    add_test(
        NAME test_pb7_fuzz
        COMMAND test_pb7_fuzz
            $<TARGET_FILE:ure_contract>
            ${CMAKE_SOURCE_DIR}/tests/assets/native_scene/pb6_scene_transaction_full/base_scene.urescene)
    add_test(
        NAME test_candidate_compatibility
        COMMAND
            ${UR_PWSH_EXECUTABLE}
            -NoProfile
            -File ${CMAKE_CURRENT_SOURCE_DIR}/test_candidate_compatibility.ps1
            -RepoRoot ${CMAKE_SOURCE_DIR}
            -CurrentClient $<TARGET_FILE:test_loader_client>
            -CurrentRuntime $<TARGET_FILE:ure_contract>)
    add_test(
        NAME test_external_client_package
        COMMAND
            ${UR_PWSH_EXECUTABLE}
            -NoProfile
            -File ${CMAKE_CURRENT_SOURCE_DIR}/test_external_client_package.ps1
            -SourceDir ${CMAKE_CURRENT_SOURCE_DIR}/external_client
            -BuildDir ${CMAKE_BINARY_DIR}/pb7_external_client_build
            -SdkStage ${UR_PB7_SDK_STAGE_DIR}
            -RuntimeStage ${UR_PB7_RUNTIME_STAGE_DIR}
            -Flatc ${UR_FLATC_EXECUTABLE}
            -CMake ${CMAKE_COMMAND}
            -Ninja ${CMAKE_MAKE_PROGRAM})
endif()

set_tests_properties(
    test_contract_registry
    test_public_headers_cpp
    test_contract_codegen_compare
    test_contract_schema_conform
    test_scene_contract_schema_conform
    test_public_loader_header_mirror
    test_public_registry_header_mirror
    test_contract_codegen_negative
    test_golden_message_mirror
    test_scene_uuid_golden
    test_phase_pb6_static
    test_phase_pb7_static
    test_mock_worker_external_client
    PROPERTIES TIMEOUT 60)

if(TARGET ure_contract)
    set_tests_properties(
        test_candidate_abi_layout
        test_candidate_loader_exports
        test_candidate_loader_client
        test_runtime_lifecycle
        test_frame_leases
        test_scene_boundary
        test_scene_transaction
        test_worker_protocol
        test_worker_crash
        test_scene_worker
        test_worker_security
        test_worker_runtime_security
        test_pb7_fuzz
        test_candidate_compatibility
        test_external_client_package
        PROPERTIES TIMEOUT 60)
    set_tests_properties(test_external_client_package PROPERTIES TIMEOUT 300)
endif()
