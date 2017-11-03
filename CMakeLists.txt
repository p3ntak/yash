cmake_minimum_required(VERSION 3.8)
project(yash)

set(CMAKE_C_STANDARD 99)

set(SOURCE_FILES main.c helpers.h)
add_executable(yash ${SOURCE_FILES})