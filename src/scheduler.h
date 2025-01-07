#pragma once

#include "fiber.h"
#include "macro.h"
#include "utils/mutex.h"
#include "utils/thread.h"
#include <atomic>
#include <ctime>
#include <list>
#include <memory>
#include <type_traits>
#include <unistd.h>
#include <utility>
#include <variant>
#include <vector>

namespace meha
{

namespace utils
{
namespace internal
{
template<typename Executable>
struct is_valid_task
{
    static constexpr bool value = std::is_same_v<std::remove_cv_t<std::remove_reference_t<Executable>>, meha::Fiber::sptr> || // std::shared_ptr<Fiber>
        std::is_same_v<std::remove_cv_t<std::remove_reference_t<Executable>>, meha::Fiber::FiberFunc> || // std::function<void()>
        std::is_pointer_v<Executable> && std::is_same_v<std::remove_pointer_t<Executable>, void (*)(void)> || // 函数指针
        std::is_invocable_r_v<void, std::decay_t<Executable>>; // 可调用对象（这里和上面的std::functional有啥区别）
};
} // namespace internal

template<typename Executable>
struct is_vaild_task
{
    static constexpr bool value = internal::is_valid_task<std::remove_reference_t<Executable>>::value;
};

// 针对std::variant的特化版本
template<typename... Types>
struct is_vaild_task<std::variant<Types...>>
{
    static constexpr bool value = (internal::is_valid_task<Types>::value || ...);
};
} // namespace utils
// TODO 完善一下上面实现判断合法task类型的模板，是否使用SFINAE

/**
 * @brief 协程调度器
 * @details 封装了 线程N:协程M 的协程调度器，内部有一个线程池，支持协程在线程池的线程间切换
 * @note 由于协程调度器会维护内部的线程池且可以利用所在的线程，因此仅允许一个caller线程中存在一个调度器，这样也就保证了Scheduler是线程内单单例，Scheduler::run方法以外的其他方法不存在线程安全问题
 * */
class Scheduler : public utils::NonCopyable, public std::enable_shared_from_this<Scheduler>
{
private:
    /**
     * @brief 任务包装类
     * @note 任务可以是协程对象，也可以是可调用对象，会自动构造为协程
     * */
    struct Task
    {
        MEHA_PTR_INSIDE_CLASS(Task)

        // uint64_t id;  // 任务id，用于唯一查找该任务 // TODO 复习一下线程池的写法
        // bool removed; // 任务是否被移除
        std::list<Task>::iterator iter; // list迭代器，用于快速删除
        Fiber::sptr handle{nullptr};
        pid_t tid{-1}; // 可选的: 指定执行该任务的线程的id

        explicit Task()
            : handle(nullptr)
            , tid(-1)
        {
        }
        Task(Fiber::sptr f, pid_t tid)
            : handle(std::move(f))
            , tid(tid)
        {
            ASSERT_FMT(f->isScheduled(), "协程必须参与调度器调度");
        }
        Task(const Fiber::FiberFunc &cb, pid_t tid)
            : handle(std::make_shared<Fiber>(cb, true))
            , tid(tid)
        {
        }

        void reset(Task &&rhs = Task())
        {
            std::swap(*this, rhs);
        }
    };

public:
    /**
     * @brief 调度策略 //TODO 还没实现，可以参考一下STL中的适配器
     * @note 由于估计任务运行时间不知道怎么做，所以只设想了这几种
     */
    enum Policy {
        FCFS, // 先来先服务（默认）
        PSA, // 优先级调度
        MIX, // 两者混合
    };

    friend class Fiber;
    MEHA_PTR_INSIDE_CLASS(Scheduler)

