#include "log.h"
#include "thread.h"
#include <gtest/gtest.h>
#include <memory>
#include <vector>
#include <fmt/format.h>

#define TEST_CASE ThreadTest

auto g_logger = GET_ROOT_LOGGER();

using namespace std;
using namespace meha;

static uint64_t cnt = 0;
RWLock s_rwlock;
Mutex s_mutex;

void fn_1()
{
    LOG_FMT_DEBUG(g_logger,
                  "当前线程 id = %ld/%d, 当前线程名 = %s",
                  GetThreadID(),
                  Thread::GetThisId(),
                  Thread::GetThisName().c_str());
}

void fn_2()
{
    WriteScopedLock rsl(&s_rwlock);
    for (int i = 0; i < 100000000; i++) {
        cnt++;
    }
}
auto x = fmt::format("thread_{}", 1);

// 测试线程创建
TEST(TEST_CASE, createThread)
{
    vector<Thread::sptr> thread_list;
    for (size_t i = 0; i < 5; ++i) {
        thread_list.push_back(make_shared<Thread>(&fn_1, "thread_" + to_string(i)));
    }
    LOG_DEBUG(g_logger, "调用 join() 启动子线程，将子线程并入主线程");
    for (auto thread : thread_list) {
        thread->join();
    }
    LOG_DEBUG(g_logger, "创建子线程，使用析构函数调用 detach() 分离子线程");
    for (size_t i = 0; i < 5; ++i) {
        make_unique<Thread>(&fn_1, "detach_thread_" + to_string(i));
    }
}

TEST(TEST_CASE, readWriteLock)
{
    vector<Thread::uptr> thread_list;
    for (int i = 0; i < 10; i++) {
        thread_list.push_back(make_unique<Thread>(&fn_2, "temp_thread" + to_string(i)));
    }

    for (auto &thread : thread_list) {
        thread->join();
    }

    LOG_FMT_DEBUG(g_logger, "count = %ld", cnt);
}

int main(int argc, char *argv[])
{
    LOG(g_logger, INFO) << x;
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}