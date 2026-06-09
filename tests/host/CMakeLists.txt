enable_testing()

if(MSVC)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/EHsc;/utf-8;/wd4819>)
endif()
