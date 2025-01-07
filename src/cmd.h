#pragma once

#include <algorithm>
#include <any>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace meha
{

class Arg
{
public:
    enum class Type {
        Flag,
        Option
    };

    explicit Arg(const std::string &longKey, const std::string &shortKey, const std::string &help, bool required = false)
        : m_longKey(longKey)
        , m_shortKey(shortKey)
        , m_help(help)
        , m_required(required)
    {
    }
    explicit Arg() = default;
    virtual ~Arg() = default;

    // 下面这几个函数标记为虚函数，是为了让返回值的Arg&，可以在子类上链式调用

    virtual Arg &setKey(const std::string &longKey, const std::string &shortKey)
    {
        m_longKey = longKey;
        m_shortKey = shortKey;
        return *this;
    }
    virtual Arg &setHelp(const std::string &help)
    {
        m_help = help;
        return *this;
    }
    virtual Arg &setRequired(bool required)
    {
        m_required = required;
        return *this;
    }

    bool isRequired() const
    {
        return m_required;
    }
    const std::string &help() const
    {
        return m_help;
    }
    const std::string &longKey() const
    {
        return m_longKey;
    }
    const std::string &shortKey() const
    {
        return m_shortKey;
    }
    virtual Type type() const = 0;
    bool operator==(const Arg &rhs) const
    {
        return this->m_longKey == rhs.longKey();
    }

protected:
    bool m_required;
    std::string m_help;
    std::string m_longKey;
    std::string m_shortKey;
};

/**
 * @brief 命令行参数 --flag
 * @note 如果是required的，没设置就会报错
 */
class Flag : public Arg
{
public:
    explicit Flag(const std::string &longKey, const std::string &shortKey, const std::string &help, bool required)
        : Arg(longKey, shortKey, help, required)
    {
    }
    explicit Flag() = default;
    Type type() const override
    {
        return Type::Flag;
    }
};

/**
 * @brief 命令行选项 --option=value
 * @note 如果是required的，没设置就会自动使用默认值，此时看起来好像设置了一样
 */
class Option : public Arg
{
public:
    friend class Rule;
    /**
     * @brief 命令行选项需要满足的自定义规则
     * @details 比如正则表达式
     */
    class Rule
    {
    public:
        using sptr = std::shared_ptr<Rule>;
        Rule() = default;
        virtual ~Rule() = default;
        bool check() const
        {
            return true;
        }

    private:
        std::string m_rule;
    };
    explicit Option(const std::string &longKey, const std::string &shortKey, const std::string &help,
                    bool required, std::any default_value, const std::vector<Rule::sptr> &rules = {})
        : Arg(longKey, shortKey, help, required)
        , m_default_value(default_value)
        , m_rules(rules)
    {
    }
    explicit Option() = default;
    Type type() const override
    {
        return Type::Option;
    }

    Option &setKey(const std::string &longKey, const std::string &shortKey) override
    {
        Arg::setKey(longKey, shortKey);
        return *this;
    }
    Option &setHelp(const std::string &help) override
    {
        Arg::setHelp(help);
        return *this;
    }
    Option &setRequired(bool required) override
    {
        Arg::setRequired(required);
        return *this;
    }
    Option &setValue(const std::any &value)
    {
        m_value = value;
        return *this;
    }
    Option &setDefaultValue(const std::any &value)
    {
        m_default_value = value;
        return *this;
    }
    Option &addRule(const std::shared_ptr<Rule> rule)
    {
        m_rules.push_back(rule);
        return *this;
    }
    std::any value() const
    {
        return m_value.value_or(m_default_value);
    }
    bool isFitRules()
    {
        return std::all_of(m_rules.cbegin(), m_rules.cend(), [this](const auto &rule) {
            return rule->check();
        });
    }

private:
    std::optional<std::any> m_value;
    std::any m_default_value;
    std::vector<Rule::sptr> m_rules;
};

/**
 * @brief 命令行参数解析器
 */
class ArgParser
{
public:
    explicit ArgParser() = default;

    /**
     * @brief 添加一个命令行参数
     * @param arg 命令行参数
     */
    [[deprecated("use addFlag/addOption instead")]] bool addArg(const Arg &arg);
    bool addFlag(const Flag &flag);
    bool addOption(const Option &option);

    /**
     * @brief 解析添加好的命令行参数列表
     * @return true 解析成功
     * @return false 解析失败（说明用户输入的不对）此时不应该使用解析的结果
     */
    bool parseArgs(); // 运行时输入
    bool parseArgs(int argc, char *argv[]); // 启动时参数

    /**
     * @brief 检查指定的flag是否被设置了
     * @return true 设置了；false 未设置
     */
    bool isFlagSet(const std::string &key) const;
    /**
     * @brief 获取指定的option的值
     * @return std::optional<std::any> 如果没有设置解析这个参数，则返回nullopt；否则返回值
     */
    std::optional<std::any> getOptionValue(const std::string &key) const;
    /**
     * @brief 获取所有期望设置的命令行参数
     * @return std::stringstream 以字符串形式返回
     */
    std::string dumpAll() const;

    /**
     * @brief 重置解析状态，不删除已有的命令行参数模板
     * @note 解析状态重置后，再次调用parseArgs()会重新解析命令行参数。
     * 如果是希望去掉设置的期望参数模板，则应该重新创建一个ArgParser对象。
     */
    void reset();
    bool isParsed() const
    {
        return m_isParsed;
    }

private:
    bool doParse(std::stringstream &ss);
    bool doParseFlags();
    bool doParseOptions();

    template<typename T>
    struct Data
    {
        mutable T data; // 这里加mutable只是为了能在unordered_set中修改非键值
        mutable bool isValid = false; // 对于Flag是是否设置，对于Option是是否满足所有规则
        Data(const T &value, bool set = false)
            : data(value)
            , isValid(set)
        {
        }
        bool operator==(const Data &other) const
        {
            return data == other.data;
        }
    };

    bool m_isParsed = false; // 是否已经解析过了
    // 用于解析命令行参数的模板
    std::unordered_map<std::string, Data<Flag>> m_flagsPattern;
    std::unordered_map<std::string, Data<Option>> m_optionsPattern;
    // 扫描搜集出来的原始数据
    std::vector<std::string> m_flags; // 位置参数flags（允许重复）
    std::unordered_map<std::string, std::any> m_options; // 命令行选项options（不允许重复）
};

inline std::ostream &operator<<(std::ostream &os, const Flag &flag)
{
    os << "Flag(" << flag.longKey() << ", " << flag.shortKey() << ": " << flag.help() << ")";
    return os;
}
inline std::ostream &operator<<(std::ostream &os, const Option &option)
{
    os << "Option(" << option.longKey() << ", " << option.shortKey() << ": " << option.help() << ", value: " << std::any_cast<char const *>(option.value()) << ")";
    return os;
}

} // namespace meha::utils