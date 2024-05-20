#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <ostream>
#include <sstream>

#include <fmt/format.h>
#include <unordered_map>

#include "exception.h"
#include "log.h"

namespace meha {

/**
 * @brief 普通文本项（就是日志字符串中出现的非content、非格式控制符的字符）
 */
struct PlainFormatItem : public LogFormatter::FormatItemBase
{
    explicit PlainFormatItem(const std::string &str) : m_str(str) {}
    void format(std::ostream &out, const LogEvent::ptr event) const override { out << m_str; }

private:
    std::string m_str;
};

/**
 * @brief 日志级别项
 */
struct LevelFormatItem : public LogFormatter::FormatItemBase
{
    void format(std::ostream &out, const LogEvent::ptr event) const override
    {
        out << LogEvent::LogLevel::levelToString(event->level);
    }
};

/**
 * @brief 文件名项
 */
struct FilenameFormatItem : public LogFormatter::FormatItemBase
{
    void format(std::ostream &out, const LogEvent::ptr event) const override { out << event->file; }
};

/**
 * @brief 行号项
 */
struct LineFormatItem : public LogFormatter::FormatItemBase
{
public:
    void format(std::ostream &out, const LogEvent::ptr event) const override { out << event->line; }
};

/**
 * @brief 线程号项
 */
struct ThreadIDFormatItem : public LogFormatter::FormatItemBase
{
public:
    void format(std::ostream &out, const LogEvent::ptr event) const override { out << event->thread_id; }
};

/**
 * @brief 协程号项
 */
struct FiberIDFormatItem : public LogFormatter::FormatItemBase
{
    void format(std::ostream &out, const LogEvent::ptr event) const override { out << event->fiber_id; }
};

/**
 * @brief 时间戳项
 */
struct TimeFormatItem : public LogFormatter::FormatItemBase
{
    explicit TimeFormatItem(const std::string &str = "%Y-%m-%d %H:%M:%S") : m_time_pattern(str)
    {
        if (m_time_pattern.empty()) {
            m_time_pattern = "%Y-%m-%d %H:%M:%S";
        }
    }
    void format(std::ostream &out, const LogEvent::ptr event) const override
    {
        // struct tm time_struct
        // {};
        // time_t time_l = event->timestamp;
        // localtime_r(&time_l, &time_struct);
        // char buffer[64]{0};
        // strftime(buffer, sizeof(buffer), m_time_pattern.c_str(), &time_struct);
        // out << buffer;
        std::time_t t = std::chrono::system_clock::to_time_t(event->timestamp);
        std::tm tm;
        ::localtime_r(&t, &tm);
        std::stringstream ss;
        ss << std::put_time(&tm, m_time_pattern.c_str());
        out << ss.str();
    }

private:
    std::string m_time_pattern;
};

/**
 * @brief 日志内容项
 */
struct ContentFormatItem : public LogFormatter::FormatItemBase
{
    void format(std::ostream &out, const LogEvent::ptr event) const override { out << event->getContent(); }
};

/**
 * @brief 换行符项
 */
struct NewLineFormatItem : public LogFormatter::FormatItemBase
{
    void format(std::ostream &out, const LogEvent::ptr event) const override { out << '\n'; }
};

/**
 * @brief 制表符项
 */
class TabFormatItem : public LogFormatter::FormatItemBase {
    void format(std::ostream &out, const LogEvent::ptr event) const override { out << '\t'; }
};

/**
 * @brief '%'项
 */
struct PercentSignFormatItem : public LogFormatter::FormatItemBase
{
    void format(std::ostream &out, const LogEvent::ptr event) const override { out << '%'; }
};

/** @brief 日志模板格式控制符对应的日志项实现类
 * %p 输出日志等级
 * %f 输出文件名
 * %l 输出行号
 * %d 输出日志时间
 * %t 输出线程号
 * %F 输出协程号
 * %m 输出日志消息
 * %n 输出换行
 * %% 输出百分号
 * %T 输出制表符
 * */
thread_local static const std::unordered_map<char, LogFormatter::FormatItemBase::ptr> g_format_item_map{
#define __FN(ch, item) {ch, std::make_shared<item>()}
    __FN('p', LevelFormatItem),
    __FN('f', FilenameFormatItem),
    __FN('l', LineFormatItem),
    __FN('d', TimeFormatItem),
    __FN('t', ThreadIDFormatItem),
    __FN('F', FiberIDFormatItem),
    __FN('m', ContentFormatItem),
    __FN('n', NewLineFormatItem),
    __FN('%', PercentSignFormatItem),
    __FN('T', TabFormatItem),
#undef __FN
};

std::string LogEvent::LogLevel::levelToString(LogEvent::LogLevel::Level level)
{
    std::string result;
    switch (level) {
#define __LEVEL(level)                                                                                                 \
    case level:                                                                                                        \
        result = #level;                                                                                               \
        break;
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

Logger::Logger()
    : m_name("default"), m_base_level(LogEvent::LogLevel::UNKNOWN), m_pattern("[%d] [%p] [T:%t F:%F]%T%m%n")
{
    m_default_formatter.reset(new LogFormatter(m_pattern));
}

Logger::Logger(const std::string &name, const LogEvent::LogLevel::Level lowest_level, const std::string &pattern)
    : m_name(name), m_base_level(lowest_level), m_pattern(pattern)
{
    m_default_formatter.reset(new LogFormatter(pattern));
}

void Logger::addAppender(LogAppender::ptr appender)
{
    ScopedLock lock(&m_mutex);
    // 在传入的日志输出适配器没有设置日志格式时，覆盖使用当前Logger的默认格式
    if (!appender->getFormatter()) {
        appender->setFormatter(m_default_formatter);
    }
    m_appenders.push_back(appender);
}

void Logger::delAppender(LogAppender::ptr appender)
{
    ScopedLock lock(&m_mutex);
    // TODO 实现可能存在问题
    const auto itor = std::find(m_appenders.begin(), m_appenders.end(), appender);
    if (itor != m_appenders.end()) {
        m_appenders.erase(itor);
    }
}

void Logger::log(const LogEvent::ptr event)
{
    // 只有当输出日志等级大于等于日志器的日志等级时才输出
    if (event->level < m_base_level)
        return;
    // 遍历所有的输出器，通过输出器的log方法输出日志
    ScopedLock lock(&m_mutex);
    for (auto &item : m_appenders) {
        item->log(event->level, event);
    }
}

void Logger::debug(const LogEvent::ptr event)
{
    event->level = LogEvent::LogLevel::DEBUG;
    log(event);
}

void Logger::info(const LogEvent::ptr event)
{
    event->level = LogEvent::LogLevel::INFO;
    log(event);
}

void Logger::warn(const LogEvent::ptr event)
{
    event->level = LogEvent::LogLevel::WARN;
    log(event);
}

void Logger::error(const LogEvent::ptr event)
{
    event->level = LogEvent::LogLevel::ERROR;
    log(event);
}

void Logger::fatal(const LogEvent::ptr event)
{
    event->level = LogEvent::LogLevel::FATAL;
    log(event);
}

LogFormatter::ptr LogAppender::getFormatter() const
{
    ScopedLock lock(&m_mutex);
    return m_formatter;
}

void LogAppender::setFormatter(LogFormatter::ptr formatter)
{
    ScopedLock lock(&m_mutex);
    m_formatter = std::move(formatter);
}

StdoutLogAppender::StdoutLogAppender(LogEvent::LogLevel::Level level) : LogAppender(level) {}

void StdoutLogAppender::log(const LogEvent::LogLevel::Level level, const LogEvent::ptr event)
{
    if (level < m_base_level)
        return;
    ScopedLock lock(&m_mutex);
    std::cout << m_formatter->format(event);
    std::cout.flush();
}

LogAppender::LogAppender(LogEvent::LogLevel::Level level) : m_base_level(level) {}

FileLogAppender::FileLogAppender(const std::string &filename, LogEvent::LogLevel::Level level)
    : LogAppender(level), m_filename(filename)
{
    openFile();
}

// NOTE 不能关了文件，因为其他日志输出器可能也在用这个文件
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

void FileLogAppender::log(const LogEvent::LogLevel::Level level, const LogEvent::ptr event)
{
    if (level < m_base_level)
        return;
    ScopedLock lock(&m_mutex);
    m_ofstream << m_formatter->format(event);
    m_ofstream.flush();
}

LogFormatter::LogFormatter(const std::string &pattern) : m_pattern(pattern) { parse(); }

void LogFormatter::parse()
{
    enum PaseStatus
    {
        DO_SCAN,    // 是普通字符，直接保存
        DO_CREATE,  // 是 %，处理占位符
    };
    PaseStatus status = DO_SCAN;
    size_t item_begin = 0, item_end = 0;               // 左闭右闭区间
    for (size_t i = 0; i < m_pattern.length(); i++) {  // 注意这里每次i都会+1
        switch (status) {
        case DO_SCAN: {  // 创建对应的普通字符处理对象后填入 m_fmt_items 中
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
            m_fmt_items.push_back(
                std::make_shared<PlainFormatItem>(m_pattern.substr(item_begin, item_end - item_begin)));
        } break;
        case DO_CREATE: {  // 处理占位符
            assert(!g_format_item_map.empty() && "g_format_item_map 没有被正确的初始化");
            auto itor = g_format_item_map.find(m_pattern[i]);
            if (itor == g_format_item_map.end()) {
                m_fmt_items.push_back(std::make_shared<PlainFormatItem>("<error format>"));
            } else {
                m_fmt_items.push_back(itor->second);
            }
            status = DO_SCAN;
        } break;
        }
    }
}

std::string LogFormatter::format(LogEvent::ptr event) const
{
    // 遍历所有的FormatItem，调用其format方法来将该项对应的格式化字符串写入流
    std::stringstream ss;
    for (const auto &item : m_fmt_items) {
        item->format(ss, event);
    }
    return ss.str();
}

__LoggerManager::__LoggerManager() { init(); }

void __LoggerManager::ensureRootLoggerExist()
{
    const auto iter = m_logger_map.find("root");
    if (iter == m_logger_map.end()) {  // 日志器 map 里不存在全局日志器
        auto global_logger = std::make_shared<Logger>();
        global_logger->addAppender(std::make_shared<StdoutLogAppender>());
        m_logger_map.insert(std::make_pair("root", std::move(global_logger)));
    } else if (!iter->second) {  // 存在同名的键，但指针为空
        iter->second = std::make_shared<Logger>();
        iter->second->addAppender(std::make_shared<StdoutLogAppender>());
    }
}

void __LoggerManager::init()
{
    ScopedLock lock(&m_mutex);
    auto config = Config::Lookup<std::vector<LogConfig>>("logs");
    const auto &config_log_list = config->getValue();
    for (const auto &config_log : config_log_list) {
        // 删除已存在的同名的 logger
        m_logger_map.erase(config_log.name);
        auto logger = std::make_shared<Logger>(config_log.name, config_log.level, config_log.pattern);
        for (const auto &config_app : config_log.appenders) {
            LogAppender::ptr appender;
            switch (config_app.type) {
            case LogAppenderConfig::STDOUT:
                appender = std::make_shared<StdoutLogAppender>(config_app.level);
                break;
            case LogAppenderConfig::FILE:
                appender = std::make_shared<FileLogAppender>(config_app.file, config_app.level);
                break;
            default:
                std::cerr << "LoggerManager::init exception 无效的 appender 配置值，appender.type=" << config_app.type
                          << std::endl;
                break;
            }
            // 如果定义了 appender 的日志格式，为其创建专属的 formatter
            // 否则在其加入 logger 时，会被设置为当前Logger的默认格式化器
            if (!config_app.pattern.empty()) {
                appender->setFormatter(std::make_shared<LogFormatter>(config_app.pattern));
            }
            logger->addAppender(std::move(appender));
        }
        std::cout << "成功创建日志器 " << config_log.name << std::endl;
        m_logger_map.insert(std::make_pair(config_log.name, std::move(logger)));
    }
    // 确保存在一个全局的日志器
    ensureRootLoggerExist();
}

Logger::ptr __LoggerManager::getLogger(const std::string &name) const
{
    ScopedLock lock(&m_mutex);  // FIXME 这个锁是可递归的吗，所以下面不能用getLogger("root") ?
    auto iter = m_logger_map.find(name);
    if (iter == m_logger_map.end()) {
        // 日志器不存在就返回全局默认日志器
        // return m_logger_map.find("root")->second;
        // 指定的日志器不存在就抛出异常
        throw Exception(fmt::format("不存在的日志器: {}", name));
    }
    return iter->second;
}

Logger::ptr __LoggerManager::getRootLogger() const { return getLogger("root"); }

}  // namespace meha
