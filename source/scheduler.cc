#include "scheduler.h"
#include "fiber.h"
#include "hook.h"
#include "log.h"
#include "util.h"
#include <fmt/format.h>

namespace meha {

static Logger::ptr root_logger = GET_LOGGER("root");
// 当前线程的协程调度器
static thread_local Scheduler *t_scheduler{nullptr};
// 新增：当前调度器所在线程的主协程
static thread_local Fiber *t_scheduler_fiber{nullptr};

Scheduler *Scheduler::GetCurrent() { return t_scheduler; }

Fiber *Scheduler::GetMainFiber() { return t_scheduler_fiber; }

Scheduler::Scheduler(size_t pool_size, bool as_master) {
  ASSERT_FMT(pool_size > 0, "线程池大小不能为空");
  if (as_master) {
    // 线程池需要的线程数减一
    --pool_size;
    ASSERT_FMT(GetCurrent() == nullptr, "线程内只能有一个调度器！");
    t_scheduler = this;
    // 利用当前线程作为主协程来调度其他任务协程
    Fiber::Init();
    m_caller_fiber = std::make_shared<Fiber>(std::bind(&Scheduler::run, this));
    Thread::SetCurrentName(fmt::format("{}_scheduler@{}",
                                       Thread::GetCurrentName(),
                                       reinterpret_cast<void *>(this)));
    t_scheduler_fiber = m_caller_fiber.get(); // REVIEW - 再看看
    m_caller_thread_id = GetThreadID();
  } else {
    m_caller_thread_id = -1;
    t_scheduler_fiber = Fiber::GetCurrent().get();
  }
  m_thread_pool_size = pool_size;
  m_thread_pool.resize(m_thread_pool_size);
}

Scheduler::~Scheduler() {
  ASSERT(m_auto_stop);
  if (GetCurrent() == this) {
    t_scheduler = nullptr;
  }
}

void Scheduler::start() {
  LOG_DEBUG(root_logger, "调用 Scheduler::start()");
  {
    if (!m_stoped) { // 调度器已经开始工作，避免重复启动
      return;
    }
    m_stoped = false;
    for (size_t i = 0; i < m_thread_pool_size; i++) {
      m_thread_pool[i] = std::make_shared<Thread>(
          std::bind(&Scheduler::run, this),
          fmt::format("scheduler@{}_worker{}", reinterpret_cast<void *>(this),
                      i));
    }
  }
  // m_caller_fiber 存在就将它换入
  // if (m_caller_fiber)
  // {
  //     LOG_DEBUG(g_logger, "开始换入 m_caller_fiber，绑定的函数是
  //     Scheduler::run()"); m_caller_fiber->swapIn();
  // }
}

void Scheduler::stop() {
  LOG_DEBUG(root_logger, "调用 Scheduler::stop()");
  m_auto_stop = true;
  // 如果当前只有一条主线程在执行，简单等待执行结束即可
  if (m_caller_fiber && m_thread_pool_size == 0 &&
      (m_caller_fiber->isFinished() ||
       m_caller_fiber->getState() == Fiber::INIT ||
       m_caller_fiber->getState() == Fiber::TERM)) {
    m_stoped = true;
    if (doStop()) {
      return;
    }
  }
  //    bool exit_on_this_fiber = false;
  //    ASSERT(m_root_thread_id == -1 && GetCurrent() != this);
  //    ASSERT(m_root_thread_id != -1 && GetCurrent() == this);
  m_stoped = true;
  for (size_t i = 0; i < m_thread_pool_size; i++) {
    tickle();
  }
  if (m_caller_fiber) {
    tickle();
    if (!isStoped()) {
      m_caller_fiber->resume();
    }
  }

  // 等待所有调度线程执行完各自的调度任务
  std::for_each(m_thread_pool.begin(), m_thread_pool.end(),
                [](auto &t) { t->join(); });
  m_thread_pool.clear();

  if (doStop()) {
    return;
  }
}

bool Scheduler::isStoped() const {
  ReadScopedLock lock(&m_mutex);
  // 调用过
  // Scheduler::stop()，并且任务列表没有新任务，也没有正在执行的协程，说明调度器已经彻底停止
  return m_auto_stop && m_task_list.empty() && m_working_thread_count == 0;
}

void Scheduler::tickle() { LOG_DEBUG(root_logger, "调用 Scheduler::tickle()"); }

void Scheduler::run() {
  // LOG_DEBUG(root_logger, "调用 Scheduler::run()");
  // t_scheduler = this;
  // setHookEnable(true);
  // // 如果当前工作线程不存在调度者协程，则创建调度者协程
  // // if (GetThreadID() != m_caller_thread_id) {
  // //     t_scheduler_fiber = Fiber::GetCurrent().get();
  // // }
  // // 线程空闲时执行的协程
  // auto idle_fiber =
  //     std::make_shared<Fiber>(std::bind(&Scheduler::doIdle, this));
  // // 开始调度
  // Task task;
  // while (true) {
  //   task.clear();
  //   bool need_tickle = false;
  //   {
  //     // 查找等待调度的任务
  //     WriteScopedLock lock(&m_mutex);
  //     for (auto iter = m_task_list.begin(); iter != m_task_list.end(); ++iter) {
  //       // 如果任务指定了要在特定线程执行，但当前线程不是指定线程，通知其他线程处理
  //       if ((*iter)->thread_id != -1 && (*iter)->thread_id != GetThreadID()) {
  //         need_tickle = true;
  //         continue;
  //       }
  //       // 跳过正在执行的任务
  //       if (task.handle && task.handle->getState() == Fiber::EXEC) {
  //         continue;
  //       }
  //       // 找到可以执行的任务（且和指定的tid匹配）
  //       task = **iter;
  //       ++m_working_thread_count;
  //       // 从任务列表里移除该任务
  //       m_task_list.erase(iter);
  //       break;
  //     }
  //   }
  //   if (need_tickle) {
  //     tickle();
  //   }
  //   // 如果该任务协程不是EXEC\TERM\ERROR，则换入该协程来执行任务
  //   if (!task.handle->isFinished()) {
  //     task.handle->swapIn(); // REVIEW 这里将该任务协程换入执行的写法对吗
  //     --m_working_thread_count;

  //     // 此时该任务协程已被换出。需要将该被换出的协程继续添加到任务队列等待调度
  //     switch (task.handle->getState()) {
  //     case Fiber::READY: // 调度执行
  //       schedule(task.handle, task.thread_id);
  //       break;
  //     case Fiber::EXEC:
  //     case Fiber::INIT:
  //       task.handle->m_state = Fiber::HOLD;
  //       break;
  //     case Fiber::HOLD:
  //       schedule(task.handle);
  //       break;
  //     case Fiber::ERROR:
  //     case Fiber::TERM:;
  //     }
  //     task.clear();
  //   } else { // 任务队列空了，换入执行 idle_fiber
  //     if (idle_fiber->isFinished()) {
  //       break;
  //     }
  //     ++m_idle_thread_count;
  //     // if (GetThreadID() == m_root_thread_id)
  //     // {
  //     //     // m_root_thread_id 等于当前线程 id，说明构造调度器时 as_master 为
  //     //     true
  //     //     // 使用 m_root_fiber 作为 master fiber
  //     //     idle_fiber->swapIn(m_root_fiber);
  //     // }
  //     // else if (m_root_thread_id == -1)
  //     // {
  //     //     idle_fiber->swapIn();
  //     // }
  //     idle_fiber->swapIn();
  //     --m_idle_thread_count;
  //     switch (idle_fiber->getState()) {
  //     case Fiber::READY:
  //     case Fiber::EXEC:
  //     case Fiber::INIT:
  //     case Fiber::HOLD:
  //       idle_fiber->m_state = Fiber::HOLD;
  //       break;
  //     case Fiber::ERROR:
  //     case Fiber::TERM:;
  //     }
  //   }
  // }
  // LOG_DEBUG(root_logger, "Scheduler::run() 结束");
}

} // namespace meha
