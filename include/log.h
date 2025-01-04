#pragma once

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <ostream>
#include <sstream>
#include <string>
#include <unordered_map>

#include "mutex.hpp"
#include "singleton.h"
#include "utils.h" // 为了引入 utils namespace

#define GET_ROOT_LOGGER() meha::LoggerManager::Instance()->getRootLogger()
#define GET_LOGGER(category) \
    meha::LoggerManager::Instance()->getLogger(category)

// 生成一个LogMessage::ptr的宏
#define MAKE_LOG_MSG(category, level, message)                                  \
    std::make_shared<meha::LogMessage>(__FILE__, __LINE__, __PRETTY_FUNCTION__, \
                                       meha::utils::GetThreadID(),              \
                                       meha::utils::GetFiberID(), category,     \
                                       meha::LogMessage::LogLevel::level, message)

// 使用LogMessageWrapper的宏
#define LOG(logger, level)                                                                                                                                                                                    \
    if (logger->getLevel() <= meha::LogMessage::LogLevel::level)                                                                                                                                              \
    meha::LogMessageWrapper(                                                                                                                                                                                  \
        logger, std::make_shared<meha::LogMessage>(__FILE__, __LINE__, __PRETTY_FUNCTION__, meha::utils::GetThreadID(), meha::utils::GetFiberID(), logger->getCategory(), meha::LogMessage::LogLevel::level)) \
        .getSS()

// 使用C-style API打印日志的宏（使用默认格式）
#define LOG_LEVEL(logger, level, message) \
    logger->log(MAKE_LOG_MSG(logger->getCategory(), level, message))
#define LOG_TRACE(logger, message) LOG_LEVEL(logger, TRACE, message)
#define LOG_DEBUG(logger, message) LOG_LEVEL(logger, DEBUG, message)
#define LOG_INFO(logger, message) LOG_LEVEL(logger, INFO, message)
#define LOG_WARN(logger, message) LOG_LEVEL(logger, WARN, message)
#define LOG_ERROR(logger, message) LOG_LEVEL(logger, ERROR, message)
#define LOG_FATAL(logger, message) LOG_LEVEL(logger, FATAL, message)

// 使用C-style API的可设置格式打印日志的宏
#define LOG_FMT_LEVEL(logger, level, format, argv...)    \
    {                                                    \
        char *b = nullptr;                               \
        int l = asprintf(&b, format, argv);              \
        if (l != -1) {                                   \
            LOG_LEVEL(logger, level, std::string(b, l)); \
            free(b);                                     \
        }                                                \
    }

#define LOG_FMT_TRACE(logger, format, argv...) \
    LOG_FMT_LEVEL(logger, TRACE, format, argv)
#define LOG_FMT_DEBUG(logger, format, argv...) \
    LOG_FMT_LEVEL(logger, DEBUG, format, argv)
#define LOG_FMT_INFO(logger, format, argv...) \
    LOG_FMT_LEVEL(logger, INFO, format, argv)
#define LOG_FMT_WARN(logger, format, argv...) \
    LOG_FMT_LEVEL(logger, WARN, format, argv)
#define LOG_FMT_ERROR(logger, format, argv...) \
    LOG_FMT_LEVEL(logger, ERROR, format, argv)
#define LOG_FMT_FATAL(logger, format, argv...) \
    LOG_FMT_LEVEL(logger, FATAL, format, argv)

namespace meha
{

class LogConfig;
class LogAppenderConfig;

/**
 * @brief 日志消息封装类
 * @details
 * 用于记录日志现场：日志级别，文件名/行号，日志消息，线程/协程号，所属日志器名称等
 */
struct LogMessage
{
    using sptr = std::shared_ptr<LogMessage>;

    struct LogLevel
    {
        enum Level {
            UNKNOWN = 0, // 默认日志等级（未设置日志等级时就是这个）
            TRACE,
            DEBUG,
            INFO,
            WARN,
            ERROR,
            FATAL
        };
        // 将日志等级转化为对应的字符串
        static std::string levelToString(const LogLevel::Level level);
    };

    LogMessage(const std::string &file, const uint32_t line,
               const std::string &func, const uint32_t tid, const uint32_t fid,
               const std::string &category,
               const LogLevel::Level level = LogLevel::DEBUG,
               const std::string &content = "")
        : category(category)
        , level(level)
        , file(file)
        , line(line)
        , function(func)
        , tid(tid)
        , fid(fid)
        , timestamp(std::chrono::system_clock::now())
        , ss(content)
    {
    }

