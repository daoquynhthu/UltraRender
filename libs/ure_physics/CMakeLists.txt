add_library(ure_physics STATIC
    src/physics_world.cpp
    src/fluid_system.cpp
    src/marching_cubes.cpp
    src/acoustic_system.cpp
    src/acoustic_ray_tracer.cpp
    src/spatial_processor.cpp
    src/modal_factory.cpp
)

target_include_directories(ure_physics PUBLIC include)
target_link_libraries(ure_physics PUBLIC ure_types)

if(MSVC)
    target_compile_options(ure_physics PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:/W4;/utf-8;/EHsc;/wd4819>
    )
endif()
