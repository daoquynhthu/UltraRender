add_library(ure_transport STATIC
    src/automatic_integrator.cpp
    src/composition.cpp
    src/legacy_technique_preset.cpp
    src/semantics.cpp
    src/pilot_statistics.cpp
    src/portfolio_drift.cpp
    src/portfolio_schedule.cpp
    src/portfolio_shard.cpp
    src/qualification.cpp
    src/support_measure_graph.cpp
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

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND
   CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
    set_source_files_properties(
        src/support_measure_graph.cpp
        src/technique_graph.cpp
        PROPERTIES COMPILE_OPTIONS -Wno-stringop-overflow)
endif()

install(TARGETS ure_transport EXPORT UltraRender_Targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
