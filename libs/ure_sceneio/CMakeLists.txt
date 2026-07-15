add_library(ure_sceneio STATIC
    src/scene_io_api.cpp
    src/gltf_scene_frontend.cpp
    src/scene_frontend.cpp
    src/image_loader.cpp
    src/image_saver.cpp
    src/spd_loader.cpp
    src/materialx_io.cpp
    src/mie_phase_io.cpp
    src/native_scene_hash.cpp
    src/native_scene_container.cpp
    src/native_scene_metadata.cpp
    src/native_scene_text.cpp
    src/native_scene_validation.cpp
    src/native_scene_ir_model.cpp
    src/native_scene_ir_resources.cpp
    src/native_scene_ir_graph.cpp
    src/native_scene_ir_io.cpp
    src/native_scene_ir_text.cpp
    src/native_procedural_model.cpp
    src/native_procedural_build.cpp
    src/native_procedural_scatter.cpp
    src/native_procedural_spectrum.cpp
    src/native_procedural_light.cpp
    src/native_procedural_schema.cpp
    src/native_script_build.cpp
    src/native_resource_catalog.cpp
    src/native_solver_contract.cpp
    src/wav_saver.cpp
    src/scene_ir.cpp
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
    ${CMAKE_SOURCE_DIR}/third_party/flatbuffers/include
    ${CMAKE_CURRENT_SOURCE_DIR}/generated
)
target_link_libraries(ure_sceneio PUBLIC ure_types ure_diag)

if(MSVC)
    target_compile_options(ure_sceneio PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:/W4;/utf-8;/EHsc;/wd4819>
    )
endif()

install(TARGETS ure_sceneio EXPORT UltraRender_Targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
