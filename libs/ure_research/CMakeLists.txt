add_library(ure_research STATIC
    src/artifact.cpp
    src/capability.cpp
    src/execution.cpp
    src/experiment.cpp
    src/promotion.cpp
    src/reference.cpp
    src/transport.cpp
)

target_include_directories(ure_research PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(ure_research PUBLIC
    ure_runtime
    ure_transport
)

if(MSVC)
    target_compile_options(
        ure_research PRIVATE /W4 /WX /utf-8 /EHsc /wd4819)
else()
    target_compile_options(
        ure_research PRIVATE -Wall -Wextra -pedantic -Werror)
endif()

install(TARGETS ure_research EXPORT UltraRender_Targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
