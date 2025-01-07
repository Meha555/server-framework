#pragma once

/* -------------------------------------------------------------------------- */
/*                参考了crow框架：https://github.com/CrowCpp/Crow               */
/* -------------------------------------------------------------------------- */

#include <functional>
#include <string>

#include "utils/noncopyable.h"

namespace meha
{
// NOTE 内嵌命名空间中的类的前向声明的正确方式
namespace internal
{
class ModuleIniter;
}

struct BootArgs
{
    int argc;
    char **argv;
    std::string configFile;
    std::function<int(int argc, char **argv)> mainFunc;
};

/**
 * @brief 应用程序类
 * @details 用于 应用程序的启动、初始化、运行等操作
 * 如果采用静态变量 static ModuleInter _; 的初始化方式，也可以代替这个类。当然那样的画要将ModuleIniter::initialize()的逻辑放到ModuleIniter的构造函数中
 */
class Application
{
    DISABLE_COPY_MOVE(Application)
public:
    explicit Application();
    virtual ~Application();

    /**
     * @brief 启动应用程序
     * @param args 启动参数
     * @return int 退出状态码
     */
    int boot(const BootArgs &args);

    static Application *Instatiate();
    static struct BootArgs BootArgs();

private:
    void drawBanner() const;
    /**
     * @brief 初始化各模块
     */
    void initialize(const std::string &configFile);

    internal::ModuleIniter *m_onStartIniter;
    struct BootArgs m_bootArgs;
    static Application *self;
};

// 仿照qt的宏定义qApp
#define mApp Application::Instatiate()

}