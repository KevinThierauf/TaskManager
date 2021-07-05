cmake_minimum_required(VERSION 3.19)

add_executable(TaskManagerDemo main.cpp)
target_link_libraries(TaskManagerDemo PUBLIC TaskManager)
target_include_directories(TaskManagerDemo PUBLIC "${CMAKE_SOURCE_DIR}")