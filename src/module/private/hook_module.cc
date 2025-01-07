#include "export.h"
extern "C" {
#include <dlfcn.h>
}
#include "module/hook.h"
#include "module/private/modules.h"

#define DEAL_FUNC(DO) \
    DO(sleep)         \
    DO(usleep)        \
    DO(nanosleep)     \
    DO(socket)        \
    DO(connect)       \
    DO(accept)        \
    DO(recv)          \
    DO(recvfrom)      \
    DO(recvmsg)       \
    DO(send)          \
    DO(sendto)        \
    DO(sendmsg)       \
    DO(getsockopt)    \
    DO(setsockopt)    \
    DO(read)          \
    DO(write)         \
    DO(close)         \
    DO(readv)         \
    DO(writev)        \
    DO(fcntl)         \
    DO(ioctl)

extern "C" {
// 定义系统 api 的函数指针的变量
#define DEF_ORIGIN_FUNC(name) name##_func name##_f = nullptr;
DEAL_FUNC(DEF_ORIGIN_FUNC)
#undef DEF_ORIGIN_FUNC
}

namespace meha::internal
{

HookModule::HookModule()
    : Module("hook")
{
    init();
    std::cerr << "hook module init" << std::endl;
}

bool HookModule::initInternel()
{
    hook::SetHookEnable(false);
    // 初始化被hook函数的原函数指针（通过dlsym）一定要是 RTLD_NEXT
#define TRY_LOAD_HOOK_FUNC(name)                               \
    name##_f = (name##_func)dlsym(RTLD_NEXT, #name);           \
    if (!name##_f) {                                           \
        std::cerr << "hook function " << #name << " failed\n"; \
        return false;                                          \
    }
    // 利用宏获取指定的系统 api 的函数指针
    DEAL_FUNC(TRY_LOAD_HOOK_FUNC)
#undef TRY_LOAD_HOOK_FUNC
    return true;
    // 这块改成以反射形式设置为属性。这说明module必须有一个类来塞这些属性
    // s_connect_timeout = g_tcp_connect_timeout->getValue();
    // g_tcp_connect_timeout->addListener(
    //     [](const int &old_value, const int &new_value) {
    //         LOG_FMT_INFO(core, "tcp connect timeout change from %d to %d", old_value, new_value);
    //         s_connect_timeout = new_value;
    //     });
}

void HookModule::cleanup()
{
    Module::cleanup();
    hook::SetHookEnable(false);
}

uint32_t HookModule::priority() const
{
    return 0;
}

Module::InitTime HookModule::initTime() const
{
    return InitTime::PreStart;
}

static HookModule s_hookModIniter;

}

#undef DEAL_FUNC