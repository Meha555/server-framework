#include "sem.h"
#include "scheduler.h"
#include "utils/exception.h"

// FIXME sylar的处理方式不使用assert,而是用抛std::logic_error
// 不用assert我可以理解，因为assert只在debug下启用，但是为啥是逻辑错误呢

#define __THROW_HELPER(expr, err, ...) \
    if (!(expr)) {                     \
        throw err(__VA_ARGS__);        \
    }

namespace meha
{

Semaphore::Semaphore(uint32_t count)
{
    __THROW_HELPER(sem_init(&m_semaphore, 0, count) == 0, SystemError, "sem_init failed");
}

uint32_t Semaphore::concurrency()
{
    int32_t count;
    __THROW_HELPER(sem_getvalue(&m_semaphore, &count) == 0, SystemError, "sem_getvalue failed");
    return count;
}

Semaphore::~Semaphore()
{
    sem_destroy(&m_semaphore);
}

void Semaphore::wait()
{
    __THROW_HELPER(sem_wait(&m_semaphore) == 0, SystemError, "sem_wait failed");
}

void Semaphore::post()
{
    __THROW_HELPER(sem_post(&m_semaphore) == 0, SystemError, "sem_post failed");
}

bool Semaphore::tryWait()
{
    return sem_trywait(&m_semaphore) == 0;
}

FiberSemaphore::FiberSemaphore(uint32_t concurrency)
    : m_concurency(concurrency)
{
    // 仿照DrmGammaRamp，在构造函数中检查条件，失败时根本不创建这个对象
    // 这样后续调用成员函数的时候就不需要每个成员函数都判断一遍了
    if (!Scheduler::GetCurrent()) {
        throw RuntimeError("使用FiberSemaphore必须有Scheduler");
    }
}

FiberSemaphore::~FiberSemaphore()
{
    ASSERT(m_waiting_list.empty());
}

bool FiberSemaphore::tryWait()
{
    SpinScopedLock lock(&m_mutex);
    if (m_concurency > 0) {
        --m_concurency;
        return true;
    } else
        return false;
}

void FiberSemaphore::wait()
{
    SpinScopedLock lock(&m_mutex);
    if (m_concurency > 0) {
        --m_concurency;
        return;
    } else {
        m_waiting_list.emplace_back(Scheduler::GetCurrent(), Fiber::GetCurrent());
        // 挂在信号量等待队列上，解锁，挂起当前协程
        Fiber::Yield();
    }
}

void FiberSemaphore::post()
{
    SpinScopedLock lock(&m_mutex);
    if (!m_waiting_list.empty()) {
        auto next = m_waiting_list.front(); // 取队首协程来调度
        m_waiting_list.pop_front();
        next.first->schedule(next.second);
    } else {
        ++m_concurency;
    }
}

uint32_t FiberSemaphore::concurrency() const
{
    return m_concurency;
}

}

#undef __THROW_HELPER