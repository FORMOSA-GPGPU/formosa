include(GoogleTest)

option(DRAMUTILS_USE_FETCH_CONTENT_GOOGLE_TEST "Enable FetchContent to provide Google Test" ${DRAMUTILS_USE_FETCH_CONTENT})

if (DRAMUTILS_USE_FETCH_CONTENT)
    if (DRAMUTILS_USE_FETCH_CONTENT_GOOGLE_TEST)
        FetchContent_Declare(
            GTest
            URL https://github.com/google/googletest/releases/download/v1.15.2/googletest-1.15.2.tar.gz
            OVERRIDE_FIND_PACKAGE
        )
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        set(INSTALL_GTEST OFF)
        FetchContent_MakeAvailable(GTest)
    endif()
endif()

find_package(GTest REQUIRED)

add_subdirectory(tests_memspec)
