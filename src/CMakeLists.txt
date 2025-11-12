find_package(fmt CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(Boost COMPONENTS REQUIRED)
if (Boost_FOUND)
    message(STATUS "Boost_INCLUDE_DIRS: ${Boost_INCLUDE_DIRS}")
    message(STATUS "Boost_LIBRARIES: ${Boost_LIBRARIES}")
    message(STATUS "Boost_VERSION: ${Boost_VERSION}")
    include_directories(${Boost_INCLUDE_DIRS})
endif ()

set(core_sources
    application.cc
    cmd.cc
    env.cc
    fd_manager.cc
    fiber.cc
    io_manager.cc
    scheduler.cc
    timer.cc
)
set(module_sources
    module/private/hook_module.cc
    module/private/log_module.cc
    module/hook.cc
    module/log.cc
)
set(utils_sources
    utils/address.cc
    utils/bytearray.cc
    utils/cond.cc
    utils/daemon.cc
    utils/exception.cc
    utils/mutex.cc
    utils/sem.cc
    utils/thread.cc
    utils/util.cc
)

add_library(conet STATIC ${core_sources} ${module_sources} ${utils_sources})

add_subdirectory(extra)

# 添加pch预编译头文件
target_precompile_headers(conet PRIVATE pch.h)

target_link_libraries(conet pthread dl ${Boost_LIBRARIES} yaml-cpp fmt::fmt)