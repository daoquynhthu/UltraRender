cmake_minimum_required(VERSION 3.25)
project(UltraRender VERSION 1.0.0 LANGUAGES CXX)

option(UR_BUILD_TESTS "Build tests" ON)
option(UR_BUILD_CLI "Build CLI" ON)
option(UR_BUILD_PHYSICS "Build physics/acoustic subsystem" ON)
option(UR_ENABLE_CUDA "Build the CUDA production backend" ON)
option(UR_ENABLE_VULKAN "Build the Vulkan compute backend" ON)
if(WIN32)
    option(UR_ENABLE_D3D12 "Build the D3D12/DXR backend" ON)
else()
    option(UR_ENABLE_D3D12 "Build the D3D12/DXR backend" OFF)
endif()
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(UR_ENABLE_CUDA)
    enable_language(CUDA)
    set(CMAKE_CUDA_STANDARD 20)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
    set(UR_CUDA_HEAVY_COMPILE_JOBS 1 CACHE STRING
        "Maximum concurrent high-memory CUDA target compilations under Ninja")
    if(CMAKE_GENERATOR MATCHES "Ninja")
        if(NOT UR_CUDA_HEAVY_COMPILE_JOBS MATCHES "^[1-9][0-9]*$")
            message(FATAL_ERROR "UR_CUDA_HEAVY_COMPILE_JOBS must be a positive integer")
        endif()
        set_property(GLOBAL PROPERTY JOB_POOLS
            ur_cuda_heavy_compile=${UR_CUDA_HEAVY_COMPILE_JOBS})
    endif()

    if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
        set(CMAKE_CUDA_ARCHITECTURES "all-major")
    endif()

    if(MSVC)
        set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} -Xcompiler /wd4819")
    endif()

    find_package(CUDAToolkit REQUIRED)
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
include(GNUInstallDirs)

add_subdirectory(libs/ure_types)
add_subdirectory(libs/ure_diag)
add_subdirectory(libs/ure_runtime)
if(UR_ENABLE_VULKAN)
    add_subdirectory(libs/ure_vulkan)
endif()
if(UR_ENABLE_D3D12)
    add_subdirectory(libs/ure_d3d12)
endif()

add_subdirectory(libs/ure_config)
add_subdirectory(libs/ure_sceneio)

if(UR_ENABLE_CUDA)
    add_subdirectory(libs/ure_core)
endif()

if(UR_BUILD_PHYSICS)
    add_subdirectory(libs/ure_physics)
endif()

if(UR_BUILD_CLI AND UR_ENABLE_CUDA)
    add_subdirectory(apps/ure_cli)
elseif(UR_BUILD_CLI)
    message(STATUS "Skipping ure_cli because no production backend is enabled")
endif()

if(UR_BUILD_TESTS)
    enable_testing()
endif()

if(UR_BUILD_TESTS AND UR_ENABLE_CUDA)
    add_subdirectory(tests/gpu)
    add_subdirectory(tests/host)
elseif(UR_BUILD_TESTS)
    message(STATUS "Skipping CUDA-coupled root tests; use tests/sdk_free")
endif()

if(UR_BUILD_TESTS AND UR_ENABLE_VULKAN)
    add_subdirectory(tests/vulkan)
endif()
if(UR_BUILD_TESTS AND UR_ENABLE_D3D12)
    add_subdirectory(tests/d3d12)
endif()
if(UR_BUILD_TESTS AND UR_ENABLE_CUDA AND UR_ENABLE_VULKAN)
    add_subdirectory(tests/multi_backend)
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

if(UR_ENABLE_VULKAN)
    install(
        DIRECTORY shaders/vulkan/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/UltraRender/shaders/vulkan
    )
endif()
if(UR_ENABLE_D3D12)
    install(
        DIRECTORY shaders/d3d12/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/UltraRender/shaders/d3d12
    )
endif()
if(UR_ENABLE_VULKAN OR UR_ENABLE_D3D12)
    install(
        DIRECTORY shaders/shared/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/UltraRender/shaders/shared
    )
endif()

message(STATUS "UltraRender v${PROJECT_VERSION} configured.")
