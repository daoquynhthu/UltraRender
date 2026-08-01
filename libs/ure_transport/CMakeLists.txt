add_library(ure_transport STATIC
    src/legacy_technique_preset.cpp
    src/semantics.cpp
    src/technique_graph.cpp
)

target_include_directories(ure_transport PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(ure_transport PUBLIC ure_types PRIVATE ure_runtime)

if(MSVC)
    target_compile_options(
        ure_transport PRIVATE /W4 /WX /utf-8 /EHsc /wd4819)
else()
    target_compile_options(
        ure_transport PRIVATE -Wall -Wextra -pedantic -Werror)
endif()

install(TARGETS ure_transport EXPORT UltraRender_Targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
