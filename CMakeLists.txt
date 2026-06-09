cmake_minimum_required(VERSION 3.25)
project(UltraRender VERSION 1.0.0 LANGUAGES CXX CUDA)

option(UR_BUILD_TESTS "Build tests" ON)
option(UR_BUILD_CLI "Build CLI" ON)
option(UR_BUILD_PHYSICS "Build physics/acoustic subsystem" ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 20)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
    set(CMAKE_CUDA_ARCHITECTURES "all-major")
endif()

include_directories(${CMAKE_CUDA_TOOLKIT_INCLUDE_DIRECTORIES})
include(GNUInstallDirs)

add_subdirectory(libs/ure_types)
add_subdirectory(libs/ure_diag)

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
