if(NOT WIN32)
    message(FATAL_ERROR "ure_d3d12 is available only on Windows")
endif()

add_library(ure_d3d12 STATIC
    src/d3d12_runtime_device.cpp
)

target_include_directories(ure_d3d12
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(
    ure_d3d12
    PUBLIC ure_types ure_runtime
    PRIVATE ure_diag d3d12 dxgi dxguid)
target_compile_features(ure_d3d12 PUBLIC cxx_std_23)
target_compile_options(
    ure_d3d12 PRIVATE /W4 /WX /utf-8 /EHsc /wd4819)
target_compile_definitions(
    ure_d3d12 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

install(TARGETS ure_d3d12 EXPORT UltraRender_Targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
