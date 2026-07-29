add_executable(test_hydra_render_delegate
    test_hydra_render_delegate.cpp
    $<TARGET_OBJECTS:ure_hydra_delegate>
)
target_include_directories(test_hydra_render_delegate
    PRIVATE
        "${CMAKE_SOURCE_DIR}/libs/ure_hydra/src"
)
target_link_libraries(test_hydra_render_delegate
    PRIVATE ure_sceneio ur_openusd_hydra_sdk
)
target_compile_definitions(test_hydra_render_delegate
    PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN
)
if(MSVC)
    target_compile_options(test_hydra_render_delegate
        PRIVATE /W4 /WX /utf-8 /EHsc /wd4819)
else()
    target_compile_options(test_hydra_render_delegate
        PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(
    NAME test_hydra_render_delegate
    COMMAND test_hydra_render_delegate
)

add_executable(test_hydra_plugin_discovery
    test_hydra_plugin_discovery.cpp
)
target_link_libraries(test_hydra_plugin_discovery
    PRIVATE ur_openusd_hydra_sdk
)
target_compile_definitions(test_hydra_plugin_discovery
    PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN
)
if(MSVC)
    target_compile_options(test_hydra_plugin_discovery
        PRIVATE /W4 /WX /utf-8 /EHsc /wd4819)
else()
    target_compile_options(test_hydra_plugin_discovery
        PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(
    NAME test_hydra_plugin_discovery
    COMMAND test_hydra_plugin_discovery
)

set_tests_properties(
    test_hydra_render_delegate
    test_hydra_plugin_discovery
    PROPERTIES
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${UR_OPENUSD_ROOT}/bin;PXR_PLUGINPATH_NAME=set:$<TARGET_FILE_DIR:ure_hydra>/resources"
)
