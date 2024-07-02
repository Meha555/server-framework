/*
 * @Author: Meha555 wdsjhzy@163.com
 * @Date: 2024-07-01 17:54:40
 * @LastEditors: Meha555 wdsjhzy@163.com
 * @LastEditTime: 2024-07-02 10:05:03
 * @FilePath: /server-framework/source/sem.cc
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
#include "sem.h"
#include "log.h"
#include "scheduler.h"

using namespace meha;

static Logger::ptr root_logger = GET_LOGGER("root");

ThreadSemaphore::ThreadSemaphore(uint32_t count)
{
    ASSERT(sem_init(&m_semaphore, 0, count) == 0);
}

ThreadSemaphore::~ThreadSemaphore()
{
    ASSERT(sem_destroy(&m_semaphore) == 0);
}

void ThreadSemaphore::wait()
{
    ASSERT(sem_wait(&m_semaphore) == 0);
}

void ThreadSemaphore::post()
{
    ASSERT(sem_post(&m_semaphore) == 0);
}

FiberSemaphore::FiberSemaphore(uint32_t concurency) : m_concurency(concurency) {}

FiberSemaphore::~FiberSemaphore() { ASSERT(m_waiting_list.empty()); }

bool FiberSemaphore::tryWait()
{
    ASSERT(Scheduler::GetCurrent());
    SpinScopedLock lock(&m_mutex);
    if (m_concurency > 0) {
        --m_concurency;
        return true;
    } else
        return false;
}

void FiberSemaphore::wait()
{
    ASSERT(Scheduler::GetCurrent());
    {
        SpinScopedLock lock(&m_mutex);
        if (m_concurency > 0) {
            --m_concurency;
            return;
        } else {
            m_waiting_list.emplace_back(std::make_pair(Scheduler::GetCurrent(), Fiber::GetCurrent()));
        }
    }
    //挂在信号量等待队列上，解锁，挂起当前协程
    // Fiber::YieldToHold(); //FIXME
}

void FiberSemaphore::post()
{
    SpinScopedLock lock(&m_mutex);
    if (!m_waiting_list.empty()) {
        auto next = m_waiting_list.front();  //取队首协程来调度
        m_waiting_list.pop_front();
        next.first->schedule(next.second);
    } else {
        ++m_concurency;
    }
}