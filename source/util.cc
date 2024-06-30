#include "util.h"
#include "fiber.h"
#include "log.h"
#include <chrono>
#include <cxxabi.h>
#include <execinfo.h>
#include <iostream>
#include <sys/time.h>

namespace meha {

// 获得top命令中展示的Linux线程ID
uint32_t GetThreadID()
{
    uint32_t tid = ::syscall(SYS_gettid);
    if (tid == -1) {
        LOG_FMT_FATAL(GET_ROOT_LOGGER(), "获取系统线程 tid 失败：%s", ::strerror(errno));
        throw std::system_error();
    }
    return tid;
}

uint64_t GetFiberID() { return Fiber::GetCurrentID(); }

void Backtrace(std::vector<std::string> &out, int size, int skip)
{
    // 问：如果这里的void_ptr_list用的是内置指针的智能指针，或者是智能指针的智能指针呢？
    // 答：没有必要，因为void_ptr_list是要传入backtrace中的
    // 在堆上分配存储调用栈的内存，因为本项目有协程，此函数可能被协程调用，而协程的栈空间很小，不应在栈上分配大数组
    void **void_ptr_list = (void **)malloc(sizeof(void *) * size);
    int call_stack_count = ::backtrace(void_ptr_list, size);
    char **string_list = ::backtrace_symbols(void_ptr_list, call_stack_count);
    if (string_list == nullptr) {
        LOG_ERROR(GET_ROOT_LOGGER(), "Backtrace() exception, 调用栈获取失败");
        return;
    }
    for (int i = skip; i < call_stack_count; i++) {
        /**
         * 解码类型信息
         * 例如一个栈信息 ./test_exception(_Z2fni+0x62) [0x564e8a313317]
         * 函数签名在符号 "(" 后 "+" 前，
         * 调用 abi::__cxa_demangle 进行demangling
         * (...+ 之间的内容是函数参数
         */
        std::stringstream ss;
        char *call_stack_str = string_list[i];
        char *brackets_pos = nullptr;
        char *plus_pos = nullptr;
        // 找到左括号的位置
        for (brackets_pos = call_stack_str; brackets_pos - call_stack_str < size - 1 && *brackets_pos != '(';
             brackets_pos++)
            ;
        assert(*brackets_pos == '(');
        // 先把截止到左括号的字符串塞进字符串流里
        *brackets_pos = '\0';
        ss << string_list[i] << '(';
        *brackets_pos = '(';
        // 找到加号的位置
        for (plus_pos = brackets_pos; plus_pos - call_stack_str < size - 1 && *plus_pos != '+'; plus_pos++)
            ;
        assert(*plus_pos == '+');
        // 解析函数参数列表
        char *type = nullptr;
        if (*brackets_pos + 1 != *plus_pos) {  // 如果有参数列表
            *plus_pos = '\0';
            int status = 0;
            type = abi::__cxa_demangle(brackets_pos + 1, nullptr, nullptr, &status);
            assert(status == 0 || status == -2);
            // 当 status == -2 时，意思是字符串解析错误，直接将原字符串塞进流里
            ss << (status == 0 ? type : brackets_pos + 1);
            *plus_pos = '+';
        }
        // 把剩下的部分也原样塞进去
        ss << plus_pos;
        out.push_back(ss.str());
        free(type);
    }
    // backtrace_symbols() 返回 malloc 分配的内存指针，需要 free
    free(string_list);
    free(void_ptr_list);
}

std::string BacktraceToString(int size, int skip)
{
    std::vector<std::string> call_stack;
    Backtrace(call_stack, size, skip);
    std::stringstream ss;
    for (const auto &item : call_stack) {
        ss << item << '\n';
    }
    return ss.str();
}

std::chrono::milliseconds GetCurrentMS()
{
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
}

std::chrono::microseconds GetCurrentUS()
{
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch());
}

std::chrono::nanoseconds GetCurrentNS()
{
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch());
}

}  // namespace meha
