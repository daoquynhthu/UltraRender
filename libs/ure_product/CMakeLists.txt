set(UR_PRODUCT_BUILD_INPUT
    "UltraRender.Product.v0|${CMAKE_CXX_COMPILER_ID}|${CMAKE_CXX_COMPILER_VERSION}|${CMAKE_SYSTEM_NAME}|${CMAKE_SYSTEM_PROCESSOR}")
foreach(source
    ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
    ${CMAKE_CURRENT_SOURCE_DIR}/include/ure/product/product_service.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/product_service.cpp)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${source})
    file(SHA256 ${source} source_digest)
    string(APPEND UR_PRODUCT_BUILD_INPUT "|${source_digest}")
endforeach()
string(SHA256 UR_PRODUCT_BUILD_DIGEST "${UR_PRODUCT_BUILD_INPUT}")

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/product_build_config.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/product_build_config.hpp
    @ONLY)

add_library(ure_product STATIC src/product_service.cpp)
target_include_directories(ure_product
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(ure_product PUBLIC ure_core ure_sceneio)

if(MSVC)
    target_compile_options(ure_product PRIVATE /W4 /WX /utf-8 /EHsc /wd4819)
else()
    target_compile_options(ure_product PRIVATE -Wall -Wextra -Werror -pedantic)
endif()
