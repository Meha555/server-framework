// #ifndef SERVER_FRAMEWORK_SCHEDULER_H
// #define SERVER_FRAMEWORK_SCHEDULER_H
#pragma once

#include "fiber.h"
#include "mutex.hpp"
#include "thread.h"
#include <atomic>
#include <ctime>
#include <list>
#include <memory>
#include <unistd.h>
#include <utility>
#include <variant>
#include <vector>

namespace meha {

/**
 * @brief 协程调度器
 * @details 封装了线程N:协程M的协程调度器，内部有一个线程池，支持协程在线程池的线程间切换
 * @note 由于协程调度器会维护线程池且可以利用所在的线程，因此仅允许一个线程中存在一个调度器
 * */
class Scheduler : public noncopyable {
private:
    /**
     * @brief 任务包装类
     * @note 任务可以是协程对象，也可以是可调用对象，会自动构造为协程
     * */
    struct Task
    {
        using sptr = std::shared_ptr<Task>;
        using uptr = std::unique_ptr<Task>;

        Fiber::sptr handle{nullptr};
        pid_t thread_id{-1};  // 可选的: 制定执行该任务的线程的id

        explicit Task() : handle(nullptr), thread_id(-1) {}
        Task(const Fiber::sptr &f, pid_t tid) : handle(std::move(f)), thread_id(tid) {}
        Task(const Fiber::FiberFunc &cb, pid_t tid) : handle(std::make_shared<Fiber>(cb)), thread_id(tid) {}

        // 重置状态
        void clear()
        {
            handle = nullptr;
            thread_id = -1;
        }
    };

public:
    /**
     * @brief 调度策略 //TODO 还没实现，可以参考一下STL中的适配器
     * @note 由于估计任务运行时间不知道怎么做，所以只设想了这几种
     */
    enum Policy
    {
        FCFS,  // 先来先服务（默认）
        PSA,   // 优先级调度
        MIX,   // 两者混合
    };

    friend class Fiber;
    using sptr = std::shared_ptr<Scheduler>;
    using uptr = std::unique_ptr<Scheduler>;

    // 获取当前的调度器
    static Scheduler *GetCurrent();
    // 获取当前调度器的调度协程
    static Fiber *GetMainFiber();

public:
    /**
     * @brief 构造函数
     * @param pool_size 线程池线程数量
     * @param as_master 是否将 Scheduler 所在的线程作为 master fiber
     * @param name 调度器名称
     * */
    explicit Scheduler(size_t pool_size = 1, bool as_master = true);
    virtual ~Scheduler();
    // 开始调度（启动调度线程）
    void start();
    // 停止调度（回收调度线程）
    void stop();
    virtual bool isStoped() const;
    bool hasIdleThread() const { return m_idle_thread_count > 0; }

    /**
     * @brief 添加任务 thread-safe
     * @param Executable 模板类型必须是 std::unique_ptr<meha::Fiber> 或者 std::function
     * @param exec Executable 的实例
     * @param instant 是否优先调度
     * @param thread_id 任务要绑定执行线程的 id，-1表示任务不绑定线程（即任意线程均可）
     * */
    template <typename Executable>
    void schedule(Executable &&exec, pid_t thread_id = -1, bool instant = false)
    {
        bool need_tickle = false;
        {
            WriteScopedLock lock(&m_mutex);
            need_tickle = addTask(std::forward<Executable>(exec), thread_id);
        }
        // 通知调度器开始新的任务分发调度
        if (need_tickle) {
            tickle();
        }
    }

    /**
     * @brief 添加多个任务 thread-safe
     * @param begin 单向迭代器
     * @param end 单向迭代器
     */
    template <typename InputIterator>
    void schedule(InputIterator begin, InputIterator end)
    {
        bool need_tickle = false;
        {
            WriteScopedLock lock(&m_mutex);
            while (begin != end) {
                need_tickle = addTask(*begin) || need_tickle;
                ++begin;
            }
        }
        if (need_tickle) {
            tickle();
        }
    }

protected:
    // 执行调度
    void run();
    // 以下函数没有写成static的是希望继承时重写

    // 通知调度器有新任务
    virtual void tickle();
    // 调度器停止时的回调函数，返回调度器当前是否处于停止工作的状态
    virtual bool doStop() const { return isStoped(); }
    // 调度器空闲时的回调函数
    virtual void doIdle()
    {
        while (!isStoped()) {
            // Fiber::YieldToHold(); //FIXME
        }
    }

private:
    /**
     * @brief 添加任务 non-thread-safe
     * @param Executable 模板类型必须是 std::unique_ptr<meha::Fiber> 或者 std::function
     * @param exec Executable 的实例
     * @param thread_id 任务要绑定执行线程的 id //TODO 还没实现
     * @param instant 是否优先调度
     * @return 是否是空闲状态下的第一个新任务（//REVIEW 注意是空闲状态，这是为什么）
     * */
    template <typename Executable>
    bool addTask(Executable &&exec, pid_t thread_id = -1, bool instant = false)
    {
        bool need_tickle = m_task_list.empty();
        auto task = std::make_unique<Task>(std::forward<Executable>(exec), thread_id);
        if (task->handle) {
            if (instant)
                m_task_list.push_front(std::move(task));
            else
                m_task_list.push_back(std::move(task));
        }
        return need_tickle;
    }

protected:
    // 复用主线程 id，仅在 as_master == true 时会被设置有效线程 id
    pid_t m_caller_thread_id = 0;
    // 线程池大小
    size_t m_thread_pool_size = 0;
    // 工作线程数量（工作线程里面跑着任务协程）
    std::atomic_uint64_t m_working_thread_count{0};
    // 空闲线程数量
    std::atomic_uint64_t m_idle_thread_count{0};
    // 执行停止状态
    bool m_stoped = true;
    // 是否自动停止
    bool m_auto_stop = false;

private:
    mutable RWLock m_mutex;
    // 调度者协程，仅在类实例化参数中 as_master 为 true 时有效
    Fiber::sptr m_caller_fiber;
    // 工作线程池
    std::vector<Thread::sptr> m_thread_pool;
    // 任务队列（任务由任务协程执行，而任务协程由工作线程中的调度者协程调度）
    std::list<Task::sptr> m_task_list;
};
}  // namespace meha

// #endif  // SERVER_FRAMEWORK_SCHEDULER_H
