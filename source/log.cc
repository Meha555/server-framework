#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <ostream>
#include <sstream>

#include <fmt/format.h>

#include "config.hpp"
#include "exception.h"
#include "log.h"
#include "util.h"

namespace meha {

// 普通文本项（就是日志字符串中出现的非content、非格式控制符的字符）
struct PlainFormatItem : public LogFormatter::FormatItemBase
{
    explicit PlainFormatItem(const std::string &str) : m_str(str) {}
    void format(std::ostream &out, const LogEvent::ptr event) const override { out << m_str; }

private:
    std::string m_str;
};

// 日志级别项
struct LevelFormatItem : public LogFormatter::FormatItemBase
{
    void format(std::ostream &out, const LogEvent::ptr event) const override
    {
        out << std::left << std::setw(5) << LogEvent::LogLevel::levelToString(event->level);
    }
};

// 日志类别项
struct CategoryFormatItem : public LogFormatter::FormatItemBase
{
    void format(std::ostream &out, const LogEvent::ptr event) const override
    {
        out << std::left << std::setw(6) << event->category;
    }
};

// 文件名项
struct FilenameFormatItem : public LogFormatter::FormatItemBase
{
    void format(std::ostream &out, const LogEvent::ptr event) const override { out << event->file; }
};

// 行号项
struct LineFormatItem : public LogFormatter::FormatItemBase
{
public:
    void format(std::ostream &out, const LogEvent::ptr event) const override { out << event->line; }
};

// 函数名项
struct FunctionFormatItem : public LogFormatter::FormatItemBase
{
    void format(std::ostream &out, const LogEvent::ptr event) const override { out << event->function; }
};

//线程号项
struct ThreadIDFormatItem : public LogFormatter::FormatItemBase
{
public:
    void format(std::ostream &out, const LogEvent::ptr event) const override { out << event->thread_id; }
};

// 协程号项
struct FiberIDFormatItem : public LogFormatter::FormatItemBase
{
    void format(std::ostream &out, const LogEvent::ptr event) const override { out << event->fiber_id; }
};

// 时间戳项
struct DateTimeFormatItem : public LogFormatter::FormatItemBase
{
    explicit DateTimeFormatItem(const std::string &str = "%Y-%m-%d %H:%M:%S") : m_time_pattern(str)
    {
        if (m_time_pattern.empty()) {
            m_time_pattern = "%Y-%m-%d %H:%M:%S";
        }
    }
    void format(std::ostream &out, const LogEvent::ptr event) const override
    {
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

// 累计毫秒数项
struct ElapseFormatItem : public LogFormatter::FormatItemBase
{
    void format(std::ostream &out, const LogEvent::ptr event) const override { out << GetCurrentMS().count(); }
};

// 日志内容项
struct ContentFormatItem : public LogFormatter::FormatItemBase
{
    void format(std::ostream &out, const LogEvent::ptr event) const override { out << event->getContent(); }
};

// 换行符项
struct NewLineFormatItem : public LogFormatter::FormatItemBase
{
    void format(std::ostream &out, const LogEvent::ptr event) const override { out << '\n'; }
};

// 制表符项
class TabFormatItem : public LogFormatter::FormatItemBase {
    void format(std::ostream &out, const LogEvent::ptr event) const override { out << '\t'; }
};

// '%'项
struct PercentSignFormatItem : public LogFormatter::FormatItemBase
{
    void format(std::ostream &out, const LogEvent::ptr event) const override { out << '%'; }
};

// 日志模板格式控制符对应的日志项实现类
thread_local static const std::unordered_map<char, LogFormatter::FormatItemBase::ptr> g_format_item_map{ //REVIEW 为什么要加thread_local才能正确初始化？（试试test_fiber）
#define __FN(ch, item) {ch, std::make_shared<item>()}
    __FN('p', LevelFormatItem),        //日志等级
    __FN('c', CategoryFormatItem),     //日志分类
    __FN('f', FilenameFormatItem),     //文件名
    __FN('l', LineFormatItem),         //行号
    __FN('C', FunctionFormatItem),     //函数名
    __FN('d', DateTimeFormatItem),     //时间
    __FN('r', ElapseFormatItem),       //累计毫秒数
    __FN('t', ThreadIDFormatItem),     //线程号
    __FN('F', FiberIDFormatItem),      //协程号
    __FN('m', ContentFormatItem),      //内容
    __FN('n', NewLineFormatItem),      //换行符
    __FN('%', PercentSignFormatItem),  //百分号
    __FN('T', TabFormatItem),          //制表符
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
    : m_category("default"),
      m_base_level(LogEvent::LogLevel::UNKNOWN),
      m_pattern("%d%T[%c] [%p] (T:%t F:%F) %f:%l%T%m%n")
{
    m_default_formatter.reset(new LogFormatter(m_pattern));
}

Logger::Logger(const std::string &category, const LogEvent::LogLevel::Level lowest_level, const std::string &pattern)
    : m_category(category),
      m_base_level(lowest_level),
      m_pattern(pattern)
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
    static const char *color = nullptr;
    switch (level) {
    case LogEvent::LogLevel::DEBUG:
        color = "\033[0;34m";
        break;
    case LogEvent::LogLevel::INFO:
        color = "\033[0;32m";
        break;
    case LogEvent::LogLevel::WARN:
        color = "\033[0;33m";
        break;
    case LogEvent::LogLevel::ERROR:
        color = "\033[0;31m";
        break;
    case LogEvent::LogLevel::FATAL:
        color = "\033[1;41;33m";
        break;
    default:
        color = "\033[0m";
    }
    std::cout << fmt::format("{}{}\033[0m", color, m_formatter->format(event));
    std::cout.flush();
}

LogAppender::LogAppender(LogEvent::LogLevel::Level level) : m_base_level(level) {}

FileLogAppender::FileLogAppender(const std::string &filename, LogEvent::LogLevel::Level level)
    : LogAppender(level),
      m_filename(filename)
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
        // 删除已存在的类别的 logger
        m_logger_map.erase(config_log.category);
        auto logger = std::make_shared<Logger>(config_log.category, config_log.level, config_log.pattern);
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
                LOG_FMT_ERROR(GET_ROOT_LOGGER(),
                              "LoggerManager::init exception 无效的 appender 配置值，appender.type= %s",
                              config_app.type ? "FILE" : "STDOUT");
                break;
            }
            // 如果定义了 appender 的日志格式，为其创建专属的 formatter
            // 否则在其加入 logger 时，会被设置为当前Logger的默认格式化器
            if (!config_app.pattern.empty()) {
                appender->setFormatter(std::make_shared<LogFormatter>(config_app.pattern));
            }
            logger->addAppender(std::move(appender));
        }
        std::cout << "成功创建日志器 " << config_log.category << std::endl;
        m_logger_map.insert(std::make_pair(config_log.category, std::move(logger)));
    }
    // 确保存在一个全局的日志器
    ensureRootLoggerExist();
}

