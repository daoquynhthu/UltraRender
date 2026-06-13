add_library(ure_core STATIC
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
    src/scene_ir_compiler.cpp
    src/distributed_contract.cpp
    src/distributed_file_io.cpp
    src/session.cpp
    src/bvh_builder.cpp
    src/bvh_accelerator.cpp
    src/world_scene_builder.cpp
    src/render_api.cpp
)

target_include_directories(ure_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../ure_sceneio/include"
)
target_link_libraries(ure_core PUBLIC ure_types ure_diag)

if(MSVC)
    target_compile_options(ure_core PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:/W4;/utf-8;/EHsc;/wd4819>
    )
endif()

target_link_libraries(ure_core PRIVATE CUDA::cudart)
target_compile_definitions(ure_core PUBLIC USE_CUDA)

set_target_properties(ure_core PROPERTIES
    CUDA_ARCHITECTURES "all-major"
    CUDA_RUNTIME_LIBRARY None
    CUDA_STANDARD 20
    CUDA_STANDARD_REQUIRED ON
)

target_compile_options(ure_core PRIVATE
    $<$<COMPILE_LANGUAGE:CUDA>:-lineinfo>
)

install(TARGETS ure_core EXPORT UltraRender_Targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    PATTERN "*.cuh" EXCLUDE
    PATTERN "*.cu" EXCLUDE
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
