add_library(ure_runtime STATIC
    src/acceleration.cpp
    src/clustered_geometry.cpp
    src/execution_graph.cpp
    src/multi_backend.cpp
    src/resource_plan.cpp
    src/runtime_validation.cpp
)

target_include_directories(ure_runtime PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(ure_runtime PUBLIC ure_types)

if(MSVC)
    target_compile_options(
        ure_runtime PRIVATE /W4 /WX /utf-8 /EHsc /wd4819)
else()
    target_compile_options(
        ure_runtime PRIVATE -Wall -Wextra -pedantic -Werror)
endif()

install(TARGETS ure_runtime EXPORT UltraRender_Targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
