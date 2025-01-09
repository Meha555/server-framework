#include "application.h"
#include "env.h"
#include "module/private/module_initer.h"
#include "module/private/modules.h"
#include <fstream>

namespace meha
{

using namespace internal;

Application::Application()
{
    drawBanner();
    self = this;
}

Application::~Application()
{
    delete m_onStartIniter;
}

int Application::boot(const struct BootArgs &args)
{
    m_bootArgs = args;
    EnvManager::Instance()->init(args.argc, args.argv);
    initialize(args.configFile);
    return args.mainFunc(args.argc, args.argv);
}

void Application::drawBanner() const
{
    // std::ifstream ifs(EnvManager::Instance()->getConfigPath() + "/banner.txt");
    std::cout << "=============================================================\n";
    std::ifstream ifs("/home/will/Workspace/Devs/projects/server-framework/misc/banner.txt");
    if (ifs.is_open()) // 使用 is_open() 检查文件是否成功打开
    {
        std::string line;
        while (std::getline(ifs, line)) {
            std::cout << line << std::endl;
        }
    } else {
        std::cerr << "Failed to open banner.txt" << std::endl; // 添加文件打开失败的错误处理
    }
    std::cout << "=============================================================\n";
}

void Application::initialize(const std::string &configFile)
{
    m_onStartIniter = new ModuleIniter(configFile);
    m_onStartIniter->addModule(std::make_shared<LogModule>());
    m_onStartIniter->initialize();
}

Application *Application::Instatiate()
{
    return self;
}

BootArgs Application::BootArgs()
{
    return mApp->m_bootArgs;
}

Application *Application::self = nullptr;

}