add_library(ure_vulkan STATIC
    src/vulkan_runtime_device.cpp
)

target_include_directories(ure_vulkan
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        "${CMAKE_SOURCE_DIR}/third_party/Vulkan-Headers/include"
        "${CMAKE_SOURCE_DIR}/third_party/volk"
)
target_link_libraries(
    ure_vulkan
    PUBLIC ure_types ure_runtime
    PRIVATE ure_diag ${CMAKE_DL_LIBS})
target_compile_features(ure_vulkan PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(
        ure_vulkan PRIVATE /W4 /WX /utf-8 /EHsc /wd4819)
else()
    target_compile_options(
        ure_vulkan PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

install(TARGETS ure_vulkan EXPORT UltraRender_Targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
