if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    return()
endif()

add_library(ure_client STATIC
    src/client.cpp
    src/direct_transport.cpp
    src/worker_transport.cpp)

target_include_directories(ure_client PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(ure_client
    PUBLIC ure_public
    PRIVATE $<BUILD_INTERFACE:ure_contract_schema> bcrypt)
target_compile_options(ure_client PRIVATE /W4 /WX /utf-8 /EHsc /wd4819)

install(TARGETS ure_client EXPORT UltraRender_Targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
