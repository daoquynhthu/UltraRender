cmake_minimum_required(VERSION 3.28)
project(UltraRenderCore1ExternalClient LANGUAGES C CXX)

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The Core 1.0 external client requires Windows x64 MSVC")
endif()
foreach(variable URE_SDK_ROOT)
    if(NOT DEFINED ${variable})
        message(FATAL_ERROR "${variable} is required")
    endif()
endforeach()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(generated ${URE_SDK_ROOT}/include/ultrarender/protocol)

add_library(ure_external_image STATIC image_artifact.c)
target_include_directories(ure_external_image PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(ure_external_direct direct_client.c)
target_include_directories(ure_external_direct PRIVATE ${URE_SDK_ROOT}/include)
target_link_libraries(ure_external_direct PRIVATE ure_external_image)

add_executable(ure_external_transaction transaction_client.cpp)
target_include_directories(ure_external_transaction PRIVATE
    ${URE_SDK_ROOT}/include
    ${URE_SDK_ROOT}/third_party/flatbuffers/include
    ${generated})
target_link_libraries(ure_external_transaction PRIVATE bcrypt ure_external_image)

add_executable(ure_external_worker worker_client.cpp worker_e2e.cpp)
target_compile_definitions(ure_external_worker PRIVATE URE_EXTERNAL_CLIENT=1)
target_include_directories(ure_external_worker PRIVATE
    ${URE_SDK_ROOT}/include
    ${URE_SDK_ROOT}/third_party/flatbuffers/include
    ${generated})
target_link_libraries(ure_external_worker PRIVATE bcrypt ure_external_image)

foreach(target ure_external_image ure_external_direct ure_external_transaction ure_external_worker)
    target_compile_options(${target} PRIVATE /W4 /WX /utf-8 /wd4819 /wd4864)
endforeach()
target_compile_options(ure_external_direct PRIVATE /wd4191)
