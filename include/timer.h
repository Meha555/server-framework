#pragma once

#include <functional>
#include <memory>
#include <set>

#include "mutex.hpp"

namespace meha
{

class TimerManager;

/**
 * @brief 定时器类
 */
class Timer : public std::enable_shared_from_this<Timer>
{
    friend class TimerManager;

public:
    using sptr = std::shared_ptr<Timer>;
    using TimeOutFunc = std::function<void()>;

    /**
     * @brief 取消定时器
     */
    void cancel();

    /**
     * @brief 重设超时间隔
     * @param elapse 新的ms超时
     * @param from_now 是否立即开始倒计时
     */
    bool reset(uint64_t elapse, bool from_now);

    /**
     * @brief 重新计时
     */
    bool restart();

private:
    /**
     * @brief Constructor
     * @param elapse 延迟时间(ms)
     * @param fn 回调函数
     * @param cyclic 是否重复执行
     * @param manager 执行环境
     */
    Timer(uint64_t elapse, TimeOutFunc cb, bool cyclic, TimerManager *manager);

    /**
     * @brief 用于创建只有时间信息的定时器，基本是用于查找超时的定时器，无其他作用
     */
    explicit Timer(uint64_t next);

private:
    bool m_cyclic = false; // 是否重复
    uint64_t m_elapsetime_relative = 0; // 相对超时时间（ms）
    uint64_t m_nexttime_absolute = 0; // 绝对超时时间戳（ms）
    TimeOutFunc m_callback{nullptr}; // 定时任务回调
    TimerManager *m_manager = nullptr;

private:
    // 比较两个Timer对象，比较的依据是绝对超时时间。这里也可以直接重写operator<
    struct Comparator
    {
        bool operator()(const Timer::sptr &lhs, const Timer::sptr &rhs) const;
    };
};

/**
 * @brief 定时器调度类
 * @details 基于小根堆实现
 */
class TimerManager
{
    friend class Timer;

public:
    TimerManager();
    virtual ~TimerManager() = default;

    /**
     * @brief 新增一个普通定时器
     * @param ms 延迟毫秒数
     * @param fn 回调函数
     * @param weak_cond 执行条件
     * @param cyclic 是否重复执行
     */
    Timer::sptr addTimer(uint64_t ms, Timer::TimeOutFunc fn, bool cyclic = false);

    /**
     * @brief 新增一个条件定时器。当到达执行时间时，提供的条件变量依旧有效，则执行，否则不执行
     * @param ms 延迟毫秒数
     * @param fn 回调函数
     * @param weak_cond 条件变量，利用智能指针是否有效作为判断条件
     * @param cyclic 是否重复执行
     */
    Timer::sptr addConditionalTimer(uint64_t ms, Timer::TimeOutFunc fn, std::weak_ptr<void> weak_cond, bool cyclic = false);

    /**
     * @brief 获取下一个定时器的等待时间
     * @return 返回结果分为三种：无定时器等待执行返回 ~0ull，存在超时未执行的定时器返回 0，存在等待执行的定时器返回剩余的等待时间
     */
    uint64_t getNextTimer() const;

    /**
     * @brief 获取所有等待超时的定时器的回调函数对象，并将定时器从队列中移除，这个函数会自动将周期调用的定时器存回队列
     */
    void listExpiredCallback(std::vector<Timer::TimeOutFunc> &fns);

    /**
     * @brief 检查是否有等待执行的定时器
     */
    bool hasTimer();

protected:
    /**
     * @brief 当创建了延迟时间最短的定时任务时，会调用此函数
     * TimerManager通过该方法来通知IOManager立刻更新当前的epoll_wait超时
     */
    virtual void onTimerInsertedAtFront() = 0;

    /**
     * @brief 添加已有的定时器对象，该函数只是为了代码复用
     */
    void addTimer(Timer::sptr timer, WriteScopedLock &lock);

    /**
     * @brief 删除指定的定时器
     * @return 是否删除成功
     */
    bool delTimer(Timer::sptr timer);

private:
    /**
     * @brief 检查系统时间是否被修改成更早的时间
     */
    bool detectClockRollover(uint64_t now_ms);

private:
    mutable RWLock m_lock;
    std::set<Timer::sptr, Timer::Comparator> m_timers; // 这里没有用std::priority_queue，因为其无法遍历(堆本身就不是查找数据的结构)
    uint64_t m_previous_time = 0; // 当前系统时间戳
};

} // end namespace meha