    // 获取当前的调度器
    static Scheduler::sptr GetCurrent();
    // 获取当前调度器的调度协程
    static Fiber::sptr GetSchedulerFiber();

public:
    /**
     * @brief 构造函数
     * @param pool_size 线程池线程数量
     * @param use_caller 是否将 Scheduler 所在的线程作为 master fiber
     * */
    explicit Scheduler(size_t pool_size = 1, bool use_caller = true);
    virtual ~Scheduler();
    // 开始调度（初始化线程池并启动所有工作线程，等待添加调度任务）
    void start();
    // 停止调度（回收调度线程）
    void stop();
    virtual bool isStoped() const;
    bool hasIdleThread() const
    {
        return m_idle_thread_count > 0;
    }

    /**
     * @brief 添加任务 thread-safe
     * @param Executable 模板类型必须是 std::shared_ptr<meha::Fiber> 或者
     * std::function<void()>。对于后者会自动构造为协程
     * @param exec Executable 的实例
     * @param instant 是否优先调度
     * @param thread_id 任务要绑定执行线程的id，-1表示任务不绑定线程（即任意线程均可）
     * */
    template<typename Executable>
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
    template<typename InputIterator>
    void schedule(InputIterator begin, InputIterator end)
    {
        // TODO 为IpnutIterator模板参数添加类型判断，必须为顺序访问迭代器
        bool need_tickle = false;
        {
            WriteScopedLock lock(&m_mutex);
            while (begin != end) {
                need_tickle = addTask(std::move(*begin)) || need_tickle;
                ++begin;
            }
        }
        if (need_tickle) {
            tickle();
        }
    }

protected:
    // 执行调度（调度协程入口）
    void run();
    // 通知调度器有新任务
    virtual void tickle();
    // 调度器空闲时的回调函数
    virtual void idle()
    {
        // Idle协程啥也不用做，只是会获取CPU，作用是保持调度器运行，这样才能响应用户的schedule添加调度任务的请求
        std::cerr << ">>> idle id= " << Fiber::GetCurrentID() << ", " << this << '\n';
        while (!isStoped()) {
            ::sleep(1);
            Fiber::Yield(); // 啥也没做直接yield
        }
    }

private:
    /**
     * @brief 添加任务 non-thread-safe
     * @param Executable 可调用对象
     * @param exec Executable 的实例
     * @param thread_id 任务要绑定执行线程的 id //TODO 还没实现
     * @param instant 是否优先调度
     * @return 是否是空闲状态下的第一个新任务（此时需要唤醒调度器来调度执行任务）
     * */
    template<typename Executable>
    bool addTask(Executable &&exec, pid_t thread_id = -1, bool instant = false)
    {
        static_assert(utils::is_vaild_task<Executable>::value, "任务类型必须是std::shared_ptr<meha::Fiber>或std::function<void()>"); // FIXME 怎么支持lambda？
        bool need_tickle = m_task_list.empty();
        auto task = Task(std::forward<Executable>(exec), thread_id);
        // 优先调度的任务放在队首
        if (task.handle) {
            if (instant)
                m_task_list.push_front(std::move(task));
            else
                m_task_list.push_back(std::move(task));
        }
        return need_tickle;
    }

protected:
    // 线程池大小
    size_t m_thread_pool_size = 0;
    // 工作线程数量（工作线程里面跑着任务协程）
    std::atomic_uint64_t m_working_thread_count{0};
    // 空闲线程数量
    std::atomic_uint64_t m_idle_thread_count{0};
    // 是否启动
    bool m_starting = false;
    // 是否停止
    bool m_stopping = false;

private:
    // 调度器所在线程的调度协程，仅在类实例化参数中 use_caller 为 true 时有效
    Fiber::sptr m_caller_fiber;
    // 工作线程池
    std::vector<Thread::uptr> m_thread_pool;
    // 任务队列（任务由任务协程执行，而任务协程由工作线程中的调度者协程调度），是临界资源
    std::list<Task> m_task_list;
    // 用于保护任务队列的读写锁
    mutable RWMutex m_mutex;
};

} // namespace meha
