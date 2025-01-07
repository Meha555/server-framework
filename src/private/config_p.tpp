/* ---------------------------------------------------------------------------- */
/*                      YAML格式字符串到其他类型的相互转换仿函数                      */
/* ---------------------------------------------------------------------------- */

namespace meha::utils
{

/**
 * @brief YAML格式字符串到其他类型的转换仿函数
 * @details meha::lexical_cast 的偏特化，std::string -> std::vector<T>
 */
template<typename T>
struct lexical_cast<std::string, std::vector<T>>
{
    using source_type = std::string;
    using target_type = std::vector<T>;
    target_type operator()(const source_type &source) const
    {
        std::vector<T> config_list;
        // 调用 YAML::Load 解析传入的字符串，解析失败会抛出异常
        YAML::Node node = YAML::Load(source);
        // 检查解析后的 node 是否是一个序列型 YAML::Node
        if (node.IsSequence()) {
            std::stringstream ss;
            for (const auto &item : node) { // DFS暴搜，对node中的所有子结点进行转换
                ss.str(""); // 清空ss
                // 利用 YAML::Node 实现的 operator<<() 将 node 转换为字符串
                ss << item;
                // 此时递归解析node的子结点，直到node为基本类型开始回溯
                config_list.push_back(from_string_to_type_cast<T>()(ss.str()));
            }
        } else {
            throw boost::bad_lexical_cast(typeid(source_type), typeid(target_type));
        }
        return config_list;
    }
};
template<typename T>
using from_string_to_vector_cast = lexical_cast<std::string, std::vector<T>>;

/**
 * @brief YAML格式字符串到其他类型的转换仿函数
 * @details meha::lexical_cast 的偏特化，std::vectror<T> -> std::string
 */
template<typename T>
struct lexical_cast<std::vector<T>, std::string>
{
    using source_type = std::vector<T>;
    using target_type = std::string;
    target_type operator()(const source_type &source) const
    {
        // 构造一个 node
        YAML::Node node;
        // DFS暴搜，将 T 解析成字符串，再解析回 YAML::Node 插入 node 的尾部
        for (const auto &item : source) {
            // 调用 meha::lexical_cast 特化版本递归解析，直到 T 为基本类型
            node.push_back(YAML::Load(from_type_to_string_cast<T>()(item)));
        }
        // 最后通过 std::stringstream 与调用 yaml-cpp 库实现的 operator<<() 将 node 转换为字符串
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};
template<typename T>
using from_vector_to_string_cast = lexical_cast<std::vector<T>, std::string>;

/**
 * @brief YAML格式字符串到其他类型的转换仿函数
 * @details meha::lexical_cast 的偏特化，std::string -> std::list<T>
 */
template<typename T>
struct lexical_cast<std::string, std::list<T>>
{
    using source_type = std::string;
    using target_type = std::list<T>;
    target_type operator()(const source_type &source) const
    {
        YAML::Node node = YAML::Load(source);
        std::list<T> config_list;
        if (node.IsSequence()) {
            std::stringstream ss;
            for (const auto &item : node) {
                ss.str("");
                ss << item;
                config_list.emplace_back(from_string_to_type_cast<T>()(ss.str()));
            }
        } else {
            throw boost::bad_lexical_cast(typeid(source_type), typeid(target_type));
        }
        return config_list;
    }
};
template<typename T>
using from_string_to_list_cast = lexical_cast<std::string, std::list<T>>;

/**
 * @brief YAML格式字符串到其他类型的转换仿函数
 * @details meha::lexical_cast 的偏特化，std::list<T>-> std::string
 */
template<typename T>
struct lexical_cast<std::list<T>, std::string>
{
    using source_type = std::list<T>;
    using target_type = std::string;
    target_type operator()(const source_type &source) const
    {
        YAML::Node node;
        for (const auto &item : source) {
            node.push_back(YAML::Load(from_type_to_string_cast<T>()(item)));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};
template<typename T>
using from_list_to_string_cast = lexical_cast<std::list<T>, std::string>;

/**
 * @brief YAML格式字符串到其他类型的转换仿函数
 * @details meha::lexical_cast 的偏特化，std::string -> std::map<std::string, T>
 */
template<typename T>
struct lexical_cast<std::string, std::map<std::string, T>>
{
    using source_type = std::string;
    using target_type = std::map<std::string, T>;
    target_type operator()(const source_type &source) const
    {
        YAML::Node node = YAML::Load(source);
        std::map<std::string, T> config_map;
        if (node.IsMap()) {
            std::stringstream ss;
            for (const auto &item : node) {
                ss.str("");
                ss << item.second;
                config_map[item.first.as<std::string>()] = from_string_to_type_cast<T>()(ss.str());
            }
        } else {
            throw boost::bad_lexical_cast(typeid(source_type), typeid(target_type));
        }
        return config_map;
    }
};
template<typename T>
using from_string_to_map_cast = lexical_cast<std::string, std::map<std::string, T>>;

/**
 * @brief YAML格式字符串到其他类型的转换仿函数
 * @details meha::lexical_cast 的偏特化，std::map<std::string, T> -> std::string
 */
template<typename T>
struct lexical_cast<std::map<std::string, T>, std::string>
{
    using source_type = std::map<std::string, T>;
    using target_type = std::string;
    target_type operator()(const source_type &source) const
    {
        YAML::Node node;
        for (const auto &[k, v] : source) {
            node[k] = YAML::Load(from_type_to_string_cast<T>()(v));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};
template<typename T>
using from_map_to_string_cast = lexical_cast<std::map<std::string, T>, std::string>;

/**
 * @brief YAML格式字符串到其他类型的转换仿函数
 * @details meha::lexical_cast 的偏特化，std::string -> std::set<T>
 */
template<typename T>
struct lexical_cast<std::string, std::set<T>>
{
    using source_type = std::string;
    using target_type = std::set<T>;
    target_type operator()(const source_type &source) const
    {
        YAML::Node node;
        node = YAML::Load(source);
        std::set<T> config_set;
        if (node.IsSequence()) {
            std::stringstream ss;
            for (const auto &item : node) {
                ss.str("");
                ss << item;
                config_set.insert(from_string_to_type_cast<T>()(ss.str()));
            }
        } else {
            throw boost::bad_lexical_cast(typeid(source_type), typeid(target_type));
        }
        return config_set;
    }
};
template<typename T>
using from_string_to_set_cast = lexical_cast<std::string, std::set<T>>;

/**
 * @brief YAML格式字符串到其他类型的转换仿函数
 * @details meha::lexical_cast 的偏特化，std::set<T> -> std::string
 */
template<typename T>
struct lexical_cast<std::set<T>, std::string>
{
    using source_type = std::set<T>;
    using target_type = std::string;
    target_type operator()(const source_type &source) const
    {
        YAML::Node node;
        for (const auto &item : source) {
            node.push_back(YAML::Load(from_type_to_string_cast<T>()(item)));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};
template<typename T>
using from_set_to_string_cast = lexical_cast<std::set<T>, std::string>;

/**
 * @brief lexical_cast 为 LogConfig 的偏特化
 */
template<>
struct lexical_cast<std::string, LogConfigs>
{
    LogConfigs operator()(const std::string &source) const
    {
        auto node = YAML::Load(source);
        LogConfigs result{};
        if (node.IsSequence()) {
            for (const auto &log_config : node) {
                LogConfig lc{};
                lc.category = log_config["category"] ? log_config["category"].as<std::string>() : "";
                lc.level = log_config["level"] ? static_cast<LogMessage::LogLevel::Level>(log_config["level"].as<int>()) : LogMessage::LogLevel::UNKNOWN;
                lc.pattern = log_config["pattern"] ? log_config["pattern"].as<std::string>() : ""; // 日志器默认格式
                if (log_config["appender"] && log_config["appender"].IsSequence()) {
                    for (const auto &app_config : log_config["appender"]) {
                        LogAppenderConfig ac{};
                        ac.type = static_cast<LogAppenderConfig::Type>(app_config["type"] ? app_config["type"].as<int>() : 0);
                        ac.file = app_config["file"] ? app_config["file"].as<std::string>() : "";
                        ac.level = static_cast<LogMessage::LogLevel::Level>(app_config["level"] ? app_config["level"].as<int>() : lc.level);
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

template<>
struct lexical_cast<LogConfigs, std::string>
{
    std::string operator()(const LogConfigs &source) const
    {
        YAML::Node node;
        for (const auto &log_config : source) {
            node["category"] = log_config.category;
            node["level"] = static_cast<int>(log_config.level);
            node["pattern"] = log_config.pattern;
            YAML::Node app_list_node;
            for (const auto &app_config : log_config.appenders) {
                YAML::Node app_node;
                app_node["type"] = static_cast<int>(app_config.type);
                app_node["file"] = app_config.file;
                app_node["level"] = static_cast<int>(app_config.level);
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

} // namespace meha::utils
