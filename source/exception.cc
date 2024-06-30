#include "exception.h"
#include "util.h"
#include <fmt/format.h>

namespace meha {

Exception::Exception(std::string what) : std::exception(), m_message(std::move(what)), m_stack(BacktraceToString(200))
{}

const char *Exception::what() const noexcept { return m_message.c_str(); }

const char *Exception::stackTrace() const noexcept { return m_stack.c_str(); }

SystemError::SystemError(std::string what)
    : std::system_error(std::error_code(errno, std::system_category())),
      Exception(fmt::format("{} :{}({})", what, code().message(), code().value()))
{}

}  // namespace meha
