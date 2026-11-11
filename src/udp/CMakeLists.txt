add_library(udp sender.cpp receiver.cpp)
target_include_directories(udp PUBLIC ${PROJECT_SOURCE_DIR}/include)