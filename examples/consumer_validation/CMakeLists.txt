cmake_minimum_required(VERSION 3.20)
project(consumer_validation LANGUAGES CXX)

find_package(RandX CONFIG REQUIRED)

# 验证 C++17 消费者工程
add_executable(consumer_cpp17 main_cpp17.cpp)
target_link_libraries(consumer_cpp17 PRIVATE RandX::Cpp17)

# 验证 C++23 消费者工程
add_executable(consumer_cpp23 main_cpp23.cpp)
target_link_libraries(consumer_cpp23 PRIVATE RandX::Cpp23)
