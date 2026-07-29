add_library(ure_core STATIC
    src/mie_solver.cpp
    src/mlt.cpp
    src/restir_reservoir.cpp
    src/restir_pt.cpp
    src/restir_pt_runtime.cu
    src/mlt_runtime.cu
    src/restir_di_runtime.cu
    src/bidirectional_runtime.cu
    src/backend_cuda.cu
    src/cuda_runtime_device.cu
    src/cuda_resource_registry.cu
    src/ure_c_api.cpp
    src/path_tracer_kernel.cu
    src/path_tracer_host_api.cu
    src/path_tracer_raygen.cu
    src/path_tracer_denoise.cu
    src/path_tracer_post.cu
    src/gpu_driver.cu
    src/gpu_hardware.cu
    src/gpu_multi_driver.cu
    src/gpu_scene_loader.cpp
    src/gpu_engine_impl.cpp
    src/gpu_scene_compiler.cpp
    src/distributed_contract.cpp
    src/distributed_file_io.cpp
    src/session.cpp
    src/acceleration_build_pipeline.cpp
    src/acceleration_upload.cu
    src/optix_acceleration_provider.cpp
    src/bvh_builder.cpp
    src/bvh_accelerator.cpp
    src/render_api.cpp
    src/specular_manifold.cpp
    src/anisotropic_optics.cpp
    src/anisotropic_optics_gpu.cu
    src/local_fullwave.cpp
    src/wave_optics.cpp
    src/wave_optics_gpu.cu
)

target_include_directories(ure_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../ure_sceneio/include"
)
target_link_libraries(ure_core PUBLIC ure_types ure_runtime ure_diag PRIVATE ure_sceneio)
if(UR_ENABLE_VULKAN)
    target_link_libraries(ure_core PRIVATE ure_vulkan)
    target_compile_definitions(ure_core PRIVATE UR_ENABLE_VULKAN=1)
endif()

if(MSVC)
    target_compile_options(ure_core PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:/W4;/utf-8;/EHsc;/wd4819>
    )
endif()

target_link_libraries(ure_core PRIVATE CUDA::cudart CUDA::cuda_driver)
target_compile_definitions(ure_core PRIVATE USE_CUDA)
if(UR_ENABLE_OPTIX)
    find_path(UR_OPTIX_INCLUDE_DIR
        NAMES optix.h
        HINTS
            "${UR_OPTIX_ROOT}"
            "$ENV{OPTIX_ROOT}"
            "$ENV{OptiX_INSTALL_DIR}"
        PATH_SUFFIXES include
        NO_CACHE)
endif()
if(UR_ENABLE_OPTIX AND UR_OPTIX_INCLUDE_DIR)
    set_source_files_properties(
        src/optix_acceleration_provider.cpp
        PROPERTIES
            COMPILE_DEFINITIONS UR_HAS_OPTIX=1
            INCLUDE_DIRECTORIES "${UR_OPTIX_INCLUDE_DIR}")
    list(GET CMAKE_CUDA_ARCHITECTURES 0 UR_OPTIX_PTX_ARCH)
    set(UR_OPTIX_TRACE_MODULE
        "${CMAKE_BINARY_DIR}/shaders/optix/phase_v7_acceleration.optixir"
        CACHE INTERNAL "Phase V.7 OptiX traversal module" FORCE)
    add_custom_command(
        OUTPUT "${UR_OPTIX_TRACE_MODULE}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${CMAKE_BINARY_DIR}/shaders/optix"
        COMMAND "${CMAKE_CUDA_COMPILER}"
            --optix-ir
            --std=c++17
            "--gpu-architecture=compute_${UR_OPTIX_PTX_ARCH}"
            "-I${UR_OPTIX_INCLUDE_DIR}"
            "${CMAKE_SOURCE_DIR}/shaders/optix/phase_v7_acceleration.cu"
            -o "${UR_OPTIX_TRACE_MODULE}"
        DEPENDS
            "${CMAKE_SOURCE_DIR}/shaders/optix/phase_v7_acceleration.cu"
        VERBATIM)
    add_custom_target(
        ure_optix_v7_shader
        DEPENDS "${UR_OPTIX_TRACE_MODULE}")
    message(STATUS "OptiX acceleration provider enabled: ${UR_OPTIX_INCLUDE_DIR}")
else()
    set(UR_OPTIX_TRACE_MODULE ""
        CACHE INTERNAL "Phase V.7 OptiX traversal module" FORCE)
    set_source_files_properties(
        src/optix_acceleration_provider.cpp
        PROPERTIES COMPILE_DEFINITIONS UR_HAS_OPTIX=0)
    if(UR_ENABLE_OPTIX)
        message(STATUS "OptiX SDK not found; OptiX provider disabled without affecting other backends")
    endif()
endif()

set_target_properties(ure_core PROPERTIES
    CUDA_ARCHITECTURES "${CMAKE_CUDA_ARCHITECTURES}"
    CUDA_RUNTIME_LIBRARY None
    CUDA_STANDARD 20
    CUDA_STANDARD_REQUIRED ON
)
if(CMAKE_GENERATOR MATCHES "Ninja")
    set_property(TARGET ure_core PROPERTY JOB_POOL_COMPILE ur_cuda_heavy_compile)
endif()

target_compile_options(ure_core PRIVATE
    $<$<COMPILE_LANGUAGE:CUDA>:-lineinfo>
)

install(TARGETS ure_core EXPORT UltraRender_Targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    PATTERN "*.cuh" EXCLUDE
    PATTERN "*.cu" EXCLUDE
    PATTERN "detail" EXCLUDE
)

add_library(pyure_native SHARED src/ure_c_api.cpp)
target_include_directories(pyure_native PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../ure_sceneio/include"
)
target_link_libraries(pyure_native PRIVATE ure_core ure_sceneio ure_diag CUDA::cudart)
target_compile_definitions(pyure_native PRIVATE USE_CUDA)
set_target_properties(pyure_native PROPERTIES
    OUTPUT_NAME "pyure_native"
    WINDOWS_EXPORT_ALL_SYMBOLS ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/pyure"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/pyure"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/pyure"
)
if(MSVC)
    target_compile_options(pyure_native PRIVATE /W4 /utf-8 /EHsc /wd4819)
endif()
