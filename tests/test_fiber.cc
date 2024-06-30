#include "fiber.h"
#include "log.h"
#include "thread.h"

#include <fmt/format.h>
#include <gtest/gtest.h>

#define TEST_CASE FiberTest

using namespace meha;

static auto g_logger = GET_ROOT_LOGGER();
static auto run_in_fiber = []() {
    LOG(g_logger, INFO) << "run in fiber[" << Fiber::GetCurrentID() << "] begin, sleep 1s";
    ::sleep(1);
    LOG(g_logger, INFO) << "1 time yield cpu";
    Fiber::YieldToHold();
    LOG(g_logger, INFO) << "run in fiber[" << Fiber::GetCurrentID() << "] end, sleep 1s";
    ::sleep(1);
    LOG(g_logger, INFO) << "2 time yield cpu";
    Fiber::YieldToHold();
};

void test_fiber()
{
    Fiber::init();
    LOG(g_logger, INFO) << "main fiber begin";
    Fiber::sptr pFiber(new Fiber(run_in_fiber, 0));
    LOG(g_logger, INFO) << "1 time swap in fiber[" << pFiber->getID() << "]";
    pFiber->swapIn();
    LOG(g_logger, INFO) << "1 time main fiber after swap in begin";
    LOG(g_logger, INFO) << "2 time swap in fiber[" << pFiber->getID() << "]";
    pFiber->swapIn();
    LOG(g_logger, INFO) << "2 time main fiber after swap in end";
    pFiber->swapIn();
    LOG(g_logger, INFO) << "main fiber end";
}

TEST(TEST_CASE, AlternateExection) { test_fiber(); }

TEST(TEST_CASE, MultiThread)
{
    Thread::SetCurrentName("main");
    std::vector<Thread::sptr> threads;
    for (int i = 0; i < 3; i++) {
        threads.emplace_back(std::make_shared<Thread>(test_fiber, fmt::format("thread[{}]", i)));
    }
    std::for_each(threads.begin(), threads.end(), [](Thread::sptr &t) { t->join(); });
}

int main(int argc, char *argv[])
{
    testing::InitGoogleTest(&argc, argv);
    g_logger->setLevel(LogEvent::LogLevel::DEBUG);
    return RUN_ALL_TESTS();
}
