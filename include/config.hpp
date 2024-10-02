#pragma once

#include "log.h"
#include "mutex.hpp"
#include <algorithm>
#include <boost/lexical_cast.hpp>
#include <boost/lexical_cast/bad_lexical_cast.hpp>
#include <exception>
#include <functional>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <vector>
#include <yaml-cpp/node/node.h>
#include <yaml-cpp/node/parse.h>
#include <yaml-cpp/node/type.h>
#include <yaml-cpp/yaml.h>

namespace meha::utils {

/*
针对YAML数据类型的序列化为string和反序列化为具体数据类型的工具
YAML有4种数据类型：scalar、sequence、mapping、null。
其中scalar和null是基本类型，sequence、mapping是复合类型，最终都拆分成基本类型

以下实现YAML字符串和vector/list/set/unordered_set/map/unordered_map的相互转换
*/

/**
 * @brief YAML格式字符串到其他类型的转换仿函数（boost::lexical_cast 的包装）
 * @param Source 源类型
 * @param Target 目标类型
 * @exception 当类型不可转换时抛出异常
 * @details 因为 boost::lexical_cast 是使用 std::stringstream 实现的字符串与目标类型的转换，
 * 所以仅支持实现了 ostream::operator<< 与 istream::operator>> 的类型,
 * 可以说默认情况下仅支持 std::string 与各类 Number 类型的双向转换。
 * 而我们需要转换自定义的类型，可以选择实现对应类型的流操作符，或者将该转换函数封装一个仿函数模板并进行偏特化，并重写提供自己的operator()，内含流运算符逻辑。
 * 这里选择后者，因为基于类模板的特化方法可以适用于嵌套的组合类型，而重载运算符只能适用于单一类型
 */
template <typename Source, typename Target>
struct lexical_cast
{
    using source_type = Source;
    using target_type = Target;
    target_type operator()(const source_type &source) const { return boost::lexical_cast<target_type>(source); }
};
template <typename T>
using from_type_to_string_cast = lexical_cast<T, std::string>;
template <typename T>
using from_string_to_type_cast = lexical_cast<std::string, T>;

/**
 * @brief YAML格式字符串到其他类型的转换仿函数
 * @details meha::lexical_cast 的偏特化，std::string -> std::vector<T>
 */
template <typename T>
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
            for (const auto &item : node) {  // DFS暴搜，对node中的所有子结点进行转换
                ss.str("");                  // 清空ss
                // 利用 YAML::Node 实现的 operator<<() 将 node 转换为字符串
                ss << item;
                // 此时递归解析node的子结点，直到node为基本类型开始回溯
                config_list.push_back(from_string_to_type_cast<T>()(ss.str()));
            }
        } else {
            LOG_FMT_WARN(GET_ROOT_LOGGER(),
                         "meha::lexical_cast<std::string, std::vector>::operator() exception %s",
                         "<source> is not a YAML sequence");
        }
        return config_list;
    }
};
template <typename T>
using from_string_to_vector_cast = lexical_cast<std::string, std::vector<T>>;

/**
 * @brief YAML格式字符串到其他类型的转换仿函数
 * @details meha::lexical_cast 的偏特化，std::vectror<T> -> std::string
 */
template <typename T>
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
            // 调用 meha::lexical_cast 特化版本递归解析，知道 T 为基本类型
            node.push_back(YAML::Load(from_type_to_string_cast<T>()(item)));
        }
        // 最后通过 std::stringstream 与调用 yaml-cpp 库实现的 operator<<() 将 node 转换为字符串
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};
template <typename T>
using from_vector_to_string_cast = lexical_cast<std::vector<T>, std::string>;

/**
 * @brief YAML格式字符串到其他类型的转换仿函数
 * @details meha::lexical_cast 的偏特化，std::string -> std::list<T>
 */
template <typename T>
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
            LOG_FMT_WARN(GET_ROOT_LOGGER(),
                         "meha::lexical_cast<std::string, std::list>::operator() exception %s",
                         "<source> is not a YAML sequence");
            throw boost::bad_lexical_cast();
        }
        return config_list;
    }
};
template <typename T>
using from_string_to_list_cast = lexical_cast<std::string, std::list<T>>;

/**
 * @brief YAML格式字符串到其他类型的转换仿函数
 * @details meha::lexical_cast 的偏特化，std::list<T>-> std::string
 */
template <typename T>
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
template <typename T>
using from_list_to_string_cast = lexical_cast<std::list<T>, std::string>;

