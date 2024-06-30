#include "sem.h"
#include "log.h"
#include "scheduler.h"

using namespace meha;

static Logger::ptr root_logger = GET_LOGGER("root");

ThreadSemaphore::ThreadSemaphore(uint32_t count)
{
    if (sem_init(&m_semaphore, 0, count)) {
        LOG_FMT_FATAL(root_logger, "sem_init() 初始化信号量失败：%s", ::strerror(errno));
        throw std::system_error();  // FIXME 有观点认为构造函数最好不要抛出异常
    }
}

ThreadSemaphore::~ThreadSemaphore()
{
    if (sem_destroy(&m_semaphore)) {
        LOG_FMT_FATAL(root_logger, "sem_destroy() 销毁信号量失败：%s", ::strerror(errno));
    }
}

void ThreadSemaphore::wait()
{
    if (sem_wait(&m_semaphore)) {
        LOG_FMT_FATAL(root_logger, "sem_wait() 异常：%s", ::strerror(errno));
        throw std::system_error();
        // TODO 失败时是否应该直接结束程序？
    }
}

void ThreadSemaphore::post()
{
    if (sem_post(&m_semaphore)) {
        LOG_FMT_FATAL(root_logger, "sem_post() 异常：%s", ::strerror(errno));
        throw std::system_error();
    }
}

FiberSemaphore::FiberSemaphore(uint32_t concurency) : m_concurency(concurency) {}

FiberSemaphore::~FiberSemaphore() { ASSERT(m_waiting_fibers.empty()); }

bool FiberSemaphore::tryWait()
{
    ASSERT(Scheduler::GetThis());
    SpinScopedLock lock(&m_mutex);
    if (m_concurency > 0) {
        --m_concurency;
        return true;
    } else
        return false;
}

void FiberSemaphore::wait()
{
    ASSERT(Scheduler::GetThis());
    {
        SpinScopedLock lock(&m_mutex);
        if (m_concurency > 0) {
            --m_concurency;
            return;
        } else {
            m_waiting_fibers.emplace_back(std::make_pair(Scheduler::GetThis(), Fiber::GetThis()));
        }
    }
    //挂在信号量等待队列上，解锁，挂起当前协程
    Fiber::YieldToHold();
}

void FiberSemaphore::post()
{
    SpinScopedLock lock(&m_mutex);
    if (!m_waiting_fibers.empty()) {
        auto next = m_waiting_fibers.front();  //取队首协程来调度
        m_waiting_fibers.pop_front();
        next.first->schedule(next.second);
    } else {
        ++m_concurency;
    }
}