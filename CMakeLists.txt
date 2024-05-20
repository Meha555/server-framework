cmake_minimum_required(VERSION 3.15)
project(server_framework)

set(CMAKE_CXX_STANDARD 17)
option(CMAKE_VERBOSE_MAKEFILE ON)
option(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_BUILD_TYPE "Debug")

add_definitions("-O0 -g -ggdb -Wno-unused-variable")

include_directories(include)

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

add_subdirectory(source)
add_subdirectory(tests)