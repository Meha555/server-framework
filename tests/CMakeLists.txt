find_package(GTest CONFIG REQUIRED)

# file(GLOB src_list *.cc)
# 如果使用aux_source_directory来代替的话，注意只能使用绝对路径
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} src_list)

message(STATUS "生成单元测试")
enable_testing() # 启用CTest

foreach(v ${src_list})
    string(REGEX MATCH "tests/.*" relative_path ${v})
    string(REGEX REPLACE "tests/" "" test_name ${relative_path})
    string(REGEX REPLACE ".cc" "" test_name ${test_name})
    add_executable(${test_name} ${v})
    target_include_directories(${test_name} PRIVATE ../source)
    target_link_libraries(${test_name} PRIVATE conet GTest::gtest GTest::gtest_main)
    # 添加CTest
    add_test(NAME ${test_name} COMMAND $<TARGET_FILE:${test_name}>) # NAME是指明测试的名称 ,COMMAND指要运行的测试文件所在的路径
endforeach()
