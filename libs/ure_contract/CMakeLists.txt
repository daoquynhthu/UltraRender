if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(STATUS "PB.2 candidate runtime disabled outside the Windows x64 MSVC profile")
    return()
endif()

set(UR_CONTRACT_RUNTIME_INPUT
    "UltraRender.RuntimeCandidate.v1|${CMAKE_CXX_COMPILER_ID}|${CMAKE_CXX_COMPILER_VERSION}|${MSVC_TOOLSET_VERSION}|${CMAKE_SYSTEM_VERSION}|windows-x64-msvc-c11")
foreach(source
    ${CMAKE_SOURCE_DIR}/CMakeLists.txt
    ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
    ${CMAKE_CURRENT_SOURCE_DIR}/src/loader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime_adapter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lifecycle.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/frame_adapter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime_adapter.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime_objects.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/private_conformance.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ultrarender_runtime_candidate.def
    ${CMAKE_SOURCE_DIR}/libs/ure_public/include/ultrarender/ure_loader.h
    ${CMAKE_SOURCE_DIR}/libs/ure_public/include/ultrarender/ure_registry.h)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${source})
    file(SHA256 ${source} source_digest)
    string(APPEND UR_CONTRACT_RUNTIME_INPUT "|${source_digest}")
endforeach()
string(SHA256 UR_CONTRACT_RUNTIME_DIGEST "${UR_CONTRACT_RUNTIME_INPUT}")

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime_build_config.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/runtime_build_config.hpp
    @ONLY)

add_library(ure_contract SHARED
    src/loader.cpp
    src/runtime_adapter.cpp
    src/lifecycle.cpp
    src/frame_adapter.cpp
    ultrarender_runtime_candidate.def)
target_link_libraries(ure_contract PRIVATE ure_public bcrypt)
target_include_directories(ure_contract PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    ${CMAKE_SOURCE_DIR}/third_party)
target_compile_definitions(ure_contract PRIVATE URE_PUBLIC_IMPLEMENTATION)
set_target_properties(ure_contract PROPERTIES
    OUTPUT_NAME ultrarender_runtime_candidate
    PREFIX ""
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    WINDOWS_EXPORT_ALL_SYMBOLS OFF)

if(MSVC)
    target_compile_options(ure_contract PRIVATE /W4 /WX /utf-8 /EHsc /wd4819)
    target_link_options(ure_contract PRIVATE /Brepro /DEF:${CMAKE_CURRENT_SOURCE_DIR}/ultrarender_runtime_candidate.def)
else()
    target_compile_options(ure_contract PRIVATE -Wall -Wextra -pedantic)
endif()

add_library(ure_contract_conformance SHARED
    src/loader.cpp
    src/runtime_adapter.cpp
    src/lifecycle.cpp
    src/frame_adapter.cpp
    ultrarender_runtime_candidate.def)
target_link_libraries(ure_contract_conformance PRIVATE ure_public bcrypt)
target_include_directories(ure_contract_conformance PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    ${CMAKE_SOURCE_DIR}/third_party)
target_compile_definitions(ure_contract_conformance PRIVATE
    URE_PUBLIC_IMPLEMENTATION
    URE_CONTRACT_CONFORMANCE=1)
set_target_properties(ure_contract_conformance PROPERTIES
    OUTPUT_NAME ultrarender_runtime_conformance
    PREFIX ""
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    WINDOWS_EXPORT_ALL_SYMBOLS OFF)
target_compile_options(ure_contract_conformance PRIVATE /W4 /WX /utf-8 /EHsc /wd4819)
target_link_options(ure_contract_conformance PRIVATE
    /Brepro
    /DEF:${CMAKE_CURRENT_SOURCE_DIR}/ultrarender_runtime_candidate.def)

install(TARGETS ure_contract
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES ${CMAKE_SOURCE_DIR}/contracts/abi/windows_x64_candidate.json
    DESTINATION ${CMAKE_INSTALL_DATADIR}/ultrarender/abi)
