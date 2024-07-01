// #ifndef SERVER_FRAMEWORK_SCHEDULER_H
// #define SERVER_FRAMEWORK_SCHEDULER_H
#pragma once

#include "fiber.h"
#include "mutex.hpp"
#include "thread.h"
#include <atomic>
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
 * */
class Scheduler : public noncopyable {
private:
    /**
     * @brief 任务包装类
     * 等待分配线程执行的任务，可以是 meha::Fiber 或 std::function
     * */
    struct Task
    {
        using sptr = std::shared_ptr<Task>;
        using uptr = std::unique_ptr<Task>;
        using TaskFunc = std::function<void()>;
        using TaskFiber = Fiber::sptr;

        std::variant<TaskFiber, TaskFunc, std::nullptr_t> handle{nullptr};
        pid_t thread_id{-1};  // 可选的: 制定执行该任务的线程的id

        explicit Task() : handle(nullptr), thread_id(-1) {}
        Task(decltype(handle) &&v, pid_t tid) : handle(std::move(v)), thread_id(tid) {}

        // 重置状态
        void clear()
        {
            handle = nullptr;
            thread_id = -1;
        }
    };

public:
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
     * @param thread_size 线程池线程数量
     * @param use_caller 是否将 Scheduler 所在的线程作为 master fiber
     * @param name 调度器名称
     * */
    explicit Scheduler(size_t thread_size = 1, bool use_caller = true, std::string name = "default");
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
            Fiber::YieldToHold();
        }
    }

private:
    /**
     * @brief 添加任务 non-thread-safe
     * @param Executable 模板类型必须是 std::unique_ptr<meha::Fiber> 或者 std::function
     * @param exec Executable 的实例
     * @param thread_id 任务要绑定执行线程的 id
     * @param instant 是否优先调度
     * @return 是否是空闲状态下的第一个新任务（//REVIEW 注意是空闲状态，这是为什么）
     * */
    template <typename Executable>
    bool addTask(Executable &&exec, pid_t thread_id = -1, bool instant = false)
    {
        bool need_tickle = m_task_list.empty();
        auto task = std::make_unique<Task>(std::forward<Executable>(exec), thread_id);
        // 创建的任务实例存在有效的 meha::Fiber 或 std::function
        if (std::holds_alternative<Task::TaskFiber>(task->handle) ||
            std::holds_alternative<Task::TaskFunc>(task->handle)) {
            if (instant)
                m_task_list.push_front(std::move(task));
            else
                m_task_list.push_back(std::move(task));
        }
        return need_tickle;
    }

protected:
    const std::string m_name;
    // 复用主线程 id，仅在 use_caller 为 true 时会被设置有效线程 id
    pid_t m_caller_thread_id = 0;
    // 有效线程数量
    size_t m_thread_count = 0;
    // 活跃线程数量
    std::atomic_uint64_t m_active_thread_count{0};
    // 空闲线程数量
    std::atomic_uint64_t m_idle_thread_count{0};
    // 执行停止状态
    bool m_stoped = true;
    // 是否自动停止
    bool m_auto_stop = false;

private:
    mutable RWLock m_mutex;
    // 负责调度的协程，仅在类实例化参数中 use_caller 为 true 时有效
    Fiber::sptr m_caller_fiber;
    // 调度线程池
    std::vector<Thread::sptr> m_thread_pool;
    // 任务队列（任务由协程执行，而协程由线程调度）
    std::list<Task::sptr> m_task_list;
};
}  // namespace meha

// #endif  // SERVER_FRAMEWORK_SCHEDULER_H
