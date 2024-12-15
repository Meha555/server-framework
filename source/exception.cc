#include "exception.h"
#include "utils.h"
#include <fmt/format.h>
#include <stdexcept>

namespace meha
{

Exception::Exception(const std::string &what)
    : std::exception()
    , m_message(std::move(what))
    , m_stack(utils::BacktraceToString(200))
{
}

const char *Exception::what() const noexcept
{
    return m_message.c_str();
}

const char *Exception::stackTrace() const noexcept
{
    return m_stack.c_str();
}

SystemError::SystemError(const std::string &what)
    : std::system_error(std::error_code(errno, std::system_category()))
    , Exception(
          fmt::format("{} : {}({})", what, code().message(), code().value()))
{
}

RuntimeError::RuntimeError(const std::string &what)
    : std::runtime_error(what)
    , Exception(what)
{
}

} // namespace meha
