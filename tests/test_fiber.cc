#include <fmt/format.h>
#include <gtest/gtest.h>

#include "application.h"
#include "fiber.h"
#include "module/log.h"
#include "utils/thread.h"

#define TEST_CASE FiberTest

using namespace meha;

#define EXPECT_HELPER(num, id, status)                              \
    if (single_thread) {                                            \
        EXPECT_EQ(Fiber::TotalFibers(), num);                       \
        EXPECT_EQ(Fiber::GetCurrentID(), id);                       \
        EXPECT_EQ(Fiber::GetCurrentState(), Fiber::Status::status); \
    }

// 正常执行的测试用例（多线程情况下TotalFibers的断言不启用，因为不确定创建协程的时机）
void test_fiber_normal(bool single_thread)
{
    Fiber::Init();
    EXPECT_HELPER(1, 0, Running);
    auto run_in_fiber = [single_thread]() {
        EXPECT_HELPER(2, 1, Running);
        LOG(root, INFO) << "run in fiber[" << Fiber::GetCurrentID() << "] begin, sleep 2s";
        ::sleep(2);
        LOG(root, INFO) << "1' time yield to main[0]";
        EXPECT_HELPER(2, 1, Running);
        Fiber::Yield();

        LOG(root, INFO) << "run in fiber[" << Fiber::GetCurrentID() << "] end, sleep 2s";
        ::sleep(2);

        LOG(root, INFO) << "2' time yield to main[0]";
        EXPECT_HELPER(2, 1, Running);
        Fiber::Yield();

        LOG(root, INFO) << "fiber[" << Fiber::GetCurrentID() << "] about to end";
    };
    // 主协程
    LOG(root, INFO) << "main[0] begin";
    ::sleep(2);

    {
        Fiber::sptr fiber(new Fiber(run_in_fiber, false));
        EXPECT_HELPER(2, 0, Running);
        EXPECT_EQ(fiber->isScheduled(), false);
        EXPECT_EQ(fiber->status(), Fiber::Status::Initialized);

        LOG(root, INFO) << "1' time main[0] resume into fiber[" << fiber->fid() << "]";
        fiber->resume();
        EXPECT_HELPER(2, 0, Running);
        EXPECT_EQ(fiber->status(), Fiber::Status::Ready);

        LOG(root, INFO) << "1' time fiber[" << fiber->fid() << "] yield to main[0]";
        ::sleep(2);

        LOG(root, INFO) << "2' time main[0] resume into fiber[" << fiber->fid() << "]";
        fiber->resume();
        EXPECT_HELPER(2, 0, Running);
        EXPECT_EQ(fiber->status(), Fiber::Status::Ready);

        LOG(root, INFO) << "2' time fiber[" << fiber->fid() << "] yield to main[0]";
        ::sleep(2);

        fiber->resume();
        EXPECT_TRUE(fiber->isFinished());
    }

    EXPECT_HELPER(1, 0, Running);
    LOG(root, INFO) << "main[0] end";
}

TEST(TEST_CASE, SingleThread)
{
    test_fiber_normal(true);
}

TEST(TEST_CASE, MultiThread)
{
    std::vector<Thread::sptr> threads;
    LOG(root, INFO) << "main thread begin";
    for (int i = 0; i < 3; i++) {
        auto thread = std::make_shared<Thread>(std::bind(&test_fiber_normal, false));
        thread->start();
        threads.push_back(thread);
    }
    std::for_each(threads.begin(), threads.end(), [](Thread::sptr &t) {
        t->join();
    });
    LOG(root, INFO) << "main thread end";
}

// 协程跑飞的测试用例（非对称协程）
TEST(TEST_CASE, AbnormalExecutionDeathTest)
{
    // NOTE 这里使用死亡断言
    ASSERT_EXIT(
        {
            Fiber::Init();
            static auto run_in_fiber2 = [] {
                static auto run_in_fiber1 = [] {
                    LOG(root, INFO) << "run in fiber1[" << Fiber::GetCurrentID() << "] begin";
                    LOG(root, INFO) << "run in fiber1[" << Fiber::GetCurrentID() << "] end";
                };
                LOG(root, INFO) << "run in fiber2[" << Fiber::GetCurrentID() << "] begin";
                int64_t fiber2_id = Fiber::GetCurrentID();
                ASSERT_GE(fiber2_id, 0);
                // 非对称协程，子协程无法直接创建并运行新的协程
                // 如果子协程再创建子协程，则主协程就跑飞了
                {
                    Fiber::sptr fiber(new Fiber(run_in_fiber1, false));
                    fiber->resume();
                }
                ASSERT_EQ(Fiber::GetCurrentID(), fiber2_id); // 这里会发现GetCurrentID()返回为0,即主协程ID,显然错误
                LOG(root, INFO) << "run in fiber2[" << Fiber::GetCurrentID() << "] end";
            };
            // 主协程
            LOG(root, INFO) << "main[0] begin";
            {
                Fiber::sptr fiber(new Fiber(run_in_fiber2, false));
                fiber->resume();
            }
            LOG(root, INFO)
                << "main[0] end";
        },
        testing::KilledBySignal(SIGSEGV),
        "");
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