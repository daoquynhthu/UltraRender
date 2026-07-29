if(NOT UR_OPENUSD_ROOT)
    message(FATAL_ERROR
        "UR_ENABLE_HYDRA requires an explicit UR_OPENUSD_ROOT")
endif()

set(_ur_houdini_config
    "${UR_OPENUSD_ROOT}/toolkit/cmake/HoudiniConfig.cmake")
if(NOT EXISTS "${_ur_houdini_config}")
    message(FATAL_ERROR
        "U.2 currently requires an OpenUSD SDK exposing the Houdini HDK CMake targets")
endif()

set(Houdini_DIR
    "${UR_OPENUSD_ROOT}/toolkit/cmake")
find_package(Houdini CONFIG REQUIRED)

set(_ur_openusd_include
    "${UR_OPENUSD_ROOT}/toolkit/include")
if(NOT EXISTS
    "${_ur_openusd_include}/pxr/imaging/hd/renderDelegate.h")
    message(FATAL_ERROR
        "UR_OPENUSD_ROOT does not contain Hydra headers")
endif()

set(_ur_openusd_targets
    Houdini::Dep::pxr_arch
    Houdini::Dep::pxr_gf
    Houdini::Dep::pxr_hd
    Houdini::Dep::pxr_hf
    Houdini::Dep::pxr_plug
    Houdini::Dep::pxr_python
    Houdini::Dep::pxr_sdf
    Houdini::Dep::pxr_tf
    Houdini::Dep::pxr_trace
    Houdini::Dep::pxr_vt
    Houdini::Dep::pxr_work
    Houdini::Dep::python3.11
    Houdini::Dep::tbb
)
foreach(_target IN LISTS _ur_openusd_targets)
    if(NOT TARGET ${_target})
        message(FATAL_ERROR
            "OpenUSD provider is missing target ${_target}")
    endif()
endforeach()

add_library(ur_openusd_hydra_sdk INTERFACE)
get_target_property(
    _ur_houdini_includes
    Houdini
    INTERFACE_INCLUDE_DIRECTORIES
)
target_include_directories(ur_openusd_hydra_sdk
    SYSTEM INTERFACE
        ${_ur_houdini_includes}
)
target_link_libraries(ur_openusd_hydra_sdk
    INTERFACE ${_ur_openusd_targets}
)
target_compile_definitions(ur_openusd_hydra_sdk
    INTERFACE
        _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING
)

add_library(ure_hydra_delegate OBJECT
    src/render_delegate.cpp
)
target_include_directories(ure_hydra_delegate
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_include_directories(ure_hydra_delegate
    SYSTEM PRIVATE
        "${_ur_openusd_include}"
)
target_link_libraries(ure_hydra_delegate
    PRIVATE ure_sceneio ur_openusd_hydra_sdk
)
target_compile_definitions(ure_hydra_delegate
    PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN
)

add_library(ure_hydra MODULE
    src/renderer_plugin.cpp
    $<TARGET_OBJECTS:ure_hydra_delegate>
)
target_include_directories(ure_hydra
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_include_directories(ure_hydra
    SYSTEM PRIVATE
        "${_ur_openusd_include}"
)
target_link_libraries(ure_hydra
    PRIVATE ure_sceneio ur_openusd_hydra_sdk
)
target_compile_definitions(ure_hydra
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        MAKING_DSO
        MFB_ALT_PACKAGE_NAME=ure_hydra
)
set_target_properties(ure_hydra PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY
        "${CMAKE_BINARY_DIR}/hydra/ure_hydra"
    LIBRARY_OUTPUT_DIRECTORY
        "${CMAKE_BINARY_DIR}/hydra/ure_hydra"
)
foreach(_config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${_config}" _config_upper)
    set_target_properties(ure_hydra PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${_config_upper}
            "${CMAKE_BINARY_DIR}/hydra/ure_hydra/${_config}"
        LIBRARY_OUTPUT_DIRECTORY_${_config_upper}
            "${CMAKE_BINARY_DIR}/hydra/ure_hydra/${_config}"
    )
endforeach()

configure_file(
    resources/plugInfo.json.in
    "${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json"
    @ONLY
)
file(GENERATE
    OUTPUT
        "${CMAKE_BINARY_DIR}/hydra/ure_hydra/$<CONFIG>/resources/plugInfo.json"
    INPUT
        "${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json"
)

set(_ur_hydra_install_dir
    "${CMAKE_INSTALL_LIBDIR}/UltraRender/hydra/ure_hydra")
install(TARGETS ure_hydra
    RUNTIME DESTINATION "${_ur_hydra_install_dir}"
    LIBRARY DESTINATION "${_ur_hydra_install_dir}"
    COMPONENT Hydra
)
install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json"
    DESTINATION "${_ur_hydra_install_dir}/resources"
    COMPONENT Hydra
)

if(MSVC)
    target_compile_options(ure_hydra_delegate
        PRIVATE /W4 /WX /utf-8 /EHsc /wd4819)
    target_compile_options(ure_hydra
        PRIVATE /W4 /WX /utf-8 /EHsc /wd4819 /Zc:inline-)
else()
    target_compile_options(ure_hydra_delegate
        PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(ure_hydra
        PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
