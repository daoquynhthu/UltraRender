add_executable(
    test_multi_backend_inventory
    test_multi_backend_inventory.cpp
)
target_link_libraries(
    test_multi_backend_inventory PRIVATE
    ure_core
    ure_runtime
    ure_vulkan
)
target_compile_definitions(
    test_multi_backend_inventory PRIVATE
    UR_CUDA_ARTIFACT="$<TARGET_FILE:ure_core>"
    UR_VULKAN_ARTIFACT="${CMAKE_SOURCE_DIR}/shaders/vulkan/generated/spectral_polarization.spv"
)
if(UR_ENABLE_D3D12)
    target_link_libraries(
        test_multi_backend_inventory PRIVATE ure_d3d12)
    target_compile_definitions(
        test_multi_backend_inventory PRIVATE
        UR_T10_HAS_D3D12=1
        UR_D3D12_ARTIFACT="${CMAKE_SOURCE_DIR}/shaders/d3d12/generated/foundation.dxil"
    )
endif()
target_compile_features(
    test_multi_backend_inventory PRIVATE cxx_std_23)
if(MSVC)
    target_compile_options(
        test_multi_backend_inventory PRIVATE
        /W4 /WX /utf-8 /EHsc /wd4819)
else()
    target_compile_options(
        test_multi_backend_inventory PRIVATE
        -Wall -Wextra -pedantic -Werror)
endif()

add_test(
    NAME multi_backend_inventory
    COMMAND test_multi_backend_inventory)
