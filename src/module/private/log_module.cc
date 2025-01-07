#include "config.h"
#include "module/private/modules.h"
#include "module/log.h"

namespace meha::internal
{

LogModule::LogModule()
    : Module("log")
{
}

bool LogModule::initInternel()
{
    auto log_config_list = meha::Config::Lookup<LogConfigs>("log", {}, "日志器的配置项");
    if (!log_config_list) {
        return false;
    }
    // 注册日志器配置项变更时的事件处理回调：当配置项变动时，更新日志器
    log_config_list->addListener([](const LogConfigs &, const LogConfigs &) {
        std::cerr << "日志器配置变动，更新日志器" << std::endl;
        LoggerManager::Instance()->update();
    });
    return true;
}

uint32_t LogModule::priority() const
{
    return 1;
}

Module::InitTime LogModule::initTime() const
{
    return InitTime::OnStart;
}

}