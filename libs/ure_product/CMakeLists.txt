set(UR_PRODUCT_BUILD_INPUT
    "UltraRender.Product.v0|${CMAKE_CXX_COMPILER_ID}|${CMAKE_CXX_COMPILER_VERSION}|${CMAKE_SYSTEM_NAME}|${CMAKE_SYSTEM_PROCESSOR}")
foreach(source
    ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
    ${CMAKE_CURRENT_SOURCE_DIR}/include/ure/product/product_service.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/ure/product/product_scene.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/ure/product/product_material.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/ure/product/product_measurement.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/ure/product/product_output.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/ure/product/scene_tool_service.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/product_service.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/product_scene.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/product_material.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/product_measurement.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/product_output.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/scene_tool_service.cpp)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${source})
    file(SHA256 ${source} source_digest)
    string(APPEND UR_PRODUCT_BUILD_INPUT "|${source_digest}")
endforeach()
string(SHA256 UR_PRODUCT_BUILD_DIGEST "${UR_PRODUCT_BUILD_INPUT}")

include(FetchContent)
if(DEFINED BUILD_SHARED_LIBS)
    set(UR_SAVED_BUILD_SHARED_LIBS "${BUILD_SHARED_LIBS}")
    set(UR_HAD_BUILD_SHARED_LIBS TRUE)
else()
    set(UR_HAD_BUILD_SHARED_LIBS FALSE)
endif()
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build third-party dependencies as static libraries" FORCE)
set(IMATH_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(IMATH_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(IMATH_BUILD_PYTHON_LIBS OFF CACHE BOOL "" FORCE)
set(IMATH_INSTALL_PKG_CONFIG OFF CACHE BOOL "" FORCE)
set(OPENEXR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(OPENEXR_TEST_LIBRARIES OFF CACHE BOOL "" FORCE)
set(OPENEXR_TEST_TOOLS OFF CACHE BOOL "" FORCE)
set(OPENEXR_TEST_PYTHON OFF CACHE BOOL "" FORCE)
set(OPENEXR_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(OPENEXR_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(OPENEXR_BUILD_PYTHON OFF CACHE BOOL "" FORCE)
set(OPENEXR_BUILD_PYTHON_LIBS OFF CACHE BOOL "" FORCE)
set(OPENEXR_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
set(OPENEXR_BUILD_WEBSITE OFF CACHE BOOL "" FORCE)
set(OPENEXR_INSTALL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(OPENEXR_INSTALL_TOOLS OFF CACHE BOOL "" FORCE)
set(OPENEXR_INSTALL_DEVELOPMENT OFF CACHE BOOL "" FORCE)
set(OPENEXR_FORCE_INTERNAL_IMATH ON CACHE BOOL "" FORCE)
FetchContent_Declare(Imath
    URL https://github.com/AcademySoftwareFoundation/Imath/archive/refs/tags/v3.2.2.tar.gz
    URL_HASH SHA256=B4275D83FB95521510E389B8D13AF10298ED5BED1C8E13EFD961D91B1105E462
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
FetchContent_MakeAvailable(Imath)
FetchContent_Declare(ure_openexr
    URL https://github.com/AcademySoftwareFoundation/openexr/archive/refs/tags/v3.4.14.tar.gz
    URL_HASH SHA256=13C3327100A7B92E4C6A048DB03EF07EE2DB8E79BAA4C517C6FAE71E5B80034B
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
FetchContent_MakeAvailable(ure_openexr)
if(UR_HAD_BUILD_SHARED_LIBS)
    set(BUILD_SHARED_LIBS "${UR_SAVED_BUILD_SHARED_LIBS}" CACHE BOOL "Build shared libraries" FORCE)
else()
    unset(BUILD_SHARED_LIBS CACHE)
endif()
foreach(target Imath Iex IlmThread OpenEXRCore OpenEXR)
    if(TARGET ${target})
        set_target_properties(${target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
            ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}")
    endif()
endforeach()

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/product_build_config.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/product_build_config.hpp
    @ONLY)

add_library(ure_product STATIC
    src/product_service.cpp
    src/product_scene.cpp
    src/product_material.cpp
    src/product_measurement.cpp
    src/product_output.cpp
    src/scene_tool_service.cpp)
target_include_directories(ure_product
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/generated
        ${CMAKE_SOURCE_DIR}/third_party)
target_link_libraries(ure_product PUBLIC ure_core ure_sceneio ure_reconstruction OpenEXR::OpenEXR)

if(MSVC)
    target_compile_definitions(ure_product PRIVATE
        _CRT_SECURE_NO_WARNINGS _SILENCE_CXX23_DENORM_DEPRECATION_WARNING)
    target_compile_options(ure_product PRIVATE /W4 /WX /utf-8 /EHsc /wd4819)
else()
    target_compile_options(ure_product PRIVATE -Wall -Wextra -Werror -pedantic)
endif()