/**
 * @brief YAML格式字符串到其他类型的转换仿函数
 * @details meha::lexical_cast 的偏特化，std::string -> std::map<std::string, T>
 */
template <typename T>
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
            LOG_FMT_WARN(GET_ROOT_LOGGER(),
                         "meha::lexical_cast<std::string, std::map>::operator() exception %s",
                         "<source> is not a YAML map");
            throw boost::bad_lexical_cast();
        }
        return config_map;
    }
};
template <typename T>
using from_string_to_map_cast = lexical_cast<std::string, std::map<std::string, T>>;

/**
 * @brief YAML格式字符串到其他类型的转换仿函数
 * @details meha::lexical_cast 的偏特化，std::map<std::string, T> -> std::string
 */
template <typename T>
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
template <typename T>
using from_map_to_string_cast = lexical_cast<std::map<std::string, T>, std::string>;

/**
 * @brief YAML格式字符串到其他类型的转换仿函数
 * @details meha::lexical_cast 的偏特化，std::string -> std::set<T>
 */
template <typename T>
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
            LOG_FMT_WARN(GET_ROOT_LOGGER(),
                         "meha::lexical_cast<std::string, std::set>::operator() exception %s",
                         "<source> is not a YAML sequence");
            throw boost::bad_lexical_cast();
        }
        return config_set;
    }
};
template <typename T>
using from_string_to_set_cast = lexical_cast<std::string, std::set<T>>;

/**
 * @brief YAML格式字符串到其他类型的转换仿函数
 * @details meha::lexical_cast 的偏特化，std::set<T> -> std::string
 */
template <typename T>
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
template <typename T>
using from_set_to_string_cast = lexical_cast<std::set<T>, std::string>;

} // namespace meha::utils

namespace meha {

/**
 * @brief 配置项虚基类，本身不含配置项类型和值，这些由派生类实现
 * @details 定义配置项共有的成员和方法
 * @note 这里之所以要搞一个虚基类，而不是直接类模板，是为了利用多态来存储这些配置项。
 * 如果是类模板的话，每个实例化的类型都是一个新的类，就不能存到一个容器内了。
 */
class ConfigItemBase {
    friend inline std::ostream &operator<<(std::ostream &out, const ConfigItemBase &cvb);

public:
    using ptr = std::shared_ptr<ConfigItemBase>;

    ConfigItemBase(const std::string &name, const std::string &description) : m_name(name), m_description(description)
    {
        // 虽然YAML对大小写不敏感，但是这里还是统一改为小写
        std::transform(m_name.begin(), m_name.end(), m_name.begin(), ::tolower);
    }
    virtual ~ConfigItemBase() = default;

    // thread-safe
    void setName(const std::string &name)
    {
        WriteScopedLock lock(&m_mutex);
        m_name = name;
    }
    // thread-safe
    void setDescription(const std::string &description)
    {
        WriteScopedLock lock(&m_mutex);
        m_description = description;
    }
    // REVIEW 这里返回引用，那就得保证外部不修改这个内部对象，所以需要常引用（由于ConfigItemBase是服务器启动时就创建好，因此这里不存在悬垂引用的问题。不过最好还是别返回局部对象的引用）
    const std::string &getName() const { return m_name; }
    const std::string &getDesccription() const { return m_description; }

    // 将配置项的值转为字符串
    virtual std::string toString() const = 0;
    // 通过字符串来获设置配置项的值
    virtual bool fromString(const std::string &val) = 0;

protected:
    std::string m_name;         // 配置项的名称
    std::string m_description;  // 配置项的备注
    mutable RWLock m_mutex;
};

// 实现ConfigItem的流式输出。注意这里加了inline来避免多次定义
inline std::ostream &operator<<(std::ostream &out, const ConfigItemBase &cvb)
{
    out << cvb.m_name << ": " << cvb.toString() << ", " << cvb.m_description;
    return out;
}

/**
 * @brief 具体配置项实现类（模板）
 * @details 支持监听配置变更事件，更新配置时会一并触发全部的配置变更回调函数
 * @param Item            配置项的值的类型
 * @param ToStringFN      {functor<std::string(T&)>} 将配置项的值转换为 std::string 的仿函数
 * @param FromStringFN    {functor<T(const std::string&)>} 将 std::string 转换为配置项的值的仿函数
 * */
template <typename Item,
          typename ToStringFN = utils::from_type_to_string_cast<Item>,
          typename FromStringFN = utils::from_string_to_type_cast<Item>>
class ConfigItem : public ConfigItemBase {
public:
    using ptr = std::shared_ptr<ConfigItem>;
    using onChangedCallback = std::function<void(const Item &old_value, const Item &new_value)>;

