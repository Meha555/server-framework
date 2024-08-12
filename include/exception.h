// #ifndef SERVER_FRAMEWORK_EXCEPTION_H
// #define SERVER_FRAMEWORK_EXCEPTION_H
#pragma once

#include <exception>
#include <system_error>

namespace meha {

/**
 * @brief std::exception 的封装
 * 增加了调用栈信息的获取接口
 */
class Exception : virtual public std::exception {
public:
  explicit Exception(std::string what);
  ~Exception() noexcept override = default;

  // 获取异常信息
  const char *what() const noexcept override;
  // 获取函数调用栈
  const char *stackTrace() const noexcept;

protected:
  std::string m_message;
  std::string m_stack;
};

#define EXCEPTION_WRAPPER(except_name, std_except)                             \
  class except_name : virtual public std::std_except, public Exception {       \
  public:                                                                      \
    using Exception::what;                                                     \
    explicit except_name(std::string what = "");                               \
  };

EXCEPTION_WRAPPER(SystemError, system_error)
EXCEPTION_WRAPPER(RuntimeError, runtime_error)

} // namespace meha

// #endif
