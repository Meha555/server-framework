#include "timer.h"
#include "utils/utils.h"

namespace meha
{

bool Timer::Comparator::operator()(const Timer::sptr &lhs, const Timer::sptr &rhs) const
{
    // 判断指针的有效性
    if (!lhs && !rhs)
        return false;
    else if (!lhs)
        return true;
    else if (!rhs)
        return false;
    // 按绝对时间戳排序
    if (lhs->m_nexttime_absolute != rhs->m_nexttime_absolute)
        return lhs->m_nexttime_absolute < rhs->m_nexttime_absolute;
    else // 时间戳相同就按指针排序
        return lhs.get() < rhs.get();
}

Timer::Timer(uint64_t elapse, TimeOutFunc cb, bool cyclic, TimerManager *manager)
    : m_cyclic(cyclic)
    , m_elapsetime_relative(elapse)
    , m_callback(cb)
    , m_manager(manager)
{
    m_nexttime_absolute = utils::GetCurrentMS().count() + m_elapsetime_relative;
}

Timer::Timer(uint64_t next)
    : m_nexttime_absolute(next)
{
}

void Timer::cancel()
{
    WriteScopedLock lock(&m_manager->m_lock);
    if (m_callback) {
        m_callback = nullptr;
        m_manager->delTimer(shared_from_this());
    }
}

bool Timer::reset(uint64_t elapse, bool from_now)
{
    if (elapse == m_elapsetime_relative && !from_now) {
        return true;
    }
    if (!m_callback) {
        return false;
    }
    WriteScopedLock lock(&m_manager->m_lock);
    m_manager->delTimer(shared_from_this());
    uint64_t start = 0;
    // 重新计时
    if (from_now) {
        start = utils::GetCurrentMS().count();
    } else {
        start = m_nexttime_absolute - m_elapsetime_relative;
    }
    m_elapsetime_relative = elapse;
    m_nexttime_absolute = start + elapse;
    m_manager->addTimer(shared_from_this(), lock); // FIXME 这里这样加速安全吗？RWMutex是递归锁？
    return true;
}

bool Timer::restart()
{
    if (!m_callback) {
        return false;
    }
    WriteScopedLock lock(&m_manager->m_lock);
    m_manager->delTimer(shared_from_this());
    m_nexttime_absolute = utils::GetCurrentMS().count() + m_elapsetime_relative;
    m_manager->addTimer(shared_from_this(), lock);
    return true;
}

TimerManager::TimerManager()
{
    m_previous_time = utils::GetCurrentMS().count();
}

Timer::sptr TimerManager::addTimer(uint64_t ms, Timer::TimeOutFunc fn, bool cyclic)
{
    Timer::sptr timer(new Timer(ms, fn, cyclic, this));
    WriteScopedLock lock(&m_lock);
    addTimer(timer, lock);
    return timer;
}

void TimerManager::addTimer(Timer::sptr timer, WriteScopedLock &lock)
{
    auto it = m_timers.insert(timer).first;
    bool at_front = (it == m_timers.begin());
    lock.unLock();
    if (at_front) {
        onTimerInsertedAtFront();
    }
}

bool TimerManager::delTimer(Timer::sptr timer)
{
    auto it = m_timers.find(timer);
    if (it == m_timers.end()) {
        return false;
    }
    m_timers.erase(it);
    return true;
}

static void OnTimer(std::weak_ptr<void> weak_cond, Timer::TimeOutFunc fn)
{
    auto tmp = weak_cond.lock();
    if (tmp) {
        fn();
    }
}

Timer::sptr TimerManager::addConditionalTimer(uint64_t ms, Timer::TimeOutFunc fn, std::weak_ptr<void> weak_cond, bool cyclic)
{
    return addTimer(ms, std::bind(&OnTimer, weak_cond, fn), cyclic);
}

uint64_t TimerManager::getNextTimer() const
{
    ReadScopedLock lock(&m_lock);
    if (m_timers.empty()) {
        // 没有定时器
        return ~0ull;
    }
    const Timer::sptr &next = *m_timers.begin();
    uint64_t now_ms = utils::GetCurrentMS().count();
    if (now_ms >= next->m_nexttime_absolute) {
        // 等待超时
        return 0;
    } else {
        // 返回剩余的等待时间
        return next->m_nexttime_absolute - now_ms;
    }
}

void TimerManager::listExpiredCallback(std::vector<Timer::TimeOutFunc> &fns)
{
    uint64_t now_ms = utils::GetCurrentMS().count();
    if (!hasTimer()) {
        return;
    }
    WriteScopedLock lock(&m_lock);
    // 检查系统时间是否被修改
    const bool rollover = detectClockRollover(now_ms);
    // 系统时间未被回拨，并且无定时器等待超时
    if (!rollover && (*m_timers.begin())->m_nexttime_absolute > now_ms) {
        return;
    }
    Timer::sptr now_timer(new Timer(now_ms)); // 插入一个用于二分查找的临时定时器
    // 获取第一个 m_next 大于或等于 now_timer->m_next 的定时器的迭代器
    // 就是已经等待到达或超时的定时器。
    // ** 如果系统时间被修改过，直接认定所有定时器均超时 **
    auto it = rollover ? m_timers.end() : m_timers.lower_bound(now_timer);
    // 包括上到达指定时间的定时器
    while (it != m_timers.end() && (*it)->m_nexttime_absolute == now_timer->m_nexttime_absolute) {
        ++it;
    }
    std::vector<Timer::sptr> expired;
    // 取出超时的定时器
    expired.insert(expired.begin(), m_timers.begin(), it);
    m_timers.erase(m_timers.begin(), it);
    fns.reserve(expired.size());
    for (auto &timer : expired) {
        fns.push_back(timer->m_callback);
        // 处理周期定时器
        if (timer->m_cyclic) {
            timer->m_nexttime_absolute = now_ms + timer->m_elapsetime_relative;
            m_timers.insert(timer);
        } else {
            timer->m_callback = nullptr;
        }
    }
}

bool TimerManager::hasTimer()
{
    ReadScopedLock lock(&m_lock);
    return !m_timers.empty();
}

bool TimerManager::detectClockRollover(uint64_t now_ms)
{
    bool rollover = false;
    // 系统时间被回拨超过一个小时
    if (now_ms < (m_previous_time - 60 * 60 * 1000)) {
        rollover = true;
    }
    m_previous_time = now_ms;
    return rollover;
}

} // namespace meha
