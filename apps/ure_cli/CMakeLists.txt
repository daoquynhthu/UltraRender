add_executable(ure_cli src/main.cpp)

target_link_libraries(ure_cli PRIVATE
    ure_core
    ure_sceneio
    ure_config
    ure_diag
)

if(TARGET ure_physics)
    target_link_libraries(ure_cli PRIVATE ure_physics)
    target_compile_definitions(ure_cli PRIVATE UR_HAS_PHYSICS)
endif()

if(MSVC)
    target_compile_options(ure_cli PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:/utf-8;/EHsc>
    )
    target_link_options(ure_cli PRIVATE /NODEFAULTLIB:LIBCMT)
endif()
