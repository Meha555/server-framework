#pragma once

#include <list>

#include "noncopyable.h"
#include "mutex.hpp"
#include "fiber.h"

namespace meha {

/**
 * @brief 线程级别的信号量
 * @details 对 semaphore.h 信号量的简单封装
 */
class ThreadSemaphore {
    DISABLE_COPY_MOVE(ThreadSemaphore)
public:
    explicit ThreadSemaphore(uint32_t count);
    ~ThreadSemaphore();
    // -1，值为零时阻塞
    void wait();
    // +1
    void post();

private:
    sem_t m_semaphore;
};

class Scheduler;
/**
 * @brief 协程级别的信号量
 * @details 由于是协程级别的，所以等待队列不能用条件变量实现，得手动用队列
 */
class FiberSemaphore {
    DISABLE_COPY_MOVE(FiberSemaphore)
public:
    explicit FiberSemaphore(uint32_t concurency);
    ~FiberSemaphore();

    bool tryWait();
    void wait();
    void post();
    uint32_t getConcurency() const { return m_concurency; }

private:
    SpinLock m_mutex;  // 这里就要用自旋锁了
    uint32_t m_concurency;
    std::list<std::pair<Scheduler *, typename Fiber::sptr>> m_waiting_fibers;
};

}