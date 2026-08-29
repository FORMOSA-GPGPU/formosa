###############################################
###               tests_memspec             ###
###############################################

add_executable(tests_memspec
    base/test_base.cpp
)

target_compile_definitions(tests_memspec PUBLIC TEST_RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/temp/")
target_link_libraries(tests_memspec
    gtest
    gtest_main
    DRAMUtils::DRAMUtils
)

gtest_discover_tests(tests_memspec
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)