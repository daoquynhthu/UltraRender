cmake_minimum_required(VERSION 3.28)
project(UltraRenderCandidateExternalClient LANGUAGES C CXX)

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The Candidate 0.1 external client requires Windows x64 MSVC")
endif()
foreach(variable URE_SDK_ROOT URE_FLATC)
    if(NOT DEFINED ${variable})
        message(FATAL_ERROR "${variable} is required")
    endif()
endforeach()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(generated ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(schemas ${URE_SDK_ROOT}/share/ultrarender/schemas)
add_custom_command(
    OUTPUT
        ${generated}/ure_payload_candidate_generated.h
        ${generated}/ure_frame_candidate_generated.h
        ${generated}/ure_scene_candidate_generated.h
        ${generated}/ure_worker_candidate_generated.h
    COMMAND ${CMAKE_COMMAND} -E make_directory ${generated}
    COMMAND ${URE_FLATC} --cpp --gen-object-api --scoped-enums -I ${schemas} -o ${generated}
        ${schemas}/ure_payload_candidate.fbs
        ${schemas}/ure_frame_candidate.fbs
        ${schemas}/ure_scene_candidate.fbs
        ${schemas}/ure_worker_candidate.fbs
    DEPENDS
        ${schemas}/ure_payload_candidate.fbs
        ${schemas}/ure_frame_candidate.fbs
        ${schemas}/ure_scene_candidate.fbs
        ${schemas}/ure_worker_candidate.fbs
    VERBATIM)
add_custom_target(external_schema DEPENDS
    ${generated}/ure_payload_candidate_generated.h
    ${generated}/ure_frame_candidate_generated.h
    ${generated}/ure_scene_candidate_generated.h
    ${generated}/ure_worker_candidate_generated.h)

add_executable(ure_external_direct direct_client.c)
target_include_directories(ure_external_direct PRIVATE ${URE_SDK_ROOT}/include)

add_executable(ure_external_transaction transaction_client.cpp)
target_include_directories(ure_external_transaction PRIVATE
    ${URE_SDK_ROOT}/include
    ${URE_SDK_ROOT}/third_party/flatbuffers/include
    ${generated})
target_link_libraries(ure_external_transaction PRIVATE bcrypt)
add_dependencies(ure_external_transaction external_schema)

add_executable(ure_external_worker worker_client.cpp worker_e2e.cpp)
target_compile_definitions(ure_external_worker PRIVATE URE_EXTERNAL_CLIENT=1)
target_include_directories(ure_external_worker PRIVATE
    ${URE_SDK_ROOT}/include
    ${URE_SDK_ROOT}/third_party/flatbuffers/include
    ${generated})
target_link_libraries(ure_external_worker PRIVATE bcrypt)
add_dependencies(ure_external_worker external_schema)

foreach(target ure_external_direct ure_external_transaction ure_external_worker)
    target_compile_options(${target} PRIVATE /W4 /WX /utf-8 /wd4819 /wd4864)
endforeach()
target_compile_options(ure_external_direct PRIVATE /wd4191)
