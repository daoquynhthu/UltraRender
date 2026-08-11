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
    "${URE_ROOT}/libs/ure_transport/src/automatic_integrator.cpp"
    "${URE_ROOT}/libs/ure_transport/src/composition.cpp"
    "${URE_ROOT}/libs/ure_transport/src/legacy_technique_preset.cpp"
    "${URE_ROOT}/libs/ure_transport/src/pilot_statistics.cpp"
    "${URE_ROOT}/libs/ure_transport/src/portfolio_drift.cpp"
    "${URE_ROOT}/libs/ure_transport/src/portfolio_schedule.cpp"
    "${URE_ROOT}/libs/ure_transport/src/portfolio_shard.cpp"
    "${URE_ROOT}/libs/ure_transport/src/qualification.cpp"
    "${URE_ROOT}/libs/ure_transport/src/semantics.cpp"
    "${URE_ROOT}/libs/ure_transport/src/support_measure_graph.cpp"
    "${URE_ROOT}/libs/ure_transport/src/technique_graph.cpp"
)
target_include_directories(ure_transport_sdk_free PUBLIC
    "${URE_ROOT}/libs/ure_types/include"
    "${URE_ROOT}/libs/ure_transport/include"
)
target_link_libraries(ure_transport_sdk_free PRIVATE
    ure_runtime_sdk_free
)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND
   CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
    set_source_files_properties(
        "${URE_ROOT}/libs/ure_transport/src/support_measure_graph.cpp"
        "${URE_ROOT}/libs/ure_transport/src/technique_graph.cpp"
        PROPERTIES COMPILE_OPTIONS -Wno-stringop-overflow)
endif()

add_library(ure_research_sdk_free STATIC
    "${URE_ROOT}/libs/ure_research/src/artifact.cpp"
    "${URE_ROOT}/libs/ure_research/src/capability.cpp"
    "${URE_ROOT}/libs/ure_research/src/execution.cpp"
    "${URE_ROOT}/libs/ure_research/src/experiment.cpp"
    "${URE_ROOT}/libs/ure_research/src/promotion.cpp"
    "${URE_ROOT}/libs/ure_research/src/reference.cpp"
    "${URE_ROOT}/libs/ure_research/src/transport.cpp"
)
target_include_directories(ure_research_sdk_free PUBLIC
    "${URE_ROOT}/libs/ure_types/include"
    "${URE_ROOT}/libs/ure_runtime/include"
    "${URE_ROOT}/libs/ure_transport/include"
    "${URE_ROOT}/libs/ure_research/include"
)
target_link_libraries(ure_research_sdk_free PUBLIC
    ure_runtime_sdk_free
    ure_transport_sdk_free
)

