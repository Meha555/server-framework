#ifndef SERVER_FRAMEWORK_LOG_H
#define SERVER_FRAMEWORK_LOG_H

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <list>
#include <memory>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "config.h"
#include "thread.h"
#include "util.h"

#define GET_ROOT_LOGGER() meha::LoggerManager::getInstance()->getRootLogger()
#define GET_LOGGER(name)  meha::LoggerManager::getInstance()->getLogger(name)

// 使用LogEventWrapper的宏
// #define LOG(logger, level)                                                                                             \
//     if (logger->getLevel() >= level)                                                                                   \
//     meha::LogEventWrapper(MAKE_LOG_EVENT(level)).getSS()
#define LOG(logger, level)                                                                                             \
    if (logger->getLevel() >= level)                                                                                   \
    meha::LogEventWrapper(                                                                                             \
        std::make_shared<meha::LogEvent>(                                                                              \
            __FILE__, __LINE__, meha::GetThreadID(), meha::GetFiberID(), meha::LogEvent::LogLevel::level))             \
        .getSS()

// 生成一个LogEvent::ptr的宏
#define MAKE_LOG_EVENT(level, message...)                                                                              \
    std::make_shared<meha::LogEvent>(__FILE__, __LINE__, meha::GetThreadID(), meha::GetFiberID(), level, message)

// 使用C-style API打印日志的宏（使用默认格式）
#define LOG_LEVEL(logger, level, message) logger->log(MAKE_LOG_EVENT(level, message))
#define LOG_DEBUG(logger, message)        LOG_LEVEL(logger, meha::LogEvent::LogLevel::DEBUG, message)
#define LOG_INFO(logger, message)         LOG_LEVEL(logger, meha::LogEvent::LogLevel::INFO, message)
#define LOG_WARN(logger, message)         LOG_LEVEL(logger, meha::LogEvent::LogLevel::WARN, message)
#define LOG_ERROR(logger, message)        LOG_LEVEL(logger, meha::LogEvent::LogLevel::ERROR, message)
#define LOG_FATAL(logger, message)        LOG_LEVEL(logger, meha::LogEvent::LogLevel::FATAL, message)

// 使用C-style API的可设置格式打印日志的宏
#define LOG_FMT_LEVEL(logger, level, format, argv...)                                                                  \
    {                                                                                                                  \
        char *b = nullptr;                                                                                             \
        int l = asprintf(&b, format, argv);                                                                            \
        if (l != -1) {                                                                                                 \
            LOG_LEVEL(logger, level, std::string(b, l));                                                               \
            free(b);                                                                                                   \
        }                                                                                                              \
    }

#define LOG_FMT_DEBUG(logger, format, argv...) LOG_FMT_LEVEL(logger, meha::LogEvent::LogLevel::DEBUG, format, argv)
#define LOG_FMT_INFO(logger, format, argv...)  LOG_FMT_LEVEL(logger, meha::LogEvent::LogLevel::INFO, format, argv)
#define LOG_FMT_WARN(logger, format, argv...)  LOG_FMT_LEVEL(logger, meha::LogEvent::LogLevel::WARN, format, argv)
#define LOG_FMT_ERROR(logger, format, argv...) LOG_FMT_LEVEL(logger, meha::LogEvent::LogLevel::ERROR, format, argv)
#define LOG_FMT_FATAL(logger, format, argv...) LOG_FMT_LEVEL(logger, meha::LogEvent::LogLevel::FATAL, format, argv)

namespace meha {

class LogConfig;
class LogAppenderConfig;

/**
 * @brief 日志事件封装类
 * @details 用于记录日志现场：日志级别，文件名/行号，日志消息，线程/协程号，所属日志器名称等
 */
struct LogEvent
{
    using ptr = std::shared_ptr<LogEvent>;

    struct LogLevel
    {
        enum Level
        {
            UNKNOWN = 0,  // 默认日志等级（未设置日志等级时就是这个）
            DEBUG = 1,
            INFO = 2,
            WARN = 3,
            ERROR = 4,
            FATAL = 5
        };
        // 将日志等级转化为对应的字符串
        static std::string levelToString(const LogLevel::Level level);
    };

    LogEvent(const std::string &file,
             const std::uint32_t line,
             const std::uint32_t tid,
             const std::uint32_t fid,
             const LogLevel::Level level = LogLevel::DEBUG,
             const std::string &content = "")
        : level(level), file(file), line(line), thread_id(tid), fiber_id(fid),
          timestamp(std::chrono::system_clock::now()), m_ss(content)
    {}

    const std::string getContent() const { return m_ss.str(); }

    LogLevel::Level level;                                  // 日志等级
    const std::string file;                                 // 代码所在文件
    const std::uint32_t line;                               // 代码行号
    const std::uint32_t thread_id;                          // 线程ID
    const std::uint32_t fiber_id;                           // 协程ID
    const std::chrono::system_clock::time_point timestamp;  // 当前时间戳
    // std::string content;                              // 日志内容
    std::stringstream m_ss;  // 日志流
};

/**
 * @brief 日志格式化器
 */
class LogFormatter {
public:
    using ptr = std::shared_ptr<LogFormatter>;

