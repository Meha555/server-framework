#include "cond.h"
#include "io_manager.h"
#include "scheduler.h"
#include "utils.h"
#include "utils/exception.h"
#include <ctime>

#define __THROW_HELPER(expr, err, ...) \
    if (!(expr)) {                     \
        throw err(__VA_ARGS__);        \
    }

namespace meha
{

ConditionVariable::ConditionVariable()
{
    __THROW_HELPER(pthread_cond_init(&m_cond, nullptr) == 0, SystemError, "pthread_cond_init failed");
}

ConditionVariable::~ConditionVariable()
{
    pthread_cond_destroy(&m_cond);
}

void ConditionVariable::wait()
{
    __THROW_HELPER(pthread_cond_wait(&m_cond, &m_mutex) == 0, SystemError, "pthread_cond_wait failed");
}

bool ConditionVariable::timeWait(uint32_t sec)
{
    struct timespec absts;
    clock_gettime(CLOCK_MONOTONIC, &absts); // 需要是绝对时间
    absts.tv_sec += sec;
    return pthread_cond_timedwait(&m_cond, &m_mutex, &absts) == 0;
}

void ConditionVariable::signal()
{
    __THROW_HELPER(pthread_cond_signal(&m_cond) == 0, SystemError, "pthread_cond_signal failed");
}

void ConditionVariable::broadcast()
{
    __THROW_HELPER(pthread_cond_broadcast(&m_cond) == 0, SystemError, "pthread_cond_broadcast failed");
}

FiberConditionVariable::FiberConditionVariable()
    : m_cond(false)
{
    if (!Scheduler::GetCurrent()) { // TODO 这里要判断IOManger,因为IOManager才有addTimer方法
        throw RuntimeError("使用FiberConditionVariable必须有Scheduler");
    }
}

FiberConditionVariable::~FiberConditionVariable()
{
    ASSERT(m_waiting_list.empty());
}

void FiberConditionVariable::wait(SpinLock &mutex)
{
    ASSERT(mutex.isLocked());
    mutex.unLock();
    utils::GenScopeGuard([&mutex] {
        mutex.lock();
    });
    if (m_cond) {
        return;
    } else {
        m_waiting_list.emplace_back(Scheduler::GetCurrent(), Fiber::GetCurrent());
        Fiber::Yield();
    }
}

bool FiberConditionVariable::timeWait(SpinLock &mutex, uint32_t sec)
{
    ASSERT(mutex.isLocked());
    mutex.unLock();
    utils::GenScopeGuard([&mutex] {
        mutex.lock();
    });
    if (m_cond) {
        return true;
    } else {
        auto fiber = Fiber::GetCurrent();
        auto iom = IOManager::GetCurrent();
        ASSERT(iom);
        iom->addTimer(sec * 1000, [iom, fiber] {
            iom->schedule(fiber);
        });
        fiber->yield();
        return false;
    }
}

void FiberConditionVariable::signal()
{
    if (m_waiting_list.empty()) {
        return;
    }
    auto next = m_waiting_list.front();
    m_waiting_list.pop_front();
    next.first->schedule(next.second);
}

void FiberConditionVariable::broadcast()
{
    while (!m_waiting_list.empty()) {
        signal();
    }
}

}

#undef __THROW_HELPER