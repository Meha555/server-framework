// #ifndef SERVER_FRAMEWORK_UTIL_H
// #define SERVER_FRAMEWORK_UTIL_H
#pragma once

#include "log.h"
#include "noncopyable.h"
#include <cassert>
#include <cinttypes>
#include <memory>
#include <pthread.h>
#include <string>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include <chrono>
#include <vector>

// 普通断言
#ifndef ASSERT
#define ASSERT(cond)                                                                                                   \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            LOG_FMT_FATAL(GET_ROOT_LOGGER(),                                                                           \
                          "Assertion: " #cond "\nSysErr: %s (%u)\nBacktrace:\n%s",                                     \
                          strerror(errno),                                                                             \
                          errno,                                                                                       \
                          meha::BacktraceToString().c_str());                                                          \
            assert(cond);                                                                                              \
        }                                                                                                              \
    } while (0)
#endif

// LOG(GET_ROOT_LOGGER(), FATAL) << "Assertion: " << #cond << ", " << msg << '\n'
//                               << "SysErr: " << strerror(errno) << " (" << errno << ")\n"
//                               << "Backtrace: " << '\n'
//                               << meha::BacktraceToString();
// 额外信息的断言
#ifndef ASSERT_FMT
#define ASSERT_FMT(cond, fmt, args...)                                                                                 \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            LOG_FMT_FATAL(GET_ROOT_LOGGER(),                                                                           \
                          "Assertion: " #cond ", " fmt "\nSysErr: %s (%u)\nBacktrace:\n%s",                            \
                          ##args,                                                                                      \
                          strerror(errno),                                                                             \
                          errno,                                                                                       \
                          meha::BacktraceToString().c_str());                                                          \
            assert(cond);                                                                                              \
        }                                                                                                              \
    } while (0)
#endif

namespace meha {

// 获取linux下线程的唯一id
uint32_t GetThreadID();

// 获取协程id
uint64_t GetFiberID();

/**
 * @brief 以 vector 的形式获取调用栈
 * @param[out] out 获取的调用栈
 * @param size 获取调用栈的最大层数，默认值为 200
 * @param skip 省略最近 n 层调用栈，默认值为 1，即忽略获取 Backtrace() 本身的调用栈
 */
void Backtrace(std::vector<std::string> &out, int size = 200, int skip = 1);

/**
 * @brief 获取调用栈字符串，内部调用 Backtrace()
 * @param size 获取调用栈的最大层数，默认值为 200
 * @param skip 省略最近 n 层调用栈，默认值为 2，即忽略获取 BacktraceToSring() 和 Backtrace() 的调用栈
 */
std::string BacktraceToString(int size = 200, int skip = 2);

std::chrono::milliseconds GetCurrentMS();

std::chrono::microseconds GetCurrentUS();

std::chrono::nanoseconds GetCurrentNS();

// uint64_t GetCurrent

}  // namespace meha
// #endif
