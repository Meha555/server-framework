#pragma once

#include <cstdint>
extern "C" {
#include <pthread.h>
}
#include <deque>
#include <memory>

#include "utils/mutex.h"
#include "utils/noncopyable.h"

namespace meha
{

/**
 * @brief 线程级别的条件变量
 * @details 对 pthread_cond_t 条件变量的简单封装
 */
class ConditionVariable
{
    DISABLE_COPY_MOVE(ConditionVariable)
public:
    explicit ConditionVariable();
    ~ConditionVariable();
    void wait();
    /**
     * @return true 条件变量触发了
     * @return false 条件变量没有触发
     */
    bool timeWait(uint32_t sec);

    void signal();
    void broadcast();

private:
    pthread_cond_t m_cond;
    pthread_mutex_t m_mutex;
};

class Scheduler;
class Fiber;
/**
 * @brief 协程级别的条件变量
 * @details 由于是协程级别的，所以等待队列不能用条件变量实现，得手动用队列
 */
class FiberConditionVariable
{
    DISABLE_COPY_MOVE(FiberConditionVariable)
public:
    explicit FiberConditionVariable();
    ~FiberConditionVariable();
    void wait(SpinLock &mutex);
    /**
     * @return true 条件变量触发了
     * @return false 条件变量没有触发
     */
    bool timeWait(SpinLock &mutex, uint32_t sec);

    void signal();
    void broadcast();

private:
    bool m_cond;
    std::deque<std::pair<std::shared_ptr<Scheduler>, std::shared_ptr<Fiber>>> m_waiting_list;
};

}