    /**
     * @brief 日志内容项基类
     * @details 就是比如%m，%n解析得到的什么行、列、时间、占多少格等这种“元数据”
     */
    struct FormatItemBase
    {
        using ptr = std::shared_ptr<FormatItemBase>;
        // 按照自己的项格式化逻辑，写入项格式化字符串到流
        virtual void format(std::ostream &out, const LogEvent::ptr msg) const = 0;
    };

    explicit LogFormatter(const std::string &pattern);
    // 将LogEvent按照构造时传入的pattern格式化成字符串
    std::string format(LogEvent::ptr msg) const;

private:
    // 基于有限状态机解析日志格式化模板字符串，得到由FormatItemBase子类对象组成的“元数据结构”
    void parse();
    std::string m_pattern;                       // 日志格式化字符串模板
    std::list<FormatItemBase::ptr> m_fmt_items;  // LogEvent按日志格式化字符串模板解析后的得到的日志项列表
};

/**
 * @brief 日志输出器基类
 * @details 负责设置日志输出位置和实际的输出动作
 * LogEvent先经过LogFormatter格式化后再输出到对应的输出地
 */
class LogAppender {
public:
    using ptr = std::shared_ptr<LogAppender>;
    explicit LogAppender(LogEvent::LogLevel::Level level = LogEvent::LogLevel::UNKNOWN);
    virtual ~LogAppender() = default;
    virtual void log(const LogEvent::LogLevel::Level level, const LogEvent::ptr event) = 0;
    // thread-safe 获取格式化器
    LogFormatter::ptr getFormatter() const;
    // thread-safe 设置格式化器
    void setFormatter(const LogFormatter::ptr formatter);

protected:
    LogEvent::LogLevel::Level m_base_level;  // 输出器最低输出日志等级
    LogFormatter::ptr m_formatter;           // 当前输出器使用的格式化器
    mutable Mutex m_mutex;
};

/**
 * @brief 输出到终端的LogAppender
 */
class StdoutLogAppender : public LogAppender {
public:
    using ptr = std::shared_ptr<StdoutLogAppender>;

    explicit StdoutLogAppender(LogEvent::LogLevel::Level level = LogEvent::LogLevel::UNKNOWN);
    // thread-safe
    void log(const LogEvent::LogLevel::Level level, const LogEvent::ptr event) override;
    // 由于输出到标准输出有std::cout，因此无需像输出到文件那样自己维护一个std::ofstream
};

/**
 * @brief 输出到文件的LogAppender
 */
class FileLogAppender : public LogAppender {
public:
    using ptr = std::shared_ptr<FileLogAppender>;

    explicit FileLogAppender(const std::string &filename,
                             LogEvent::LogLevel::Level level = LogEvent::LogLevel::UNKNOWN);
    // thread-safe
    void log(const LogEvent::LogLevel::Level level, const LogEvent::ptr event) override;
    bool openFile();

private:
    std::string m_filename;    // 输出文件名
    std::ofstream m_ofstream;  // 输出流对象
};

/**
 * @brief 日志器（直接由用户使用）
 * @details 合并封装了前几个类的使用，负责（同步）判断日志级别，将高于最低级别的日志用LogAppender输出器输出指定的位置
 */
class Logger {
public:
    using ptr = std::shared_ptr<Logger>;

    Logger();
    Logger(const std::string &name, const LogEvent::LogLevel::Level lowest_level, const std::string &pattern);
    // thread-safe 输出日志
    void log(const LogEvent::ptr event);
    // C-style API
    void debug(const LogEvent::ptr event);
    void info(const LogEvent::ptr event);
    void warn(const LogEvent::ptr event);
    void error(const LogEvent::ptr event);
    void fatal(const LogEvent::ptr event);

    // thread-safe 增加输出器
    void addAppender(const LogAppender::ptr appender);
    // thread-safe 删除输出器
    void delAppender(const LogAppender::ptr appender);

    LogEvent::LogLevel::Level getLevel() const { return m_base_level; }
    void setLevel(LogEvent::LogLevel::Level level) { m_base_level = level; }

private:
    const std::string m_name;                 // 日志器名称
    LogEvent::LogLevel::Level m_base_level;   // 日志最低输出级别
    std::string m_pattern;                    // 日志格式化器的默认pattern
    LogFormatter::ptr m_default_formatter;    // 日志默认格式化器，当加入 m_appenders
                                              // 的 appender 没有自己 pattern
                                              // 时，使用该 Logger 默认的的 pattern
    std::list<LogAppender::ptr> m_appenders;  // Appender列表
    mutable Mutex m_mutex;
};

/**
 * @brief 日志器管理器单例类
 * @details 用于统一管理所有的Logger，提供日志器的创建与获取方法
 * 自带一个root Logger作为日志模块初始可用的日志器
 */
class __LoggerManager {
    friend class LogIniter;

public:
    using ptr = std::shared_ptr<__LoggerManager>;

