add_executable(
    test_vulkan_runtime
    test_vulkan_runtime.cpp
)
target_link_libraries(
    test_vulkan_runtime PRIVATE
    ure_vulkan
)
target_compile_definitions(
    test_vulkan_runtime PRIVATE
    UR_VULKAN_SHADER_DIR="${CMAKE_SOURCE_DIR}/shaders/vulkan/generated"
)
target_compile_features(test_vulkan_runtime PRIVATE cxx_std_23)

if(MSVC)
    target_compile_options(
        test_vulkan_runtime PRIVATE
        /W4 /WX /utf-8 /EHsc /wd4819)
else()
    target_compile_options(
        test_vulkan_runtime PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME vulkan_runtime COMMAND test_vulkan_runtime)
