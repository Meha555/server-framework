#include <gtest/gtest.h>

#include "application.h"
#include "scheduler.h"

using namespace meha;

#define TEST_CASE SchedulerTest

void fn1()
{
    for (int i = 0; i < 3; i++) {
      LOG(root, INFO) << "任务fn1 fid= " << Fiber::GetCurrentID();
    }
}

void fn2()
{
    for (int i = 0; i < 3; i++) {
        LOG(root, INFO) << "任务fn2 fid= " << Fiber::GetCurrentID();
    }
}

TEST(TEST_CASE, ScheduleTaskUseCaller)
{
    Scheduler sc(3, true);
    sc.start();
    // 此时可以添加任务执行了
    sc.schedule(fn1);
    for (int i = 0; i < 5; i++) {
        sc.schedule(std::bind([](int i) { LOG(root, INFO) << "添加 任务" << i; }, i));
    }
    sc.schedule(fn2);
    sc.stop();
}

TEST(TEST_CASE, ScheduleTaskNotUseCaller)
{
    Scheduler sc(3, false);
    sc.start();
    // 此时可以添加任务执行了
    sc.schedule(fn1);
    for (int i = 0; i < 5; i++) {
        sc.schedule(std::bind([](int i) { LOG(root, INFO) << ">>> 任务 " << i; }, i));
    }
    sc.schedule(fn2);
    sc.stop();
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