project(UltraRenderTests)

# Locate sources to test
# Ideally, we should build the engine as a library, but for now we can include source files directly.
# Exclude main.cpp to avoid multiple entry points.

file(GLOB_RECURSE TEST_SOURCES_CPP
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
)
list(FILTER TEST_SOURCES_CPP EXCLUDE REGEX ".*main\\.cpp$")
# Exclude GPU and API files that depend on CUDA
list(FILTER TEST_SOURCES_CPP EXCLUDE REGEX ".*/src/gpu/.*")
list(FILTER TEST_SOURCES_CPP EXCLUDE REGEX ".*/src/api/.*")

# Unit Tests
add_executable(unit_tests
    unit/test_vector.cpp
    unit/test_matrix.cpp
    unit/test_image_saver.cpp
    unit/test_ray.cpp
    unit/test_aabb.cpp
    ${TEST_SOURCES_CPP}
)

target_include_directories(unit_tests PRIVATE 
    "${CMAKE_SOURCE_DIR}/include"
)

target_link_libraries(unit_tests
    GTest::gtest_main
)

if(MSVC)
    target_compile_options(unit_tests PRIVATE "/EHsc" "/utf-8" "/wd4819")
    target_link_options(unit_tests PRIVATE "/NODEFAULTLIB:LIBCMT")
endif()

# Integration Tests
add_executable(integration_tests
    integration/test_scene_factory.cpp
    ${TEST_SOURCES_CPP}
)

target_include_directories(integration_tests PRIVATE 
    "${CMAKE_SOURCE_DIR}/include"
)

target_link_libraries(integration_tests PRIVATE 
    GTest::gtest_main
)

if(MSVC)
    target_compile_options(integration_tests PRIVATE "/EHsc" "/utf-8" "/wd4819")
    target_link_options(integration_tests PRIVATE "/NODEFAULTLIB:LIBCMT")
endif()

# Interactive API Test
add_executable(interactive_test
    test_interactive_api.cpp
    "${CMAKE_SOURCE_DIR}/src/api/gpu_engine_impl.cpp"
    "${CMAKE_SOURCE_DIR}/src/gpu/gpu_driver_stub.cpp"
)

target_include_directories(interactive_test PRIVATE 
    "${CMAKE_SOURCE_DIR}/include"
    ${CMAKE_CUDA_TOOLKIT_INCLUDE_DIRECTORIES}
)

target_link_libraries(interactive_test 
    GTest::gtest_main
)

if(MSVC)
    target_compile_options(interactive_test PRIVATE "/EHsc" "/utf-8" "/wd4819")
    target_link_options(interactive_test PRIVATE "/NODEFAULTLIB:LIBCMT")
endif()

# Register Tests
include(GoogleTest)
gtest_discover_tests(unit_tests)
gtest_discover_tests(integration_tests)
gtest_discover_tests(interactive_test)

add_executable(repro
    "${CMAKE_SOURCE_DIR}/repro.cpp"
    ${TEST_SOURCES_CPP}
)
target_include_directories(repro PRIVATE "${CMAKE_SOURCE_DIR}/include")
if(MSVC)
    target_compile_options(repro PRIVATE "/EHsc" "/utf-8" "/wd4819")
    target_link_options(repro PRIVATE "/NODEFAULTLIB:LIBCMT")
endif()
