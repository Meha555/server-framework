find_package(fmt CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(Boost COMPONENTS REQUIRED)
if (Boost_FOUND)
    message(STATUS "Boost_INCLUDE_DIRS: ${Boost_INCLUDE_DIRS}")
    message(STATUS "Boost_LIBRARIES: ${Boost_LIBRARIES}")
    message(STATUS "Boost_VERSION: ${Boost_VERSION}")

    include_directories(${Boost_INCLUDE_DIRS})
endif ()

# file(GLOB src_list *.cc)
aux_source_directory(. src_list)

add_library(conet STATIC ${src_list})

target_link_libraries(conet pthread dl ${Boost_LIBRARIES} yaml-cpp fmt::fmt)