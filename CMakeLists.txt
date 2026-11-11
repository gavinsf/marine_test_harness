cmake_minimum_required(VERSION 3.20)

project(project LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()


add_subdirectory(src/common)
add_subdirectory(src/sim)
add_subdirectory(src/udp)
add_subdirectory(tests/unit)