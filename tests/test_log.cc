#include <exception>
#include <functional>
#include <iostream>
#include <ostream>

#include <boost/array.hpp>
#include <gtest/gtest.h>
#include <stdexcept>
#include <yaml-cpp/exceptions.h>

#include "application.h"
#include "config.h"
#include "module/log.h"
#include "utils/thread.h"

#define TEST_CASE LogTest

using namespace meha;

TEST(TEST_CASE, CStyleLogger)
{
    std::cout << ">>>>>> 测试日志器的默认用法 <<<<<<" << std::endl;
    auto logger = LoggerManager::Instance()->getLogger("root");
    auto msg = MAKE_LOG_MSG(logger->getCategory(), DEBUG, "hahaha");
    logger->log(msg);
    logger->trace(msg);
    logger->debug(msg);
    logger->info(msg);
    logger->warn(msg);
    logger->error(msg);
    logger->fatal(msg);
}

TEST(TEST_CASE, MacroLogger)
{
    std::cout << ">>>>>> 测试日志器的宏函数 <<<<<<" << std::endl;
    LOG_TRACE(root, "消息消息 " + std::to_string(time(nullptr)));
    LOG_DEBUG(root, "消息消息 " + std::to_string(time(nullptr)));
    LOG_INFO(root, "消息消息 " + std::to_string(time(nullptr)));
    LOG_WARN(root, "消息消息 " + std::to_string(time(nullptr)));
    LOG_ERROR(root, "消息消息 " + std::to_string(time(nullptr)));
    LOG_FATAL(root, "消息消息 " + std::to_string(time(nullptr)));
    LOG_FMT_TRACE(root, "消息消息 %s", "trace");
    LOG_FMT_DEBUG(root, "消息消息 %s", "debug");
    LOG_FMT_INFO(root, "消息消息 %s", "info");
    LOG_FMT_WARN(root, "消息消息 %s", "warn");
    LOG_FMT_ERROR(root, "消息消息 %s", "error");
    LOG_FMT_FATAL(root, "消息消息 %s", "fatal");
}

TEST(TEST_CASE, StreamLogger)
{
    LOG(root, TRACE) << "消息消息 trace";
    LOG(root, DEBUG) << "消息消息 debug";
    LOG(root, INFO) << "消息消息 info";
    LOG(root, WARN) << "消息消息 warn";
    LOG(root, ERROR) << "消息消息 error";
    LOG(root, FATAL) << "消息消息 fatal";
}

TEST(TEST_CASE, NonexistLogger)
{
    EXPECT_THROW(
        LoggerManager::Instance()->getLogger("nonexistent");
        , std::out_of_range);
}

TEST(TEST_CASE, LoggerConfig)
{
    auto config = Config::Lookup("log");
    LOG_DEBUG(root, config->toString().c_str());
    try {
        Config::LoadFromFile("misc/config.yml");
        LOG_DEBUG(root, config->toString().c_str());
    } catch (const YAML::BadFile &e) {
        std::cerr << "打开文件失败：" << e.what() << std::endl;
    }
}

TEST(TEST_CASE, CreateLoggerByYAMLFile)
{
    try {
        auto yaml_node = YAML::LoadFile("config.yml");
        Config::LoadFromNode(yaml_node);

        LOG_DEBUG(root, "输出一条 debug 日志到全局日志器");

    } catch (const YAML::BadFile &e) {
        std::cerr << "打开文件失败：" << e.what() << std::endl;
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
    }
}

TEST(TEST_CASE, MultiThreadLog)
{
    auto fn = [](const char *msg) {
        for (int i = 0; i < 10; i++) {
            LOG_INFO(root, msg);
        }
    };
    Thread t_1(std::bind(fn, "+++++"));
    Thread t_2(std::bind(fn, "-----"));
    t_1.join();
    t_2.join();
}

int main(int argc, char *argv[])
{
    Application app;
    return app.boot(BootArgs{
        .argc = argc,
        .argv = argv,
        .configFile = "/home/will/Workspace/Devs/projects/server-framework/misc/config.yml",
        .mainFunc = [](int argc, char **argv) -> int {
            ::testing::InitGoogleTest(&argc, argv);
            return RUN_ALL_TESTS();
        }});
}