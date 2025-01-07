cmake_minimum_required(VERSION 3.15)
project(server_framework)

option(BUILD_TESTING "生成测试" ON)
option(BUILD_EXAMPLES "生成示例" ON)

# 注意CMAKE预定义变量无法自己设置为缓存的，因为他们初始化地更早，所以自己写的set/option不会对它们起作用
set(CMAKE_CXX_STANDARD 17 REQUIRED)
set(CMAKE_BUILD_TYPE "Debug")
# set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_definitions("-O0 -g -rdynamic -ggdb -Wno-unused-variable")

# for clangd to diagnose c++ standard
add_definitions("-std=c++17")

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

include(CTest) # 引入CTest

# 全局头文件搜索目录
include_directories(src)

add_subdirectory(src)
if(BUILD_TESTING)
	add_subdirectory(tests)
endif()
if(BUILD_EXAMPLES)
	add_subdirectory(examples)
endif()