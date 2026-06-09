add_library(ure_diag STATIC
    src/log.cpp
)

target_include_directories(ure_diag PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if(MSVC)
    target_compile_options(ure_diag PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:/W4;/utf-8;/EHsc;/wd4819>
    )
endif()

# Compile-time log level: Debug→Trace+, Release→Info+
target_compile_definitions(ure_diag PUBLIC
    $<$<CONFIG:Debug>:UR_LOG_LEVEL=0>
    $<$<CONFIG:Release>:UR_LOG_LEVEL=2>
)

install(TARGETS ure_diag EXPORT UltraRender_Targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
