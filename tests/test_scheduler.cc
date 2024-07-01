#include "log.h"
#include "scheduler.h"
#include <iostream>

#include <gtest/gtest.h>

using namespace meha;

#define TEST_CASE Scheduler

void fn()
{
    for (int i = 0; i < 3; i++) {
        std::cout << "啊啊啊啊啊啊" << std::endl;
        Fiber::YieldToHold();
    }
}

void fn2()
{
    for (int i = 0; i < 3; i++) {
        std::cout << "哦哦哦哦哦哦" << std::endl;
        Fiber::YieldToHold();
    }
}

TEST(TEST_CASE, test1)
{
    Scheduler sc(2, true);
    sc.start();

    int i = 0;
    for (i = 0; i < 3; i++) {
        sc.schedule(std::bind([](int i) { std::cout << ">>>>>> " << i << std::endl; }, i));
    }

    sc.stop();
}

int main(int argc, char *argv[])
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
