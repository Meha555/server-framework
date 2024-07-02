#include "fiber.h"
#include "log.h"
#include "thread.h"

#include <fmt/format.h>
#include <gtest/gtest.h>

#define TEST_CASE FiberTest

using namespace meha;

static auto g_logger = GET_ROOT_LOGGER();

// 正常执行的测试用例
void test_fiber_normal() {
  Fiber::Init();
  static auto run_in_fiber = []() {
    LOG(g_logger, INFO) << "run in fiber[" << Fiber::GetCurrentID()
                        << "] begin, sleep 1s";
    ::sleep(1);
    LOG(g_logger, INFO) << "1' time yield cpu";
    Fiber::GetCurrent()->yield();
    LOG(g_logger, INFO) << "run in fiber[" << Fiber::GetCurrentID()
                        << "] end, sleep 1s";
    ::sleep(1);
    LOG(g_logger, INFO) << "2' time yield cpu";
    Fiber::GetCurrent()->yield();
    LOG(g_logger, INFO) << "fiber[" << Fiber::GetCurrentID()
                        << "] about to end";
  };
  // 主协程
  LOG(g_logger, INFO) << "main[0] begin";
  Fiber::sptr fiber(new Fiber(run_in_fiber, 0));
  LOG(g_logger, INFO) << "1' time main[0] resume into fiber[" << fiber->getID()
                      << "]";
  fiber->resume();
  LOG(g_logger, INFO) << "1' time fiber[" << fiber->getID()
                      << "] yield outto main[0]";
  LOG(g_logger, INFO) << "2' time main[0] resume into fiber[" << fiber->getID()
                      << "]";
  fiber->resume();
  LOG(g_logger, INFO) << "2' time fiber[" << fiber->getID()
                      << "] yield outto main[0]";
  fiber->resume();
  LOG(g_logger, INFO) << "main[0] end";
}

//协程跑飞的测试用例
void test_fiber_abnormal() {
  Fiber::Init();
  static auto run_in_fiber2 = [] {
    static auto run_in_fiber1 = [] {
      LOG(g_logger, INFO) << "run in fiber1[" << Fiber::GetCurrentID()
                          << "] begin";
      LOG(g_logger, INFO) << "run in fiber1[" << Fiber::GetCurrentID()
                          << "] end";
    };
    LOG(g_logger, INFO) << "run in fiber2[" << Fiber::GetCurrentID()
                        << "] begin";
    // 非对称协程，子协程无法直接创建并运行新的协程
    // 如果子协程再创建子协程，则主协程就跑飞了
    Fiber::sptr fiber(new Fiber(run_in_fiber1, 0));
    fiber->resume(); // 这里就会断言失败挂掉
    LOG(g_logger, INFO) << "run in fiber2[" << Fiber::GetCurrentID() << "] end";
  };
  // 主协程
  LOG(g_logger, INFO) << "main[0] begin";
  Fiber::sptr fiber(new Fiber(run_in_fiber2, 0));
  fiber->resume();
  LOG(g_logger, INFO) << "main[0] end";
}

TEST(TEST_CASE, NormalExection) { test_fiber_normal(); }

TEST(TEST_CASE, MultiThread) {
  std::vector<Thread::sptr> threads;
  LOG(g_logger, INFO) << "main thread begin";
  for (int i = 0; i < 3; i++) {
    threads.emplace_back(std::make_shared<Thread>(
        test_fiber_normal, fmt::format("thread[{}]", i)));
  }
  std::for_each(threads.begin(), threads.end(),
                [](Thread::sptr &t) { t->join(); });
  LOG(g_logger, INFO) << "main thread end";
}

TEST(TEST_CASE, AbnormalExecution) { test_fiber_abnormal(); }

int main(int argc, char *argv[]) {
  testing::InitGoogleTest(&argc, argv);
  g_logger->setLevel(LogEvent::LogLevel::DEBUG);
  Thread::SetCurrentName("main_thread");
  return RUN_ALL_TESTS();
}
