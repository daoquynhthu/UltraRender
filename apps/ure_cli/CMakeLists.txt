add_executable(ure_cli src/main.cpp)
add_dependencies(ure_cli ure_native_tool)

target_link_libraries(ure_cli PRIVATE
    ure_client
    ure_config
)

if(MSVC)
    target_compile_options(ure_cli PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:/utf-8;/EHsc>
    )
    target_link_options(ure_cli PRIVATE /NODEFAULTLIB:LIBCMT)
endif()
