cmake_minimum_required(VERSION 3.28)
project(UltraRenderPreviewClientExample LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The Preview exact-build client requires Windows x64 MSVC")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(UltraRender 0.3.0 EXACT CONFIG REQUIRED COMPONENTS Client)
if(NOT UltraRender_PREVIEW_CLIENT_COMPATIBILITY STREQUAL "ExactBuild")
    message(FATAL_ERROR "The Preview client package lost exact-build classification")
endif()

add_library(ure_preview_example_common STATIC render_example.cpp)
target_link_libraries(ure_preview_example_common PUBLIC UltraRender::Client)

add_executable(ure_preview_direct direct_main.cpp)
target_link_libraries(ure_preview_direct PRIVATE ure_preview_example_common)

add_executable(ure_preview_worker worker_main.cpp)
target_link_libraries(ure_preview_worker PRIVATE ure_preview_example_common)

foreach(target ure_preview_example_common ure_preview_direct ure_preview_worker)
    target_compile_options(${target} PRIVATE /W4 /WX /utf-8 /EHsc /wd4819)
endforeach()
