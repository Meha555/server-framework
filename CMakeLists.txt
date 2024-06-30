cmake_minimum_required(VERSION 3.15)
project(server_framework)

option(BUILD_TESTING ON)

set(CMAKE_CXX_STANDARD 17)
option(CMAKE_VERBOSE_MAKEFILE ON)
option(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_BUILD_TYPE "Debug")

add_definitions("-O0 -g -rdynamic -ggdb -Wno-unused-variable")

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

find_package(fmt CONFIG REQUIRED)

include(CTest) # 引入CTest

include_directories(include) # 全局头文件搜索目录

add_subdirectory(source)
if(BUILD_TESTING)
	add_subdirectory(tests)
endif()