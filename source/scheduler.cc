#include "scheduler.h"
#include "exception.h"
#include "fiber.h"
#include "hook.h"
#include "log.h"
#include "utils.h"
#include <fmt/format.h>

namespace meha {

static Logger::ptr root_logger = GET_LOGGER("root");
// 当前线程的协程调度器
static thread_local Scheduler *t_scheduler{nullptr};
// 新增：当前线程的调度协程（用于切到调度协程）。加上Fiber模块中记录的当前协程和主协程，现在记录了3个协程
static thread_local Fiber *t_scheduler_fiber{nullptr};

Scheduler *Scheduler::GetCurrent() { return t_scheduler; }

Fiber *Scheduler::GetSchedulerFiber() { return t_scheduler_fiber; }

Scheduler::Scheduler(size_t pool_size, bool use_caller) {
  ASSERT_FMT(pool_size > 0, "线程池大小不能为空");
  ASSERT_FMT(GetCurrent() == nullptr, "每个线程只能有一个调度器");
  // 如果是利用调度器所在线程作为调度线程
  if (use_caller) {
    --pool_size;
    t_scheduler = this;
    Fiber::Init();
    m_caller_fiber =
        std::make_shared<Fiber>(std::bind(&Scheduler::run, this), true);
    LOG(root_logger, WARN) << "call_fiber id=" << m_caller_fiber->id();
    Thread::SetCurrentName(fmt::format("{}_caller_scheduler@{}",
                                       Thread::GetCurrentName(),
                                       reinterpret_cast<void *>(this)));
    t_scheduler_fiber = m_caller_fiber.get();
  }
  // 如果是另开一个线程作为调度线程
  else {
    m_caller_fiber = nullptr;
    t_scheduler_fiber = nullptr;
  }
  m_thread_pool_size = pool_size;
  m_thread_pool.resize(m_thread_pool_size);
}

Scheduler::~Scheduler() {
  ASSERT(isStoped());
  // 需要满足caller线程可以再次创建一个Scheduler并启动之，因此要重置线程局部变量
  // 判断一下析构的位置是否在caller线程中
  if (GetCurrent() == this) {
    t_scheduler = nullptr;
    t_scheduler_fiber = nullptr;
  }
  m_caller_fiber = nullptr;
}

void Scheduler::start() {
  if (m_startting) { // 调度器已经开始工作，避免重复启动
    return;
  }
  m_startting = true;
  for (size_t i = 0; i < m_thread_pool_size; i++) {
    m_thread_pool[i] = std::make_unique<Thread>(
        std::bind(&Scheduler::run, this),
        fmt::format("{}_scheduler{}", reinterpret_cast<void *>(this),
                    i));
  }
}

void Scheduler::stop() {
  if (isStoped()) {
    return;
  }
  m_stopping = true;
  // 必然是调度协程来调用stop
  if (m_caller_fiber) {
    ASSERT(GetCurrent() == this);
  } else {
    ASSERT(GetCurrent() != this);
  }

  for (auto &&t : m_thread_pool) {
    tickle(); // REVIEW 这里tickle这么多次有用吗？
  }

  if (m_caller_fiber) {
    tickle(); // 补上thread_pool-1的那次
    // 对于use caller的情况，此时才换入调度协程开始调度
    m_caller_fiber->resume();
  }

  // 等待所有调度线程执行完各自的调度任务
  for (auto &&t : m_thread_pool) {
    t->join();
  }
}

bool Scheduler::isStoped() const {
  ReadScopedLock lock(&m_mutex);
  // 调用过stop、任务列表没有新任务，也没有正在执行的任务，说明调度器已经彻底停止
  return m_stopping && m_task_list.empty() && m_working_thread_count == 0;
}

void Scheduler::tickle() { LOG_INFO(root_logger, "调用 Scheduler::tickle()"); }

void Scheduler::run() {
  setHookEnable(true);
  t_scheduler = this;
  // 为调度线程开启协程
  t_scheduler_fiber = Fiber::GetCurrent().get();
  // 线程空闲时执行的协程（注意线程对象是每个run()方法独占的，因为一个线程不可能同时被多个线程执行）
  auto idle_fiber =
      std::make_shared<Fiber>(std::bind(&Scheduler::doIdle, this), true);
  // 开始调度
  Task task;
  while (true) {
    task.clear();
    bool need_tickle = false;
    {
      // 查找等待调度的任务
      WriteScopedLock lock(&m_mutex);
      for (auto iter = m_task_list.begin(); iter != m_task_list.end(); ++iter) {
        // 如果任务指定了要在特定线程执行，但当前线程不是指定线程，通知其他线程处理
        if ((*iter)->thread_id != -1 && (*iter)->thread_id != utils::GetThreadID()) {
          need_tickle = true;
          continue;
        }
        // 跳过正在执行的任务
        if (task.handle && task.handle->state() == Fiber::EXEC) {
          continue;
        }
        // 找到可以执行的任务（且和指定的tid匹配）
        task = **iter; // 拷贝一份，不要拷贝会失效的迭代器（虽然这里是std::list）
        ++m_working_thread_count;
        // 从任务列表里移除该任务
        m_task_list.erase(iter);
        break;
      }
    }
    if (need_tickle) {
      tickle();
    }
    // 如果该任务协程没有停止运行，则换入该协程来执行任务
    if (task.handle && !(task.handle->isFinished())) {
      task.handle->resume();
      --m_working_thread_count;
      // 此时该任务协程已被换出，回到了调度协程。
      switch (task.handle->state()) {
      // 调度执行
      case Fiber::INIT:
      case Fiber::READY:
        schedule(std::move(task.handle), task.thread_id);
        break;
      case Fiber::EXEC:
        throw RuntimeError(fmt::format("协程[{}]执行状态异常：当前状态 {}",
                                       task.handle->id(),
                                       static_cast<int>(task.handle->state())));
        break;
      case Fiber::TERM:
        LOG_FMT_DEBUG(root_logger, "协程[%ld]运行结束", task.handle->id());
        break;
      }
    }
    // 任务队列空了，换入执行 idle_fiber，避免stop整个调度器
    else {
      switch (idle_fiber->state()) {
      case Fiber::INIT:
      case Fiber::READY:
        // 调度idle协程
        ++m_idle_thread_count;
        idle_fiber->resume();
        --m_idle_thread_count;
        break;
      case Fiber::TERM:
        // 当idle协程停止时说明调度器需要结束了
        std::cerr << idle_fiber->id() << " jieshu\n";;
        return;
      case Fiber::EXEC:
        throw RuntimeError(fmt::format("idle协程 {} 执行状态异常：当前状态 {}",
                                       task.handle->id(),
                                       static_cast<int>(task.handle->state())));
        break;
      }
    }
  }
}

} // namespace meha
