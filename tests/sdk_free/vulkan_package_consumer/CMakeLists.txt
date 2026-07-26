cmake_minimum_required(VERSION 3.25)
project(UltraRenderVulkanConsumer LANGUAGES CXX)

find_package(
    UltraRender CONFIG REQUIRED
    COMPONENTS ure_vulkan
)
if(NOT UltraRender_VULKAN_BACKEND_AVAILABLE)
    message(FATAL_ERROR "Installed package does not advertise Vulkan")
endif()

add_executable(
    ultrarender_vulkan_consumer
    main.cpp
)
target_link_libraries(
    ultrarender_vulkan_consumer
    PRIVATE UltraRender::ure_vulkan
)
target_compile_features(
    ultrarender_vulkan_consumer PRIVATE cxx_std_23
)

if(MSVC)
    target_compile_options(
        ultrarender_vulkan_consumer
        PRIVATE /W4 /WX /utf-8 /EHsc
    )
else()
    target_compile_options(
        ultrarender_vulkan_consumer
        PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
endif()
