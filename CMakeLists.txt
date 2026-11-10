cmake_minimum_required(VERSION 3.20)
set(CMAKE_CXX_STANDARD 20)
enable_testing()

add_subdirectory(src/common)
add_subdirectory(src/simulator)
add_subdirectory(src/receiver)
add_subdirectory(tests/unit)