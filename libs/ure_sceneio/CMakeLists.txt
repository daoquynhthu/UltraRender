add_library(ure_sceneio STATIC
    src/gltf_scene_frontend.cpp
    src/scene_ir_frontend.cpp
    src/scene_frontend.cpp
    src/scene_parser.cpp
    src/procedural.cpp
    src/image_loader.cpp
    src/image_saver.cpp
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

target_include_directories(ure_sceneio PUBLIC include)
target_link_libraries(ure_sceneio PUBLIC ure_types ure_core)

if(MSVC)
    target_compile_options(ure_sceneio PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:/W4;/utf-8;/EHsc;/wd4819>
    )
endif()
