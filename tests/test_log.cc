#include <exception>
#include <functional>
#include <iostream>
#include <ostream>

#include <boost/array.hpp>
#include <gtest/gtest.h>
#include <yaml-cpp/exceptions.h>

#include "config.hpp"
#include "log.h"
#include "thread.h"
#include "util.h"

#define TEST_CASE LogTest

TEST(TEST_CASE, CStyleLogger)
{
    std::cout << ">>>>>> 测试日志器的默认用法 <<<<<<" << std::endl;
    auto logger = meha::LoggerManager::GetInstance()->getLogger("root");
    auto event = MAKE_LOG_EVENT(DEBUG, "wdnmd");
    logger->log(event);
    logger->debug(event);
    logger->info(event);
    logger->warn(event);
    logger->error(event);
    logger->fatal(event);
}

TEST(TEST_CASE, MacroLogger)
{
    std::cout << ">>>>>> 测试日志器的宏函数 <<<<<<" << std::endl;
    auto logger = GET_ROOT_LOGGER();
    LOG_DEBUG(logger, "消息消息 " + std::to_string(time(nullptr)));
    LOG_INFO(logger, "消息消息 " + std::to_string(time(nullptr)));
    LOG_WARN(logger, "消息消息 " + std::to_string(time(nullptr)));
    LOG_ERROR(logger, "消息消息 " + std::to_string(time(nullptr)));
    LOG_FATAL(logger, "消息消息 " + std::to_string(time(nullptr)));
    LOG_FMT_DEBUG(logger, "消息消息 %s", "debug");
    LOG_FMT_INFO(logger, "消息消息 %s", "info");
    LOG_FMT_WARN(logger, "消息消息 %s", "warn");
    LOG_FMT_ERROR(logger, "消息消息 %s", "error");
    LOG_FMT_FATAL(logger, "消息消息 %s", "fatal");
}

TEST(TEST_CASE, StreamLogger)
{
    std::cout << ">>>>>> 测试日志器的流式宏 <<<<<<" << std::endl;
    auto logger = GET_ROOT_LOGGER();
    LOG(logger, DEBUG) << "消息消息 debug";
    LOG(logger, INFO) << "消息消息 info";
    LOG(logger, WARN) << "消息消息 warn";
    LOG(logger, ERROR) << "消息消息 error";
    LOG(logger, FATAL) << "消息消息 fatal";
}

TEST(TEST_CASE, NonexistLogger)
{
    std::cout << ">>>>>> 测试获取不存在的日志器 <<<<<<" << std::endl;
    try {
        meha::LoggerManager::GetInstance()->getLogger("nonexistent");
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
    }
}

TEST(TEST_CASE, LoggerConfig)
{
    std::cout << ">>>>>> 测试日志器的配置文件加载 <<<<<<" << std::endl;
    auto config = meha::Config::Lookup("logs");
    LOG_DEBUG(GET_ROOT_LOGGER(), config->toString().c_str());
    try {
        auto yaml_node = YAML::LoadFile("config.yml");
        meha::Config::LoadFromYAML(yaml_node);
        LOG_DEBUG(GET_ROOT_LOGGER(), config->toString().c_str());
    } catch (const YAML::BadFile &e) {
        std::cerr << "打开文件失败：" << e.what() << std::endl;
    }
}

TEST(TEST_CASE, CreateLoggerByYAMLFile)
{
    std::cout << ">>>>>> 测试配置功能 <<<<<<" << std::endl;
    try {
        auto yaml_node = YAML::LoadFile("config.yml");
        meha::Config::LoadFromYAML(yaml_node);
        auto root_logger = GET_ROOT_LOGGER();

        LOG_DEBUG(root_logger, "输出一条 debug 日志到全局日志器");
        LOG_INFO(root_logger, "输出一条 info 日志到全局日志器");
        LOG_ERROR(root_logger, "输出一条 error 日志到全局日志器");

        // auto event = MAKE_LOG_EVENT(meha::LogLevel::DEBUG, "输出一条 debug 日志");
        // global_logger->log(event);
        // root_logger->log(event);
    } catch (const YAML::BadFile &e) {
        std::cerr << "打开文件失败：" << e.what() << std::endl;
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
    }
}

TEST(TEST_CASE, MultiThreadLog)
{
    auto fn = [](const char *msg) {
        auto logger = GET_ROOT_LOGGER();
        for (int i = 0; i < 100; i++) {
            LOG_INFO(logger, msg);
        }
    };
    LOG_INFO(GET_ROOT_LOGGER(), ">>>>>> 多线程打日志 <<<<<<");
    meha::Thread t_1(std::bind(fn, "+++++"), "thread_1");
    meha::Thread t_2(std::bind(fn, "-----"), "thread_2");
    t_1.join();
    t_2.join();
}

int main(int argc, char *argv[])
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
