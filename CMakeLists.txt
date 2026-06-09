cmake_minimum_required(VERSION 3.25)
project(UltraRender VERSION 1.0.0 LANGUAGES CXX CUDA)

option(UR_BUILD_TESTS "Build tests" ON)
option(UR_BUILD_CLI "Build CLI" ON)
option(UR_BUILD_PHYSICS "Build physics/acoustic subsystem" ON)
option(UR_USE_OLD_LEGACY_BUILD "Use legacy single-EXE build (deprecated)" OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 20)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
    set(CMAKE_CUDA_ARCHITECTURES "all-major")
endif()

include_directories(${CMAKE_CUDA_TOOLKIT_INCLUDE_DIRECTORIES})
include(GNUInstallDirs)

# --- New modular build ---
if(NOT UR_USE_OLD_LEGACY_BUILD)
    add_subdirectory(libs/ure_types)

    add_subdirectory(libs/ure_config)
    add_subdirectory(libs/ure_sceneio)
    add_subdirectory(libs/ure_core)

    if(UR_BUILD_PHYSICS)
        add_subdirectory(libs/ure_physics)
    endif()

    if(UR_BUILD_CLI)
        add_subdirectory(apps/ure_cli)
    endif()

    if(UR_BUILD_TESTS)
        enable_testing()
        add_subdirectory(tests/gpu)
        add_subdirectory(tests/host)
    endif()

# --- Legacy build (deprecated, will be removed) ---
else()
    # Preserved from old CMakeLists.txt
    include_directories(include)

    file(GLOB_RECURSE ENGINE_SOURCES_CPP src/*.cpp)
    list(APPEND ENGINE_SOURCES_CPP "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/bvh_builder.cpp")
    list(APPEND ENGINE_SOURCES_CPP "${CMAKE_CURRENT_SOURCE_DIR}/src/api/gpu_engine_impl.cpp")
    list(REMOVE_DUPLICATES ENGINE_SOURCES_CPP)
    list(REMOVE_ITEM ENGINE_SOURCES_CPP "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/gpu_driver_stub.cpp")

    set(ENGINE_SOURCES_CUDA
        "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/gpu_driver.cu"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/gpu_hardware.cu"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/path_tracer_kernel.cu"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/path_tracer_raygen.cu"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/path_tracer_denoise.cu"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/path_tracer_post.cu"
    )

    file(GLOB_RECURSE ENGINE_HEADERS include/*.hpp include/*.h)

    set(FINAL_SOURCES ${ENGINE_SOURCES_CPP} ${ENGINE_SOURCES_CUDA})

    find_package(OpenMP)
    if(OpenMP_CXX_FOUND)
        link_libraries(OpenMP::OpenMP_CXX)
    endif()

    foreach(source_file ${FINAL_SOURCES})
        if(source_file MATCHES "\\.cpp$")
            if(MSVC)
                if(OpenMP_CXX_FOUND)
                    set_source_files_properties(${source_file} PROPERTIES COMPILE_OPTIONS "/W4;/utf-8;/EHsc;/wd4819;/openmp")
                else()
                    set_source_files_properties(${source_file} PROPERTIES COMPILE_OPTIONS "/W4;/utf-8;/EHsc;/wd4819")
                endif()
            else()
                set_source_files_properties(${source_file} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-pedantic")
            endif()
        endif()
    endforeach()

    add_executable(UltraRender ${FINAL_SOURCES} ${ENGINE_HEADERS})
    set_target_properties(UltraRender PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    if(MSVC)
        target_link_options(UltraRender PRIVATE "/NODEFAULTLIB:LIBCMT")
    endif()

    if(CMAKE_CUDA_COMPILER)
        target_link_libraries(UltraRender PRIVATE ${CUDA_LIBRARIES})
        target_compile_definitions(UltraRender PRIVATE USE_CUDA)
    endif()

    if(CMAKE_CUDA_COMPILER)
        add_subdirectory(tests/gpu)
    endif()
endif()

# ── Install / Export for find_package() support ──
include(CMakePackageConfigHelpers)

set(INCLUDE_INSTALL_DIR ${CMAKE_INSTALL_INCLUDEDIR})

configure_package_config_file(
    cmake/UltraRenderConfig.cmake.in
    ${CMAKE_BINARY_DIR}/UltraRenderConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/UltraRender
    PATH_VARS INCLUDE_INSTALL_DIR
)

install(
    FILES ${CMAKE_BINARY_DIR}/UltraRenderConfig.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/UltraRender
)

install(
    EXPORT UltraRender_Targets
    FILE UltraRenderTargets.cmake
    NAMESPACE UltraRender::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/UltraRender
)

message(STATUS "UltraRender v${PROJECT_VERSION} configured.")
message(STATUS "  Modular build: ${UR_USE_OLD_LEGACY_BUILD}")