    ConfigItem(const std::string &name, const Item &value, const std::string &description)
        : ConfigItemBase(name, description), m_value(value)
    {}

    // thread-safe 获取配置项的值
    Item getValue() const
    {
        ReadScopedLock lock(&m_mutex);
        return m_value;
    }
    // thread-safe 设置配置项的值
    void setValue(const Item value)
    {
        {  // 上读锁
            ReadScopedLock lock(&m_mutex);
            if (value == m_value) {  // 这里存在比较运算符，需要在Item中重载
                return;
            }
            // 值被修改，调用所有的变更事件处理器
            notifyAll(m_value);
        }
        // 上写锁
        WriteScopedLock lock(&m_mutex);
        m_value = value;
    }

    // 将序列化配置项的值为字符串
    std::string toString() const override
    {
        try {
            return ToStringFN()(getValue());
        } catch (std::exception &e) {
            LOG_FMT_ERROR(GET_ROOT_LOGGER(),
                          "ConfigItem::toString exception %s convert: %s to string",
                          e.what(),
                          typeid(m_value).name());
        }
        return "<error>";
    }
    // 将字符串反序列化为配置项的值
    bool fromString(const std::string &val) override
    {
        try {
            setValue(FromStringFN()(val));  // 此时需要设置配置项的值
            return true;
        } catch (std::exception &e) {
            LOG_FMT_ERROR(GET_ROOT_LOGGER(),
                          "ConfigItem::fromString exception %s convert: string to %s",
                          e.what(),
                          typeid(m_value).name());
        }
        return false;
    }

    // thread-safe 增加配置项变更事件处理器，返回处理器的唯一编号
    uint64_t addListener(onChangedCallback cb)
    {
        static uint64_t s_cb_id = 0;  // 编号是始终递增的
        WriteScopedLock lock(&m_mutex);
        m_callback_map[s_cb_id++] = cb;
        return s_cb_id;
    }
    // thread-safe 删除配置项变更事件处理器
    void delListener(uint64_t key)
    {
        WriteScopedLock lock(&m_mutex);
        m_callback_map.erase(key);
    }

    // thread-safe 获取配置项变更事件处理器
    onChangedCallback getListener(uint64_t key) const
    {
        ReadScopedLock lock(&m_mutex);
        auto iter = m_callback_map.find(key);
        if (iter == m_callback_map.end()) {
            return nullptr;
        }
        return iter->second;
    }
    // thread-safe 清除所有配置项变更事件处理器
    void removeListeners()
    {
        WriteScopedLock lock(&m_mutex);
        m_callback_map.clear();
    }
    void notifyAll(const Item &new_value)
    {
        Item old_value = m_value;
        for (const auto &[id, cb] : m_callback_map) {
            cb(old_value, m_value);
        }
    }

private:
    Item m_value;                                         // 配置项的值
    std::map<uint64_t, onChangedCallback> m_callback_map;  // 配置项变更回调函数数组<回调函数ID, 回调函数指针>
};

/**
 * @brief 配置文件管理类
 * @details 解析YAML配置文件，并管理所有的ConfigItem对象
 * @note 这个类没有创建对象来用，其所有方法都是static
 */
class Config {
public:
    using ConfigItemMap = std::map<std::string, ConfigItemBase::ptr>;

    // thread-safe 查找配置项，返回 ConfigItemBase 智能指针
    static ConfigItemBase::ptr Lookup(const std::string &name)
    {
        ReadScopedLock lock(&GetRWLock());
        ConfigItemMap &s_data = GetData();
        auto iter = s_data.find(name);
        if (iter == s_data.end()) {
            return nullptr;
        }
        return iter->second;
    }

    // 查找配置项，返回指定类型的 ConfigItem 智能指针（注意这里出现了嵌套从属名）
    template <typename T>
    static typename ConfigItem<T>::ptr Lookup(const std::string &name)
    {
        auto base_ptr = Lookup(name);
        if (!base_ptr) {
            return nullptr;
        }
        // 配置项存在，尝试下行转换成指定的派生类指针，转型失败会返回一个空的智能指针
        auto ptr = std::dynamic_pointer_cast<ConfigItem<T>>(base_ptr);
        if (!ptr) {
            LOG_FMT_ERROR(GET_ROOT_LOGGER(),
                          "Config::Lookup<%s> exception, 无法转换到 ConfigItem<%s>",
                          typeid(T).name(),
                          typeid(T).name());
            throw std::bad_cast();
        }
        return ptr;
    }

