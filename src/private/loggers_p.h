#pragma once

#include <memory>

namespace meha
{

class Logger;

extern const std::shared_ptr<Logger> s_coreLogger; // 框架内部使用的默认日志器

}