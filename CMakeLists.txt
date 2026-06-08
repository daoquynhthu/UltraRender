cmake_minimum_required(VERSION 3.25)
project(UltraRenderEngine LANGUAGES CXX CUDA)

# --- 基础配置 ---
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CUDA_STANDARD 20)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

# CI does not have a physical GPU, so avoid the "native" probe.
# The build targets a broad set of common architectures instead.
if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
    set(CMAKE_CUDA_ARCHITECTURES "all-major")
endif()

# 编译选项
# (Moved to set_source_files_properties below)

# 包含路径
include_directories(include)

# --- 源文件收集 ---
# 递归寻找 C++ 源文件 (排除 src/gpu，稍后手动添加)
file(GLOB_RECURSE ENGINE_SOURCES_CPP 
    src/*.cpp
)
# Ensure new files are included (triggers re-configure if modified)
list(APPEND ENGINE_SOURCES_CPP "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/bvh_builder.cpp")
list(APPEND ENGINE_SOURCES_CPP "${CMAKE_CURRENT_SOURCE_DIR}/src/api/gpu_engine_impl.cpp")
list(REMOVE_DUPLICATES ENGINE_SOURCES_CPP)

# 移除 gpu_driver_stub.cpp，默认假设我们可能要用 CUDA
list(REMOVE_ITEM ENGINE_SOURCES_CPP "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/gpu_driver_stub.cpp")

message(STATUS "CUDA Compiler found: ${CMAKE_CUDA_COMPILER}")

set(ENGINE_SOURCES_CUDA
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/gpu_driver.cu"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/path_tracer_kernel.cu"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/path_tracer_raygen.cu"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/path_tracer_denoise.cu"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/path_tracer_post.cu"
)

message(STATUS "Building with CUDA support.")

# Ensure C++ compiler can find CUDA headers (e.g. cuda_runtime.h)
include_directories(${CMAKE_CUDA_TOOLKIT_INCLUDE_DIRECTORIES})

set(FINAL_SOURCES
    ${ENGINE_SOURCES_CPP}
    ${ENGINE_SOURCES_CUDA}
)

# 核心头文件
file(GLOB_RECURSE ENGINE_HEADERS
    include/*.hpp
    include/*.h
)

# --- OpenMP 支持 ---
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    message(STATUS "OpenMP found. Enabling multi-threading.")
    link_libraries(OpenMP::OpenMP_CXX)
else()
    message(WARNING "OpenMP not found. Simulation will be single-threaded (SLOW).")
endif()

# 针对 C++ 源文件设置编译选项 (避免传递给 NVCC 导致错误)
foreach(source_file ${FINAL_SOURCES})
    if(source_file MATCHES "\\.cpp$")
        if(MSVC)
            # 添加 /wd4819 以抑制编码警告（虽然我们已经用了 /utf-8，但有时第三方头文件仍会触发）
            # 添加 /openmp (Explicitly add /openmp for MSVC if found)
            if(OpenMP_CXX_FOUND)
                 set_source_files_properties(${source_file} PROPERTIES COMPILE_OPTIONS "/W4;/utf-8;/EHsc;/wd4819;/openmp")
            else()
                 set_source_files_properties(${source_file} PROPERTIES COMPILE_OPTIONS "/W4;/utf-8;/EHsc;/wd4819")
            endif()
        else()
            set_source_files_properties(${source_file} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-pedantic")
        endif()
    endif()
endforeach()
 # Touch to force re-configure

# --- 目标定义 ---
add_executable(UltraRender
    ${FINAL_SOURCES}
    ${ENGINE_HEADERS}
)

# 核心头文件设置输出目录
set_target_properties(UltraRender PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

if(MSVC)
    target_link_options(UltraRender PRIVATE "/NODEFAULTLIB:LIBCMT")
endif()

if(CMAKE_CUDA_COMPILER)
    target_link_libraries(UltraRender PRIVATE ${CUDA_LIBRARIES})
    target_compile_definitions(UltraRender PRIVATE USE_CUDA)
endif()

# --- Testing Configuration ---
# enable_testing()

# include(FetchContent)
# FetchContent_Declare(
#   googletest
#   URL https://github.com/google/googletest/archive/refs/heads/main.zip
# )
# # For Windows: Prevent overriding the parent project's compiler/linker settings
# set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
# FetchContent_MakeAvailable(googletest)

# add_subdirectory(tests)

message(STATUS "Ultra Render Engine Project Configured.")
message(STATUS "Sources: ${FINAL_SOURCES}")
