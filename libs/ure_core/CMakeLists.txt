add_library(ure_core STATIC
    src/path_tracer_kernel.cu
    src/path_tracer_raygen.cu
    src/path_tracer_denoise.cu
    src/path_tracer_post.cu
    src/gpu_driver.cu
    src/gpu_hardware.cu
    src/gpu_scene_loader.cpp
    src/gpu_engine_impl.cpp
    src/gpu_scene_compiler.cpp
    src/scene_ir_compiler.cpp
    src/bvh_builder.cpp
    src/bvh_accelerator.cpp
)

target_include_directories(ure_core PUBLIC include
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../ure_sceneio/include"
)
target_link_libraries(ure_core PUBLIC ure_types)

if(MSVC)
    target_compile_options(ure_core PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:/W4;/utf-8;/EHsc;/wd4819>
    )
endif()

target_link_libraries(ure_core PRIVATE ${CUDA_LIBRARIES})
target_compile_definitions(ure_core PUBLIC USE_CUDA)

set_target_properties(ure_core PROPERTIES
    CUDA_ARCHITECTURES "all-major"
    CUDA_STANDARD 20
    CUDA_STANDARD_REQUIRED ON
)
