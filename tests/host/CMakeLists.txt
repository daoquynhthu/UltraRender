enable_testing()

if(MSVC)
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/EHsc;/utf-8;/wd4819>")
endif()

add_executable(test_world test_world.cpp)
target_link_libraries(test_world PRIVATE ure_types)
add_test(NAME test_world COMMAND test_world)

add_executable(test_asset_pipeline test_asset_pipeline.cpp)
target_link_libraries(test_asset_pipeline PRIVATE ure_sceneio ure_diag)
add_test(NAME test_asset_pipeline COMMAND test_asset_pipeline)

add_executable(test_config test_config.cpp)
target_link_libraries(test_config PRIVATE ure_config ure_diag)
add_test(NAME test_config COMMAND test_config)

add_executable(test_runtime_contract test_runtime_contract.cpp)
target_link_libraries(test_runtime_contract PRIVATE ure_runtime)
add_test(NAME test_runtime_contract COMMAND test_runtime_contract)

add_executable(
    test_acceleration_contract
    test_acceleration_contract.cpp)
target_link_libraries(
    test_acceleration_contract PRIVATE ure_runtime)
add_test(
    NAME test_acceleration_contract
    COMMAND test_acceleration_contract)

add_executable(
    test_clustered_geometry
    test_clustered_geometry.cpp)
target_link_libraries(
    test_clustered_geometry PRIVATE ure_runtime)
add_test(
    NAME test_clustered_geometry
    COMMAND test_clustered_geometry)

add_executable(
    test_cluster_lod
    test_cluster_lod.cpp)
target_link_libraries(
    test_cluster_lod PRIVATE ure_runtime)
add_test(
    NAME test_cluster_lod
    COMMAND test_cluster_lod)

add_executable(
    test_dynamic_geometry
    test_dynamic_geometry.cpp)
target_link_libraries(
    test_dynamic_geometry PRIVATE ure_runtime)
add_test(
    NAME test_dynamic_geometry
    COMMAND test_dynamic_geometry)

add_executable(test_resource_plan test_resource_plan.cpp)
target_include_directories(test_resource_plan PRIVATE
    "${CMAKE_SOURCE_DIR}/libs/ure_core/include"
)
target_link_libraries(test_resource_plan PRIVATE ure_runtime)
add_test(NAME test_resource_plan COMMAND test_resource_plan)

add_executable(test_execution_graph test_execution_graph.cpp)
target_link_libraries(test_execution_graph PRIVATE ure_runtime)
add_test(NAME test_execution_graph COMMAND test_execution_graph)

add_executable(
    test_multi_backend_schedule
    test_multi_backend_schedule.cpp)
target_link_libraries(
    test_multi_backend_schedule PRIVATE ure_runtime)
add_test(
    NAME test_multi_backend_schedule
    COMMAND test_multi_backend_schedule)

add_executable(test_spectral_oracle test_spectral_oracle.cpp)
target_link_libraries(test_spectral_oracle PRIVATE ure_types)
add_test(NAME test_spectral_oracle COMMAND test_spectral_oracle)

add_executable(test_wave_optics test_wave_optics.cpp)
target_link_libraries(test_wave_optics PRIVATE ure_core ure_diag)
add_test(NAME test_wave_optics COMMAND test_wave_optics)

add_executable(
    test_local_fullwave
    test_local_fullwave.cpp)
target_link_libraries(
    test_local_fullwave
    PRIVATE ure_core ure_sceneio ure_diag)
add_test(
    NAME test_local_fullwave
    COMMAND test_local_fullwave)

add_executable(test_integrator test_integrator.cpp)
target_link_libraries(test_integrator PRIVATE ure_core ure_diag)
add_test(NAME test_integrator COMMAND test_integrator)

add_executable(test_mie_phase test_mie_phase.cpp)
target_link_libraries(test_mie_phase PRIVATE ure_sceneio ure_core ure_diag)
target_compile_definitions(test_mie_phase PRIVATE
    URE_TEST_ASSET_DIR="${CMAKE_SOURCE_DIR}/tests/assets"
)
add_test(NAME test_mie_phase COMMAND test_mie_phase)

add_executable(test_native_scene test_native_scene.cpp)
target_link_libraries(test_native_scene PRIVATE ure_sceneio ure_diag)
target_compile_definitions(test_native_scene PRIVATE
    URE_TEST_ASSET_DIR="${CMAKE_SOURCE_DIR}/tests/assets/native_scene"
)
add_test(NAME test_native_scene COMMAND test_native_scene)

add_executable(test_native_scene_ir test_native_scene_ir.cpp)
target_link_libraries(test_native_scene_ir PRIVATE ure_sceneio ure_core ure_diag)
target_compile_definitions(test_native_scene_ir PRIVATE
    URE_TEST_ASSET_DIR="${CMAKE_SOURCE_DIR}/tests/assets/native_scene/q3_full_scene"
)
add_test(NAME test_native_scene_ir COMMAND test_native_scene_ir)

