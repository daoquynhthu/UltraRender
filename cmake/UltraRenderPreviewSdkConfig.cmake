get_filename_component(_UltraRender_prefix "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

if(NOT TARGET UltraRender::CoreHeaders)
    add_library(UltraRender::CoreHeaders INTERFACE IMPORTED)
    set_target_properties(UltraRender::CoreHeaders PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${_UltraRender_prefix}/include")
endif()

if(NOT TARGET UltraRender::Client)
    add_library(UltraRender::Client STATIC IMPORTED)
    set_target_properties(UltraRender::Client PROPERTIES
        IMPORTED_LOCATION "${_UltraRender_prefix}/lib/ure_client.lib"
        INTERFACE_COMPILE_FEATURES cxx_std_23
        INTERFACE_INCLUDE_DIRECTORIES "${_UltraRender_prefix}/include"
        INTERFACE_LINK_LIBRARIES "UltraRender::CoreHeaders;bcrypt")
endif()

set(UltraRender_CoreHeaders_FOUND TRUE)
set(UltraRender_Client_FOUND TRUE)
set(UltraRender_CORE_ABI_VERSION "1.0")
set(UltraRender_WORKER_PROTOCOL_VERSION "1.0")
set(UltraRender_PREVIEW_CLIENT_VERSION "0.4")
set(UltraRender_PREVIEW_CLIENT_COMPATIBILITY "ExactBuild")

foreach(_UltraRender_component IN LISTS UltraRender_FIND_COMPONENTS)
    if(NOT UltraRender_${_UltraRender_component}_FOUND)
        set(UltraRender_FOUND FALSE)
        set(UltraRender_NOT_FOUND_MESSAGE
            "Unsupported UltraRender SDK component: ${_UltraRender_component}")
    endif()
endforeach()

unset(_UltraRender_prefix)
