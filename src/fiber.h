#pragma once

#include <functional>
#include <memory>
#include <optional>
extern "C" {
#include <ucontext.h>
}

#include "macro.h"
#include "utils/noncopyable.h"

namespace meha
{

class Scheduler;

/**
 * @brief 协程类（不可拷贝但可移动）
 * @note 因为Fiber需要获取自身的智能指针，因此需要继承enable_shared_from_this。
 * @details 由于协程本身是单核并发，因此无需确保“协程安全”
 */
class Fiber : public utils::NonCopyable, public std::enable_shared_from_this<Fiber>
{
    friend class Scheduler;

public:
    MEHA_PTR_INSIDE_CLASS(Fiber)
    using FiberFunc = std::function<void()>;

    // 协程状态，用于调度
    enum Status {
        Initialized, // 初始化（尚未执行过）
        Ready, // 就绪（准备好执行）
        Running, // 执行（正在运行）
        Terminated, // 结束（运行结束）
    };

    /**
     * @brief 创建新子协程
     * @param callback 协程执行函数
     * @param scheduled 是否参与协程调度器调度
     * @param stack_size 协程栈大小，如果传 0，使用配置项 "fiber.stack_size" 的值
     * */
    explicit Fiber(FiberFunc callback, bool scheduled, size_t stack_size = 0);

    // TODO 三五法则
    Fiber(Fiber &&rhs) noexcept;
    Fiber &operator=(Fiber &&rhs) noexcept;
    ~Fiber();

    /**
     * @brief 复用栈更换协程执行函数
     * @param callback 要更换为的执行函数
     * @note 此时协程的状态应该是 Initialized 或 Terminated 或 ERROR
     */
    void reset(FiberFunc &&callback) noexcept;

    /**
     * @brief 换入该协程执行
     * @details 该协程状态变为EXEC，正在运行的协程状态变为HOLD
     * @note 在主协程中调用该函数，因此正在运行的写成是主协程
     */
    void resume();

    /**
     * @brief 挂起当前协程，切回主协程
     * @details 当前协程与上次
     * @note 在该协程中调用该函数
     */
    void yield();

    uint64_t fid() const
    {
        return m_fid;
    }
    Status status() const
    {
        return m_status;
    }

    bool isRunning() const noexcept
    {
        return m_status == Running;
    }
    bool isTerminated() const noexcept
    {
        return m_status == Terminated;
    }
    bool isScheduled() const noexcept
    {
        return m_scheduled;
    }

private:
    // 无参构造用于创建 master fiber，设置为私有的即不允许用户创建master fiber
    explicit Fiber();
    // // 换出old_fiber，换入当前协程
    // void swapOutof(Fiber *old_fiber);
    // // 换出当前协程，换入new_fiber
    // void swapInto(Fiber *new_fiber);
    // 换出from协程，换入to协程
    static void SwapFromTo(Fiber *from, Fiber *to);
    // 设置当前执行的协程
    static void SetCurrent(Fiber *fiber);
    // 协程入口函数（makecontext的第二个参数）
    static void Run();

public:
    // 在当前线程上创建主协程。线程如果要创建协程就要先执行这个函数
    static void Init();
    // static-wrapper：挂起当前协程
    static void Yield();
    // 获取当前正在执行的协程的智能指针，如果不存在，则在当前线程上创建主协程（通过调Init实现）。
    static sptr GetCurrent();
    // 获取当前协程 id
    static int64_t GetCurrentID();
    // 获取当前协程状态
    static std::optional<Status> GetCurrentState();
    // 获取存在的协程数量
    static uint64_t TotalFibers();

private:
    uint64_t m_fid; // 协程 id
    uint64_t m_stack_size; // 协程栈大小
    Status m_status; // 协程状态
    ucontext_t m_ctx; // 当前协程上下文
    void *m_stack; // 协程栈空间指针
    FiberFunc m_callback; // 协程执行函数
    bool m_scheduled; // 是否参与协程调度器调度
};

} // namespace meha
