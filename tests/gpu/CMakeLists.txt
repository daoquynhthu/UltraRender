if(CMAKE_CUDA_COMPILER)
    message(STATUS "Building GPU tests (modular)")

    set(GPU_TEST_INCLUDES
        ${CMAKE_CUDA_TOOLKIT_INCLUDE_DIRECTORIES}
    )

    add_executable(gpu_test_device
        test_device.cu
    )
    target_include_directories(gpu_test_device PRIVATE ${GPU_TEST_INCLUDES})
    target_link_libraries(gpu_test_device PRIVATE ure_core)

    add_executable(gpu_test_math
        test_math_functions.cu
    )
    target_include_directories(gpu_test_math PRIVATE ${GPU_TEST_INCLUDES})
    target_link_libraries(gpu_test_math PRIVATE ure_core)

    add_executable(gpu_test_spectral
        test_spectral_pipeline.cu
    )
    target_include_directories(gpu_test_spectral PRIVATE ${GPU_TEST_INCLUDES})
    target_link_libraries(gpu_test_spectral PRIVATE ure_core)

    add_executable(gpu_test_render
        test_render_basic.cu
    )
    target_include_directories(gpu_test_render PRIVATE ${GPU_TEST_INCLUDES})
    target_link_libraries(gpu_test_render PRIVATE ure_core)

    add_executable(gpu_test_hardware
        test_hardware.cu
    )
    target_include_directories(gpu_test_hardware PRIVATE ${GPU_TEST_INCLUDES})
    target_link_libraries(gpu_test_hardware PRIVATE ure_core)

    add_executable(gpu_test_instance
        test_instance_hotupdate.cu
    )
    target_include_directories(gpu_test_instance PRIVATE ${GPU_TEST_INCLUDES})
    target_link_libraries(gpu_test_instance PRIVATE ure_core)

    if(MSVC)
        foreach(gpu_test gpu_test_device gpu_test_math gpu_test_spectral gpu_test_hardware gpu_test_render gpu_test_instance)
            target_compile_options(${gpu_test} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/EHsc;/utf-8;/wd4819>")
        endforeach()
    endif()

    add_custom_target(gpu_tests
        COMMAND gpu_test_device
        COMMAND gpu_test_math
        COMMAND gpu_test_spectral
        COMMAND gpu_test_hardware
        COMMAND gpu_test_render
        COMMAND gpu_test_instance
        DEPENDS gpu_test_device gpu_test_math gpu_test_spectral gpu_test_hardware gpu_test_render gpu_test_instance
        COMMENT "Running all GPU tests"
    )
endif()
