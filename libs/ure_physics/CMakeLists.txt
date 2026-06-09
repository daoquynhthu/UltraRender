add_library(ure_physics STATIC
    src/physics_world.cpp
    src/fluid_system.cpp
    src/marching_cubes.cpp
    src/acoustic_system.cpp
    src/acoustic_ray_tracer.cpp
    src/spatial_processor.cpp
    src/modal_factory.cpp
)

target_include_directories(ure_physics PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(ure_physics PUBLIC ure_types ure_diag)

if(MSVC)
    target_compile_options(ure_physics PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:/W4;/utf-8;/EHsc;/wd4819>
    )
endif()

install(TARGETS ure_physics EXPORT UltraRender_Targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
