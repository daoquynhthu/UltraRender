if(CMAKE_CUDA_COMPILER)
    message(STATUS "Building GPU tests (modular)")

    set(GPU_TEST_INCLUDES
        ${CMAKE_CUDA_TOOLKIT_INCLUDE_DIRECTORIES}
    )

    add_executable(gpu_test_device
        test_device.cu
    )
    target_include_directories(gpu_test_device PRIVATE ${GPU_TEST_INCLUDES})
    target_link_libraries(gpu_test_device PRIVATE ure_core CUDA::cudart)

    add_executable(gpu_test_math
        test_math_functions.cu
    )
    target_include_directories(gpu_test_math PRIVATE ${GPU_TEST_INCLUDES})
    target_link_libraries(gpu_test_math PRIVATE ure_core CUDA::cudart)

    add_executable(gpu_test_spectral
        test_spectral_pipeline.cu
    )
    target_include_directories(gpu_test_spectral PRIVATE ${GPU_TEST_INCLUDES})
    target_link_libraries(gpu_test_spectral PRIVATE ure_core CUDA::cudart)

    add_executable(gpu_test_spectral_soa
        test_spectral_pipeline_soa.cu
    )
    target_include_directories(gpu_test_spectral_soa PRIVATE ${GPU_TEST_INCLUDES})
    target_link_libraries(gpu_test_spectral_soa PRIVATE ure_core CUDA::cudart)

    add_executable(gpu_test_render
        test_render_basic.cu
    )
    target_include_directories(gpu_test_render PRIVATE ${GPU_TEST_INCLUDES})
    target_link_libraries(gpu_test_render PRIVATE ure_core CUDA::cudart)

    add_executable(gpu_test_hardware
        test_hardware.cu
    )
    target_include_directories(gpu_test_hardware PRIVATE ${GPU_TEST_INCLUDES})
    target_link_libraries(gpu_test_hardware PRIVATE ure_core CUDA::cudart)

    add_executable(gpu_test_instance
        test_instance_hotupdate.cu
    )
    target_include_directories(gpu_test_instance PRIVATE ${GPU_TEST_INCLUDES})
    target_link_libraries(gpu_test_instance PRIVATE ure_core CUDA::cudart)

    add_executable(gpu_test_tangents
        test_gpu_tangents.cu
    )
    target_include_directories(gpu_test_tangents PRIVATE ${GPU_TEST_INCLUDES})
    target_link_libraries(gpu_test_tangents PRIVATE ure_core CUDA::cudart)

    add_executable(gpu_test_denoise
        test_gpu_denoise.cu
    )
    target_include_directories(gpu_test_denoise PRIVATE ${GPU_TEST_INCLUDES})
    target_link_libraries(gpu_test_denoise PRIVATE ure_core CUDA::cudart)

    add_executable(gpu_test_polarization
        test_gpu_polarization.cu
    )
    target_include_directories(gpu_test_polarization PRIVATE ${GPU_TEST_INCLUDES})
    target_link_libraries(gpu_test_polarization PRIVATE ure_core CUDA::cudart)

    add_executable(gpu_test_volume
        test_gpu_volume.cu
    )
    target_include_directories(gpu_test_volume PRIVATE ${GPU_TEST_INCLUDES})
    target_link_libraries(gpu_test_volume PRIVATE ure_core ure_sceneio CUDA::cudart)

    add_executable(gpu_test_contract
        test_distributed_contract.cu
    )
    target_include_directories(gpu_test_contract PRIVATE ${GPU_TEST_INCLUDES})
    target_link_libraries(gpu_test_contract PRIVATE ure_core CUDA::cudart)

    add_executable(gpu_test_wave_optics
        test_wave_optics_gpu.cu
    )
    target_include_directories(gpu_test_wave_optics PRIVATE
        ${GPU_TEST_INCLUDES}
        "${CMAKE_SOURCE_DIR}/libs/ure_core/src"
    )
    target_link_libraries(gpu_test_wave_optics PRIVATE ure_core CUDA::cudart)

    add_executable(gpu_test_cuda_runtime
        test_cuda_runtime_device.cu
    )
    target_include_directories(gpu_test_cuda_runtime PRIVATE
        ${GPU_TEST_INCLUDES}
        "${CMAKE_SOURCE_DIR}/libs/ure_core/src"
    )
    target_link_libraries(gpu_test_cuda_runtime PRIVATE
        ure_core CUDA::cudart CUDA::cuda_driver
    )
    if(TARGET ure_optix_v7_shader)
        add_dependencies(
            gpu_test_cuda_runtime
            ure_optix_v7_shader)
        target_compile_definitions(
            gpu_test_cuda_runtime PRIVATE
            "UR_OPTIX_TRACE_MODULE_PATH=\"${UR_OPTIX_TRACE_MODULE}\"")
    endif()

    add_executable(gpu_test_acceleration_contract
        test_acceleration_hit_contract.cu
    )
    target_include_directories(
        gpu_test_acceleration_contract PRIVATE
        ${GPU_TEST_INCLUDES}
        "${CMAKE_SOURCE_DIR}/libs/ure_core/src"
    )
    target_link_libraries(
        gpu_test_acceleration_contract PRIVATE
        ure_core CUDA::cudart
    )

    add_executable(
        gpu_test_clustered_geometry
        test_clustered_geometry.cu
    )
    target_include_directories(
        gpu_test_clustered_geometry PRIVATE
        ${GPU_TEST_INCLUDES}
    )
    target_link_libraries(
        gpu_test_clustered_geometry PRIVATE
        ure_runtime CUDA::cudart
    )

    add_executable(
        gpu_test_cluster_lod
        test_cluster_lod.cu
    )
    target_include_directories(
        gpu_test_cluster_lod PRIVATE
        ${GPU_TEST_INCLUDES}
    )
    target_link_libraries(
        gpu_test_cluster_lod PRIVATE
        ure_runtime CUDA::cudart
    )

    add_executable(
        gpu_test_dynamic_geometry
        test_dynamic_geometry.cu
    )
    target_include_directories(
        gpu_test_dynamic_geometry PRIVATE
        ${GPU_TEST_INCLUDES}
    )
    target_link_libraries(
        gpu_test_dynamic_geometry PRIVATE
        ure_core CUDA::cudart
    )

    add_executable(
        gpu_test_support_measure_composition
        test_support_measure_composition.cu
    )
    target_include_directories(
        gpu_test_support_measure_composition PRIVATE
        ${GPU_TEST_INCLUDES}
        "${CMAKE_SOURCE_DIR}/libs/ure_core/src"
    )
    target_link_libraries(
        gpu_test_support_measure_composition PRIVATE
        ure_transport ure_runtime CUDA::cudart
    )

    add_executable(
        gpu_test_pilot_statistics
        test_pilot_statistics.cu
    )
    target_include_directories(
        gpu_test_pilot_statistics PRIVATE
        ${GPU_TEST_INCLUDES}
    )
    target_link_libraries(
        gpu_test_pilot_statistics PRIVATE
        ure_transport ure_runtime CUDA::cudart
    )

    add_executable(gpu_phase_r_guiding_benchmark
        phase_r_guiding_benchmark.cu
    )
    target_include_directories(gpu_phase_r_guiding_benchmark PRIVATE ${GPU_TEST_INCLUDES})
    target_link_libraries(gpu_phase_r_guiding_benchmark PRIVATE ure_core CUDA::cudart)

    set(GPU_TEST_TARGETS
        gpu_test_device gpu_test_math gpu_test_spectral gpu_test_spectral_soa
        gpu_test_hardware gpu_test_render gpu_test_instance gpu_test_tangents
        gpu_test_denoise gpu_test_polarization gpu_test_volume gpu_test_contract
        gpu_test_wave_optics gpu_test_cuda_runtime
        gpu_test_acceleration_contract
        gpu_test_clustered_geometry
        gpu_test_cluster_lod
        gpu_test_dynamic_geometry
        gpu_test_support_measure_composition
        gpu_test_pilot_statistics
        gpu_phase_r_guiding_benchmark
    )
    foreach(gpu_test IN LISTS GPU_TEST_TARGETS)
        if(MSVC)
            target_compile_options(${gpu_test} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/EHsc;/utf-8;/wd4819>")
        endif()
        set_target_properties(${gpu_test} PROPERTIES CUDA_RUNTIME_LIBRARY None)
        if(CMAKE_GENERATOR MATCHES "Ninja")
            set_property(TARGET ${gpu_test} PROPERTY JOB_POOL_COMPILE ur_cuda_heavy_compile)
        endif()
    endforeach()

    add_test(NAME gpu_device COMMAND gpu_test_device)
    add_test(NAME gpu_math COMMAND gpu_test_math)
    add_test(NAME gpu_spectral COMMAND gpu_test_spectral)
    add_test(NAME gpu_spectral_soa COMMAND gpu_test_spectral_soa)
    add_test(NAME gpu_hardware COMMAND gpu_test_hardware)
    add_test(NAME gpu_render COMMAND gpu_test_render)
    add_test(NAME gpu_instance COMMAND gpu_test_instance)
    add_test(NAME gpu_tangents COMMAND gpu_test_tangents)
    add_test(NAME gpu_denoise COMMAND gpu_test_denoise)
    add_test(NAME gpu_polarization COMMAND gpu_test_polarization)
    add_test(NAME gpu_volume COMMAND gpu_test_volume)
    add_test(NAME gpu_contract COMMAND gpu_test_contract)
    add_test(NAME gpu_wave_optics COMMAND gpu_test_wave_optics)
    add_test(NAME gpu_cuda_runtime COMMAND gpu_test_cuda_runtime)
    add_test(
        NAME gpu_acceleration_contract
        COMMAND gpu_test_acceleration_contract)
    add_test(
        NAME gpu_clustered_geometry
        COMMAND gpu_test_clustered_geometry)
    add_test(
        NAME gpu_cluster_lod
        COMMAND gpu_test_cluster_lod)
    add_test(
        NAME gpu_dynamic_geometry
        COMMAND gpu_test_dynamic_geometry)
    add_test(
        NAME gpu_support_measure_composition
        COMMAND gpu_test_support_measure_composition)
    add_test(
        NAME gpu_pilot_statistics
        COMMAND gpu_test_pilot_statistics)

    add_custom_target(gpu_tests
        COMMAND gpu_test_device
        COMMAND gpu_test_math
        COMMAND gpu_test_spectral
        COMMAND gpu_test_spectral_soa
        COMMAND gpu_test_hardware
        COMMAND gpu_test_render
        COMMAND gpu_test_instance
        COMMAND gpu_test_tangents
        COMMAND gpu_test_denoise
        COMMAND gpu_test_polarization
        COMMAND gpu_test_volume
        COMMAND gpu_test_contract
        COMMAND gpu_test_wave_optics
        COMMAND gpu_test_cuda_runtime
        COMMAND gpu_test_acceleration_contract
        COMMAND gpu_test_clustered_geometry
        COMMAND gpu_test_support_measure_composition
        COMMAND gpu_test_pilot_statistics
        DEPENDS gpu_test_device gpu_test_math gpu_test_spectral gpu_test_spectral_soa gpu_test_hardware gpu_test_render gpu_test_instance gpu_test_tangents gpu_test_denoise gpu_test_polarization gpu_test_volume gpu_test_contract gpu_test_wave_optics gpu_test_cuda_runtime gpu_test_acceleration_contract gpu_test_clustered_geometry gpu_test_support_measure_composition gpu_test_pilot_statistics
        COMMENT "Running all GPU tests"
    )
endif()
