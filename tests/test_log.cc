#include "config.h"
#include "log.h"
#include "thread.h"
#include "util.h"
#include <boost/array.hpp>
#include <iostream>
#include <ostream>
#include <pthread.h>
#include <yaml-cpp/exceptions.h>

void TEST_defaultLogger()
{
    std::cout << ">>>>>> Call TEST_defaultLogger 测试日志器的默认用法 <<<<<<" << std::endl;
    auto logger = meha::LoggerManager::getInstance()->getLogger("root");
    auto event = MAKE_LOG_EVENT(meha::LogEvent::LogLevel::DEBUG, "wdnmd");
    logger->log(event);
    logger->debug(event);
    logger->info(event);
    logger->warn(event);
    logger->error(event);
    logger->fatal(event);
}

void TEST_macroDefaultLogger()
{
    std::cout << ">>>>>> Call TEST_macroLogger 测试日志器的宏函数 <<<<<<" << std::endl;
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

void TEST_getNonexistentLogger()
{
    std::cout << ">>>>>> Call TEST_getNonexistentLogger 测试获取不存在的日志器 <<<<<<" << std::endl;
    try {
        meha::LoggerManager::getInstance()->getLogger("nonexistent");
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
    }
}

void TEST_loggerConfig()
{
    std::cout << ">>>>>> Call TEST_loggerConfig 测试日志器的配置文件加载 <<<<<<" << std::endl;
    auto config = meha::Config::Lookup("logs");
    LOG_DEBUG(GET_ROOT_LOGGER(), config->toString().c_str());
    try {
        auto yaml_node = YAML::LoadFile("../config.yml");
        meha::Config::LoadFromYAML(yaml_node);
        LOG_DEBUG(GET_ROOT_LOGGER(), config->toString().c_str());
    } catch (const YAML::BadFile &e) {
        std::cerr << "打开文件失败：" << e.what() << std::endl;
    }
}

void TEST_createLoggerByYAMLFile()
{
    std::cout << ">>>>>> Call TEST_createAndUsedLogger 测试配置功能 <<<<<<" << std::endl;
    try {
        auto yaml_node = YAML::LoadFile("../config.yml");
        meha::Config::LoadFromYAML(yaml_node);
        auto global_logger = GET_ROOT_LOGGER();
        auto system_logger = GET_LOGGER("system");

        LOG_DEBUG(global_logger, "输出一条 debug 日志到全局日志器");
        LOG_INFO(global_logger, "输出一条 info 日志到全局日志器");
        LOG_ERROR(global_logger, "输出一条 error 日志到全局日志器");

        LOG_DEBUG(system_logger, "输出一条 debug 日志到 system 日志器");
        LOG_INFO(system_logger, "输出一条 info 日志到 system 日志器");
        LOG_ERROR(system_logger, "输出一条 error 日志到 system 日志器");
        // auto event = MAKE_LOG_EVENT(meha::LogLevel::DEBUG, "输出一条 debug 日志");
        // global_logger->log(event);
        // system_logger->log(event);
    } catch (const YAML::BadFile& e) {
        std::cerr << "打开文件失败：" << e.what() << std::endl;
    }
}

void fn_1()
{
    auto logger = GET_ROOT_LOGGER();
    for (int i = 0; i < 100; i++) {
        LOG_INFO(logger, "++++++++++++++");
    }
}
void fn_2()
{
    auto logger = GET_ROOT_LOGGER();
    for (int i = 0; i < 100; i++) {
        LOG_INFO(logger, "--------------");
    }
}

void TEST_multiThreadLog()
{
    LOG_INFO(GET_ROOT_LOGGER(), ">>>>>> Call TEST_multiThreadLog 多线程打日志 <<<<<<");
    {
        meha::Thread t_1(fn_1, "thread_1");
        meha::Thread t_2(fn_2, "thread_2");
    }
    sleep(10);
}

int main()
{

    TEST_macroDefaultLogger();
    TEST_defaultLogger();
    TEST_getNonexistentLogger();
    TEST_loggerConfig();
    TEST_createLoggerByYAMLFile();
    TEST_multiThreadLog();

    return 0;
}
