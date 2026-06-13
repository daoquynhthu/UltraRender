enable_testing()

if(MSVC)
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/EHsc;/utf-8;/wd4819>")
endif()

add_executable(test_world test_world.cpp)
target_link_libraries(test_world PRIVATE ure_types)
add_test(NAME test_world COMMAND test_world)

add_executable(test_asset_pipeline test_asset_pipeline.cpp)
target_link_libraries(test_asset_pipeline PRIVATE ure_sceneio ure_diag)
add_test(NAME test_asset_pipeline COMMAND test_asset_pipeline)

add_executable(test_gltf_frontend test_gltf_frontend.cpp)
target_link_libraries(test_gltf_frontend PRIVATE ure_sceneio ure_core ure_diag)
add_test(NAME test_gltf_frontend COMMAND test_gltf_frontend)

add_executable(test_session test_session.cpp)
target_link_libraries(test_session PRIVATE ure_sceneio ure_core ure_diag)
add_test(NAME test_session COMMAND test_session)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME test_pyure_smoke
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_pyure_smoke.py
    )
    set_tests_properties(test_pyure_smoke PROPERTIES
        ENVIRONMENT "PYTHONPATH=${CMAKE_SOURCE_DIR};PYURE_NATIVE=$<TARGET_FILE:pyure_native>"
    )
endif()
