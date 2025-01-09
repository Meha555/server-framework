#include "application.h"
#include "module/log.h"
#include "utils/utils.h"

using namespace meha;

int main(int argc, char *argv[])
{
    Application app;
    return app.boot(BootArgs{
        .argc = argc,
        .argv = argv,
        .configFile = "/home/will/Workspace/Devs/projects/server-framework/misc/config.yml",
        .mainFunc = [](int argc, char **argv) -> int {
            {
                LOG(root, DEBUG) << "good morning";
                auto cleanup = utils::GenScopeGuard([]() {
                    LOG(root, DEBUG) << "good night";
                });
                LOG(root, DEBUG) << "hello world!";
            }
            return 0;
        }});
}