#include <fmt/format.h>

#include "config.h"
#include "fiber.h"
#include "module/hook.h"
#include "module/log.h"

#include "scheduler.h"
#include "utils/exception.h"
#include "utils/utils.h"

namespace meha
{

// 当前线程的协程调度器
static thread_local Scheduler *t_scheduler{nullptr};
// 新增：当前线程的调度协程（用于切到调度协程）。加上Fiber模块中记录的当前协程和主协程，现在记录了3个协程
static thread_local Fiber::sptr t_scheduler_fiber{nullptr};

// 调度器tickle频率配置项（默认1s一次）
static ConfigItem<uint64_t>::sptr g_scheduler_tickle_time{Config::Lookup<uint64_t>("scheduler.tickle_time", 1, "单位:us")};

Scheduler* Scheduler::GetCurrent()
{
    if (t_scheduler == nullptr) {
        return nullptr;
    }
    return t_scheduler;
}

Fiber::sptr Scheduler::GetSchedulerFiber()
{
    return t_scheduler_fiber;
}

Scheduler::Scheduler(size_t pool_size, bool use_caller)
{
    ASSERT_FMT(pool_size > 0, "线程池大小不能为空");
    ASSERT_FMT(t_scheduler == nullptr, "每个线程只能有一个调度器");
    // 如果是利用调度器所在线程作为调度线程
    if (use_caller) {
        --pool_size;
        t_scheduler = this;
        Fiber::Init();
        m_callerFiber = std::make_shared<Fiber>(std::bind(&Scheduler::run, this), false); // NOTE 这里必须为false，否则会导致无法换入callerFiber
        t_scheduler_fiber = m_callerFiber;
    }
    // 如果是另开一个线程作为调度线程
    else {
        m_cv = new ConditionVariable();
        m_syncCount = pool_size;
        m_callerFiber = nullptr;
        t_scheduler_fiber = nullptr;
    }
    m_threadPoolSize = pool_size;
    m_threadPool.reserve(m_threadPoolSize);
}

Scheduler::~Scheduler()
{
    ASSERT_FMT(isStoped(), "Scheduler销毁时，要求必须消费完里面的任务");
    // 执行这个析构函数的只可能是caller线程
    // 需要满足caller线程可以再次创建一个Scheduler并启动之，因此要重置线程局部变量
    if (m_cv) {
        delete m_cv;
    }
    t_scheduler = nullptr;
    t_scheduler_fiber = nullptr;
    m_callerFiber = nullptr;
}

void Scheduler::start()
{
    if (m_started) {
        return;
    }
    m_started = true;
    for (size_t i = 0; i < m_threadPoolSize; i++) {
        m_threadPool.emplace_back(std::make_shared<Thread>(std::bind(&Scheduler::run, this)));
        m_threadPool.back()->start();
    }
    if (m_cv) {
        m_cv->wait([this]() {
            return m_syncCount > 0;
        });
        LOG_FMT_DEBUG(root, "WAIT: m_syncCount = %lu", m_syncCount);
    }
}

void Scheduler::stop()
{
    if (!m_started || isStoped()) {
        return;
    }
    m_stopped = true;
    // 必然是调度协程来调用stop

    for (auto &&t : m_threadPool) {
        tickle(); // REVIEW 这里tickle这么多次有用吗？
    }

    if (m_callerFiber) {
        tickle(); // 补上thread_pool-1的caller线程的那次
        // NOTE 对于使用caller线程且pool_size = 1的情况，由于此前线程池为空，此时才换入调度协程（位于caller线程中）开始调度
        m_callerFiber->resume();
    }

    // 等待所有调度线程执行完各自的调度任务
    for (auto &t : m_threadPool) {
        t->join();
    }
}

bool Scheduler::isStoped() const
{
    ScopedLock lock(&m_mutex);
    // 调用过stop、任务列表没有新任务，也没有正在执行的任务，说明调度器已经彻底停止
    return m_stopped && m_taskList.empty() && m_workers == 0;
}

void Scheduler::tickle()
{
    LOG_INFO(core, "tickle!");
}

void Scheduler::idle()
{
    // Idle协程啥也不用做，只是会获取CPU，作用是保持调度器运行，这样才能响应用户的schedule添加调度任务的请求
    LOG(core, TRACE) << "idle协程[" << Fiber::GetCurrentID() << "] on scheduler " << this;
    while (!isStoped()) {
        ::usleep(g_scheduler_tickle_time->getValue());
        Fiber::Yield(); // 啥也没做直接yield
    }
}

void Scheduler::sync()
{
    if (!t_scheduler_fiber) {
        // 为调度线程开启协程
        t_scheduler_fiber = Fiber::GetCurrent();
    }
    if (m_cv && !t_scheduler) {
        t_scheduler = this;
        m_syncCount--;
        if (m_syncCount == 0)
            m_cv->signal();
    }
}

void Scheduler::run()
{
    sync();
    // 开启Hook
    hook::SetHookEnable(true); // FIXME 临时
    auto cleanup = utils::GenScopeGuard([]() {
        // 关闭Hook
        hook::SetHookEnable(false);
    });

    // 该线程空闲时执行的协程（每个执行Scheduler::run方法的线程都有一个idle协程）
    auto idle_fiber = std::make_shared<Fiber>(std::bind(&Scheduler::idle, this), true);
    // 开始调度（死循环，一直遍历任务队列，取出任务，通知空闲的协程来执行。此时来执行的协程是运行在当前线程上的）
    Task task;
    while (true) {
        task.reset();
        bool need_tickle = false;
        {
            // 查找等待调度的任务
            ScopedLock lock(&m_mutex);
            auto it = m_taskList.begin();
            while (it != m_taskList.end()) {
                ASSERT(it->handle);
                // 如果任务指定了要在特定线程执行，但当前线程不是指定线程，通知其他线程处理
                if (it->tid != -1 && it->tid != utils::GetThreadID()) {
                    need_tickle = true;
                    ++it;
                    continue;
                }
                // 跳过正在执行的任务
                if (it->handle->isRunning()) {
                    ++it;
                    continue;
                }
                // 找到可以执行的任务（且和指定的tid匹配）
                task = *it; // 拷贝一份
                m_taskList.erase(it); // 这里要用erase,导致我m_mutex用不了读写锁（否则就会出现读锁里加写锁）
                break; // 直接break了，所以不会迭代器失效（虽然这里是std::list本来也不会失效）
            }
        }
        if (need_tickle) { // 通知其他线程处理
            tickle();
        }
        // 换入该协程来执行任务
        if (task.handle) {
            // 换入执行该任务协程
            if (!task.handle->isTerminated()) {
                ++m_workers;
                task.handle->resume();
                --m_workers;
            }
            // 此时该任务协程已被换出，回到了调度协程
            switch (task.handle->status()) {
            case Fiber::Initialized:
            case Fiber::Ready:
                LOG_FMT_TRACE(core, "工作协程[%ld]调度执行", task.handle->fid());
                // 这种情况要重新塞入队列调度执行
                schedule(task.handle, task.tid);
                break;
            case Fiber::Terminated:
                // 从任务列表里移除该任务
                LOG_FMT_TRACE(core, "工作协程[%ld]运行结束", task.handle->fid());
                if (task.iter) {
                    ScopedLock lock(&m_mutex);
                    m_taskList.erase(*task.iter);
                }
                break;
            case Fiber::Running:
                // 如果换出时还是执行状态就抛异常
                throw RuntimeError(fmt::format("工作协程[{}]执行状态异常：当前状态 {}", task.handle->fid(), static_cast<int>(task.handle->status())));
                break;
            }
        }
        // 任务队列空了，换入执行 idle 协程，避免hang住整个调度器
        else {
            switch (idle_fiber->status()) {
            case Fiber::Initialized:
            case Fiber::Ready:
                LOG_FMT_TRACE(core, "idle协程[%ld]调度执行", idle_fiber->fid());
                // 换入idle协程执行
                ++m_idlers;
                idle_fiber->resume();
                --m_idlers;
                break;
            case Fiber::Terminated:
                // 当idle协程停止时说明调度器需要结束了
                LOG_FMT_TRACE(core, "idle协程[%ld]运行结束", idle_fiber->fid());
                return;
            case Fiber::Running:
                // 如果换出时还是执行状态就抛异常
                throw RuntimeError(fmt::format("idle协程[{}]执行状态异常：当前状态 {}", idle_fiber->fid(), static_cast<int>(idle_fiber->status())));
                break;
            }
        }
    }
}

} // namespace meha
