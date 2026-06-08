if(CMAKE_CUDA_COMPILER)
    message(STATUS "Building GPU tests")

    set(GPU_TEST_INCLUDES
        "${CMAKE_SOURCE_DIR}/include"
        ${CMAKE_CUDA_TOOLKIT_INCLUDE_DIRECTORIES}
    )

    add_executable(gpu_test_device
        test_device.cu
    )
    target_include_directories(gpu_test_device PRIVATE ${GPU_TEST_INCLUDES})
    target_link_libraries(gpu_test_device PRIVATE ${CUDA_LIBRARIES})

    add_executable(gpu_test_math
        test_math_functions.cu
    )
    target_include_directories(gpu_test_math PRIVATE ${GPU_TEST_INCLUDES})
    target_link_libraries(gpu_test_math PRIVATE ${CUDA_LIBRARIES})

    add_executable(gpu_test_spectral
        test_spectral_pipeline.cu
    )
    target_include_directories(gpu_test_spectral PRIVATE ${GPU_TEST_INCLUDES})
    target_link_libraries(gpu_test_spectral PRIVATE ${CUDA_LIBRARIES})

    set(GPU_RENDER_DEPS
        "${CMAKE_SOURCE_DIR}/src/gpu/gpu_scene_loader.cpp"
        "${CMAKE_SOURCE_DIR}/src/gpu/bvh_builder.cpp"
        "${CMAKE_SOURCE_DIR}/src/gpu/path_tracer_denoise.cu"
        "${CMAKE_SOURCE_DIR}/src/gpu/path_tracer_post.cu"
    )

    add_executable(gpu_test_render
        test_render_basic.cu
        ${GPU_RENDER_DEPS}
    )
    target_include_directories(gpu_test_render PRIVATE ${GPU_TEST_INCLUDES})
    target_link_libraries(gpu_test_render PRIVATE ${CUDA_LIBRARIES})

    if(MSVC)
        foreach(gpu_test gpu_test_device gpu_test_math gpu_test_spectral gpu_test_render)
            target_compile_options(${gpu_test} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/EHsc;/utf-8;/wd4819>")
        endforeach()
    endif()

    add_custom_target(gpu_tests
        COMMAND gpu_test_device
        COMMAND gpu_test_math
        COMMAND gpu_test_spectral
        COMMAND gpu_test_render
        DEPENDS gpu_test_device gpu_test_math gpu_test_spectral gpu_test_render
        COMMENT "Running all GPU tests"
    )
endif()
