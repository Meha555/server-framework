#pragma once

extern "C" {
#include <pthread.h>
#include <semaphore.h>
}
#include <deque>
#include <memory>

#include "utils/mutex.h"
#include "utils/noncopyable.h"

namespace meha
{

/**
 * @brief 线程级别的信号量
 * @details 对 semaphore.h 信号量的简单封装
 */
class Semaphore
{
    DISABLE_COPY_MOVE(Semaphore)
public:
    explicit Semaphore(uint32_t count);
    ~Semaphore();
    // -1，值为零时阻塞
    void wait();
    bool tryWait();
    // +1
    void post();

    uint32_t concurrency();

private:
    sem_t m_semaphore; // REVIEW 这里需要加mutable吗
};

class Scheduler;
class Fiber;
/**
 * @brief 协程级别的信号量
 * @details 由于是协程级别的，所以等待队列不能用条件变量实现，得手动用队列
 */
class FiberSemaphore
{
    DISABLE_COPY_MOVE(FiberSemaphore)
public:
    explicit FiberSemaphore(uint32_t concurrency);
    ~FiberSemaphore();
    // -1，值为零时阻塞
    bool tryWait();
    void wait();
    // +1
    void post();

    uint32_t concurrency() const;

private:
    SpinLock m_mutex; // NOTE 这里就要用自旋锁了，因为用Mutex会阻塞掉。那么CASLock呢？？
    uint32_t m_concurency;
    std::deque<std::pair<std::shared_ptr<Scheduler>, std::shared_ptr<Fiber>>> m_waiting_list; // 等待队列
};

}