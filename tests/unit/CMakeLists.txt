include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(unit_tests
    checksum_test.cpp
    builder_test.cpp
)
target_link_libraries(unit_tests PRIVATE common sim GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(unit_tests)