add_library(ure_reconstruction_sdk_free STATIC
    "${URE_ROOT}/libs/ure_reconstruction/src/checkpoint.cpp"
    "${URE_ROOT}/libs/ure_reconstruction/src/measurement.cpp"
    "${URE_ROOT}/libs/ure_reconstruction/src/portfolio_measurement.cpp"
    "${URE_ROOT}/libs/ure_reconstruction/src/sample_reconstruction.cpp"
    "${URE_ROOT}/libs/ure_reconstruction/src/statistical_reconstruction.cpp"
)
target_include_directories(ure_reconstruction_sdk_free PUBLIC
    "${URE_ROOT}/libs/ure_types/include"
    "${URE_ROOT}/libs/ure_runtime/include"
    "${URE_ROOT}/libs/ure_transport/include"
    "${URE_ROOT}/libs/ure_research/include"
    "${URE_ROOT}/libs/ure_reconstruction/include"
)
target_link_libraries(ure_reconstruction_sdk_free PUBLIC
    ure_runtime_sdk_free
    ure_transport_sdk_free
    ure_research_sdk_free
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

add_executable(test_technique_graph_sdk_free
    "${URE_ROOT}/tests/host/test_technique_graph.cpp"
)
target_link_libraries(test_technique_graph_sdk_free PRIVATE
    ure_transport_sdk_free
    ure_runtime_sdk_free
)
add_executable(test_support_measure_graph_sdk_free
    "${URE_ROOT}/tests/host/test_support_measure_graph.cpp"
)
add_executable(test_pilot_qualification_sdk_free
    "${URE_ROOT}/tests/host/test_pilot_qualification.cpp"
)
add_executable(test_portfolio_scheduler_sdk_free
    "${URE_ROOT}/tests/host/test_portfolio_scheduler.cpp"
)
add_executable(test_automatic_integrator_sdk_free
    "${URE_ROOT}/tests/host/test_automatic_integrator.cpp"
)
target_link_libraries(test_automatic_integrator_sdk_free PRIVATE
    ure_transport_sdk_free
    ure_runtime_sdk_free
)
target_link_libraries(test_portfolio_scheduler_sdk_free PRIVATE
    ure_reconstruction_sdk_free
)
target_link_libraries(test_pilot_qualification_sdk_free PRIVATE
    ure_transport_sdk_free
    ure_runtime_sdk_free
)
target_link_libraries(test_support_measure_graph_sdk_free PRIVATE
    ure_transport_sdk_free
    ure_runtime_sdk_free
)
target_include_directories(test_high_order_semantics_sdk_free PRIVATE
    "${URE_ROOT}/libs/ure_runtime/include"
)
target_link_libraries(test_high_order_semantics_sdk_free PRIVATE
    ure_transport_sdk_free
)

add_executable(test_research_substrate_sdk_free
    "${URE_ROOT}/tests/host/test_research_substrate.cpp"
)
target_link_libraries(test_research_substrate_sdk_free PRIVATE
    ure_research_sdk_free
)
add_executable(test_transport_research_sdk_free
    "${URE_ROOT}/tests/host/test_transport_research.cpp"
)
target_link_libraries(test_transport_research_sdk_free PRIVATE
    ure_research_sdk_free
)
add_executable(test_measurement_bundle_sdk_free
    "${URE_ROOT}/tests/host/test_measurement_bundle.cpp"
)
target_link_libraries(test_measurement_bundle_sdk_free PRIVATE
    ure_reconstruction_sdk_free
)
add_executable(test_statistical_reconstruction_sdk_free
    "${URE_ROOT}/tests/host/test_statistical_reconstruction.cpp"
)
target_link_libraries(test_statistical_reconstruction_sdk_free PRIVATE
    ure_reconstruction_sdk_free
)
add_executable(test_sample_reconstruction_sdk_free
    "${URE_ROOT}/tests/host/test_sample_reconstruction.cpp"
)
target_link_libraries(test_sample_reconstruction_sdk_free PRIVATE
    ure_reconstruction_sdk_free
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
    target_compile_options(test_technique_graph_sdk_free PRIVATE
        /W4 /WX /utf-8 /EHsc /wd4819
    )
    target_compile_options(test_support_measure_graph_sdk_free PRIVATE
        /W4 /WX /utf-8 /EHsc /wd4819
    )
    target_compile_options(test_pilot_qualification_sdk_free PRIVATE
        /W4 /WX /utf-8 /EHsc /wd4819
    )
    target_compile_options(test_portfolio_scheduler_sdk_free PRIVATE
        /W4 /WX /utf-8 /EHsc /wd4819
    )
    target_compile_options(test_automatic_integrator_sdk_free PRIVATE
        /W4 /WX /utf-8 /EHsc /wd4819
    )
    target_compile_options(ure_research_sdk_free PRIVATE
        /W4 /WX /utf-8 /EHsc /wd4819
    )
    target_compile_options(test_research_substrate_sdk_free PRIVATE
        /W4 /WX /utf-8 /EHsc /wd4819
    )
    target_compile_options(test_transport_research_sdk_free PRIVATE
        /W4 /WX /utf-8 /EHsc /wd4819
    )
    target_compile_options(ure_reconstruction_sdk_free PRIVATE
        /W4 /WX /utf-8 /EHsc /wd4819
    )
    target_compile_options(test_measurement_bundle_sdk_free PRIVATE
        /W4 /WX /utf-8 /EHsc /wd4819
    )
    target_compile_options(test_statistical_reconstruction_sdk_free PRIVATE
        /W4 /WX /utf-8 /EHsc /wd4819
    )
    target_compile_options(test_sample_reconstruction_sdk_free PRIVATE
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
    target_compile_options(test_technique_graph_sdk_free PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
    target_compile_options(test_support_measure_graph_sdk_free PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
    target_compile_options(test_pilot_qualification_sdk_free PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
    target_compile_options(test_portfolio_scheduler_sdk_free PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
    target_compile_options(test_automatic_integrator_sdk_free PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
    target_compile_options(ure_research_sdk_free PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
    target_compile_options(test_research_substrate_sdk_free PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
    target_compile_options(test_transport_research_sdk_free PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
    target_compile_options(ure_reconstruction_sdk_free PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
    target_compile_options(test_measurement_bundle_sdk_free PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
    target_compile_options(test_statistical_reconstruction_sdk_free PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
    target_compile_options(test_sample_reconstruction_sdk_free PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
endif()

enable_testing()
add_test(NAME resource_plan_sdk_free COMMAND test_resource_plan_sdk_free)
add_test(NAME execution_graph_sdk_free COMMAND test_execution_graph_sdk_free)
add_test(NAME multi_backend_sdk_free COMMAND test_multi_backend_sdk_free)
add_test(NAME public_surface_sdk_free COMMAND test_public_surface_sdk_free)
add_test(NAME high_order_semantics_sdk_free COMMAND test_high_order_semantics_sdk_free)
add_test(NAME technique_graph_sdk_free COMMAND test_technique_graph_sdk_free)
add_test(NAME support_measure_graph_sdk_free COMMAND test_support_measure_graph_sdk_free)
add_test(NAME pilot_qualification_sdk_free COMMAND test_pilot_qualification_sdk_free)
add_test(NAME portfolio_scheduler_sdk_free COMMAND test_portfolio_scheduler_sdk_free)
add_test(NAME automatic_integrator_sdk_free COMMAND test_automatic_integrator_sdk_free)
add_test(NAME research_substrate_sdk_free COMMAND test_research_substrate_sdk_free)
add_test(NAME transport_research_sdk_free COMMAND test_transport_research_sdk_free)
add_test(NAME measurement_bundle_sdk_free COMMAND test_measurement_bundle_sdk_free)
add_test(NAME statistical_reconstruction_sdk_free COMMAND test_statistical_reconstruction_sdk_free)
add_test(NAME sample_reconstruction_sdk_free COMMAND test_sample_reconstruction_sdk_free)
