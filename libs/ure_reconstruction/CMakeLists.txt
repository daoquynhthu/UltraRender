add_library(ure_reconstruction STATIC
    src/checkpoint.cpp
    src/measurement.cpp
    src/portfolio_measurement.cpp
)

target_include_directories(ure_reconstruction PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(ure_reconstruction PUBLIC
    ure_transport
    ure_runtime
    ure_research
)

if(MSVC)
    target_compile_options(
        ure_reconstruction PRIVATE /W4 /WX /utf-8 /EHsc /wd4819)
else()
    target_compile_options(
        ure_reconstruction PRIVATE -Wall -Wextra -pedantic -Werror)
endif()

install(TARGETS ure_reconstruction EXPORT UltraRender_Targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
