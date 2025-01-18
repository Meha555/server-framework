aux_source_directory(. SRCS)

add_library(conet_zk SHARED
    ${SRCS}
)
target_include_directories(conet_zk PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(conet_zk PRIVATE
    zookeeper_mt
    pthread
)