    explicit __LoggerManager();
    // 传入日志器名称来获取指定的日志器。如果不存在则返回全局日志器
    Logger::ptr getLogger(const std::string &name) const;
    Logger::ptr getRootLogger() const;

private:
    void init();
    void ensureRootLoggerExist();
    std::unordered_map<std::string, Logger::ptr> m_logger_map;
    mutable Mutex m_mutex;
};

/**
 * @brief __LoggerManager 的单例类
 */
using LoggerManager = SingletonPtr<__LoggerManager>;

struct LogIniter
{
    LogIniter()
    {
        auto log_config_list = meha::Config::Lookup<std::vector<LogConfig>>("logs", {}, "日志器的配置项");
        // 注册日志器配置项变更时的事件处理回调：当配置项变动时，更新日志器
        log_config_list->addListener([](const std::vector<LogConfig> &, const std::vector<LogConfig> &) {
            std::cout << "日志器配置变动，更新日志器" << std::endl;
            LoggerManager::getInstance()->init();
        });
    }
};
static LogIniter __log_init__;

/**
 * @brief 日志器本身的配置信息类
 * @details 默认UNKNOWN等级
 */
struct LogConfig
{
    LogConfig() : level(LogEvent::LogLevel::UNKNOWN) {}
    bool operator==(const LogConfig &rhs) const { return name == rhs.name; }

    std::string name;                        // 日志器名称
    LogEvent::LogLevel::Level level;         // 日志器日志等级
    std::string pattern;                     // 日志器日志格式
    std::list<LogAppenderConfig> appenders;  // 日志器绑定的当前输出器的配置
};

/**
 * @brief 日志输出器Appender的配置信息类
 * @details 默认标准输出，UNKNOWN级别
 */
struct LogAppenderConfig
{
    enum Type
    {
        STDOUT = 0,
        FILE = 1
    };
    LogAppenderConfig()
        : type(Type::STDOUT), level(LogEvent::LogLevel::UNKNOWN)
    {}  // REVIEW 这里枚举类的使用有点坑，如果LogLevel是放在全局命名空间则情况就不一样了
    bool operator==(const LogAppenderConfig &rhs) const
    {
        return type == rhs.type && level == rhs.level && pattern == rhs.pattern && file == rhs.file;
    }

    LogAppenderConfig::Type type;     // 输出器类型
    LogEvent::LogLevel::Level level;  // 输入器日志等级
    std::string pattern;              // 输出器日志格式
    std::string file;                 // 输出器目标文件路径
};

/**
 * @brief 日志事件RAII风格包装类
 * @details
 * 在日志现场的全表达式构造，包装了日志器和日志事件两个对象来组合为匿名对象。在日志记录结束后，LogEventWrapper析构时，调用日志器的log方法输出日志事件。
 */
class LogEventWrapper final {
public:
    LogEventWrapper(Logger::ptr logger, const LogEvent::ptr event) : m_logger(logger), m_event(event) {}
    ~LogEventWrapper() { m_logger->log(m_event); }

    LogEvent::ptr getEvent() const { return m_event; }
    // 获取输出日志流
    std::stringstream &getSS() { return m_event->m_ss; }

private:
    Logger::ptr m_logger;   // 使用的日志器
    LogEvent::ptr m_event;  // 要打印的日志事件
};

// TODO 日志配置文件分析
/**
 * @brief LexicalCast 的偏特化
 */
template <>
class LexicalCast<std::string, std::vector<LogConfig>> {
public:
    std::vector<LogConfig> operator()(const std::string &source)
    {
        auto node = YAML::Load(source);
        std::vector<LogConfig> result{};
        if (node.IsSequence()) {
            for (const auto log_config : node) {
                LogConfig lc{};
                lc.name = log_config["name"] ? log_config["name"].as<std::string>() : "";
                lc.level = log_config["level"] ? (LogEvent::LogLevel::Level)(log_config["level"].as<int>())
                                               : LogEvent::LogLevel::UNKNOWN;
                lc.pattern = log_config["pattern"] ? log_config["pattern"].as<std::string>() : "";
                if (log_config["appender"] && log_config["appender"].IsSequence()) {
                    for (const auto app_config : log_config["appender"]) {
                        LogAppenderConfig ac{};
                        ac.type = (LogAppenderConfig::Type)(app_config["type"] ? app_config["type"].as<int>() : 0);
                        ac.file = app_config["file"] ? app_config["file"].as<std::string>() : "";
                        ac.level =
                            (LogEvent::LogLevel::Level)(app_config["level"] ? app_config["level"].as<int>() : lc.level);
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
class LexicalCast<std::vector<LogConfig>, std::string> {
public:
    std::string operator()(const std::vector<LogConfig> &source)
    {
        YAML::Node node;
        for (const auto &log_config : source) {
            node["name"] = log_config.name;
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

#endif  // SERVER_FRAMEWORK_LOG_H
