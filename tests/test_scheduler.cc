#include <iostream>
#include <gtest/gtest.h>

#include "application.h"
#include "utils/utils.h"
#include "scheduler.h"

using namespace meha;

#define TEST_CASE Scheduler

void fn1()
{
    for (int i = 0; i < 3; i++) {
      std::cout << "啊啊啊啊id= " << utils::GetFiberID() << std::endl;
    }
}

void fn2()
{
    for (int i = 0; i < 3; i++) {
        std::cout << "哦哦哦哦id= " << utils::GetFiberID() << std::endl;
    }
}

TEST(TEST_CASE, test1)
{
    Scheduler sc(1, true);
    sc.start();
    // 此时可以添加任务执行了
    sc.schedule(fn1);
    for (int i = 0; i < 3; i++) {
        sc.schedule(std::bind([](int i) { std::cout << ">>>任务 " << i << std::endl; }, i));
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