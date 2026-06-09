enable_testing()

if(MSVC)
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/EHsc;/utf-8;/wd4819>")
endif()

add_executable(test_world test_world.cpp)
target_link_libraries(test_world PRIVATE ure_types)
add_test(NAME test_world COMMAND test_world)
