if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    return()
endif()

set(UR_WORKER_SOURCES
    main.cpp
    local_transport.cpp
    runtime_client.cpp)

add_executable(ure_worker ${UR_WORKER_SOURCES})
target_link_libraries(ure_worker PRIVATE
    ure_public
    ure_contract_schema
    advapi32
    bcrypt)

set(UR_WORKER_CONFORMANCE_SCHEMA
    ${CMAKE_SOURCE_DIR}/tests/fixtures/contracts/worker/ure_worker_conformance.fbs)
set(UR_WORKER_CONFORMANCE_HEADER
    ${CMAKE_CURRENT_BINARY_DIR}/generated/ure_worker_conformance_generated.h)
add_custom_command(
    OUTPUT ${UR_WORKER_CONFORMANCE_HEADER}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/generated
    COMMAND ${UR_FLATC_EXECUTABLE}
        --cpp
        --scoped-enums
        --gen-object-api
        -o ${CMAKE_CURRENT_BINARY_DIR}/generated
        ${UR_WORKER_CONFORMANCE_SCHEMA}
    DEPENDS ${UR_WORKER_CONFORMANCE_SCHEMA}
    VERBATIM)

add_library(ure_worker_conformance_schema INTERFACE)
target_sources(ure_worker_conformance_schema INTERFACE
    ${UR_WORKER_CONFORMANCE_HEADER})
target_include_directories(ure_worker_conformance_schema INTERFACE
    ${CMAKE_CURRENT_BINARY_DIR}/generated)

add_executable(ure_worker_conformance
    ${UR_WORKER_SOURCES})
target_compile_definitions(ure_worker_conformance PRIVATE
    URE_WORKER_CONFORMANCE=1)
target_link_libraries(ure_worker_conformance PRIVATE
    ure_public
    ure_contract_schema
    ure_worker_conformance_schema
    advapi32
    bcrypt)

foreach(target ure_worker ure_worker_conformance)
    target_compile_options(${target} PRIVATE /W4 /WX /utf-8 /EHsc /wd4819)
    target_link_options(${target} PRIVATE /Brepro)
endforeach()

install(TARGETS ure_worker RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