add_executable(test_native_procedural_graph test_native_procedural_graph.cpp)
target_link_libraries(test_native_procedural_graph PRIVATE ure_sceneio ure_core ure_diag)
target_compile_definitions(test_native_procedural_graph PRIVATE
    URE_TEST_ASSET_DIR="${CMAKE_SOURCE_DIR}/tests/assets/native_scene/q4_procedural_scene"
)
add_test(NAME test_native_procedural_graph COMMAND test_native_procedural_graph)

add_executable(test_native_script_build test_native_script_build.cpp)
target_link_libraries(test_native_script_build PRIVATE ure_sceneio ure_diag)
add_test(NAME test_native_script_build COMMAND test_native_script_build)

add_executable(test_native_resource_catalog test_native_resource_catalog.cpp)
target_link_libraries(test_native_resource_catalog PRIVATE ure_sceneio ure_diag)
add_test(NAME test_native_resource_catalog COMMAND test_native_resource_catalog)

add_executable(test_native_solver_contract test_native_solver_contract.cpp)
target_link_libraries(test_native_solver_contract PRIVATE ure_sceneio ure_diag)
add_test(NAME test_native_solver_contract COMMAND test_native_solver_contract)

add_executable(test_native_simulation_contract test_native_simulation_contract.cpp)
target_link_libraries(test_native_simulation_contract PRIVATE ure_sceneio ure_diag)
add_test(NAME test_native_simulation_contract COMMAND test_native_simulation_contract)

add_executable(test_native_tooling test_native_tooling.cpp)
target_link_libraries(test_native_tooling PRIVATE ure_sceneio ure_diag)
target_compile_definitions(test_native_tooling PRIVATE
    URE_TEST_ASSET_DIR="${CMAKE_SOURCE_DIR}/tests/assets/native_scene/q3_full_scene"
)
add_test(NAME test_native_tooling COMMAND test_native_tooling)

add_executable(test_native_adapter test_native_adapter.cpp)
target_link_libraries(test_native_adapter PRIVATE ure_sceneio ure_diag)
target_compile_definitions(test_native_adapter PRIVATE
    URE_TEST_SCENE_DIR="${CMAKE_SOURCE_DIR}/scenes"
)
add_test(NAME test_native_adapter COMMAND test_native_adapter)

add_executable(test_native_compiled_cache test_native_compiled_cache.cpp)
target_link_libraries(test_native_compiled_cache PRIVATE ure_sceneio ure_diag)
add_test(NAME test_native_compiled_cache COMMAND test_native_compiled_cache)

add_executable(test_native_validation_suite test_native_validation_suite.cpp)
target_include_directories(test_native_validation_suite PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
target_link_libraries(test_native_validation_suite PRIVATE ure_sceneio ure_diag)
target_compile_definitions(test_native_validation_suite PRIVATE
    URE_Q12_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/assets/native_scene/q12_validation"
)
add_test(NAME test_native_validation_suite COMMAND test_native_validation_suite)

add_executable(test_gltf_frontend test_gltf_frontend.cpp)
target_link_libraries(test_gltf_frontend PRIVATE ure_sceneio ure_core ure_diag)
add_test(NAME test_gltf_frontend COMMAND test_gltf_frontend)

add_executable(test_material_graph test_material_graph.cpp)
target_link_libraries(test_material_graph PRIVATE ure_core ure_diag)
add_test(NAME test_material_graph COMMAND test_material_graph)

add_executable(test_materialx_io test_materialx_io.cpp)
target_link_libraries(test_materialx_io PRIVATE ure_sceneio ure_core ure_diag)
add_test(NAME test_materialx_io COMMAND test_materialx_io)

add_executable(test_session test_session.cpp)
target_link_libraries(test_session PRIVATE ure_sceneio ure_core ure_diag)
add_test(NAME test_session COMMAND test_session)

add_executable(test_distributed_file_io test_distributed_file_io.cpp)
target_link_libraries(test_distributed_file_io PRIVATE ure_core ure_diag)
add_test(NAME test_distributed_file_io COMMAND test_distributed_file_io)

set(CUDA_BACKEND_HOST_TESTS
    test_gltf_frontend
    test_materialx_io
    test_material_graph
    test_mie_phase
    test_native_procedural_graph
    test_native_scene_ir
)
foreach(cuda_backend_test IN LISTS CUDA_BACKEND_HOST_TESTS)
    target_link_libraries(${cuda_backend_test} PRIVATE CUDA::cudart)
endforeach()

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME test_pyure_smoke
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_pyure_smoke.py
    )
    set_tests_properties(test_pyure_smoke PROPERTIES
        ENVIRONMENT "PYTHONPATH=${CMAKE_SOURCE_DIR};PYURE_NATIVE=$<TARGET_FILE:pyure_native>;URE_CLI=$<TARGET_FILE:ure_cli>;URE_NATIVE_TEST_SCENE=${CMAKE_SOURCE_DIR}/tests/assets/native_scene/q3_full_scene/full_scene.urescene"
    )
endif()
