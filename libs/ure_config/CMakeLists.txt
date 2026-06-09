add_library(ure_config STATIC
    src/config_impl.cpp
)

target_include_directories(ure_config PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_include_directories(ure_config PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party
)

target_link_libraries(ure_config PUBLIC ure_types ure_diag)

if(MSVC)
    target_compile_options(ure_config PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:/W4;/utf-8;/EHsc;/wd4819>
    )
endif()

install(TARGETS ure_config EXPORT UltraRender_Targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
