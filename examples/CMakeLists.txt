# 如果使用aux_source_directory来代替的话，注意只能使用绝对路径
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} src_list)

message(STATUS "生成示例")

foreach(v ${src_list})
    string(REGEX MATCH "${CMAKE_CURRENT_SOURCE_DIR}/.*" relative_path ${v})
    string(REGEX REPLACE "${CMAKE_CURRENT_SOURCE_DIR}/" "" exe_name ${relative_path})
    string(REGEX REPLACE ".cc" "" exe_name ${exe_name})
    add_executable(${exe_name} ${v})
    target_include_directories(${exe_name} PRIVATE ../src)
    target_link_libraries(${exe_name} PRIVATE conet)
endforeach()
