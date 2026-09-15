cmake_minimum_required(VERSION 3.16)
project(MyWebServer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(server src/main.cpp)
add_executable(thread_demo src/thread_demo.cpp)
add_executable(thread_pool_demo src/thread_pool_demo.cpp src/thread_pool.cpp)
target_include_directories(thread_pool_demo PRIVATE include)
