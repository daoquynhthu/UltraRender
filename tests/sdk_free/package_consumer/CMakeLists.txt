cmake_minimum_required(VERSION 3.25)
project(UltraRenderSdkFreeConsumer LANGUAGES CXX)

find_package(
    UltraRender REQUIRED
    COMPONENTS ure_types ure_runtime ure_transport ure_research ure_reconstruction ure_sceneio ure_config)

add_executable(ultrarender_sdk_free_consumer main.cpp)
target_compile_features(
    ultrarender_sdk_free_consumer PRIVATE cxx_std_23)
target_link_libraries(
    ultrarender_sdk_free_consumer PRIVATE
    UltraRender::ure_types
    UltraRender::ure_runtime
    UltraRender::ure_transport
    UltraRender::ure_research
    UltraRender::ure_reconstruction
    UltraRender::ure_sceneio
    UltraRender::ure_config)

if(MSVC)
    target_compile_options(
        ultrarender_sdk_free_consumer PRIVATE
        /W4 /WX /utf-8 /EHsc)
endif()
