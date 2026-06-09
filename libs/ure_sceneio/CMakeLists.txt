add_library(ure_sceneio STATIC
    src/scene_io_api.cpp
    src/gltf_scene_frontend.cpp
    src/scene_ir_frontend.cpp
    src/scene_frontend.cpp
    src/scene_parser.cpp
    src/procedural.cpp
    src/image_loader.cpp
    src/image_saver.cpp
    src/spd_loader.cpp
    src/wav_saver.cpp
    src/scene_ir.cpp
    src/scene.cpp
    src/scene_factory.cpp
    src/camera.cpp
    src/mesh.cpp
    src/sphere.cpp
    src/triangle.cpp
    src/obj_loader.cpp
)

target_include_directories(ure_sceneio PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(ure_sceneio PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party
)
target_link_libraries(ure_sceneio PUBLIC ure_types ure_core ure_diag)

if(MSVC)
    target_compile_options(ure_sceneio PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:/W4;/utf-8;/EHsc;/wd4819>
    )
endif()

install(TARGETS ure_sceneio EXPORT UltraRender_Targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
