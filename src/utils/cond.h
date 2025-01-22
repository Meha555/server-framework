#pragma once

#include <cerrno>
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

    template<typename Predicate>
    void wait(Predicate p)
    {
        pthread_mutex_lock(&m_mutex);
        while (!p()) {
            pthread_cond_wait(&m_cond, &m_mutex);
        }
        pthread_mutex_unlock(&m_mutex);
    }
    /**
     * @return true 条件变量触发了
     * @return false 条件变量没有触发
     */
    template<typename Predicate>
    bool timeWait(uint32_t sec, Predicate p)
    {
        struct timespec absts;
        clock_gettime(CLOCK_MONOTONIC, &absts); // 需要是绝对时间
        absts.tv_sec += sec;
        pthread_mutex_lock(&m_mutex);
        int ret = 0;
        while (!p()) {
            ret = pthread_cond_timedwait(&m_cond, &m_mutex, &absts);
            if (ret != 0 && errno == ETIMEDOUT) {
                break;
            }
        }
        pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }

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