    const std::string message() const
    {
        return ss.str();
    }

    const std::string category; // 日志器分类
    LogLevel::Level level; // 日志等级
    const std::string file; // 文件名
    const std::string function; // 函数名
    const uint32_t line; // 行号
    const uint32_t tid; // 线程ID
    const uint32_t fid; // 协程ID
    const std::chrono::system_clock::time_point timestamp; // 当前时间戳
    std::stringstream ss; // 日志流
};

/**
 * @brief 日志格式化器
 */
class LogFormatter
{
public:
    using sptr = std::shared_ptr<LogFormatter>;

    /**
     * @brief 日志内容项基类
     * @details 就是比如%m，%n解析得到的什么行、列、时间、占多少格等这种“元数据”
     */
    struct FormatItemBase
    {
        using sptr = std::shared_ptr<FormatItemBase>;
        // 按照自己的项格式化逻辑，写入项格式化字符串到流
        virtual void format(std::ostream &out, const LogMessage::sptr msg) const = 0;
    };

    explicit LogFormatter(const std::string &pattern);
    // 将LogMessage按照构造时传入的pattern格式化成字符串
    std::string format(LogMessage::sptr msg) const;

private:
    // 基于有限状态机解析日志格式化模板字符串，得到由FormatItemBase子类对象组成的“元数据结构”
    void parse();
    std::string m_pattern; // 日志格式化字符串模板
    std::vector<FormatItemBase::sptr> m_fmtItems; // LogMessage按日志格式化字符串模板解析后的得到的日志项列表
};

extern const LogFormatter::sptr s_defaultLogFormatter;

/**
 * @brief 日志输出器基类
 * @note 可能是单例，需要保证线程安全
 * @details 负责设置日志输出位置和实际的输出动作
 * LogMessage先经过LogFormatter格式化后再输出到对应的输出地
 */
class LogAppender
{
public:
    using sptr = std::shared_ptr<LogAppender>;
    explicit LogAppender(LogMessage::LogLevel::Level level = LogMessage::LogLevel::UNKNOWN);
    virtual ~LogAppender() = default;
    // thread-safe 打印日志
    virtual void sink(const LogMessage::LogLevel::Level level, const LogMessage::sptr msg) = 0;
    // thread-safe 获取格式化器
    LogFormatter::sptr getFormatter() const;
    // thread-safe 设置格式化器
    void setFormatter(const LogFormatter::sptr formatter);

protected:
    LogMessage::LogLevel::Level m_baseLevel; // 输出器最低输出日志等级
    LogFormatter::sptr m_formatter; // 当前输出器使用的格式化器
    mutable Mutex m_mutex;
};

/**
 * @brief 输出到终端的LogAppender
 */
class StdoutLogAppender : public LogAppender
{
public:
    using sptr = std::shared_ptr<StdoutLogAppender>;

    explicit StdoutLogAppender(LogMessage::LogLevel::Level level = LogMessage::LogLevel::UNKNOWN);
    // thread-safe
    void sink(const LogMessage::LogLevel::Level level, const LogMessage::sptr msg) override;

private:
    static inline bool IsColorSupported();
    static bool s_hasColor;
    static std::once_flag s_onceFlag;
};

/**
 * @brief 输出到文件的LogAppender
 * TODO 添加c++17 filesystem和fd的支持
 */
class FileLogAppender : public LogAppender
{
public:
    using sptr = std::shared_ptr<FileLogAppender>;

    explicit FileLogAppender(const std::string &filename, LogMessage::LogLevel::Level level = LogMessage::LogLevel::UNKNOWN);
    // thread-safe
    void sink(const LogMessage::LogLevel::Level level, const LogMessage::sptr msg) override;
    bool openFile();

private:
    std::string m_filename; // 输出文件名
    std::ofstream m_ofstream; // 输出流对象
};

/**
 * @brief 日志器（直接由用户使用）
 * @details
 * 合并封装了前几个类的使用，负责（同步）判断日志级别，将高于最低级别的日志用LogAppender输出器输出指定的位置
 */
class Logger
{
public:
    using sptr = std::shared_ptr<Logger>;

    explicit Logger();
    Logger(const std::string &category, const LogMessage::LogLevel::Level lowest_level, const std::string &pattern);

    // thread-safe 输出日志
    void log(const LogMessage::sptr msg);
    // C-style API
    void trace(const LogMessage::sptr msg);
    void debug(const LogMessage::sptr msg);
    void info(const LogMessage::sptr msg);
    void warn(const LogMessage::sptr msg);
    void error(const LogMessage::sptr msg);
    void fatal(const LogMessage::sptr msg);

