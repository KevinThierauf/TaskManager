cmake_minimum_required(VERSION 3.19)
project(TaskManager)

set(CMAKE_CXX_STANDARD 17)

add_library(TaskManager STATIC Task.hpp Worker.hpp TaskScheduler.hpp TaskScheduler.cpp Worker.cpp Task.cpp)

if (MSVC)
    target_compile_options(TaskManager PUBLIC /EHsc)
endif ()

add_subdirectory(demo)