    // thread-safe 创建或更新配置项
    template <typename T>
    static typename ConfigItem<T>::ptr
    Lookup(const std::string &name, const T &value, const std::string &description = "")
    {
        auto tmp = Lookup<T>(name);
        // 已存在同名配置项
        if (tmp) {
            return tmp;
        }
        // 判断名称是否合法
        // REVIEW 这里看看换成正则表达式
        if (name.find_first_not_of("qwertyuiopasdfghjklzxcvbnm0123456789._") != std::string::npos) {
            // 开头前缀不是这些字符
            LOG_FMT_ERROR(GET_ROOT_LOGGER(),
                          "Congif::Lookup exception name=%s"
                          "参数只能以字母数字点或下划线开头",
                          name.c_str());
            throw std::invalid_argument(name);
        }
        auto v = std::make_shared<ConfigItem<T>>(name, value, description);
        WriteScopedLock lock(&GetRWLock());
        GetData()[name] = v;
        return v;
    }

    // thread-safe 从 YAML::Node 中载入配置
    static void LoadFromNode(const YAML::Node &root)
    {
        std::vector<std::pair<std::string, YAML::Node>> node_list;
        TraversalNode(root, "", node_list);  // 把root node中的所有node都扁平化到列表node_list中

        for (auto &[key, node] : node_list) {
            if (key.empty()) {
                continue;
            }
            std::transform(key.begin(), key.end(), key.begin(), ::tolower);
            // 根据配置项名称在内存中的配置中查找对应的配置项
            auto val = Lookup(key);
            // 只处理注册过的配置项，没有注册过的配置项直接忽略
            if (val) { // REVIEW 这里到底是采用异常还是空指针来进行错误处理更好？
                std::stringstream ss;
                ss << node;
                val->fromString(ss.str());  // 完成setValue的过程
                LOG_FMT_DEBUG(GET_ROOT_LOGGER(),
                              "配置项: (%s, %s, %s)",
                              val->getName().c_str(),
                              val->toString().c_str(),
                              val->getDesccription().c_str());
            }
        }
    }

    // thread-safe 从 YAML 文件中载入配置
    static void LoadFromFile(const std::string& filename)
    {
        LoadFromNode(YAML::LoadFile(std::move(filename)));
    }

private:
    /**
     * @brief 遍历指定的 YAML::Node 对象，并将遍历结果扁平化存到列表里返回（扁平化为list是为了方便查询）
     * @details YAML中保存的内容是树形结构的，这里统一将其扁平压缩为一条<key,val>记录
     * @param node 本次所处理的YAML::Node对象
     * @param name node对应的字符串名称
     * @param[out] output 保存扁平化YAML设置的vector
     */
    static void TraversalNode(const YAML::Node &node,
                              const std::string &name,
                              std::vector<std::pair<std::string, YAML::Node>> &output)
    {
        // 将 YAML::Node 存入 output（找到插入位置）
        auto output_iter =
            std::find_if(output.begin(), output.end(), [&name](const std::pair<std::string, YAML::Node> &item) {
                return item.first == name;
            });
        if (output_iter != output.end()) {
            output_iter->second = node;
        } else {
            output.push_back({name, node});
        }
        switch (node.Type()) {
        case YAML::NodeType::Map: {
            for (auto iter = node.begin(); iter != node.end(); ++iter) {
                TraversalNode(
                    iter->second, name.empty() ? iter->first.Scalar() : name + "." + iter->first.Scalar(), output);
            }
        } break;
        case YAML::NodeType::Sequence: {
            for (size_t i = 0; i < node.size(); ++i) {
                TraversalNode(node[i], name + "." + std::to_string(i), output);
            }
        } break;
        default:  // Scalar、Null，前者直接就用Scalar()方法拿到字符串，后者不创建YAML::Node对象
            break;
        }
    }

private:
    static ConfigItemMap &GetData()
    {
        static ConfigItemMap s_data;  // 保存结构化的YAML设置
        return s_data;
    }

    static RWLock &GetRWLock()
    {
        static RWLock s_lock;
        return s_lock;
    }
};

}  // namespace meha