    // thread-safe 增加输出器
    void addAppender(const LogAppender::sptr appender);
    // thread-safe 删除输出器
    void delAppender(const LogAppender::sptr appender);

    std::string getCategory() const
    {
        return m_category;
    }
    void setCategory(const std::string &category)
    {
        m_category = category;
    }
    LogMessage::LogLevel::Level getLevel() const
    {
        return m_baseLevel;
    }
    void setLevel(const LogMessage::LogLevel::Level level)
    {
        m_baseLevel = level;
    }

private:
    std::string m_category; // 日志器类别
    LogMessage::LogLevel::Level m_baseLevel; // 日志最低输出级别
    std::string m_pattern; // 日志格式化器的默认pattern
    LogFormatter::sptr m_defaultFormatter; // 日志默认格式化器，当加入 m_appenders 的 appender 没有自己 pattern 时，使用该 Logger 默认的的 pattern
    std::vector<LogAppender::sptr> m_appenders; // Appender列表
    mutable Mutex m_mutex;
};

/**
 * @brief 日志器管理器单例类
 * @details 用于统一管理所有的Logger，提供日志器的创建与获取方法
 * 自带一个root Logger作为日志模块初始可用的日志器
 */
class __LoggerManager
{
    friend class _LogIniter;

public:
    using sptr = std::shared_ptr<__LoggerManager>;

    explicit __LoggerManager();
    // 传入日志器名称来获取指定的日志器。如果不存在则返回全局日志器
    Logger::sptr getLogger(const std::string &category) const;
    Logger::sptr getRootLogger() const;

private:
    // 根据配置文件来创建日志器
    void init();
    void ensureRootLoggerExist();
    std::unordered_map<std::string, Logger::sptr> m_loggerMap;
    mutable Mutex m_mutex;
};

using LoggerManager = SingletonPtr<__LoggerManager>;

/**
 * @brief 日志器本身的配置信息类
 * @details 默认UNKNOWN等级
 */
struct LogConfig
{
    LogConfig()
        : level(LogMessage::LogLevel::UNKNOWN)
    {
    }
    bool operator==(const LogConfig &rhs) const
    {
        return category == rhs.category;
    }

    std::string category; // 日志器分类
    LogMessage::LogLevel::Level level; // 日志器日志等级
    std::string pattern; // 日志器日志格式
    std::vector<LogAppenderConfig> appenders; // 日志器绑定的当前输出器的配置
};

/**
 * @brief 日志输出器Appender的配置信息类
 * @details 默认标准输出，UNKNOWN级别
 */
struct LogAppenderConfig
{
    enum Type {
        STDOUT = 0,
        FILE = 1
    };
    LogAppenderConfig()
        : type(Type::STDOUT)
        , level(LogMessage::LogLevel::UNKNOWN)
    {
    }

    bool operator==(const LogAppenderConfig &rhs) const
    {
        return type == rhs.type && level == rhs.level && pattern == rhs.pattern && file == rhs.file;
    }

    LogAppenderConfig::Type type; // 输出器类型
    LogMessage::LogLevel::Level level; // 输入器日志等级
    std::string pattern; // 输出器日志格式
    std::string file; // 输出器目标文件路径
};

/**
 * @brief 日志事件RAII风格包装类
 * @details 在日志现场的全表达式构造，包装了日志器和日志事件两个对象来组合为匿名对象。在日志记录结束后，LogMessageWrapper析构时，调用日志器的log方法输出日志事件。
 */
class LogMessageWrapper final
{
public:
    LogMessageWrapper(Logger::sptr logger, const LogMessage::sptr msg)
        : m_logger(logger)
        , m_message(msg)
    {
    }
    ~LogMessageWrapper()
    {
        m_logger->log(m_message);
    }

    LogMessage::sptr getEvent() const
    {
        return m_message;
    }
    // 获取输出日志流
    std::stringstream &getSS()
    {
        return m_message->ss;
    }

private:
    Logger::sptr m_logger; // 使用的日志器
    LogMessage::sptr m_message; // 要打印的日志消息
};

struct _LogIniter
{
    explicit _LogIniter();
};

// REVIEW 未解之谜：为什么LogInter的静态全局对象必须放在log.h中，如果放在log.cc中，则test_thread就会段错误？
static _LogIniter _;

// extern template struct lexical_cast<std::string, std::vector<LogConfig>>;
// extern template struct lexical_cast<std::vector<LogConfig>, std::string>;

} // namespace meha
