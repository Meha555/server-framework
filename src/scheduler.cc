#include <fmt/format.h>

#include "fiber.h"
#include "module/hook.h"
#include "module/log.h"

#include "scheduler.h"
#include "utils/exception.h"
#include "utils/utils.h"

namespace meha
{

// 当前线程的协程调度器
static thread_local Scheduler::sptr t_scheduler{nullptr};
// 新增：当前线程的调度协程（用于切到调度协程）。加上Fiber模块中记录的当前协程和主协程，现在记录了3个协程
static thread_local Fiber::sptr t_scheduler_fiber{nullptr};

Scheduler::sptr Scheduler::GetCurrent()
{
    return t_scheduler;
}

Fiber::sptr Scheduler::GetSchedulerFiber()
{
    return t_scheduler_fiber;
}

Scheduler::Scheduler(size_t pool_size, bool use_caller)
{
    ASSERT_FMT(pool_size > 0, "线程池大小不能为空");
    ASSERT_FMT(GetCurrent() == nullptr, "每个线程只能有一个调度器");
    // 如果是利用调度器所在线程作为调度线程
    if (use_caller) {
        --pool_size;
        t_scheduler = shared_from_this();
        Fiber::Init();
        m_caller_fiber = std::make_shared<Fiber>(std::bind(&Scheduler::run, this), true);
        t_scheduler_fiber = m_caller_fiber;
    }
    // 如果是另开一个线程作为调度线程
    else {
        m_caller_fiber = nullptr;
        t_scheduler_fiber = nullptr;
    }
    m_thread_pool_size = pool_size;
    m_thread_pool.reserve(m_thread_pool_size);
}

Scheduler::~Scheduler()
{
    ASSERT(isStoped());
    // 需要满足caller线程可以再次创建一个Scheduler并启动之，因此要重置线程局部变量
    // 判断一下析构的位置是否在caller线程中 // REVIEW 执行这个析构函数的只可能是caller线程吧，所以这个判断没有什么意义
    // if (GetCurrent().get() == this) {
    t_scheduler = nullptr;
    t_scheduler_fiber = nullptr;
    // }
    m_caller_fiber = nullptr;
}

void Scheduler::start()
{
    if (m_starting) { // 调度器已经开始工作，避免重复启动
        return;
    }
    m_starting = true;
    for (size_t i = 0; i < m_thread_pool_size; i++) {
        m_thread_pool.emplace_back(std::make_unique<Thread>(std::bind(&Scheduler::run, this)));
    }
}

void Scheduler::stop()
{
    if (!m_starting || isStoped()) {
        return;
    }
    m_stopping = true;
    // 必然是调度协程来调用stop // REVIEW 因为stop函数本来就是caller线程调用的，所以这个判断没有什么意义
    // if (m_caller_fiber) {
    //     ASSERT(GetCurrent().get() == this);
    // } else {
    //     ASSERT(GetCurrent().get() != this);
    // }

    for (auto &&t : m_thread_pool) {
        tickle(); // REVIEW 这里tickle这么多次有用吗？
    }

    if (m_caller_fiber) {
        tickle(); // 补上thread_pool-1的caller线程的那次
        // 对于使用caller线程且pool_size = 1的情况，由于此前线程池为空，此时才换入调度协程开始调度
        m_caller_fiber->resume();
    }

    // 等待所有调度线程执行完各自的调度任务
    for (auto &&t : m_thread_pool) {
        t->join();
    }
}

bool Scheduler::isStoped() const
{
    ReadScopedLock lock(&m_mutex);
    // 调用过stop、任务列表没有新任务，也没有正在执行的任务，说明调度器已经彻底停止
    return m_stopping && m_task_list.empty() && m_working_thread_count == 0;
}

void Scheduler::tickle()
{
    LOG_INFO(core, "调用 Scheduler::tickle()");
}

void Scheduler::run()
{
    hook::SetHookEnable(true);
    t_scheduler = shared_from_this();
    // 为调度线程开启协程
    t_scheduler_fiber = Fiber::GetCurrent();
    // 该线程空闲时执行的协程（每个执行Scheduler::run方法的线程都有一个idle协程）
    auto idle_fiber = std::make_shared<Fiber>(std::bind(&Scheduler::idle, this), true);
    // 开始调度
    Task task;
    while (true) {
        task.reset();
        bool need_tickle = false;
        {
            // 查找等待调度的任务
            ReadScopedLock lock(&m_mutex);
            auto it = m_task_list.begin();
            while (it != m_task_list.end()) {
                // 如果任务指定了要在特定线程执行，但当前线程不是指定线程，通知其他线程处理
                if (it->tid != -1 && it->tid != utils::GetThreadID()) {
                    need_tickle = true;
                    ++it;
                    continue;
                }
                // 跳过正在执行的任务
                if (task.handle && task.handle->status() == Fiber::Running) {
                    ++it;
                    continue;
                }
                // 找到可以执行的任务（且和指定的tid匹配）
                task.iter = it;
                task = *it; // 拷贝一份
                ++m_working_thread_count;
                break; // 直接break了，所以不会迭代器失效（虽然这里是std::list）
            }
        }
        if (need_tickle) { // 通知其他线程处理
            tickle();
        }
        // 如果该任务协程没有停止运行，则换入该协程来执行任务
        if (task.handle && !(task.handle->isFinished())) {
            // 换入执行该任务协程
            task.handle->resume();
            --m_working_thread_count;
            // 此时该任务协程已被换出，回到了调度协程
            switch (task.handle->status()) {
            case Fiber::Initialized:
            case Fiber::Ready:
                // 这种情况要重新塞入队列调度执行
                schedule(std::move(task.handle), task.tid);
                break;
            case Fiber::Terminated:
                // 从任务列表里移除该任务
                LOG_FMT_DEBUG(core, "协程[%ld]运行结束", task.handle->fid());
                m_mutex.writeLock();
                m_task_list.erase(task.iter);
                m_mutex.unLock();
                break;
            case Fiber::Running:
                // 如果换出时还是执行状态就抛异常
                throw RuntimeError(fmt::format("协程[{}]执行状态异常：当前状态 {}", task.handle->fid(), static_cast<int>(task.handle->status())));
                break;
            }
        }
        // 任务队列空了，换入执行 idle_fiber，避免hang整个调度器
        else {
            switch (idle_fiber->status()) {
            case Fiber::Initialized:
            case Fiber::Ready:
                // 换入idle协程执行
                ++m_idle_thread_count;
                idle_fiber->resume();
                --m_idle_thread_count;
                break;
            case Fiber::Terminated:
                // 当idle协程停止时说明调度器需要结束了
                LOG_FMT_DEBUG(core, "idle协程[%ld]运行结束", idle_fiber->fid());
                return;
            case Fiber::Running:
                // 如果换出时还是执行状态就抛异常
                throw RuntimeError(fmt::format("idle协程[{}]执行状态异常：当前状态 {}", task.handle->fid(), static_cast<int>(task.handle->status())));
                break;
            }
        }
    }
}

} // namespace meha
