cmake_minimum_required(VERSION 3.28)

project(UltraRenderSdkFreeResourceContract LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(URE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

add_library(ure_runtime_sdk_free STATIC
    "${URE_ROOT}/libs/ure_runtime/src/acceleration.cpp"
    "${URE_ROOT}/libs/ure_runtime/src/execution_graph.cpp"
    "${URE_ROOT}/libs/ure_runtime/src/multi_backend.cpp"
    "${URE_ROOT}/libs/ure_runtime/src/resource_plan.cpp"
    "${URE_ROOT}/libs/ure_runtime/src/runtime_validation.cpp"
)
target_include_directories(ure_runtime_sdk_free PUBLIC
    "${URE_ROOT}/libs/ure_types/include"
    "${URE_ROOT}/libs/ure_runtime/include"
)

add_library(ure_transport_sdk_free STATIC
    "${URE_ROOT}/libs/ure_transport/src/semantics.cpp"
)
target_include_directories(ure_transport_sdk_free PUBLIC
    "${URE_ROOT}/libs/ure_types/include"
    "${URE_ROOT}/libs/ure_transport/include"
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

add_executable(test_execution_graph_sdk_free
    "${URE_ROOT}/tests/host/test_execution_graph.cpp"
)
target_link_libraries(test_execution_graph_sdk_free PRIVATE
    ure_runtime_sdk_free
)

add_executable(test_multi_backend_sdk_free
    "${URE_ROOT}/tests/host/test_multi_backend_schedule.cpp"
)
target_link_libraries(test_multi_backend_sdk_free PRIVATE
    ure_runtime_sdk_free
)

add_executable(test_public_surface_sdk_free
    "${URE_ROOT}/tests/host/test_public_surface_sdk_free.cpp"
)

add_executable(test_high_order_semantics_sdk_free
    "${URE_ROOT}/tests/host/test_high_order_semantics.cpp"
)
target_include_directories(test_high_order_semantics_sdk_free PRIVATE
    "${URE_ROOT}/libs/ure_runtime/include"
)
target_link_libraries(test_high_order_semantics_sdk_free PRIVATE
    ure_transport_sdk_free
)
target_include_directories(test_public_surface_sdk_free PRIVATE
    "${URE_ROOT}/libs/ure_core/include"
    "${URE_ROOT}/libs/ure_sceneio/include"
    "${URE_ROOT}/libs/ure_types/include"
    "${URE_ROOT}/libs/ure_runtime/include"
)

if(MSVC)
    target_compile_options(ure_runtime_sdk_free PRIVATE
        /W4 /WX /utf-8 /EHsc /wd4819
    )
    target_compile_options(test_resource_plan_sdk_free PRIVATE
        /W4 /WX /utf-8 /EHsc /wd4819
    )
    target_compile_options(test_execution_graph_sdk_free PRIVATE
        /W4 /WX /utf-8 /EHsc /wd4819
    )
    target_compile_options(test_multi_backend_sdk_free PRIVATE
        /W4 /WX /utf-8 /EHsc /wd4819
    )
    target_compile_options(test_public_surface_sdk_free PRIVATE
        /W4 /WX /utf-8 /EHsc /wd4819
    )
    target_compile_options(ure_transport_sdk_free PRIVATE
        /W4 /WX /utf-8 /EHsc /wd4819
    )
    target_compile_options(test_high_order_semantics_sdk_free PRIVATE
        /W4 /WX /utf-8 /EHsc /wd4819
    )
else()
    target_compile_options(ure_runtime_sdk_free PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
    target_compile_options(test_resource_plan_sdk_free PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
    target_compile_options(test_execution_graph_sdk_free PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
    target_compile_options(test_multi_backend_sdk_free PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
    target_compile_options(test_public_surface_sdk_free PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
    target_compile_options(ure_transport_sdk_free PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
    target_compile_options(test_high_order_semantics_sdk_free PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
endif()

enable_testing()
add_test(NAME resource_plan_sdk_free COMMAND test_resource_plan_sdk_free)
add_test(NAME execution_graph_sdk_free COMMAND test_execution_graph_sdk_free)
add_test(NAME multi_backend_sdk_free COMMAND test_multi_backend_sdk_free)
add_test(NAME public_surface_sdk_free COMMAND test_public_surface_sdk_free)
add_test(NAME high_order_semantics_sdk_free COMMAND test_high_order_semantics_sdk_free)