Logger::ptr __LoggerManager::getLogger(const std::string &category) const
{
    ScopedLock lock(&m_mutex);  // FIXME 这个锁是可递归的吗，所以下面不能用getLogger("root") ?
    auto iter = m_logger_map.find(category);
    if (iter == m_logger_map.end()) {
        // 日志器不存在就返回全局默认日志器
        // return m_logger_map.find("root")->second;
        // 指定的日志器不存在就抛出异常
        throw Exception(fmt::format("不存在的日志器: {}", category));
    }
    return iter->second;
}

Logger::ptr __LoggerManager::getRootLogger() const { return getLogger("root"); }

LogIniter::LogIniter()
{
    auto log_config_list = meha::Config::Lookup<std::vector<LogConfig>>("logs", {}, "日志器的配置项");
    // 注册日志器配置项变更时的事件处理回调：当配置项变动时，更新日志器
    log_config_list->addListener([](const std::vector<LogConfig> &, const std::vector<LogConfig> &) {
        std::cout << "日志器配置变动，更新日志器" << std::endl;
        LoggerManager::GetInstance()->init();
    });
}

/**
 * @brief lexical_cast 的偏特化
 */
template <>
struct lexical_cast<std::string, std::vector<LogConfig>>
{
    std::vector<LogConfig> operator()(const std::string &source)
    {
        auto node = YAML::Load(source);
        std::vector<LogConfig> result{};
        if (node.IsSequence()) {
            for (const auto log_config : node) {
                LogConfig lc{};
                lc.category = log_config["category"] ? log_config["category"].as<std::string>() : "";
                lc.level = log_config["level"] ? (LogEvent::LogLevel::Level)(log_config["level"].as<int>())
                                               : LogEvent::LogLevel::UNKNOWN;
                lc.pattern = log_config["pattern"] ? log_config["pattern"].as<std::string>() : "";  // 日志器默认格式
                if (log_config["appender"] && log_config["appender"].IsSequence()) {
                    for (const auto app_config : log_config["appender"]) {
                        LogAppenderConfig ac{};
                        ac.type = static_cast<LogAppenderConfig::Type>(
                            (app_config["type"] ? app_config["type"].as<int>() : 0));
                        ac.file = app_config["file"] ? app_config["file"].as<std::string>() : "";
                        ac.level = static_cast<LogEvent::LogLevel::Level>(
                            app_config["level"] ? app_config["level"].as<int>() : lc.level);
                        ac.pattern = app_config["pattern"] ? app_config["pattern"].as<std::string>() : lc.pattern;
                        lc.appenders.push_back(ac);
                    }
                }
                result.push_back(lc);
            }
        }
        return result;
    }
};

template <>
struct lexical_cast<std::vector<LogConfig>, std::string>
{
    std::string operator()(const std::vector<LogConfig> &source)
    {
        YAML::Node node;
        for (const auto &log_config : source) {
            node["category"] = log_config.category;
            node["level"] = (int)(log_config.level);
            node["pattern"] = log_config.pattern;
            YAML::Node app_list_node;
            for (const auto &app_config : log_config.appenders) {
                YAML::Node app_node;
                app_node["type"] = (int)(app_config.type);
                app_node["file"] = app_config.file;
                app_node["level"] = (int)(app_config.level);
                app_node["pattern"] = app_config.pattern;
                app_list_node.push_back(app_node);
            }
            node["appender"] = app_list_node;
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

}  // namespace meha
