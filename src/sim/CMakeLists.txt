add_library(sim builder.cpp)
target_include_directories(sim PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(sim PUBLIC common)