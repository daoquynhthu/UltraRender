add_executable(
    test_d3d12_runtime
    test_d3d12_runtime.cpp
)
target_link_libraries(
    test_d3d12_runtime PRIVATE
    ure_d3d12
)
target_compile_definitions(
    test_d3d12_runtime PRIVATE
    UR_D3D12_SHADER_DIR="${CMAKE_SOURCE_DIR}/shaders/d3d12/generated"
)
target_compile_features(
    test_d3d12_runtime PRIVATE cxx_std_23)
target_compile_options(
    test_d3d12_runtime PRIVATE
    /W4 /WX /utf-8 /EHsc /wd4819)

add_test(
    NAME d3d12_runtime
    COMMAND test_d3d12_runtime)
