add_library(ure_runtime STATIC
    src/resource_plan.cpp
    src/runtime_validation.cpp
)

target_include_directories(ure_runtime PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(ure_runtime PUBLIC ure_types)

if(MSVC)
    target_compile_options(ure_runtime PRIVATE /W4 /utf-8 /EHsc /wd4819)
else()
    target_compile_options(ure_runtime PRIVATE -Wall -Wextra -pedantic)
endif()

install(TARGETS ure_runtime EXPORT UltraRender_Targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
