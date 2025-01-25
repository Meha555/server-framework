#include <gtest/gtest.h>
#include "application.h"
#include "macro.h"
#include "module/hook.h"
#include "module/log.h"
#include "utils/mt_queue.h"
#include "utils/thread.h"
#include <variant>

using namespace meha;
using namespace meha::utils;

MtQueue<std::optional<int>> nums(5);
MtQueue<std::monostate> cmds;

void producer()
{
    hook::SetHookEnable(false);
    for (int i = 0; i < 5; ++i) {
        nums.push(i);
        LOG(root, INFO) << "produce " + std::to_string(i);
        usleep(1000000);
    }
    LOG(root, INFO) << "producer exit";
    cmds.push(std::monostate());
}

void consumer()
{
    hook::SetHookEnable(false);
    int sum = 0;
    while (true) {
        if (auto i = nums.pop()) {
            LOG(root, INFO) << "consume " + std::to_string(*i);
            sum += *i;
        } else {
            LOG(root, INFO) << "consumer exit\n";
            break;
        }
        usleep(1500000);
    }
    LOG(root, INFO) << "sum: " + std::to_string(sum);
}

TEST(MultiThreadQueue, test)
{
    std::vector<Thread::sptr> v;
    v.emplace_back(std::make_shared<Thread>(producer));
    v.emplace_back(std::make_shared<Thread>(consumer));
    v.emplace_back(std::make_shared<Thread>(consumer));
    v.emplace_back(std::make_shared<Thread>([]() {
        UNUSED(cmds.pop());
        LOG(root, WARN) << "send exit cmd";
        nums.push(std::nullopt, true);
        nums.push(std::nullopt, true);
    }));
    for (auto &t : v) {
        t->start();
    }
    for (auto &t : v) {
        t->join();
    }
    EXPECT_FALSE(nums.tryPop());
    EXPECT_FALSE(cmds.tryPop());
}

int main(int argc, char *argv[])
{
    Application app;
    return app.boot(BootArgs{
        .argc = argc,
        .argv = argv,
        .configFile = "/home/will/Workspace/Devs/projects/server-framework/misc/config.yml",
        .mainFunc = [](int argc, char **argv) -> int {
            hook::SetHookEnable(false);
            GET_LOGGER("core")->setLevel(LogMessage::LogLevel::INFO);
            ::testing::InitGoogleTest(&argc, argv);
            return RUN_ALL_TESTS();
        }});
}