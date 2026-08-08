find_program(UR_PWSH_EXECUTABLE NAMES pwsh REQUIRED)

add_test(
    NAME test_public_boundary_audit
    COMMAND
        ${UR_PWSH_EXECUTABLE}
        -NoProfile
        -File ${CMAKE_CURRENT_SOURCE_DIR}/test_public_boundary_audit.ps1
        -RepoRoot ${CMAKE_SOURCE_DIR}
        -BuildDir ${CMAKE_BINARY_DIR})

set_tests_properties(
    test_public_boundary_audit
    PROPERTIES TIMEOUT 90)
