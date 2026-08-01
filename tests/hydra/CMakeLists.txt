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
if(UR_ENABLE_CUDA)
    target_link_libraries(test_hydra_render_delegate
        PRIVATE ure_core)
    target_compile_definitions(test_hydra_render_delegate
        PRIVATE UR_HYDRA_RENDERING=1)
endif()
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

add_executable(test_hydra_mesh_rprim
    test_hydra_mesh_rprim.cpp
    $<TARGET_OBJECTS:ure_hydra_delegate>
)

add_executable(test_hydra_material_sprim
    test_hydra_material_sprim.cpp
    $<TARGET_OBJECTS:ure_hydra_delegate>
)
target_include_directories(test_hydra_material_sprim
    PRIVATE
        "${CMAKE_SOURCE_DIR}/libs/ure_hydra/src"
)
target_link_libraries(test_hydra_material_sprim
    PRIVATE ure_sceneio ur_openusd_hydra_sdk
)
if(UR_ENABLE_CUDA)
    target_link_libraries(test_hydra_material_sprim
        PRIVATE ure_core)
endif()
target_compile_definitions(test_hydra_material_sprim
    PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN
)
target_include_directories(test_hydra_mesh_rprim
    PRIVATE
        "${CMAKE_SOURCE_DIR}/libs/ure_hydra/src"
)
target_link_libraries(test_hydra_mesh_rprim
    PRIVATE ure_sceneio ur_openusd_hydra_sdk
)
if(UR_ENABLE_CUDA)
    target_link_libraries(test_hydra_mesh_rprim
        PRIVATE ure_core)
endif()
target_compile_definitions(test_hydra_mesh_rprim
    PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN
)
target_link_libraries(test_hydra_plugin_discovery
    PRIVATE ur_openusd_hydra_sdk
)
target_compile_definitions(test_hydra_plugin_discovery
    PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN
)
if(UR_ENABLE_CUDA)
    target_compile_definitions(test_hydra_plugin_discovery
        PRIVATE UR_HYDRA_RENDERING=1)
endif()
if(MSVC)
    target_compile_options(test_hydra_plugin_discovery
        PRIVATE /W4 /WX /utf-8 /EHsc /wd4819)
    target_compile_options(test_hydra_mesh_rprim
        PRIVATE /W4 /WX /utf-8 /EHsc /wd4819)
    target_compile_options(test_hydra_material_sprim
        PRIVATE /W4 /WX /utf-8 /EHsc /wd4819)
else()
    target_compile_options(test_hydra_plugin_discovery
        PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(test_hydra_mesh_rprim
        PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(test_hydra_material_sprim
        PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(
    NAME test_hydra_plugin_discovery
    COMMAND test_hydra_plugin_discovery
)
add_test(
    NAME test_hydra_mesh_rprim
    COMMAND test_hydra_mesh_rprim
)
add_test(
    NAME test_hydra_material_sprim
    COMMAND test_hydra_material_sprim
)

add_executable(test_hydra_render_buffer
    test_hydra_render_buffer.cpp
    $<TARGET_OBJECTS:ure_hydra_delegate>
)
target_include_directories(test_hydra_render_buffer
    PRIVATE
        "${CMAKE_SOURCE_DIR}/libs/ure_hydra/src"
)
target_link_libraries(test_hydra_render_buffer
    PRIVATE ure_sceneio ur_openusd_hydra_sdk
)
if(UR_ENABLE_CUDA)
    target_link_libraries(test_hydra_render_buffer
        PRIVATE ure_core)
endif()
target_compile_definitions(test_hydra_render_buffer
    PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN
)
if(MSVC)
    target_compile_options(test_hydra_render_buffer
        PRIVATE /W4 /WX /utf-8 /EHsc /wd4819)
else()
    target_compile_options(test_hydra_render_buffer
        PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(
    NAME test_hydra_render_buffer
    COMMAND test_hydra_render_buffer
)

add_executable(test_usda_export
    test_usda_export.cpp
)
target_link_libraries(test_usda_export
    PRIVATE ure_sceneio ur_openusd_hydra_sdk
)
target_compile_definitions(test_usda_export
    PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN
)
if(MSVC)
    target_compile_options(test_usda_export
        PRIVATE /W4 /WX /utf-8 /EHsc /wd4819)
else()
    target_compile_options(test_usda_export
        PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(
    NAME test_usda_export
    COMMAND test_usda_export
)

if(UR_ENABLE_CUDA)
    add_executable(test_hydra_progressive_render
        test_hydra_progressive_render.cpp
        $<TARGET_OBJECTS:ure_hydra_delegate>
    )
    target_include_directories(test_hydra_progressive_render
        PRIVATE
            "${CMAKE_SOURCE_DIR}/libs/ure_hydra/src"
    )
    target_link_libraries(test_hydra_progressive_render
        PRIVATE ure_core ure_sceneio ur_openusd_hydra_sdk
    )
    target_compile_definitions(test_hydra_progressive_render
        PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UR_HYDRA_RENDERING=1
    )
    if(MSVC)
        target_compile_options(test_hydra_progressive_render
            PRIVATE /W4 /WX /utf-8 /EHsc /wd4819)
    else()
        target_compile_options(test_hydra_progressive_render
            PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(
        NAME test_hydra_progressive_render
        COMMAND test_hydra_progressive_render
    )
endif()

set_tests_properties(
    test_hydra_render_delegate
    test_hydra_plugin_discovery
    test_hydra_mesh_rprim
    test_hydra_material_sprim
    test_hydra_render_buffer
    test_usda_export
    PROPERTIES
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${UR_OPENUSD_ROOT}/bin;PXR_PLUGINPATH_NAME=set:$<TARGET_FILE_DIR:ure_hydra>/resources"
)
if(UR_ENABLE_CUDA)
    set_tests_properties(
        test_hydra_progressive_render
        PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${UR_OPENUSD_ROOT}/bin;PXR_PLUGINPATH_NAME=set:$<TARGET_FILE_DIR:ure_hydra>/resources"
    )
endif()
