#pragma once

#include <chrono>
#include <string>
#include <sys/syscall.h>
#include <unistd.h>
#include <utility>
#include <vector>

#include "utils/noncopyable.h"

namespace meha::utils
{

// 获得top命令中展示的Linux线程全局唯一ID（跨进程唯一）
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

/**
 * @brief 日期时间转字符串
 */
std::string Time2Str(time_t ts = time(0), const std::string &format = "%Y-%m-%d %H:%M:%S");

/**
 * @brief 字符串转日期时间
 */
time_t Str2Time(const char *str, const char *format = "%Y-%m-%d %H:%M:%S");

template<typename Callback>
struct ScopeGuard
{
    DISABLE_COPY(ScopeGuard)
    ScopeGuard(const Callback &cb) noexcept
        : m_cb(cb)
        , m_invoke(true)
    {
    }
    ScopeGuard(Callback &&cb) noexcept
        : m_cb(std::move(cb))
        , m_invoke(true)
    {
    }
    ScopeGuard(ScopeGuard &&other) noexcept
        : m_cb(std::move(other.m_cb))
        , m_invoke(std::exchange(other.m_invoke, false))
    {
    }
    ~ScopeGuard() noexcept
    {
        if (m_invoke) {
            m_cb();
        }
    }
    void dismiss()
    {
        m_invoke = false;
    }

private:
    Callback m_cb;
    bool m_invoke;
};

template<typename Callback>
ScopeGuard<typename std::decay_t<Callback>> GenScopeGuard(Callback &&cb)
{
    return ScopeGuard<typename std::decay_t<Callback>>(std::forward<Callback>(cb));
}

} // namespace meha::utils