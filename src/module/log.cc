#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <ostream>
#include <sstream>

#include <fmt/format.h>

#include "config.h"
#include "module/log.h"
#include "utils/exception.h"
#include "utils/mutex.h"
#include "utils/utils.h"

namespace meha
{

std::string LogMessage::LogLevel::levelToString(LogMessage::LogLevel::Level level)
{
    std::string result;
    switch (level) {
#define __LEVEL(level)   \
    case level:          \
        result = #level; \
        break;
        __LEVEL(TRACE)
        __LEVEL(DEBUG)
        __LEVEL(INFO)
        __LEVEL(WARN)
        __LEVEL(ERROR)
        __LEVEL(FATAL)
        __LEVEL(UNKNOWN)
#undef __LEVEL
    }
    return result;
}

// 普通文本项（就是日志字符串中出现的非content、非格式控制符的字符）
struct PlainFormatItem : public LogFormatter::FormatItemBase
{
    explicit PlainFormatItem(const std::string &str)
        : m_str(str)
    {
    }
    void format(std::ostream &out, const LogMessage::sptr msg) const override
    {
        out << m_str;
    }

private:
    std::string m_str;
};

// 日志级别项
struct LevelFormatItem : public LogFormatter::FormatItemBase
{
    void format(std::ostream &out, const LogMessage::sptr msg) const override
    {
        out << std::left << std::setw(5) << LogMessage::LogLevel::levelToString(msg->level);
    }
};

// 日志类别项
struct CategoryFormatItem : public LogFormatter::FormatItemBase
{
    void format(std::ostream &out, const LogMessage::sptr msg) const override
    {
        out << std::left << std::setw(6) << msg->category;
    }
};

// 文件名项
struct FilenameFormatItem : public LogFormatter::FormatItemBase
{
    void format(std::ostream &out, const LogMessage::sptr msg) const override
    {
        out << msg->file;
    }
};

// 行号项
struct LineFormatItem : public LogFormatter::FormatItemBase
{
public:
    void format(std::ostream &out, const LogMessage::sptr msg) const override
    {
        out << msg->line;
    }
};

// 函数名项
struct FunctionFormatItem : public LogFormatter::FormatItemBase
{
    void format(std::ostream &out, const LogMessage::sptr msg) const override
    {
        out << msg->function;
    }
};

// 线程号项
struct ThreadIDFormatItem : public LogFormatter::FormatItemBase
{
public:
    void format(std::ostream &out, const LogMessage::sptr msg) const override
    {
        out << msg->tid;
    }
};

// 协程号项
struct FiberIDFormatItem : public LogFormatter::FormatItemBase
{
    void format(std::ostream &out, const LogMessage::sptr msg) const override
    {
        out << msg->fid;
    }
};

// 时间戳项
struct DateTimeFormatItem : public LogFormatter::FormatItemBase
{
    explicit DateTimeFormatItem(const std::string &str = "%Y-%m-%d %H:%M:%S")
        : m_time_pattern(str)
    {
        if (m_time_pattern.empty()) {
            m_time_pattern = "%Y-%m-%d %H:%M:%S";
        }
    }
    void format(std::ostream &out, const LogMessage::sptr msg) const override
    {
        std::time_t t = std::chrono::system_clock::to_time_t(msg->timestamp);
        std::tm tm;
        ::localtime_r(&t, &tm);
        std::stringstream ss;
        ss << std::put_time(&tm, m_time_pattern.c_str());
        out << ss.str();
    }

private:
    std::string m_time_pattern;
};

// 累计毫秒数项
struct ElapseFormatItem : public LogFormatter::FormatItemBase
{
    void format(std::ostream &out, const LogMessage::sptr msg) const override
    {
        out << utils::GetCurrentMS().count();
    }
};

// 日志内容项
struct ContentFormatItem : public LogFormatter::FormatItemBase
{
    void format(std::ostream &out, const LogMessage::sptr msg) const override
    {
        out << msg->message();
    }
};

// 换行符项
struct NewLineFormatItem : public LogFormatter::FormatItemBase
{
    void format(std::ostream &out, const LogMessage::sptr msg) const override
    {
        out << '\n';
    }
};

// 制表符项
class TabFormatItem : public LogFormatter::FormatItemBase
{
    void format(std::ostream &out, const LogMessage::sptr msg) const override
    {
        out << '\t';
    }
};

// '%'项
struct PercentSignFormatItem : public LogFormatter::FormatItemBase
{
    void format(std::ostream &out, const LogMessage::sptr msg) const override
    {
        out << '%';
    }
};

// 日志模板格式控制符对应的日志项实现类
// REVIEW 为什么要加thread_local才能正确初始化？（试试test_fiber）
thread_local static const std::unordered_map<char, LogFormatter::FormatItemBase::sptr>
    g_format_item_map{
#define __FORMAT(ch, item) {ch, std::make_shared<item>()}
        __FORMAT('p', LevelFormatItem), // 日志等级
        __FORMAT('c', CategoryFormatItem), // 日志分类
        __FORMAT('f', FilenameFormatItem), // 文件名
        __FORMAT('l', LineFormatItem), // 行号
        __FORMAT('C', FunctionFormatItem), // 函数名
        __FORMAT('d', DateTimeFormatItem), // 时间
        __FORMAT('r', ElapseFormatItem), // 累计毫秒数
        __FORMAT('t', ThreadIDFormatItem), // 线程号
        __FORMAT('F', FiberIDFormatItem), // 协程号
        __FORMAT('m', ContentFormatItem), // 内容
        __FORMAT('n', NewLineFormatItem), // 换行符
        __FORMAT('%', PercentSignFormatItem), // 百分号
        __FORMAT('T', TabFormatItem), // 制表符
#undef __FORMAT
    };

static const std::string s_defaultFormatPattern = "%d%T[%c] [%p] (T:%t F:%F) %f:%l%T%m%n";
const LogFormatter::sptr s_defaultLogFormatter = std::make_shared<LogFormatter>(s_defaultFormatPattern);

Logger::Logger()
    : m_category("core")
    , m_baseLevel(LogMessage::LogLevel::UNKNOWN)
    , m_pattern(s_defaultFormatPattern)
    , m_defaultFormatter(s_defaultLogFormatter)
{
}

Logger::Logger(const std::string &category,
               const LogMessage::LogLevel::Level lowest_level,
               const std::string &pattern)
    : m_category(category)
    , m_baseLevel(lowest_level)
    , m_pattern(pattern)
    , m_defaultFormatter(new LogFormatter(pattern))
{
}

void Logger::addAppender(LogAppender::sptr appender)
{
    ScopedLock lock(&m_mutex);
    // 在传入的日志输出适配器没有设置日志格式时，覆盖使用当前Logger的默认格式
    if (!appender->getFormatter()) {
        appender->setFormatter(m_defaultFormatter);
    }
    m_appenders.push_back(appender);
}

void Logger::delAppender(LogAppender::sptr appender)
{
    ScopedLock lock(&m_mutex);
    const auto itor = std::find(m_appenders.begin(), m_appenders.end(), appender);
    if (itor != m_appenders.end()) {
        m_appenders.erase(itor);
    }
}

void Logger::log(const LogMessage::sptr msg)
{
    // 只有当输出日志等级大于等于日志器的日志等级时才输出
    if (msg->level < m_baseLevel)
        return;
    // 遍历所有的输出器，通过输出器的log方法输出日志
    ScopedLock lock(&m_mutex);
    for (auto &appender : m_appenders) {
        appender->sink(msg->level, msg);
    }
}

void Logger::trace(const LogMessage::sptr msg)
{
    msg->level = LogMessage::LogLevel::TRACE;
    log(msg);
}

void Logger::debug(const LogMessage::sptr msg)
{
    msg->level = LogMessage::LogLevel::DEBUG;
    log(msg);
}

void Logger::info(const LogMessage::sptr msg)
{
    msg->level = LogMessage::LogLevel::INFO;
    log(msg);
}

void Logger::warn(const LogMessage::sptr msg)
{
    msg->level = LogMessage::LogLevel::WARN;
    log(msg);
}

void Logger::error(const LogMessage::sptr msg)
{
    msg->level = LogMessage::LogLevel::ERROR;
    log(msg);
}

void Logger::fatal(const LogMessage::sptr msg)
{
    msg->level = LogMessage::LogLevel::FATAL;
    log(msg);
}

LogFormatter::sptr LogAppender::getFormatter() const
{
    ScopedLock lock(&m_mutex);
    return m_formatter;
}

void LogAppender::setFormatter(LogFormatter::sptr formatter)
{
    ScopedLock lock(&m_mutex);
    m_formatter = std::move(formatter);
}

StdoutLogAppender::StdoutLogAppender(LogMessage::LogLevel::Level level)
    : LogAppender(level)
{
}

bool StdoutLogAppender::s_hasColor = false;
std::once_flag StdoutLogAppender::s_onceFlag;

bool StdoutLogAppender::IsColorSupported()
{
    // static bool is_color_supported = false; // 'is_color_supported' cannot be captured because it does not have automatic storage duration
    std::call_once(s_onceFlag, []() {
        const char *no_color = std::getenv("NO_COLOR");
        if (no_color) {
            s_hasColor = false;
            return;
        }
        const char *term = std::getenv("TERM");
        if (!term) {
            s_hasColor = false;
            return;
        }
        s_hasColor = std::strstr(term, "color") || std::strstr(term, "xterm") || std::strstr(term, "rxvt") || std::strstr(term, "ansi");
    });
    return s_hasColor;
}

void StdoutLogAppender::sink(const LogMessage::LogLevel::Level level, const LogMessage::sptr msg)
{
    if (level < m_baseLevel)
        return;
    ScopedLock lock(&m_mutex);
    if (IsColorSupported()) {
        const char *color = nullptr;
        switch (level) {
        case LogMessage::LogLevel::TRACE:
            color = "\033[1;30m"; // 暗灰色
            break;
        case LogMessage::LogLevel::DEBUG:
            color = "\033[0;34m"; // 蓝色
            break;
        case LogMessage::LogLevel::INFO:
            color = "\033[0;32m"; // 绿色
            break;
        case LogMessage::LogLevel::WARN:
            color = "\033[0;33m"; // 黄色
            break;
        case LogMessage::LogLevel::ERROR:
            color = "\033[0;31m"; // 红色
            break;
        case LogMessage::LogLevel::FATAL:
            color = "\033[1;41;33m"; // 洋红色背景，亮黄色文字
            break;
        default:
            color = "\033[0m"; // 默认白色
        }
        std::cout << fmt::format("{}{}\033[0m", color, m_formatter->format(msg));
    } else {
        std::cout << fmt::format("{}", m_formatter->format(msg));
    }
    std::cout.flush(); // TODO 这里可以优化一下，例如缓冲输出，以减少频繁刷新带来的性能开销
}

LogAppender::LogAppender(LogMessage::LogLevel::Level level)
    : m_baseLevel(level)
{
}

FileLogAppender::FileLogAppender(const std::string &filename,
                                 LogMessage::LogLevel::Level level)
    : LogAppender(level)
    , m_filename(filename)
{
    openFile();
}

// NOTE 不能关了文件，因为其他日志输出器可能也在用这个文件（需不需要管这个问题，ofs有像fd那样的引用计数吗）
// FileLogAppender::~FileLogAppender() override {
//     if (!m_ofstream) {
//         m_ofstream.close();
//     }
// }

bool FileLogAppender::openFile()
{
    ScopedLock lock(&m_mutex);
    // 使用 fstream::operator!() 判断文件是否被正确打开
    if (!m_ofstream) {
        m_ofstream.close();
    }
    m_ofstream.open(m_filename, std::ios_base::out | std::ios_base::app);
    return !!m_ofstream;
}

void FileLogAppender::sink(const LogMessage::LogLevel::Level level, const LogMessage::sptr msg)
{
    if (level < m_baseLevel)
        return;
    ScopedLock lock(&m_mutex);
    m_ofstream << m_formatter->format(msg);
    m_ofstream.flush(); // TODO 这里可以优化一下，例如缓冲输出，以减少频繁刷新带来的性能开销
}

LogFormatter::LogFormatter(const std::string &pattern)
    : m_pattern(pattern)
{
    parse();
}

void LogFormatter::parse()
{
    enum ParseStatus {
        DO_SCAN, // 是普通字符，直接保存
        DO_CREATE, // 是 %，处理占位符
    };
    ParseStatus status = DO_SCAN;
    size_t item_begin = 0, item_end = 0; // 左闭右闭区间
    for (size_t i = 0; i < m_pattern.length(); i++) { // 注意这里每次i都会+1
        switch (status) {
        case DO_SCAN: { // 创建对应的普通字符处理对象后填入 m_fmt_items 中
            // 双指针提取出普通字符
            item_begin = i;
            for (item_end = i; item_end < m_pattern.length(); ++item_end) {
                // 扫描到 % 结束普通字符串查找，更新状态为占位符处理状态 DO_CREATE
                if (m_pattern[item_end] == '%') {
                    status = DO_CREATE;
                    break;
                }
            }
            i = item_end;
            m_fmtItems.push_back(std::make_shared<PlainFormatItem>(m_pattern.substr(item_begin, item_end - item_begin)));
        } break;
        case DO_CREATE: { // 处理占位符
            assert(!g_format_item_map.empty() && "g_format_item_map 没有被正确的初始化"); // REVIEW 这里不能用自己的ASSERT宏，因为这是在Logger中
            auto itor = g_format_item_map.find(m_pattern[i]);
            if (itor == g_format_item_map.end()) {
                m_fmtItems.push_back(std::make_shared<PlainFormatItem>("<error format>"));
            } else {
                m_fmtItems.push_back(itor->second);
            }
            status = DO_SCAN;
        } break;
        }
    }
}

std::string LogFormatter::format(LogMessage::sptr msg) const
{
    // 遍历所有的FormatItem，调用其format方法来将该项对应的格式化字符串写入流
    std::stringstream ss;
    for (const auto &item : m_fmtItems) {
        item->format(ss, msg);
    }
    return ss.str();
}

__LoggerManager::__LoggerManager()
{
    update();
}

void __LoggerManager::ensureCoreLoggerExist()
{
    const auto iter = m_loggerMap.find("core");
    if (iter == m_loggerMap.end()) { // 日志器 map 里不存在core日志器
        auto coreLogger = std::make_shared<Logger>();
        coreLogger->setLevel(LogMessage::LogLevel::TRACE);
        coreLogger->addAppender(std::make_shared<StdoutLogAppender>());
        coreLogger->addAppender(std::make_shared<FileLogAppender>("/var/log/server-framework/core.log"));
        m_loggerMap.insert(std::make_pair("core", std::move(coreLogger)));
    } else if (!iter->second) { // 存在同名的键，但指针为空
        iter->second = std::make_shared<Logger>();
        iter->second->setLevel(LogMessage::LogLevel::TRACE);
        iter->second->addAppender(std::make_shared<StdoutLogAppender>());
        iter->second->addAppender(std::make_shared<FileLogAppender>("/var/log/server-framework/core.log"));
    }
}

void __LoggerManager::update()
{
    WriteScopedLock lock(&m_mutex);
    auto config = Config::Lookup<LogConfigs>("log");
    if (!config) {
        return;
    }
    const auto &config_log_list = config->getValue();
    for (const auto &config_log : config_log_list) {
        // 删除已存在的类别的 logger
        m_loggerMap.erase(config_log.category);
        auto logger = std::make_shared<Logger>(config_log.category, config_log.level, config_log.pattern);
        for (const auto &config_app : config_log.appenders) {
            LogAppender::sptr appender;
            switch (config_app.type) {
            case LogAppenderConfig::STDOUT:
                appender = std::make_shared<StdoutLogAppender>(config_app.level);
                break;
            case LogAppenderConfig::FILE:
                appender = std::make_shared<FileLogAppender>(config_app.file, config_app.level);
                break;
            default:
                std::cerr << "LoggerManager::init exception 无效的 appender 配置值，appender.type= " << (config_app.type ? "FILE" : "STDOUT") << std::endl;
                break;
            }
            // 如果定义了 appender 的日志格式，为其创建专属的 formatter
            // 否则在其加入 logger 时，会被设置为当前Logger的默认格式化器
            if (!config_app.pattern.empty()) {
                appender->setFormatter(
                    std::make_shared<LogFormatter>(config_app.pattern));
            }
            logger->addAppender(appender);
        }
        std::cerr << "成功创建日志器 " << config_log.category << std::endl;
        m_loggerMap.insert(std::make_pair(config_log.category, logger));
    }
    // 确保存在一个全局的日志器
    ensureCoreLoggerExist();
}

Logger::sptr __LoggerManager::getLogger(const std::string &category) const
{
    ReadScopedLock lock(&m_mutex); // FIXME 这个锁是可递归的吗，所以下面不能用getLogger("root") ?
    return m_loggerMap.at(category);
    // auto iter = m_loggerMap.find(category);
    // if (iter == m_loggerMap.end()) {
    //     // return m_loggerMap.find("core")->second;
    //     throw Exception(fmt::format("不存在的日志器: {}", category));
    // }
    // return iter->second;
}

Logger::sptr __LoggerManager::getRootLogger() const
{
    return getLogger("root");
}

// static const std::string log_config = "/home/will/Workspace/Devs/projects/server-framework/misc/config.yml";

// _LogIniter::_LogIniter()
// {
//     // Config::LoadFromFile(log_config); // FIXME 修复各module的初始化问题
//     auto log_config_list = meha::Config::Lookup<LogConfigs>("log", {}, "日志器的配置项");
//     // 注册日志器配置项变更时的事件处理回调：当配置项变动时，更新日志器
//     log_config_list->addListener([](const LogConfigs &, const LogConfigs &) {
//         std::cerr << "日志器配置变动，更新日志器" << std::endl;
//         LoggerManager::Instance()->update();
//     });
// }

} // namespace meha
