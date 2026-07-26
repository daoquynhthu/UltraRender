cmake_minimum_required(VERSION 3.28)

project(UltraRenderSdkFreeResourceContract LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(URE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

add_library(ure_runtime_sdk_free STATIC
    "${URE_ROOT}/libs/ure_runtime/src/resource_plan.cpp"
    "${URE_ROOT}/libs/ure_runtime/src/runtime_validation.cpp"
)
target_include_directories(ure_runtime_sdk_free PUBLIC
    "${URE_ROOT}/libs/ure_types/include"
    "${URE_ROOT}/libs/ure_runtime/include"
)

add_executable(test_resource_plan_sdk_free
    "${URE_ROOT}/tests/host/test_resource_plan.cpp"
)
target_include_directories(test_resource_plan_sdk_free PRIVATE
    "${URE_ROOT}/libs/ure_core/include"
)
target_link_libraries(test_resource_plan_sdk_free PRIVATE
    ure_runtime_sdk_free
)

if(MSVC)
    target_compile_options(ure_runtime_sdk_free PRIVATE
        /W4 /WX /utf-8 /EHsc /wd4819
    )
    target_compile_options(test_resource_plan_sdk_free PRIVATE
        /W4 /WX /utf-8 /EHsc /wd4819
    )
else()
    target_compile_options(ure_runtime_sdk_free PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
    target_compile_options(test_resource_plan_sdk_free PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
endif()

enable_testing()
add_test(NAME resource_plan_sdk_free COMMAND test_resource_plan_sdk_free)
