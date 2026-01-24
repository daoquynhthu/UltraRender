cmake_minimum_required(VERSION 3.25)
project(UltraRenderEngine LANGUAGES CXX)

# --- 基础配置 ---
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

# --- CUDA 支持配置 ---
include(CheckLanguage)
check_language(CUDA)

if(CMAKE_CUDA_COMPILER)
    message(STATUS "CUDA Compiler found: ${CMAKE_CUDA_COMPILER}")
    enable_language(CUDA)
    
    # 设置 CUDA 架构 (Native 表示针对当前机器编译，性能最佳)
    if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
        set(CMAKE_CUDA_ARCHITECTURES "native")
    endif()

    # 设置 CUDA 标准
    set(CMAKE_CUDA_STANDARD 20) # CUDA 13 支持 C++20

    set(ENGINE_SOURCES_CUDA
        "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/gpu_driver.cu"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/path_tracer_kernel.cu"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/path_tracer_material.cu"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/path_tracer_raygen.cu"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/path_tracer_denoise.cu"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/path_tracer_post.cu"
    )
    
    message(STATUS "Building with CUDA support.")
    add_definitions(-DUSE_CUDA)
    
    set(FINAL_SOURCES 
        ${ENGINE_SOURCES_CPP}
        ${ENGINE_SOURCES_CUDA}
    )
else()
    message(STATUS "CUDA Compiler NOT found. Building in CPU-only mode.")
    # 如果找不到 CUDA，使用桩代码
    set(FINAL_SOURCES 
        ${ENGINE_SOURCES_CPP}
        "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/gpu_driver_stub.cpp"
    )
endif()

# 核心头文件
file(GLOB_RECURSE ENGINE_HEADERS
    include/*.hpp
    include/*.h
)

# 针对 C++ 源文件设置编译选项 (避免传递给 NVCC 导致错误)
foreach(source_file ${FINAL_SOURCES})
    if(source_file MATCHES "\\.cpp$")
        if(MSVC)
            # 添加 /wd4819 以抑制编码警告（虽然我们已经用了 /utf-8，但有时第三方头文件仍会触发）
            set_source_files_properties(${source_file} PROPERTIES COMPILE_OPTIONS "/W4;/permissive-;/utf-8;/EHsc;/wd4819")
        else()
            set_source_files_properties(${source_file} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-pedantic")
        endif()
    endif()
endforeach()

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

message(STATUS "Ultra Render Engine Project Configured.")
message(STATUS "Sources: ${FINAL_SOURCES}")
