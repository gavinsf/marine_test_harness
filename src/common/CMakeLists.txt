add_library(common utc.cpp checksum.cpp)

target_include_directories(common PUBLIC ${PROJECT_SOURCE_DIR}/include)
