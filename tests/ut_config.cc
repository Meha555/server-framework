#include <gtest/gtest.h>
#include <iostream>
#include <list>
#include <map>
#include <ostream>
#include <vector>

#include "application.h"
#include "config.h"
#include "module/log.h"

using namespace std;
using namespace meha;

#define TEST_CASE ConfigTest

// 创建默认配置项
auto config_system_port = Config::Lookup<int>("system.port", 6666);
auto config_test_array = Config::Lookup<vector<string>>("test_array", vector<string>{"vector", "string"});
auto config_test_linklist = Config::Lookup<list<string>>("test_list", list<string>{"list", "string"});
auto config_test_map = Config::Lookup<map<string, string>>("test_map", map<string, string>{{"map1", "srting"}, {"map2", "srting"}, {"map3", "srting"}});
auto config_test_set = Config::Lookup<set<int>>("test_set", set<int>{10, 20, 30});

/* --------------------------------- 自定义类型测试 -------------------------------- */

struct Goods
{
    string name;
    double price;

    string toString() const
    {
        stringstream ss;
        ss << "**" << name << "** $" << price;
        return ss.str();
    }

    bool operator==(const Goods &rhs) const
    {
        return name == rhs.name && price == rhs.price;
    }
};

ostream &operator<<(ostream &out, const Goods &g)
{
    out << g.toString();
    return out;
}

namespace meha::utils
{

// meha::lexical_cast 针对自定义类型的全特化
template<>
struct lexical_cast<string, Goods>
{
    Goods operator()(const string &source)
    {
        auto node = YAML::Load(source);
        Goods g;
        if (node.IsMap()) {
            g.name = node["name"].as<string>();
            g.price = node["price"].as<double>();
        }
        return g;
    }
};

template<>
struct lexical_cast<Goods, string>
{
    string operator()(const Goods &source)
    {
        YAML::Node node;
        node["name"] = source.name;
        node["price"] = source.price;
        stringstream ss;
        ss << node;
        return ss.str();
    }
};
} // namespace meha

auto config_test_user_type = Config::Lookup<Goods>("user.goods", Goods{});
auto config_test_user_type_list = Config::Lookup<vector<Goods>>("user.goods_array", vector<Goods>{});

/* ---------------------------------- 测试用例 ---------------------------------- */

// 测试配置项的 toString 方法
TEST(TEST_CASE, ConfigItemToString)
{
    cout << *config_system_port << endl;
    cout << *config_test_array << endl;
    cout << *config_test_linklist << endl;
    cout << *config_test_map << endl;
    cout << *config_test_set << endl;
    cout << *config_test_user_type << endl;
    cout << *config_test_user_type_list << endl;
}

// 测试通过解析 yaml 文件更新配置项
TEST(TEST_CASE, loadConfig)
{
    YAML::Node cfg;
    // 测试读取配置
    try {
        cfg = YAML::LoadFile("tests/test_config.yml");
    } catch (const exception &e) {
        LOG_FMT_ERROR(root, "文件加载失败：%s", e.what());
    }
    // 测试设置配置
    try {
        Config::LoadFromNode(cfg);
    } catch (const exception &e) {
        LOG_FMT_ERROR(root, "配置设置失败：%s", e.what());
    }
}

// 测试获取并使用配置的值
TEST(TEST_CASE, GetConfigItemValue)
{
// 遍历线性容器的宏
#define TSEQ(config_var)                                        \
    cout << "name = " << config_var->getName() << "; value = "; \
    for (const auto &item : config_var->getValue()) {           \
        cout << item << ", ";                                   \
    }                                                           \
    cout << endl;

    TSEQ(config_test_array);
    TSEQ(config_test_linklist);
    TSEQ(config_test_set);
    TSEQ(config_test_user_type_list);
#undef TSEQ
// 遍历映射容器的宏
#define TMAP(config_var)                                        \
    cout << "name = " << config_var->getName() << "; value = "; \
    for (const auto &[k, v] : config_var->getValue()) {         \
        cout << "<" << k << ", " << v << ">, ";                 \
    }                                                           \
    cout << endl;

    TMAP(config_test_map);
#undef TMAP
}

// 测试获取不存在的配置项
TEST(TEST_CASE, nonexistentConfig)
{
    LOG_DEBUG(root, "call TEST_nonexistentConfig 测试获取不存在的配置项");
    auto log_name = Config::Lookup("nonexistent");
    if (!log_name) {
        LOG_ERROR(root, "non value");
    }
}

int main(int argc, char *argv[])
{
    Application app;
    return app.boot(BootArgs{
        .argc = argc,
        .argv = argv,
        .configFile = "/home/will/Workspace/Devs/projects/server-framework/misc/config.yml",
        .mainFunc = [](int argc, char **argv) -> int {
            testing::InitGoogleTest(&argc, argv);
            // 注册一个监听器
            config_system_port->addListener([](const int &old_value, const int &new_value) {
                LOG_FMT_DEBUG(root, "配置项 %s 的值被修改，从 %d 到 %d", config_system_port->getName().c_str(), old_value, new_value);
            });
            YAML::Node node;
            auto str = node["node"] ? node["node"].as<string>() : "";
            cout << str << endl;
            return RUN_ALL_TESTS();
        }});
}