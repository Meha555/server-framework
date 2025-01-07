#include <gtest/gtest.h>

#include "application.h"
#include "module/log.h"
#include "utils/thread.h"

#define TEST_CASE ThreadTest

using namespace std;
using namespace meha;

static int cnt = 0;
static RWMutex s_rwlock;
static Mutex s_mutex;

void fn_1()
{
    auto thread = Thread::GetCurrent().lock();
    LOG_FMT_DEBUG(root, "当前线程 id = %d/%d", thread->tid(), cnt);
}

void fn_2()
{
    WriteScopedLock rsl(&s_rwlock);
    for (int i = 0; i < 100000000; i++) {
        cnt++;
    }
}

TEST(TEST_CASE, CreateThread)
{
    EXPECT_NO_THROW(
    vector<Thread::sptr> thread_list;
    for (size_t i = 0; i < 5; ++i) {
        auto thread = make_shared<Thread>(&fn_1);
        thread_list.push_back(thread);
        thread->start();
        LOG_FMT_DEBUG(root, "调用 start() 启动子线程 %d", thread->tid());
    }
    for (auto thread : thread_list) {
        LOG_FMT_DEBUG(root, "调用 join() 将子线程 %d 并入主线程", thread->tid());
        thread->join();
    }
    for (size_t i = 0; i < 5; ++i) {
        auto thread = make_shared<Thread>(&fn_1); // FIXME enable_shared_from_this会强制导致必须用shared_ptr创建对象了？？？
        thread->start();
        LOG_FMT_DEBUG(root, "创建子线程 %d，使用析构函数调用 detach() 分离子线程 %d", thread->tid(), thread->tid());
        thread->detach();
    }
    );
}

TEST(TEST_CASE, ReadWriteLock)
{
    EXPECT_NO_THROW(
    vector<Thread::uptr> thread_list;
    for (int i = 0; i < 10; i++) {
        thread_list.emplace_back(make_unique<Thread>(&fn_2));
    }

    for (auto &thread : thread_list) {
        thread->join();
    }

    LOG_FMT_DEBUG(root, "count = %d", cnt);
    );
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